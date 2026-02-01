/* copyright (c) 2022 - 2026 grunfink et al. / MIT license */

#ifndef _XS_GLOB_H

#define _XS_GLOB_H

xs_list *xs_glob_n(const char *spec, int basename, int reverse, int mark, int max);
#define xs_glob(spec, basename, reverse) xs_glob_n(spec, basename, reverse, 0, XS_ALL)
#define xs_glob_m(spec, basename, reverse) xs_glob_n(spec, basename, reverse, 1, XS_ALL)


#ifdef XS_IMPLEMENTATION

#ifdef SNAC_ESP32

#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>

static int xs_glob_match_pat(const char *pat, const char *str)
{
    while (*pat) {
        if (*pat == '*') {
            pat++;
            if (*pat == '\0')
                return 1;
            while (*str) {
                if (xs_glob_match_pat(pat, str))
                    return 1;
                str++;
            }
            return 0;
        }
        if (*str == '\0')
            return 0;
        if (*pat != *str)
            return 0;
        pat++;
        str++;
    }
    return *str == '\0';
}

static int xs_glob_qsort_cmp(const void *a, const void *b)
{
    const char *aa = *(const char * const *)a;
    const char *bb = *(const char * const *)b;
    return strcmp(aa, bb);
}

xs_list *xs_glob_n(const char *spec, int basename, int reverse, int mark, int max)
/* ESP32/newlib: implement a small glob() replacement */
{
    xs_list *list = xs_list_new();

    if (spec == NULL)
        return list;

    /* split spec into dir + pattern */
    const char *slash = strrchr(spec, '/');
    xs *dir = NULL;
    const char *pattern = spec;

    if (slash) {
        dir = xs_str_new_sz(spec, (int)(slash - spec));
        pattern = slash + 1;
    }
    else
        dir = xs_str_new(".");

    /* If there is no wildcard, just stat() and return if exists. */
    if (strchr(pattern, '*') == NULL) {
        struct stat st;
        if (stat(spec, &st) == 0) {
            const char *p = spec;
            xs *out = NULL;

            if (basename) {
                if ((p = strrchr(spec, '/')) != NULL)
                    p++;
            }

            out = xs_str_new(p);
            if (mark && S_ISDIR(st.st_mode))
                out = xs_str_cat(out, "/");

            list = xs_list_append(list, out);
        }

        return list;
    }

    DIR *dp = opendir(dir);
    if (dp == NULL)
        return list;

    /* collect candidates first so we can sort/reverse and apply max */
    char **items = NULL;
    int items_sz = 0;
    int items_cap = 0;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        const char *name = de->d_name;

        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
            continue;

        if (!xs_glob_match_pat(pattern, name))
            continue;

        xs *full = xs_fmt("%s/%s", dir, name);
        struct stat st;

        if (stat(full, &st) != 0)
            continue;

        const char *outp = basename ? name : full;
        xs *out = xs_str_new(outp);
        if (mark && S_ISDIR(st.st_mode))
            out = xs_str_cat(out, "/");

        if (items_sz == items_cap) {
            int ncap = items_cap == 0 ? 16 : items_cap * 2;
            items = xs_realloc(items, sizeof(char *) * ncap);
            items_cap = ncap;
        }

        /* xs strings are owned by list; keep a copy for sorting */
        items[items_sz++] = xs_dup(out);
    }

    closedir(dp);

    if (items_sz > 1) {
        qsort(items, items_sz, sizeof(char *), xs_glob_qsort_cmp);
    }

    if (max <= 0 || max > items_sz)
        max = items_sz;

    for (int i = 0; i < max; i++) {
        int idx = reverse ? (items_sz - i - 1) : i;
        list = xs_list_append(list, items[idx]);
        xs_free(items[idx]);
    }

    xs_free(items);

    return list;
}

#else /* SNAC_ESP32 */

#include <glob.h>

xs_list *xs_glob_n(const char *spec, int basename, int reverse, int mark, int max)
/* does a globbing and returns the found files */
{
    glob_t globbuf;
    xs_list *list = xs_list_new();

    if (glob(spec, mark ? GLOB_MARK : 0, NULL, &globbuf) == 0) {
        int n;

        if (max > (int) globbuf.gl_pathc)
            max = globbuf.gl_pathc;

        for (n = 0; n < max; n++) {
            char *p;

            if (reverse)
                p = globbuf.gl_pathv[globbuf.gl_pathc - n - 1];
            else
                p = globbuf.gl_pathv[n];

            if (p != NULL) {
                if (basename) {
                    if ((p = strrchr(p, '/')) == NULL)
                        continue;

                    p++;
                }

                list = xs_list_append(list, p);
            }
        }
    }

    globfree(&globbuf);

    return list;
}

#endif /* SNAC_ESP32 */

#endif /* XS_IMPLEMENTATION */

#endif /* _XS_GLOB_H */
