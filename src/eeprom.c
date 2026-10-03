/* Save-game EEPROM, byte-compatible with the engine's apps/zit/eeprom_mgr.c (see
 * docs/NOTES.md "Save EEPROM"). 512 pages of 64 bytes, page i at address i*64:
 *   0  u16 LE  CRC-16/XMODEM of bytes 2..63
 *   2  u16 LE  flags: bit15 free, bits13-12 type (0 single, 1 first, 2 middle, 3 last),
 *              bits8-0 next page
 *   4  u32     always 0
 *   8  56 bytes payload
 * A save's first page holds {u16 app_id, u16 size, char app_name[17], char save_name[33]}
 * and the first 2 data bytes; following pages hold 56 data bytes each. The whole image is
 * written back to the host file after every change (the engine writes pages through). */
#include "eeprom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGES 512
#define PAGE 64
#define PAYLOAD 56
#define HDR 54        /* header bytes in the first page's payload */

#define F_FREE 0x8000
#define F_BIT14 0x4000
#define TYPE(f) (((f) >> 12) & 3)
#define NEXT(f) ((f) & 0x1ff)

typedef struct {
    EepHeader h;
    int first;
} Entry;

static uint8_t g_img[EEP_SIZE];
static char g_path[1024];
static Entry g_list[PAGES];   /* built by the last Enumerate call; IDs index it */
static int g_count;

static uint8_t *page(int i) { return g_img + i * PAGE; }
static int get16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static void set16(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static int flags(int i) { return get16(page(i) + 2); }

static int crc16(const uint8_t *p, int n)
{
    unsigned c = 0;
    while (n--) {
        c = ((c >> 8) | (c << 8)) & 0xffff;
        c ^= *p++;
        c ^= (c & 0xff) >> 4;
        c = (c ^ (c << 12)) & 0xffff;
        c = (c ^ ((c & 0xff) << 5)) & 0xffff;
    }
    return (int)c;
}

static void seal(int i) { set16(page(i), crc16(page(i) + 2, PAGE - 2)); }
static int page_ok(int i) { return get16(page(i)) == crc16(page(i) + 2, PAGE - 2); }

static void init_page(int i)
{
    memset(page(i), 0, PAGE);
    set16(page(i) + 2, F_FREE);
    seal(i);
}

static int pages_for(int size) { return (size + HDR + PAYLOAD - 1) / PAYLOAD; }

static void flush(void)
{
    char tmp[1040];
    FILE *f;
    if (!g_path[0])
        return;
    snprintf(tmp, sizeof(tmp), "%s.tmp", g_path);
    f = fopen(tmp, "wb");
    if (!f || fwrite(g_img, 1, EEP_SIZE, f) != EEP_SIZE) {
        fprintf(stderr, "eeprom: cannot write %s\n", tmp);
        if (f)
            fclose(f);
        return;
    }
    fclose(f);
    remove(g_path);
    if (rename(tmp, g_path) != 0)
        fprintf(stderr, "eeprom: cannot replace %s\n", g_path);
}

void eep_open(const char *path)
{
    FILE *f;
    int i;

    snprintf(g_path, sizeof(g_path), "%s", path);
    f = fopen(path, "rb");
    if (f && fread(g_img, 1, EEP_SIZE, f) == EEP_SIZE) {
        fclose(f);
        printf("eeprom: %s\n", path);
        return;
    }
    if (f)
        fclose(f);
    /* a blank (0xFF) chip would not work with the engine; start formatted */
    for (i = 0; i < PAGES; i++)
        init_page(i);
    printf("eeprom: %s (new, formatted)\n", path);
}

/* Data bytes go 2 to the first page (after the header), then 56 per page. */
static void write_data(const int *chain, int n, int size, const uint8_t *data)
{
    int k, off = 2;
    memcpy(page(chain[0]) + 8 + HDR, data, 2);
    for (k = 1; k < n; k++) {
        int len = size - off < PAYLOAD ? size - off : PAYLOAD;
        if (len > 0)
            memcpy(page(chain[k]) + 8, data + off, (size_t)len);
        off += PAYLOAD;
    }
}

int eep_save_new(const EepHeader *h, const uint8_t *data)
{
    int chain[PAGES], n = pages_for(h->size), found = 0, i, k;
    uint8_t *p;

    for (i = 0; i < PAGES && found < n; i++)   /* first fit, ascending */
        if (flags(i) & F_FREE)
            chain[found++] = i;
    if (found < n) {
        printf("eeprom: not enough pages for %d bytes (%d needed, %d free)\n", h->size, n,
               found);
        return 0;
    }
    for (k = 0; k < n; k++) {
        int f = flags(chain[k]) & ~(F_FREE | 0x3000);  /* single page: type 0 */
        if (n == 1)
            ;
        else if (k == n - 1)
            f |= 0x3000;
        else
            f = (f & ~0x1ff) | (k == 0 ? 0x1000 : 0x2000) | chain[k + 1];
        set16(page(chain[k]) + 2, f);
    }
    p = page(chain[0]) + 8;
    set16(p, h->app_id);
    set16(p + 2, h->size);
    memset(p + 4, 0, 17 + 33);
    memcpy(p + 4, h->app_name, strnlen(h->app_name, 16));   /* engine: memcpy + NUL, the */
    memcpy(p + 21, h->save_name, strnlen(h->save_name, 32)); /* tail is heap junk; we zero it */
    write_data(chain, n, h->size, data);
    for (k = 0; k < n; k++)
        seal(chain[k]);
    flush();
    return 1;
}

/* Pages of a listed save, from its first page along the next links. */
static int chain_of(const Entry *e, int *chain)
{
    int n = 0, i = e->first;
    while (n < PAGES) {
        int f = flags(i);
        chain[n++] = i;
        if (TYPE(f) == 0 || TYPE(f) == 3)
            break;
        i = NEXT(f);
    }
    return n;
}

int eep_save_existing(int id, const uint8_t *data)
{
    int chain[PAGES], n, k;
    if (id < 0 || id >= g_count)
        return 0;
    n = chain_of(&g_list[id], chain);
    write_data(chain, n, g_list[id].h.size, data);   /* stored size: it cannot change */
    for (k = 0; k < n; k++)
        seal(chain[k]);
    flush();
    return 1;
}

static int enumerate(int app_id, const char *app_name)
{
    int i;
    g_count = 0;
    for (i = 0; i < PAGES; i++) {
        int f = flags(i);
        const uint8_t *p = page(i) + 8;
        Entry *e = &g_list[g_count];
        if ((f & (F_FREE | F_BIT14)) || TYPE(f) >= 2)
            continue;   /* not a first page */
        e->h.app_id = get16(p);
        e->h.size = get16(p + 2);
        memcpy(e->h.app_name, p + 4, 17);
        e->h.app_name[16] = 0;
        memcpy(e->h.save_name, p + 21, 33);
        e->h.save_name[32] = 0;
        e->first = i;
        if (app_name ? e->h.app_id != 0 && !strcmp(e->h.app_name, app_name)
                     : app_id == 0 || e->h.app_id == app_id)
            g_count++;
    }
    return g_count;
}

int eep_enum_by_id(int app_id) { return enumerate(app_id, NULL); }
int eep_enum_by_name(const char *app_name) { return enumerate(0, app_name); }

const char *eep_app_name(int id) { return id >= 0 && id < g_count ? g_list[id].h.app_name : ""; }
const char *eep_save_name(int id) { return id >= 0 && id < g_count ? g_list[id].h.save_name : ""; }

/* As the engine: a buffer of whole pages (n*56 bytes, data first), following the raw next
 * bits; a CRC failure is reported but the data is still returned. */
int eep_load(int id, uint8_t **data, int *size)
{
    int n, k, i, ok = 1;
    uint8_t *d;

    *data = NULL;
    *size = 0;
    if (id < 0 || id >= g_count)
        return 0;
    n = pages_for(g_list[id].h.size);
    d = calloc(1, (size_t)n * PAYLOAD);
    i = g_list[id].first;
    memcpy(d, page(i) + 8 + HDR, 2);
    ok &= page_ok(i);
    for (k = 1; k < n; k++) {
        i = NEXT(flags(i));
        memcpy(d + 2 + (k - 1) * PAYLOAD, page(i) + 8, PAYLOAD);
        ok &= page_ok(i);
    }
    *data = d;
    *size = n * PAYLOAD;
    return ok;
}

void eep_format(void)
{
    int i;
    for (i = 0; i < PAGES; i++)
        init_page(i);
    flush();
}

void eep_corrupt(void)
{
    int i;
    for (i = 0; i < EEP_SIZE; i++)
        g_img[i] = (uint8_t)rand();
    flush();
}

int eep_check(void)
{
    int i, bad = 0;
    for (i = 0; i < PAGES; i++)
        bad += !page_ok(i);
    return bad;
}
