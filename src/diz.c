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

int diz_parse(const char *text, Diz *out)
{
    char section[32] = "", key[64], val[256];
    const char *line = text;

    memset(out, 0, sizeof(*out));
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        const char *eq = memchr(line, '=', len);

        if (len && line[0] == '[') {
            const char *close = memchr(line, ']', len);
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
                if (!strcasecmp(key, "engine")) snprintf(out->engine, sizeof(out->engine), "%s", val);
                else if (!strcasecmp(key, "version")) snprintf(out->engine_version, sizeof(out->engine_version), "%s", val);
                else if (!strcasecmp(key, "board")) out->board = atoi(val);
            }
        }
        line += len + (end ? 1 : 0);
    }
    return out->appfile[0] ? 0 : -1;
}
