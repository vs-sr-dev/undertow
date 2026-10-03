/* Save-game EEPROM: the console's 32 KB AT25256A image, persisted to a host file, and the
 * engine's save manager (apps/zit/eeprom_mgr.c) that keeps named saves in it. */
#ifndef UNDERTOW_EEPROM_H
#define UNDERTOW_EEPROM_H

#include <stdint.h>

#define EEP_SIZE 0x8000

typedef struct {
    int app_id;          /* u16 */
    int size;            /* u16, data bytes */
    char app_name[17];   /* 16 + NUL */
    char save_name[33];  /* 32 + NUL */
} EepHeader;

/* Load the image from path, or start from a formatted one if the file is missing. */
void eep_open(const char *path);

/* Engine save manager. IDs index the list built by the last Enumerate call. */
int eep_save_new(const EepHeader *h, const uint8_t *data); /* 1 on success */
int eep_save_existing(int id, const uint8_t *data);        /* 1 on success */
int eep_enum_by_id(int app_id);                            /* number of saves */
int eep_enum_by_name(const char *app_name);
const char *eep_app_name(int id);                          /* "" for a bad id */
const char *eep_save_name(int id);
int eep_load(int id, uint8_t **data, int *size);           /* 1 on success, data malloc'd */
void eep_format(void);
void eep_corrupt(void);
int eep_check(void);                                       /* number of bad pages */

#endif
