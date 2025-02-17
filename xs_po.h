/* copyright (c) 2025 grunfink et al. / MIT license */

#ifndef _XS_PO_H

#define _XS_PO_H

xs_dict *xs_po_to_dict(const char *fn);

#ifdef XS_IMPLEMENTATION

xs_dict *xs_po_to_dict(const char *fn)
/* converts a PO file to a dict */
{
    xs_dict *d = NULL;
    FILE *f;

    if ((f = fopen(fn, "r")) != NULL) {
        d = xs_dict_new();

        xs *k = NULL;
        xs *v = NULL;
        enum { IN_NONE, IN_K, IN_V } mode = IN_NONE;

        while (!feof(f)) {
            xs *l = xs_strip_i(xs_readline(f));

            /* discard empty lines and comments */
            if (*l == '\0' || *l == '#')
                continue;

            if (xs_startswith(l, "msgid ")) {
                if (mode == IN_V) {
                    /* flush */
                    if (xs_is_string(k) && xs_is_string(v) && *v)
                        d = xs_dict_set(d, k, v);

                    k = xs_free(k);
                    v = xs_free(v);
                }

                l = xs_replace_i(l, "msgid ", "");
                mode = IN_K;

                k = xs_str_new(NULL);
            }
            else
            if (xs_startswith(l, "msgstr ")) {
                if (mode != IN_K)
                    break;

                l = xs_replace_i(l, "msgstr ", "");
                mode = IN_V;

                v = xs_str_new(NULL);
            }

            l = xs_replace_i(l, "\\n", "\n");
            l = xs_strip_chars_i(l, "\"");

            switch (mode) {
            case IN_K:
                k = xs_str_cat(k, l);
                break;

            case IN_V:
                v = xs_str_cat(v, l);
                break;

            case IN_NONE:
                break;
            }
        }

        /* final flush */
        if (xs_is_string(k) && xs_is_string(v) && *v)
            d = xs_dict_set(d, k, v);

        fclose(f);
    }

    return d;
}

#endif /* XS_IMPLEMENTATION */

#endif /* XS_PO_H */
