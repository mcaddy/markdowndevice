#include "docstore.h"
#include "lfs_flash.h"

#include <string.h>
#include <stdio.h>

static lfs_t          s_lfs;
static struct lfs_config s_cfg;
static int            s_mounted = 0;

int docstore_init(void) {
  lfs_flash_get_config(&s_cfg);

  int err = lfs_mount(&s_lfs, &s_cfg);
  if (err) {
    /* First boot (or corrupted): format then mount. */
    err = lfs_format(&s_lfs, &s_cfg);
    if (err) return -1;
    err = lfs_mount(&s_lfs, &s_cfg);
    if (err) return -1;
  }
  s_mounted = 1;
  return 0;
}

void docstore_task(void) {
  /* Reserved for deferred/batched flash work. LittleFS writes are currently
   * performed synchronously within request handling, which is fine because
   * those handlers run in the main loop, not an interrupt. */
}

int docstore_sanitize(char *dst, const char *src) {
  if (!src) return -1;

  /* Drop any directory components: keep only the basename. */
  const char *base = src;
  for (const char *p = src; *p; p++) {
    if (*p == '/' || *p == '\\') base = p + 1;
  }

  size_t n = 0;
  for (const char *p = base; *p && n < DOCSTORE_MAX_NAME; p++) {
    unsigned char c = (unsigned char) *p;
    int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
             (c >= '0' && c <= '9') || c == '.' || c == '_' ||
             c == '-' || c == ' ' || c == '(' || c == ')';
    if (ok) {
      dst[n++] = (char) c;
    }
    /* silently skip disallowed chars */
  }
  /* Strip trailing/leading spaces and dots to avoid odd names. */
  while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '.')) n--;
  dst[n] = '\0';

  size_t start = 0;
  while (dst[start] == ' ') start++;
  if (start) memmove(dst, dst + start, n - start + 1);

  if (dst[0] == '\0') return -1;              /* nothing usable */
  if (strcmp(dst, ".") == 0 || strcmp(dst, "..") == 0) return -1;
  return 0;
}

int docstore_write(const char *name, const void *data, uint32_t len) {
  if (!s_mounted) return -1;
  if (len > DOCSTORE_MAX_DOC_SIZE) return -1;

  char safe[DOCSTORE_MAX_NAME + 1];
  if (docstore_sanitize(safe, name) != 0) return -1;

  lfs_file_t f;
  int err = lfs_file_open(&s_lfs, &f, safe, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
  if (err) return -1;

  lfs_ssize_t wrote = lfs_file_write(&s_lfs, &f, data, len);
  int cerr = lfs_file_close(&s_lfs, &f);
  if (wrote < 0 || (uint32_t) wrote != len || cerr) return -1;
  return 0;
}

int docstore_delete(const char *name) {
  if (!s_mounted) return -1;
  char safe[DOCSTORE_MAX_NAME + 1];
  if (docstore_sanitize(safe, name) != 0) return -1;
  return lfs_remove(&s_lfs, safe) == 0 ? 0 : -1;
}

int docstore_list(docstore_list_cb cb, void *arg) {
  if (!s_mounted) return -1;

  lfs_dir_t dir;
  if (lfs_dir_open(&s_lfs, &dir, "/") != 0) return -1;

  struct lfs_info info;
  int stop = 0;
  while (!stop) {
    int r = lfs_dir_read(&s_lfs, &dir, &info);
    if (r < 0) { lfs_dir_close(&s_lfs, &dir); return -1; }
    if (r == 0) break;                          /* end of directory */
    if (info.type != LFS_TYPE_REG) continue;    /* skip . / .. / dirs */
    if (info.name[0] == '.') continue;          /* skip hidden */
    stop = cb(info.name, (uint32_t) info.size, arg);
  }
  lfs_dir_close(&s_lfs, &dir);
  return 0;
}

int docstore_open_read(lfs_file_t *file, const char *name) {
  if (!s_mounted) return -1;
  char safe[DOCSTORE_MAX_NAME + 1];
  if (docstore_sanitize(safe, name) != 0) return -1;
  return lfs_file_open(&s_lfs, file, safe, LFS_O_RDONLY) == 0 ? 0 : -1;
}

lfs_ssize_t docstore_read(lfs_file_t *file, void *buf, uint32_t len) {
  return lfs_file_read(&s_lfs, file, buf, len);
}

lfs_soff_t docstore_size_of_open(lfs_file_t *file) {
  return lfs_file_size(&s_lfs, file);
}

int docstore_close(lfs_file_t *file) {
  return lfs_file_close(&s_lfs, file) == 0 ? 0 : -1;
}
