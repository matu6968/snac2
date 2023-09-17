/* copyright (c) 2022 - 2023 grunfink et al. / MIT license */

#ifndef _XS_MATCH_H

#define _XS_MATCH_H

/* spec is very similar to shell file globbing:
   an * matches anything;
   a ? matches any character;
   | select alternative strings to match;
   a \\ escapes a special character;
   any other char matches itself. */

int xs_match(const char *str, const char *spec);

#ifdef XS_IMPLEMENTATION

int xs_match(const char *str, const char *spec)
{
    const char *o_str = str;

again:
    if (*spec == '*') {
        spec++;                 /* wildcard */

        do {
            if (xs_match(str, spec))
                return 1;
            str++;
        } while (*str);

        return 0;
    }

    if (*spec == '?' && *str) {
        spec++;                 /* any character */
        str++;
        goto again;
    }

    if (*spec == '|')
        return 1;               /* alternative separator? positive match */

    if (!*spec)
        return 1;               /* end of spec? positive match */

    if (*spec == '\\')
        spec++;                 /* escaped char */

    if (*spec == *str) {
        spec++;                 /* matched 1 char */
        str++;
        goto again;
    }

    /* not matched; are there any alternatives? */
    while (*spec) {
        if (*spec == '|')
            return xs_match(o_str, spec + 1);   /* try next alternative */

        if (*spec == '\\')
            spec++;             /* escaped char */
        spec++;
    }

    return 0;
}

#endif /* XS_IMPLEMENTATION */

#endif /* XS_MATCH_H */
