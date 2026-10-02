/* gamewave.diz: tiny INI with [global] appname/appfile/version and
 * [platform] board/engine/version. */
#ifndef UNDERTOW_DIZ_H
#define UNDERTOW_DIZ_H

typedef struct {
    char appname[64];
    char appfile[256];
    char version[64];
    char engine[256];
    char engine_version[64];
    int board;
} Diz;

int diz_parse(const char *text, Diz *out);

#endif
