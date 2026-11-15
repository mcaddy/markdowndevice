#include "lfs_flash.h"

#include <string.h>

#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

/* Reserve the top LFS_FLASH_FS_SIZE bytes of flash for the filesystem. */
#define FS_FLASH_OFFSET   (PICO_FLASH_SIZE_BYTES - LFS_FLASH_FS_SIZE)

/* RP2040 flash geometry. */
#define FS_READ_SIZE      256
#define FS_PROG_SIZE      FLASH_PAGE_SIZE      /* 256 */
#define FS_BLOCK_SIZE     FLASH_SECTOR_SIZE    /* 4096 */
#define FS_BLOCK_COUNT    (LFS_FLASH_FS_SIZE / FS_BLOCK_SIZE)
#define FS_CACHE_SIZE     256
#define FS_LOOKAHEAD_SIZE 32

/* Static caches so LittleFS never mallocs its top-level buffers. */
static uint8_t s_read_buf[FS_CACHE_SIZE];
static uint8_t s_prog_buf[FS_CACHE_SIZE];
static uint8_t s_lookahead_buf[FS_LOOKAHEAD_SIZE] __attribute__((aligned(4)));

/* ---- flash_safe_execute trampolines ------------------------------------ */
typedef struct {
  uint32_t flash_off;   /* absolute flash offset */
  const uint8_t *data;
  uint32_t len;
} prog_args_t;

static void do_erase(void *param) {
  uint32_t off = *(uint32_t *) param;
  flash_range_erase(off, FS_BLOCK_SIZE);
}

static void do_prog(void *param) {
  prog_args_t *a = (prog_args_t *) param;
  flash_range_program(a->flash_off, a->data, a->len);
}

/* ---- LittleFS block device callbacks ------------------------------------ */
static int bd_read(const struct lfs_config *c, lfs_block_t block,
                   lfs_off_t off, void *buffer, lfs_size_t size) {
  (void) c;
  const uint8_t *src = (const uint8_t *) (XIP_BASE + FS_FLASH_OFFSET
                                          + (block * FS_BLOCK_SIZE) + off);
  memcpy(buffer, src, size);
  return LFS_ERR_OK;
}

static int bd_prog(const struct lfs_config *c, lfs_block_t block,
                   lfs_off_t off, const void *buffer, lfs_size_t size) {
  (void) c;
  prog_args_t a = {
    .flash_off = FS_FLASH_OFFSET + (block * FS_BLOCK_SIZE) + off,
    .data = (const uint8_t *) buffer,
    .len = size,
  };
  int rc = flash_safe_execute(do_prog, &a, 250);
  return (rc == PICO_OK) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int bd_erase(const struct lfs_config *c, lfs_block_t block) {
  (void) c;
  uint32_t off = FS_FLASH_OFFSET + (block * FS_BLOCK_SIZE);
  int rc = flash_safe_execute(do_erase, &off, 250);
  return (rc == PICO_OK) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int bd_sync(const struct lfs_config *c) {
  (void) c;
  return LFS_ERR_OK;
}

void lfs_flash_get_config(struct lfs_config *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->read  = bd_read;
  cfg->prog  = bd_prog;
  cfg->erase = bd_erase;
  cfg->sync  = bd_sync;

  cfg->read_size      = FS_READ_SIZE;
  cfg->prog_size      = FS_PROG_SIZE;
  cfg->block_size     = FS_BLOCK_SIZE;
  cfg->block_count    = FS_BLOCK_COUNT;
  cfg->cache_size     = FS_CACHE_SIZE;
  cfg->lookahead_size = FS_LOOKAHEAD_SIZE;
  cfg->block_cycles   = 500;

  cfg->read_buffer      = s_read_buf;
  cfg->prog_buffer      = s_prog_buf;
  cfg->lookahead_buffer = s_lookahead_buf;
}
