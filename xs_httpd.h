/* copyright (c) 2022 - 2023 grunfink et al. / MIT license */

#ifndef _XS_HTTPD_H

#define _XS_HTTPD_H

xs_dict *xs_httpd_request(FILE *f, xs_str **payload, int *p_size);
void xs_httpd_response(FILE *f, int status, xs_dict *headers, xs_str *body, int b_size);


#ifdef XS_IMPLEMENTATION

xs_dict *xs_httpd_request(FILE *f, xs_str **payload, int *p_size)
/* processes an httpd connection */
{
    xs *q_vars = NULL;
    xs *p_vars = NULL;
    xs *l1, *l2;
    char *v;

    xs_socket_timeout(fileno(f), 2.0, 0.0);

    /* read the first line and split it */
    l1 = xs_strip_i(xs_readline(f));
    l2 = xs_split(l1, " ");

    if (xs_list_len(l2) != 3) {
        /* error or timeout */
        return NULL;
    }

    xs_dict *req = xs_dict_new();

    req = xs_dict_append(req, "method", xs_list_get(l2, 0));
    req = xs_dict_append(req, "proto",  xs_list_get(l2, 2));

    {
        /* split the path with its optional variables */
        xs *udp = xs_url_dec(xs_list_get(l2, 1));
        xs *pnv = xs_split_n(udp, "?", 1);

        /* store the path */
        req = xs_dict_append(req, "path", xs_list_get(pnv, 0));

        /* get the variables */
        q_vars = xs_url_vars(xs_list_get(pnv, 1));
    }

    /* read the headers */
    for (;;) {
        xs *l, *p = NULL;

        l = xs_strip_i(xs_readline(f));

        /* done with the header? */
        if (strcmp(l, "") == 0)
            break;

        /* split header and content */
        p = xs_split_n(l, ": ", 1);

        if (xs_list_len(p) == 2)
            req = xs_dict_append(req, xs_tolower_i(xs_list_get(p, 0)), xs_list_get(p, 1));
    }

    xs_socket_timeout(fileno(f), 5.0, 0.0);

    if ((v = xs_dict_get(req, "content-length")) != NULL) {
        /* if it has a payload, load it */
        *p_size  = atoi(v);
        *payload = xs_read(f, p_size);
    }

    v = xs_dict_get(req, "content-type");

    if (*payload && v && strcmp(v, "application/x-www-form-urlencoded") == 0) {
        xs *upl = xs_url_dec(*payload);
        p_vars  = xs_url_vars(upl);
    }
    else
    if (*payload && v && xs_startswith(v, "multipart/form-data")) {
        p_vars = xs_multipart_form_data(*payload, *p_size, v);
    }
    else
        p_vars = xs_dict_new();

    req = xs_dict_append(req, "q_vars",  q_vars);
    req = xs_dict_append(req, "p_vars",  p_vars);

    if (errno)
        req = xs_free(req);

    return req;
}


void xs_httpd_response(FILE *f, int status, xs_dict *headers, xs_str *body, int b_size)
/* sends an httpd response */
{
    xs *proto;
    xs_dict *p;
    xs_str *k;
    xs_val *v;

    proto = xs_fmt("HTTP/1.1 %d %s", status, status / 100 == 2 ? "OK" : "ERROR");
    fprintf(f, "%s\r\n", proto);

    p = headers;
    while (xs_dict_iter(&p, &k, &v)) {
        fprintf(f, "%s: %s\r\n", k, v);
    }

    if (b_size != 0)
        fprintf(f, "content-length: %d\r\n", b_size);

    fprintf(f, "\r\n");

    if (body != NULL && b_size != 0)
        fwrite(body, b_size, 1, f);
}


#endif /* XS_IMPLEMENTATION */

#endif /* XS_HTTPD_H */
