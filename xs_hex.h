/* copyright (c) 2022 - 2023 grunfink et al. / MIT license */

#ifndef _XS_HEX_H

#define _XS_HEX_H

xs_str *xs_hex_enc(const xs_val *data, int size);
xs_val *xs_hex_dec(const xs_str *hex, int *size);
int xs_is_hex(const char *str);

#ifdef XS_IMPLEMENTATION

/** hex **/

static char rev_hex_digits[] = "fedcba9876543210FEDCBA";

xs_str *xs_hex_enc(const xs_val *data, int size)
/* returns an hexdump of data */
{
    xs_str *s;
    char *p;
    int n;

    p = s = xs_realloc(NULL, _xs_blk_size(size * 2 + 1));

    for (n = 0; n < size; n++) {
        *p++ = rev_hex_digits[0xf - (*data >> 4 & 0xf)];
        *p++ = rev_hex_digits[0xf - (*data      & 0xf)];
        data++;
    }

    *p = '\0';

    return s;
}


xs_val *xs_hex_dec(const xs_str *hex, int *size)
/* decodes an hexdump into data */
{
    int sz = strlen(hex);
    xs_val *s = NULL;
    char *p;
    int n;

    if (sz % 2)
        return NULL;

    p = s = xs_realloc(NULL, _xs_blk_size(sz / 2 + 1));

    for (n = 0; n < sz; n += 2) {
        char *d1 = strchr(rev_hex_digits, *hex++);
        char *d2 = strchr(rev_hex_digits, *hex++);

        if (!d1 || !d2) {
            /* decoding error */
            return xs_free(s);
        }

        *p++ = (0xf - ((d1 - rev_hex_digits) & 0xf)) << 4 |
               (0xf - ((d2 - rev_hex_digits) & 0xf));
    }

    *p = '\0';
    *size = sz / 2;

    return s;
}


int xs_is_hex(const char *str)
/* returns 1 if str is an hex string */
{
    while (*str) {
        if (strchr(rev_hex_digits, *str++) == NULL)
            return 0;
    }

    return 1;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_HEX_H */
