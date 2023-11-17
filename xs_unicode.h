/* copyright (c) 2022 - 2023 grunfink et al. / MIT license */

#ifndef _XS_UNICODE_H

#define _XS_UNICODE_H

 int _xs_utf8_enc(char buf[4], unsigned int cpoint);
 xs_str *xs_utf8_enc(xs_str *str, unsigned int cpoint);
 unsigned int xs_utf8_dec(char **str);
 int xs_unicode_width(unsigned int cpoint);
 int xs_is_surrogate(unsigned int cpoint);
 unsigned int xs_surrogate_dec(unsigned int p1, unsigned int p2);
 unsigned int xs_surrogate_enc(unsigned int cpoint);
 unsigned int *_xs_unicode_upper_search(unsigned int cpoint);
 unsigned int *_xs_unicode_lower_search(unsigned int cpoint);
 #define xs_unicode_is_upper(cpoint) (!!_xs_unicode_upper_search(cpoint))
 #define xs_unicode_is_lower(cpoint) (!!_xs_unicode_lower_search(cpoint))
 unsigned int xs_unicode_to_upper(unsigned int cpoint);
 unsigned int xs_unicode_to_lower(unsigned int cpoint);
 int xs_unicode_nfd(unsigned int cpoint, unsigned int *base, unsigned int *diac);
 int xs_unicode_nfc(unsigned int base, unsigned int diac, unsigned int *cpoint);
 int xs_unicode_is_alpha(unsigned int cpoint);

#ifdef XS_IMPLEMENTATION


int _xs_utf8_enc(char buf[4], unsigned int cpoint)
/* encodes an Unicode codepoint to utf-8 into buf and returns the size in bytes */
{
    unsigned char *p = (unsigned char *)buf;

    if (cpoint < 0x80) /* 1 byte char */
        *p++ = cpoint & 0xff;
    else {
        if (cpoint < 0x800) /* 2 byte char */
            *p++ = 0xc0 | (cpoint >> 6);
        else {
            if (cpoint < 0x10000) /* 3 byte char */
                *p++ = 0xe0 | (cpoint >> 12);
            else { /* 4 byte char */
                *p++ = 0xf0 | (cpoint >> 18);
                *p++ = 0x80 | ((cpoint >> 12) & 0x3f);
            }

            *p++ = 0x80 | ((cpoint >> 6) & 0x3f);
        }

        *p++ = 0x80 | (cpoint & 0x3f);
    }

    return p - (unsigned char *)buf;
}


xs_str *xs_utf8_enc(xs_str *str, unsigned int cpoint)
/* encodes an Unicode codepoint to utf-8 into str */
{
    char tmp[4];

    int c = _xs_utf8_enc(tmp, cpoint);

    return xs_append_m(str, tmp, c);
}


unsigned int xs_utf8_dec(char **str)
/* decodes an utf-8 char inside str and updates the pointer */
{
    unsigned char *p = (unsigned char *)*str;
    unsigned int cpoint = 0;
    int c = *p++;
    int cb = 0;

    if ((c & 0x80) == 0) { /* 1 byte char */
        cpoint = c;
    }
    else
    if ((c & 0xe0) == 0xc0) { /* 2 byte char */
        cpoint = (c & 0x1f) << 6;
        cb = 1;
    }
    else
    if ((c & 0xf0) == 0xe0) { /* 3 byte char */
        cpoint = (c & 0x0f) << 12;
        cb = 2;
    }
    else
    if ((c & 0xf8) == 0xf0) { /* 4 byte char */
        cpoint = (c & 0x07) << 18;
        cb = 3;
    }

    /* process the continuation bytes */
    while (cb--) {
        if ((*p & 0xc0) == 0x80)
            cpoint |= (*p++ & 0x3f) << (cb * 6);
        else {
            cpoint = 0xfffd;
            break;
        }
    }

    *str = (char *)p;
    return cpoint;
}


static int int_range_cmp(const void *p1, const void *p2)
{
    const unsigned int *a = p1;
    const unsigned int *b = p2;

    return *a < b[0] ? -1 : *a > b[1] ? 1 : 0;
}


/* intentionally dead simple */

static unsigned int xs_unicode_width_table[] = {
    0x300,      0x36f,      0,      /* diacritics */
    0x1100,     0x11ff,     2,      /* Hangul */
    0x2e80,     0xa4cf,     2,      /* CJK */
    0xac00,     0xd7a3,     2,      /* more Hangul */
    0xe000,     0xf8ff,     0,      /* private use */
    0xf900,     0xfaff,     2,      /* CJK compatibility */
    0xff00,     0xff60,     2,      /* full width things */
    0xffdf,     0xffe6,     2,      /* full width things */
    0x1f200,    0x1ffff,    2,      /* emojis */
    0x20000,    0x2fffd,    2       /* more CJK */
};

int xs_unicode_width(unsigned int cpoint)
/* returns the width in columns of a Unicode codepoint (somewhat simplified) */
{
    unsigned int *r = bsearch(&cpoint, xs_unicode_width_table,
                        sizeof(xs_unicode_width_table) / (sizeof(unsigned int) * 3),
                        sizeof(unsigned int) * 3,
                        int_range_cmp);

    return r ? r[2] : 1;
}


/** surrogate pairs **/

int xs_is_surrogate(unsigned int cpoint)
/* checks if cpoint is the first element of a Unicode surrogate pair */
{
    return cpoint >= 0xd800 && cpoint <= 0xdfff;
}


unsigned int xs_surrogate_dec(unsigned int p1, unsigned int p2)
/* "decodes" a surrogate pair into a codepoint */
{
    return 0x10000 | ((p1 & 0x3ff) << 10) | (p2 & 0x3ff);
}


unsigned int xs_surrogate_enc(unsigned int cpoint)
/* "encodes" a Unicode into a surrogate pair (p1 in the MSB word) */
{
    unsigned int p1 = 0xd7c0 + (cpoint >> 10);
    unsigned int p2 = 0xdc00 + (cpoint & 0x3ff);

    return (p1 << 16) | p2;
}


#ifdef _XS_UNICODE_TBL_H

/* include xs_unicode_tbl.h before this one to use these functions */

static int int_cmp(const void *p1, const void *p2)
{
    const unsigned int *a = p1;
    const unsigned int *b = p2;

    return *a < *b ? -1 : *a > *b ? 1 : 0;
}


unsigned int *_xs_unicode_upper_search(unsigned int cpoint)
/* searches for an uppercase codepoint in the case fold table */
{
    return bsearch(&cpoint, xs_unicode_case_fold_table,
        sizeof(xs_unicode_case_fold_table) / (sizeof(unsigned int) * 2),
        sizeof(unsigned int) * 2,
        int_cmp);
}


unsigned int *_xs_unicode_lower_search(unsigned int cpoint)
/* searches for a lowercase codepoint in the case fold table */
{
    unsigned int *p = xs_unicode_case_fold_table + 1;
    unsigned int *e = xs_unicode_case_fold_table +
            sizeof(xs_unicode_case_fold_table) / sizeof(unsigned int);

    while (p < e) {
        if (cpoint == *p)
            return p;

        p += 2;
    }

    return NULL;
}


unsigned int xs_unicode_to_upper(unsigned int cpoint)
/* returns the cpoint to uppercase */
{
    unsigned int *p = _xs_unicode_lower_search(cpoint);

    return p == NULL ? cpoint : p[-1];
}


unsigned int xs_unicode_to_lower(unsigned int cpoint)
/* returns the cpoint to lowercase */
{
    unsigned int *p = _xs_unicode_upper_search(cpoint);

    return p == NULL ? cpoint : p[1];
}


int xs_unicode_nfd(unsigned int cpoint, unsigned int *base, unsigned int *diac)
/* applies unicode Normalization Form D */
{
    unsigned int *r = bsearch(&cpoint, xs_unicode_nfd_table,
                        sizeof(xs_unicode_nfd_table) / (sizeof(unsigned int) * 3),
                        sizeof(unsigned int) * 3,
                        int_cmp);

    if (r != NULL) {
        *base = r[1];
        *diac = r[2];
    }

    return !!r;
}


int xs_unicode_nfc(unsigned int base, unsigned int diac, unsigned int *cpoint)
/* applies unicode Normalization Form C */
{
    unsigned int *p = xs_unicode_nfd_table;
    unsigned int *e = xs_unicode_nfd_table +
        sizeof(xs_unicode_nfd_table) / sizeof(unsigned int);

    while (p < e) {
        if (p[1] == base && p[2] == diac) {
            *cpoint = p[0];
            return 1;
        }

        p += 3;
    }

    return 0;
}


int xs_unicode_is_alpha(unsigned int cpoint)
/* checks if a codepoint is an alpha (i.e. a letter) */
{
    unsigned int *r = bsearch(&cpoint, xs_unicode_alpha_table,
                        sizeof(xs_unicode_alpha_table) / (sizeof(unsigned int) * 2),
                        sizeof(unsigned int) * 2,
                        int_range_cmp);

    return !!r;
}


#endif /* _XS_UNICODE_TBL_H */

#endif /* XS_IMPLEMENTATION */

#endif /* _XS_UNICODE_H */
