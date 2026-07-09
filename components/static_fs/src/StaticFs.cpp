#include "static_fs/StaticFs.h"

const char* const TAG = "StaticFs";

#include "Logging.h"

#include <cstring>

using namespace OpenShock;

// Mount parameters must be compatible with how the littlefs image is built (see the
// CONFIG_LITTLEFS_* defaults / mklittlefs): 4 KB erase blocks, 128-byte read/prog.
// littlefs only fixes block_size + block_count on disk (validated against the
// superblock at mount); the rest are runtime buffer sizes.
static constexpr lfs_size_t LFS_BLOCK_SIZE     = 4096;
static constexpr lfs_size_t LFS_READ_SIZE      = 128;
static constexpr lfs_size_t LFS_PROG_SIZE      = 128;
static constexpr lfs_size_t LFS_CACHE_SIZE     = 512;
static constexpr lfs_size_t LFS_LOOKAHEAD_SIZE = 128;

int StaticFs::bdRead(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size)
{
  auto* self  = static_cast<StaticFs*>(c->context);
  size_t addr = static_cast<size_t>(block) * c->block_size + off;
  return esp_partition_read(self->m_partition, addr, buffer, size) == ESP_OK ? 0 : LFS_ERR_IO;
}

int StaticFs::bdProg(const struct lfs_config*, lfs_block_t, lfs_off_t, const void*, lfs_size_t)
{
  return LFS_ERR_IO;  // read-only mount
}

int StaticFs::bdErase(const struct lfs_config*, lfs_block_t)
{
  return LFS_ERR_IO;  // read-only mount
}

int StaticFs::bdSync(const struct lfs_config*)
{
  return 0;  // nothing to flush
}

StaticFs::StaticFs()
  : m_partition(nullptr)
  , m_lfs {}
  , m_cfg {}
  , m_mounted(false)
{
}

StaticFs::~StaticFs()
{
  unmount();
}

bool StaticFs::mount(const esp_partition_t* partition)
{
  if (m_mounted) {
    return true;
  }
  if (partition == nullptr) {
    return false;
  }

  m_partition = partition;

  memset(&m_cfg, 0, sizeof(m_cfg));
  m_cfg.context        = this;
  m_cfg.read           = &StaticFs::bdRead;
  m_cfg.prog           = &StaticFs::bdProg;
  m_cfg.erase          = &StaticFs::bdErase;
  m_cfg.sync           = &StaticFs::bdSync;
  m_cfg.read_size      = LFS_READ_SIZE;
  m_cfg.prog_size      = LFS_PROG_SIZE;
  m_cfg.block_size     = LFS_BLOCK_SIZE;
  m_cfg.block_count    = partition->size / LFS_BLOCK_SIZE;
  m_cfg.block_cycles   = -1;  // read-only, no wear leveling
  m_cfg.cache_size     = LFS_CACHE_SIZE;
  m_cfg.lookahead_size = LFS_LOOKAHEAD_SIZE;

  int err = lfs_mount(&m_lfs, &m_cfg);
  if (err < 0) {
    OS_LOGE(TAG, "Failed to mount littlefs partition (lfs error %d)", err);
    m_partition = nullptr;
    return false;
  }

  m_mounted = true;
  return true;
}

void StaticFs::unmount()
{
  if (m_mounted) {
    lfs_unmount(&m_lfs);
    m_mounted = false;
  }
  m_partition = nullptr;
}

bool StaticFs::exists(const char* path)
{
  if (!m_mounted) {
    return false;
  }
  struct lfs_info info;
  return lfs_stat(&m_lfs, path, &info) >= 0;
}

bool StaticFs::readFile(const char* path, const std::function<bool(std::span<const uint8_t>)>& sink)
{
  if (!m_mounted) {
    return false;
  }

  lfs_file_t file;
  if (lfs_file_open(&m_lfs, &file, path, LFS_O_RDONLY) < 0) {
    return false;
  }

  uint8_t buf[1024];
  bool ok = true;
  for (;;) {
    lfs_ssize_t r = lfs_file_read(&m_lfs, &file, buf, sizeof(buf));
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

  lfs_file_close(&m_lfs, &file);
  return ok;
}
