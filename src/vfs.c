#include "vfs.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <SDL.h>

#define SECTOR 2048

typedef struct {
    char *name;       /* lower-case-insensitive match, ";1" stripped */
    uint32_t lba;
    uint32_t size;
    int is_dir;
} IsoEntry;

struct VfsFile {
    FILE *fp;
    uint64_t base;    /* byte offset in host file */
    uint64_t size;
    uint64_t pos;
};

static char g_root[1024];
static FILE *g_iso;           /* NULL -> directory backend; directory lookups only */
static char g_iso_path[1024];
static SDL_mutex *g_iso_lock; /* guards g_iso (lookups come from several threads) */
static uint32_t g_root_lba, g_root_size;

/* ---------------- directory backend ---------------- */

static int match_entry(const char *dir, const char *name, char *out, size_t outsize)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = -1;
    if (!d)
        return -1;
    while ((e = readdir(d)) != NULL) {
        if (strcasecmp(e->d_name, name) == 0) {
            snprintf(out, outsize, "%s/%s", dir, e->d_name);
            found = 0;
            break;
        }
    }
    closedir(d);
    return found;
}

/* Split the next path component; returns pointer after it, or NULL when done. */
static const char *next_comp(const char *p, char *comp, size_t compsize)
{
    size_t n = 0;
    while (*p == '/' || *p == '\\')
        p++;
    if (!*p)
        return NULL;
    while (*p && *p != '/' && *p != '\\') {
        if (n < compsize - 1)
            comp[n++] = *p;
        p++;
    }
    comp[n] = 0;
    return p;
}

static int dir_resolve(const char *disc_path, char *out, size_t outsize)
{
    char cur[1024], next[1024], comp[256];
    const char *p = disc_path;

    snprintf(cur, sizeof(cur), "%s", g_root);
    while ((p = next_comp(p, comp, sizeof(comp))) != NULL) {
        if (match_entry(cur, comp, next, sizeof(next)) != 0)
            return -1;
        memcpy(cur, next, sizeof(cur));
    }
    snprintf(out, outsize, "%s", cur);
    return 0;
}

/* ---------------- ISO (Joliet) backend ---------------- */

static uint32_t le32(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int iso_read(uint32_t lba, void *buf, size_t n)
{
    if (fseeko64(g_iso, (off64_t)lba * SECTOR, SEEK_SET) != 0)
        return -1;
    return fread(buf, 1, n, g_iso) == n ? 0 : -1;
}

/* Joliet names are UCS-2 BE; Game Wave names are ASCII. */
static void joliet_name(const unsigned char *p, int len, char *out, size_t outsize)
{
    size_t n = 0;
    int i;
    for (i = 0; i + 1 < len && n < outsize - 1; i += 2) {
        unsigned c = (p[i] << 8) | p[i + 1];
        if (c == ';')
            break;
        out[n++] = c < 128 ? (char)c : '?';
    }
    out[n] = 0;
}

/* Find `name` in directory extent (lba, size). */
static int iso_find(uint32_t lba, uint32_t size, const char *name, IsoEntry *e)
{
    unsigned char *data = malloc(size);
    uint32_t p = 0;
    char nm[256];
    int found = -1;

    if (!data || iso_read(lba, data, size) != 0) {
        free(data);
        return -1;
    }
    while (p < size) {
        unsigned len = data[p];
        if (len == 0) {
            p = (p / SECTOR + 1) * SECTOR;
            continue;
        }
        if (p + len > size)
            break;
        if (data[p + 32] == 1 && (data[p + 33] == 0 || data[p + 33] == 1)) {
            p += len;     /* . and .. */
            continue;
        }
        joliet_name(data + p + 33, data[p + 32], nm, sizeof(nm));
        if (strcasecmp(nm, name) == 0) {
            e->lba = le32(data + p + 2);
            e->size = le32(data + p + 10);
            e->is_dir = (data[p + 25] & 2) != 0;
            found = 0;
            break;
        }
        p += len;
    }
    free(data);
    return found;
}

static int iso_resolve(const char *disc_path, IsoEntry *out)
{
    IsoEntry cur = {NULL, g_root_lba, g_root_size, 1};
    char comp[256];
    const char *p = disc_path;
    int rc = 0;

    SDL_LockMutex(g_iso_lock);
    while ((p = next_comp(p, comp, sizeof(comp))) != NULL) {
        if (!cur.is_dir || iso_find(cur.lba, cur.size, comp, &cur) != 0) {
            rc = -1;
            break;
        }
    }
    SDL_UnlockMutex(g_iso_lock);
    *out = cur;
    return rc;
}

static int iso_mount(const char *path)
{
    unsigned char vd[SECTOR];
    int sec;

    g_iso = fopen(path, "rb");
    if (!g_iso)
        return -1;
    snprintf(g_iso_path, sizeof(g_iso_path), "%s", path);
    g_iso_lock = SDL_CreateMutex();
    for (sec = 16; sec < 32; sec++) {
        if (iso_read(sec, vd, SECTOR) != 0 || memcmp(vd + 1, "CD001", 5) != 0)
            break;
        /* supplementary descriptor with Joliet escape sequence %/@ %/C %/E */
        if (vd[0] == 2 && vd[88] == '%' && vd[89] == '/') {
            g_root_lba = le32(vd + 156 + 2);
            g_root_size = le32(vd + 156 + 10);
            return 0;
        }
        if (vd[0] == 255)
            break;
    }
    fclose(g_iso);
    g_iso = NULL;
    return -1;
}

/* ---------------- public API ---------------- */

int vfs_mount(const char *path)
{
    size_t n = strlen(path);
    DIR *d;

    if (n == 0 || n >= sizeof(g_root))
        return -1;
    d = opendir(path);   /* (stat() can't size >2 GB images on MinGW) */
    if (!d)
        return iso_mount(path);
    closedir(d);
    memcpy(g_root, path, n + 1);
    while (n > 1 && (g_root[n - 1] == '/' || g_root[n - 1] == '\\'))
        g_root[--n] = 0;
    return 0;
}

VfsFile *vfs_open(const char *disc_path)
{
    VfsFile *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    if (g_iso) {
        IsoEntry e;
        if (iso_resolve(disc_path, &e) != 0 || e.is_dir) {
            free(f);
            return NULL;
        }
        f->fp = fopen(g_iso_path, "rb");   /* private handle: files are read concurrently */
        if (!f->fp) {
            free(f);
            return NULL;
        }
        f->base = (uint64_t)e.lba * SECTOR;
        f->size = e.size;
    } else {
        char host[1024];
        if (dir_resolve(disc_path, host, sizeof(host)) != 0 || !(f->fp = fopen(host, "rb"))) {
            free(f);
            return NULL;
        }
        fseeko64(f->fp, 0, SEEK_END);
        f->size = (uint64_t)ftello64(f->fp);
    }
    return f;
}

size_t vfs_read(VfsFile *f, void *buf, size_t n)
{
    size_t got;
    if (f->pos >= f->size)
        return 0;
    if (n > f->size - f->pos)
        n = (size_t)(f->size - f->pos);
    if (fseeko64(f->fp, (off64_t)(f->base + f->pos), SEEK_SET) != 0)
        return 0;
    got = fread(buf, 1, n, f->fp);
    f->pos += got;
    return got;
}

int vfs_seek(VfsFile *f, uint64_t pos)
{
    if (pos > f->size)
        return -1;
    f->pos = pos;
    return 0;
}

uint64_t vfs_size(VfsFile *f)
{
    return f->size;
}

void vfs_close(VfsFile *f)
{
    if (!f)
        return;
    if (f->fp)
        fclose(f->fp);
    free(f);
}

unsigned char *vfs_read_all(const char *disc_path, size_t *size)
{
    VfsFile *f = vfs_open(disc_path);
    unsigned char *buf;
    size_t n;

    if (!f)
        return NULL;
    n = (size_t)f->size;
    buf = malloc(n ? n : 1);
    if (buf && vfs_read(f, buf, n) != n) {
        free(buf);
        buf = NULL;
    }
    vfs_close(f);
    if (buf && size)
        *size = n;
    return buf;
}
