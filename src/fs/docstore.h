/*
 * Document store: markdown files kept in a LittleFS filesystem on flash.
 *
 * Thin wrapper over LittleFS providing name sanitisation, whole-file writes
 * (used for uploads/saves where the HTTP layer has buffered the body), delete,
 * listing, and streaming reads (used to serve/download files chunk by chunk).
 *
 * All calls run in the main-loop / lwIP-callback context (never an ISR), so the
 * flash writes they trigger are safe.
 */
#ifndef DOCSTORE_H_
#define DOCSTORE_H_

#include <stdint.h>
#include <stddef.h>
#include "lfs.h"

#define DOCSTORE_MAX_NAME     48    /* sanitised filename length cap */
#define DOCSTORE_MAX_DOC_SIZE (64u * 1024u)    /* per-document size cap */

/* Mount the filesystem, formatting it on first use. Returns 0 on success. */
int  docstore_init(void);

/* Main-loop hook (reserved for deferred work). Currently a no-op. */
void docstore_task(void);

/* Sanitise a caller-supplied name into a safe basename (dst must hold
 * DOCSTORE_MAX_NAME+1). Returns 0 on success, <0 if the name is unusable. */
int  docstore_sanitize(char *dst, const char *src);

/* Whole-file write (create/replace). name is sanitised internally.
 * Returns 0 on success, <0 on error. */
int  docstore_write(const char *name, const void *data, uint32_t len);

/* Delete a document. Returns 0 on success, <0 on error. */
int  docstore_delete(const char *name);

/* Listing callback: return non-zero to stop iteration early. */
typedef int (*docstore_list_cb)(const char *name, uint32_t size, void *arg);
int  docstore_list(docstore_list_cb cb, void *arg);

/* Streaming read API (for HTTP GET / download). */
int         docstore_open_read(lfs_file_t *file, const char *name);
lfs_ssize_t docstore_read(lfs_file_t *file, void *buf, uint32_t len);
lfs_soff_t  docstore_size_of_open(lfs_file_t *file);
int         docstore_close(lfs_file_t *file);

#endif /* DOCSTORE_H_ */
