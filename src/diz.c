#include "diz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void copy_trim(char *dst, size_t dstsize, const char *s, size_t n)
{
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
        n--;
    while (n && (*s == ' ' || *s == '\t')) {
        s++;
        n--;
    }
    if (n >= dstsize)
        n = dstsize - 1;
    memcpy(dst, s, n);
    dst[n] = 0;
}

/* A disc may list several [platform] sections (Rewind: board 3 and board a); keep the
 * board 3 one (the Game Wave), else the first. */
static void commit_platform(Diz *out, const Diz *p, int *have)
{
    if (!p->engine[0] || (*have && (out->board == 3 || p->board != 3)))
        return;
    snprintf(out->engine, sizeof(out->engine), "%s", p->engine);
    snprintf(out->engine_version, sizeof(out->engine_version), "%s", p->engine_version);
    out->board = p->board;
    *have = 1;
}

int diz_parse(const char *text, Diz *out)
{
    char section[32] = "", key[64], val[256];
    const char *line = text;
    Diz plat;
    int have = 0;

    memset(out, 0, sizeof(*out));
    memset(&plat, 0, sizeof(plat));
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        const char *eq = memchr(line, '=', len);

        if (len && line[0] == '[') {
            const char *close = memchr(line, ']', len);
            commit_platform(out, &plat, &have);
            memset(&plat, 0, sizeof(plat));
            if (close)
                copy_trim(section, sizeof(section), line + 1, (size_t)(close - line - 1));
        } else if (eq) {
            copy_trim(key, sizeof(key), line, (size_t)(eq - line));
            copy_trim(val, sizeof(val), eq + 1, len - (size_t)(eq - line) - 1);
            if (!strcasecmp(section, "global")) {
                if (!strcasecmp(key, "appname")) snprintf(out->appname, sizeof(out->appname), "%s", val);
                else if (!strcasecmp(key, "appfile")) snprintf(out->appfile, sizeof(out->appfile), "%s", val);
                else if (!strcasecmp(key, "version")) snprintf(out->version, sizeof(out->version), "%s", val);
            } else if (!strcasecmp(section, "platform")) {
                if (!strcasecmp(key, "engine")) snprintf(plat.engine, sizeof(plat.engine), "%s", val);
                else if (!strcasecmp(key, "version")) snprintf(plat.engine_version, sizeof(plat.engine_version), "%s", val);
                else if (!strcasecmp(key, "board")) plat.board = atoi(val);
            }
        }
        line += len + (end ? 1 : 0);
    }
    commit_platform(out, &plat, &have);
    return out->appfile[0] ? 0 : -1;
}
