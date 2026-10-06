// The config partition is 3 x 4 KiB (partitions/ota_4mb.csv). littlefs keeps its root metadata pair in two of those
// blocks, and ConfigFs::writeAll writes "<path>.tmp" and renames it over the old file. A file too large to be
// inlined in the metadata needs its own data block while the old copy still occupies the only free one, so the
// review suspected that larger configs can't be rewritten at all (REVIEW_ACTIONS.md, X01).
//
// These tests run the real littlefs core with LfsPartition's exact mount parameters over a RAM block device and
// replay ConfigFs's write sequence, so they answer that directly. The "large config" cases describe the behaviour
// the firmware needs; if they fail, X01 is real.
#include "unity.h"

#include "lfs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
  // Must match components/fs/src/LfsPartition.cpp and the config partition size.
  constexpr lfs_size_t kBlockSize   = 4096;
  constexpr lfs_size_t kBlockCount  = 0x3000 / kBlockSize;
  constexpr lfs_size_t kReadSize    = 128;
  constexpr lfs_size_t kProgSize    = 128;
  constexpr lfs_size_t kCacheSize   = 512;
  constexpr lfs_size_t kLookahead   = 128;
  constexpr int32_t kBlockCyclesRw  = 512;
  constexpr const char* kConfigPath = "/config";

  std::vector<uint8_t> s_flash;

  int bdRead(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size)
  {
    std::memcpy(buffer, &s_flash[block * c->block_size + off], size);
    return 0;
  }
  int bdProg(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size)
  {
    std::memcpy(&s_flash[block * c->block_size + off], buffer, size);
    return 0;
  }
  int bdErase(const struct lfs_config* c, lfs_block_t block)
  {
    std::memset(&s_flash[block * c->block_size], 0xFF, c->block_size);
    return 0;
  }
  int bdSync(const struct lfs_config*)
  {
    return 0;
  }

  struct Fs {
    lfs_t lfs {};
    lfs_config cfg {};

    Fs()
    {
      s_flash.assign(kBlockSize * kBlockCount, 0xFF);

      cfg.read           = bdRead;
      cfg.prog           = bdProg;
      cfg.erase          = bdErase;
      cfg.sync           = bdSync;
      cfg.read_size      = kReadSize;
      cfg.prog_size      = kProgSize;
      cfg.block_size     = kBlockSize;
      cfg.block_count    = kBlockCount;
      cfg.block_cycles   = kBlockCyclesRw;
      cfg.cache_size     = kCacheSize;
      cfg.lookahead_size = kLookahead;

      TEST_ASSERT_EQUAL(0, lfs_format(&lfs, &cfg));
      TEST_ASSERT_EQUAL(0, lfs_mount(&lfs, &cfg));
    }
    ~Fs() { lfs_unmount(&lfs); }

    // The same sequence as LfsPartition::writeAll: write <path>.tmp, then rename it over <path>.
    int writeAll(const std::vector<uint8_t>& data)
    {
      std::string tmp = std::string(kConfigPath) + ".tmp";

      lfs_file_t file;
      int err = lfs_file_open(&lfs, &file, tmp.c_str(), LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
      if (err < 0) return err;

      lfs_ssize_t written = lfs_file_write(&lfs, &file, data.data(), data.size());
      int closeErr        = lfs_file_close(&lfs, &file);
      if (written < 0 || closeErr < 0 || static_cast<size_t>(written) != data.size()) {
        lfs_remove(&lfs, tmp.c_str());
        return written < 0 ? static_cast<int>(written) : (closeErr < 0 ? closeErr : LFS_ERR_IO);
      }

      err = lfs_rename(&lfs, tmp.c_str(), kConfigPath);
      if (err < 0) lfs_remove(&lfs, tmp.c_str());
      return err;
    }

    bool readBack(std::vector<uint8_t>& out)
    {
      lfs_file_t file;
      if (lfs_file_open(&lfs, &file, kConfigPath, LFS_O_RDONLY) < 0) return false;
      lfs_soff_t size = lfs_file_size(&lfs, &file);
      out.resize(size < 0 ? 0 : static_cast<size_t>(size));
      lfs_ssize_t r = lfs_file_read(&lfs, &file, out.data(), out.size());
      lfs_file_close(&lfs, &file);
      return r == static_cast<lfs_ssize_t>(out.size());
    }
  };

  std::vector<uint8_t> blob(size_t size, uint8_t seed)
  {
    std::vector<uint8_t> v(size);
    for (size_t i = 0; i < size; i++) v[i] = static_cast<uint8_t>(seed + i * 31);
    return v;
  }

  // Writes `size` bytes `times` times (as repeated config saves would), checking each write reads back.
  void assertRewritable(size_t size, int times)
  {
    Fs fs;
    for (int i = 0; i < times; i++) {
      auto data = blob(size, static_cast<uint8_t>(i));
      char msg[96];
      int err = fs.writeAll(data);
      std::snprintf(msg, sizeof(msg), "write #%d of a %zu-byte config failed (lfs error %d)", i + 1, size, err);
      TEST_ASSERT_EQUAL_MESSAGE(0, err, msg);

      std::vector<uint8_t> back;
      TEST_ASSERT_TRUE(fs.readBack(back));
      TEST_ASSERT_TRUE_MESSAGE(back == data, "config read back differs from what was written");
    }
  }
}  // namespace

TEST_CASE("A small (inlined) config can be rewritten repeatedly", "[fs][config]")
{
  assertRewritable(256, 50);
}

TEST_CASE("A config just over the inline limit can be rewritten repeatedly (X01)", "[fs][config]")
{
  assertRewritable(600, 50);
}

TEST_CASE("A config of a few saved networks plus a token (~1.5 KiB) can be rewritten repeatedly (X01)", "[fs][config]")
{
  assertRewritable(1536, 50);
}

TEST_CASE("Reports the largest config size that survives repeated rewrites", "[fs][config][info]")
{
  // Not a pass/fail check: prints where the partition stops accepting rewrites, to size MaxConfigSize / the
  // partition. Binary search over sizes, each candidate written 20 times on a fresh filesystem.
  size_t lo = 0, hi = 4096;
  while (lo < hi) {
    size_t mid = (lo + hi + 1) / 2;
    Fs fs;
    bool ok = true;
    for (int i = 0; i < 20 && ok; i++) {
      ok = fs.writeAll(blob(mid, static_cast<uint8_t>(i))) == 0;
    }
    if (ok) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  std::printf("[fs] largest config rewritable 20 times on the 3-block partition: %zu bytes\n", lo);
}
