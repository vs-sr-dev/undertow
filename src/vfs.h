/* Disc file access. Game paths are case-insensitive ("/Data/game.zbc" vs "data/").
 * Backends: an extracted directory, or a disc image (.iso) read through its Joliet tree
 * (Game Wave discs are UDF/ISO9660 bridge discs; the plain ISO9660 names are 8.3-truncated). */
#ifndef UNDERTOW_VFS_H
#define UNDERTOW_VFS_H

#include <stddef.h>
#include <stdint.h>

/* Mount a directory or an .iso file. Returns 0 on success. */
int vfs_mount(const char *path);

typedef struct VfsFile VfsFile;

VfsFile *vfs_open(const char *disc_path);
size_t vfs_read(VfsFile *f, void *buf, size_t n);
int vfs_seek(VfsFile *f, uint64_t pos);
uint64_t vfs_size(VfsFile *f);
void vfs_close(VfsFile *f);

/* Read a whole file; caller frees. Returns NULL if missing. */
unsigned char *vfs_read_all(const char *disc_path, size_t *size);

#endif
