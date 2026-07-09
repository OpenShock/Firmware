#include "fs/LfsPartition.h"

const char* const TAG = "LfsPartition";

#include "Logging.h"

#include "lfs.h"

#include <cstring>
#include <string>

using namespace OpenShock;

// Mount parameters must be compatible with how the image is built (see the
// CONFIG_LITTLEFS_* defaults / mklittlefs): 4 KB erase blocks, 128-byte read/prog.
// littlefs only fixes block_size + block_count on disk (validated against the
// superblock at mount); the rest are runtime buffer sizes.
static constexpr lfs_size_t LFS_BLOCK_SIZE     = 4096;
static constexpr lfs_size_t LFS_READ_SIZE      = 128;
static constexpr lfs_size_t LFS_PROG_SIZE      = 128;
static constexpr lfs_size_t LFS_CACHE_SIZE     = 512;
static constexpr lfs_size_t LFS_LOOKAHEAD_SIZE = 128;
static constexpr int32_t LFS_BLOCK_CYCLES_RW   = 512;  // wear leveling for read/write mounts

// All littlefs state lives here so lfs.h stays out of the public headers. The block
// device callbacks recover it via lfs_config::context.
struct OpenShock::detail::LfsState {
  lfs_t lfs;
  struct lfs_config cfg;
  const esp_partition_t* partition;
  bool writable;
};

using OpenShock::detail::LfsState;

static int bdRead(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size)
{
  auto* st    = static_cast<LfsState*>(c->context);
  size_t addr = static_cast<size_t>(block) * c->block_size + off;
  return esp_partition_read(st->partition, addr, buffer, size) == ESP_OK ? 0 : LFS_ERR_IO;
}

static int bdProg(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size)
{
  auto* st = static_cast<LfsState*>(c->context);
  if (!st->writable) {
    return LFS_ERR_IO;
  }
  size_t addr = static_cast<size_t>(block) * c->block_size + off;
  return esp_partition_write(st->partition, addr, buffer, size) == ESP_OK ? 0 : LFS_ERR_IO;
}

static int bdErase(const struct lfs_config* c, lfs_block_t block)
{
  auto* st = static_cast<LfsState*>(c->context);
  if (!st->writable) {
    return LFS_ERR_IO;
  }
  size_t addr = static_cast<size_t>(block) * c->block_size;
  return esp_partition_erase_range(st->partition, addr, c->block_size) == ESP_OK ? 0 : LFS_ERR_IO;
}

static int bdSync(const struct lfs_config*)
{
  return 0;  // esp_partition writes are synchronous
}

LfsPartition::LfsPartition()
  : m_state(new LfsState {})
  , m_mounted(false)
{
}

LfsPartition::~LfsPartition()
{
  unmount();
  delete m_state;
}

bool LfsPartition::mount(const esp_partition_t* partition, bool writable, bool formatOnFail)
{
  if (m_mounted) {
    return true;
  }
  if (partition == nullptr) {
    return false;
  }

  m_state->partition = partition;
  m_state->writable  = writable;

  struct lfs_config& cfg = m_state->cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.context        = m_state;
  cfg.read           = &bdRead;
  cfg.prog           = &bdProg;
  cfg.erase          = &bdErase;
  cfg.sync           = &bdSync;
  cfg.read_size      = LFS_READ_SIZE;
  cfg.prog_size      = LFS_PROG_SIZE;
  cfg.block_size     = LFS_BLOCK_SIZE;
  cfg.block_count    = partition->size / LFS_BLOCK_SIZE;
  cfg.block_cycles   = writable ? LFS_BLOCK_CYCLES_RW : -1;  // -1 disables wear leveling
  cfg.cache_size     = LFS_CACHE_SIZE;
  cfg.lookahead_size = LFS_LOOKAHEAD_SIZE;

  int err = lfs_mount(&m_state->lfs, &cfg);
  if (err < 0) {
    if (writable && formatOnFail) {
      OS_LOGW(TAG, "Mount failed (lfs error %d), formatting partition", err);
      if (lfs_format(&m_state->lfs, &cfg) < 0 || lfs_mount(&m_state->lfs, &cfg) < 0) {
        OS_LOGE(TAG, "Failed to format and mount partition");
        m_state->partition = nullptr;
        return false;
      }
    } else {
      OS_LOGE(TAG, "Failed to mount partition (lfs error %d)", err);
      m_state->partition = nullptr;
      return false;
    }
  }

  m_mounted = true;
  return true;
}

void LfsPartition::unmount()
{
  if (m_mounted) {
    lfs_unmount(&m_state->lfs);
    m_mounted = false;
  }
  m_state->partition = nullptr;
}

bool LfsPartition::exists(const char* path)
{
  if (!m_mounted) {
    return false;
  }
  struct lfs_info info;
  return lfs_stat(&m_state->lfs, path, &info) >= 0;
}

bool LfsPartition::readStream(const char* path, const std::function<bool(std::span<const uint8_t>)>& sink)
{
  if (!m_mounted) {
    return false;
  }

  lfs_file_t file;
  if (lfs_file_open(&m_state->lfs, &file, path, LFS_O_RDONLY) < 0) {
    return false;
  }

  uint8_t buf[1024];
  bool ok = true;
  for (;;) {
    lfs_ssize_t r = lfs_file_read(&m_state->lfs, &file, buf, sizeof(buf));
    if (r < 0) {
      ok = false;
      break;
    }
    if (r == 0) {
      break;  // EOF
    }
    if (!sink(std::span<const uint8_t>(buf, static_cast<size_t>(r)))) {
      ok = false;
      break;
    }
  }

  lfs_file_close(&m_state->lfs, &file);
  return ok;
}

bool LfsPartition::readAll(const char* path, std::vector<uint8_t>& out)
{
  out.clear();
  if (!m_mounted) {
    return false;
  }

  lfs_file_t file;
  if (lfs_file_open(&m_state->lfs, &file, path, LFS_O_RDONLY) < 0) {
    return false;
  }

  bool ok         = true;
  lfs_soff_t size = lfs_file_size(&m_state->lfs, &file);
  if (size < 0) {
    ok = false;
  } else if (size > 0) {
    out.resize(static_cast<size_t>(size));
    lfs_ssize_t r = lfs_file_read(&m_state->lfs, &file, out.data(), static_cast<lfs_size_t>(size));
    if (r < 0 || static_cast<size_t>(r) != static_cast<size_t>(size)) {
      ok = false;
    }
  }

  lfs_file_close(&m_state->lfs, &file);
  if (!ok) {
    out.clear();
  }
  return ok;
}

bool LfsPartition::writeAll(const char* path, std::span<const uint8_t> data)
{
  if (!m_mounted || !m_state->writable) {
    return false;
  }

  // Write to a temp file then rename, so a power loss can't leave a half-written file.
  std::string tmp = std::string(path) + ".tmp";

  lfs_file_t file;
  if (lfs_file_open(&m_state->lfs, &file, tmp.c_str(), LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) < 0) {
    return false;
  }

  bool ok = true;
  if (!data.empty()) {
    lfs_ssize_t w = lfs_file_write(&m_state->lfs, &file, data.data(), data.size());
    if (w < 0 || static_cast<size_t>(w) != data.size()) {
      ok = false;
    }
  }
  if (lfs_file_close(&m_state->lfs, &file) < 0) {
    ok = false;
  }

  if (!ok) {
    lfs_remove(&m_state->lfs, tmp.c_str());
    return false;
  }

  if (lfs_rename(&m_state->lfs, tmp.c_str(), path) < 0) {
    lfs_remove(&m_state->lfs, tmp.c_str());
    return false;
  }

  return true;
}

bool LfsPartition::remove(const char* path)
{
  if (!m_mounted || !m_state->writable) {
    return false;
  }
  return lfs_remove(&m_state->lfs, path) >= 0;
}
