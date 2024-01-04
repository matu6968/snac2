/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#ifndef _XS_URL_H

#define _XS_URL_H

xs_str *xs_url_dec(const char *str);
xs_dict *xs_url_vars(const char *str);
xs_dict *xs_multipart_form_data(const char *payload, int p_size, const char *header);


#ifdef XS_IMPLEMENTATION

xs_str *xs_url_dec(const char *str)
/* decodes an URL */
{
    xs_str *s = xs_str_new(NULL);

    while (*str) {
        if (*str == '%') {
            int i;

            if (sscanf(str + 1, "%02x", &i) == 1) {
                unsigned char uc = i;

                s = xs_append_m(s, (char *)&uc, 1);
                str += 2;
            }
        }
        else
        if (*str == '+')
            s = xs_append_m(s, " ", 1);
        else
            s = xs_append_m(s, str, 1);

        str++;
    }

    return s;
}


xs_dict *xs_url_vars(const char *str)
/* parse url variables */
{
    xs_dict *vars;

    vars = xs_dict_new();

    if (str != NULL) {
        /* split by arguments */
        xs *args = xs_split(str, "&");

        xs_list *l;
        xs_val *v;

        l = args;
        while (xs_list_iter(&l, &v)) {
            xs *kv = xs_split_n(v, "=", 2);

            if (xs_list_len(kv) == 2) {
                const char *key = xs_list_get(kv, 0);
                const char *pv  = xs_dict_get(vars, key);

                if (!xs_is_null(pv)) {
                    /* there is a previous value: convert to a list and append */
                    xs *vlist = NULL;
                    if (xs_type(pv) == XSTYPE_LIST)
                        vlist = xs_dup(pv);
                    else {
                        vlist = xs_list_new();
                        vlist = xs_list_append(vlist, pv);
                    }

                    vlist = xs_list_append(vlist, xs_list_get(kv, 1));
                    vars  = xs_dict_set(vars, key, vlist);
                }
                else {
                    /* ends with []? force to always be a list */
                    if (xs_endswith(key, "[]")) {
                        xs *vlist = xs_list_new();
                        vlist = xs_list_append(vlist, xs_list_get(kv, 1));
                        vars = xs_dict_append(vars, key, vlist);
                    }
                    else
                        vars = xs_dict_append(vars, key, xs_list_get(kv, 1));
                }
            }
        }
    }

    return vars;
}


xs_dict *xs_multipart_form_data(const char *payload, int p_size, const char *header)
/* parses a multipart/form-data payload */
{
    xs *boundary = NULL;
    int offset = 0;
    int bsz;
    char *p;

    /* build the boundary string */
    {
        xs *l1 = xs_split(header, "=");

        if (xs_list_len(l1) != 2)
            return NULL;

        boundary = xs_fmt("--%s", xs_list_get(l1, 1));
    }

    bsz = strlen(boundary);

    xs_dict *p_vars = xs_dict_new();

    /* iterate searching the boundaries */
    while ((p = xs_memmem(payload + offset, p_size - offset, boundary, bsz)) != NULL) {
        xs *s1 = NULL;
        xs *l1 = NULL;
        char *vn = NULL;
        char *fn = NULL;
        char *q;
        int po, ps;

        /* final boundary? */
        p += bsz;

        if (p[0] == '-' && p[1] == '-')
            break;

        /* skip the \r\n */
        p += 2;

        /* now on a Content-Disposition... line; get it */
        q = strchr(p, '\r');
        s1 = xs_realloc(NULL, q - p + 1);
        memcpy(s1, p, q - p);
        s1[q - p] = '\0';

        /* move on (over a \r\n) */
        p = q;

        /* split by " like a primitive man */
        l1 = xs_split(s1, "\"");

        /* get the variable name */
        vn = xs_list_get(l1, 1);

        /* is it an attached file? */
        if (xs_list_len(l1) >= 4 && strcmp(xs_list_get(l1, 2), "; filename=") == 0) {
            /* get the file name */
            fn = xs_list_get(l1, 3);
        }

        /* find the start of the part content */
        if ((p = xs_memmem(p, p_size - (p - payload), "\r\n\r\n", 4)) == NULL)
            break;

        p += 4;

        /* find the next boundary */
        if ((q = xs_memmem(p, p_size - (p - payload), boundary, bsz)) == NULL)
            break;

        po = p - payload;
        ps = q - p - 2;     /* - 2 because the final \r\n */

        /* is it a filename? */
        if (fn != NULL) {
            /* p_var value is a list */
            xs *l1 = xs_list_new();
            xs *vpo = xs_number_new(po);
            xs *vps = xs_number_new(ps);

            l1 = xs_list_append(l1, fn);
            l1 = xs_list_append(l1, vpo);
            l1 = xs_list_append(l1, vps);

            p_vars = xs_dict_append(p_vars, vn, l1);
        }
        else {
            /* regular variable; just copy */
            xs *vc = xs_realloc(NULL, ps + 1);
            memcpy(vc, payload + po, ps);
            vc[ps] = '\0';

            p_vars = xs_dict_append(p_vars, vn, vc);
        }

        /* move on */
        offset = q - payload;
    }

    return p_vars;
}


#endif /* XS_IMPLEMENTATION */

#endif /* XS_URL_H */
