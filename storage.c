/*
 * storage.c - the Kalkylark file format.
 *
 * Plain text, one record per line, in the spirit of VisiCalc's files
 * (which were essentially replayable keystrokes):
 *
 *     # kalkylark 1
 *     /GO C               recalc order (R, C or N for natural)
 *     /GC 9               global column width
 *     /GF G               global format
 *     >A1:"Price          a label (always stored with its '"')
 *     >B1:/F$:19.95       a value with a cell format
 *     >B2:+B1*2           a value
 *
 * Lines starting with '#' and blank lines are ignored. Saving writes a
 * temporary file and renames it over the target, so a failed save never
 * destroys the old file.
 */
#define _POSIX_C_SOURCE 200809L

#include "sheet.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 1024

int sheet_save(const char *path, char *err, size_t errsize)
{
    char tmp[4096], name[4];
    const struct cell *c;
    FILE *f;
    int r, col, ok;

    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) {
        snprintf(err, errsize, "file name too long");
        return -1;
    }
    f = fopen(tmp, "w");
    if (f == NULL) {
        snprintf(err, errsize, "%s", strerror(errno));
        return -1;
    }

    fprintf(f, "# kalkylark 1\n");
    fprintf(f, "/GO %c\n/GC %d\n/GF %c\n", sheet_order, sheet_colw, sheet_gfmt);
    for (r = 0; r < SHEET_ROWS; r++) {
        for (col = 0; col < SHEET_COLS; col++) {
            c = sheet_cell(r, col);
            if (c->type == CELL_EMPTY && c->fmt == 0)
                continue;
            fprintf(f, ">%s%d:", col_name(col, name), r + 1);
            if (c->fmt)
                fprintf(f, "/F%c:", c->fmt);
            if (c->type == CELL_LABEL)
                fprintf(f, "\"%s", c->text);
            else if (c->type == CELL_VALUE)
                fputs(c->text, f);
            fputc('\n', f);
        }
    }

    ok = !ferror(f);
    if (fclose(f) != 0)
        ok = 0;
    if (!ok || rename(tmp, path) != 0) {
        snprintf(err, errsize, "%s", strerror(errno));
        remove(tmp);
        return -1;
    }
    sheet_dirty = 0;
    return 0;
}

/* Parse one line into the sheet. Returns NULL or an error message. */
static const char *load_line(char *line)
{
    char *colon, *body;
    const char *msg;
    int r, c, pos, fmt = 0, n;

    if (line[0] == '\0' || line[0] == '#')
        return NULL;

    if (strncmp(line, "/GO ", 4) == 0) {
        if (line[4] != 'R' && line[4] != 'C' && line[4] != 'N')
            return "bad recalc order";
        sheet_order = line[4];
        return NULL;
    }
    if (strncmp(line, "/GC ", 4) == 0) {
        n = atoi(line + 4);
        if (n < COLW_MIN || n > COLW_MAX)
            return "bad column width";
        sheet_colw = n;
        return NULL;
    }
    if (strncmp(line, "/GF ", 4) == 0) {
        if (!sheet_valid_format((unsigned char)line[4]))
            return "bad global format";
        sheet_gfmt = line[4];
        return NULL;
    }
    if (line[0] != '>')
        return "unknown record";

    colon = strchr(line, ':');
    if (colon == NULL)
        return "missing ':' after coordinate";
    *colon = '\0';
    if (parse_coord(line + 1, &r, &c) != 0)
        return "bad coordinate";
    body = colon + 1;

    if (body[0] == '/' && body[1] == 'F' && body[2] != '\0' && body[3] == ':') {
        fmt = (unsigned char)body[2];
        if (!sheet_valid_format(fmt))
            return "bad cell format";
        body += 4;
    }
    if (body[0] != '\0' && sheet_enter(r, c, body, &msg, &pos) != 0)
        return msg;
    if (fmt)
        sheet_set_format(r, c, fmt);
    return NULL;
}

int sheet_load(const char *path, char *err, size_t errsize)
{
    char line[LINE_MAX_LEN];
    const char *msg;
    FILE *f;
    size_t len;
    int lineno = 0;

    f = fopen(path, "r");
    if (f == NULL) {
        snprintf(err, errsize, "%s", strerror(errno));
        return -1;
    }
    sheet_clear();
    while (fgets(line, sizeof line, f) != NULL) {
        lineno++;
        len = strlen(line);
        if (len > 0 && line[len - 1] != '\n' && !feof(f)) {
            snprintf(err, errsize, "line %d: too long", lineno);
            fclose(f);
            return -1;
        }
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if ((msg = load_line(line)) != NULL) {
            snprintf(err, errsize, "line %d: %s", lineno, msg);
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    sheet_recalc();
    sheet_dirty = 0;
    return 0;
}
