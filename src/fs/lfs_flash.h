/*
 * LittleFS block device backed by the RP2040's on-board QSPI flash.
 *
 * A region at the top of flash is reserved for the filesystem so it never
 * collides with the firmware image. All erase/program operations go through
 * flash_safe_execute() so XIP is suspended safely (USB stalls for a few ms
 * per sector but the host retransmits — no corruption).
 */
#ifndef LFS_FLASH_H_
#define LFS_FLASH_H_

#include "lfs.h"

/* Size of the filesystem region (must be a multiple of the 4 KB sector).
 * Default 1 MB carved from the top of the 8 MB Tiny2040 flash. Shrink this
 * for the 2 MB Tiny2040 variant. */
#ifndef LFS_FLASH_FS_SIZE
#define LFS_FLASH_FS_SIZE   (1024u * 1024u)
#endif

/* Fill in an lfs_config for the reserved flash region. */
void lfs_flash_get_config(struct lfs_config *cfg);

#endif /* LFS_FLASH_H_ */
