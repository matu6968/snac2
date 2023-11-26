/* copyright (c) 2022 - 2023 grunfink et al. / MIT license */

#ifndef _XS_HTML_H

#define _XS_HTML_H

typedef struct xs_html xs_html;

xs_str *xs_html_encode(char *str);

xs_html *xs_html_attr(char *key, char *value);
xs_html *xs_html_text(char *content);
xs_html *xs_html_raw(char *content);

xs_html *_xs_html_add(xs_html *tag, xs_html *var[]);
#define xs_html_add(tag, ...) _xs_html_add(tag, (xs_html *[]) { __VA_ARGS__, NULL })

xs_html *_xs_html_tag(char *tag, xs_html *var[]);
#define xs_html_tag(tag, ...) _xs_html_tag(tag, (xs_html *[]) { __VA_ARGS__, NULL })

xs_html *_xs_html_sctag(char *tag, xs_html *var[]);
#define xs_html_sctag(tag, ...) _xs_html_sctag(tag, (xs_html *[]) { __VA_ARGS__, NULL })

xs_str *xs_html_render_s(xs_html *h, xs_str *s);
#define xs_html_render(h) xs_html_render_s(h, xs_str_new(NULL))

#ifdef XS_IMPLEMENTATION

typedef enum {
    XS_HTML_TAG,
    XS_HTML_SCTAG,
    XS_HTML_ATTR,
    XS_HTML_TEXT
} xs_html_type;

struct xs_html {
    xs_html_type type;
    xs_str *content;
    xs_html *f_attr;
    xs_html *l_attr;
    xs_html *f_tag;
    xs_html *l_tag;
    xs_html *next;
};

xs_str *xs_html_encode(char *str)
/* encodes str using HTML entities */
{
    xs_str *s = xs_str_new(NULL);
    int o = 0;
    char *e = str + strlen(str);

    for (;;) {
        char *ec = "<>\"'&";   /* characters to escape */
        char *q = e;
        int z;

        /* find the nearest happening of a char */
        while (*ec) {
            char *m = memchr(str, *ec++, q - str);
            if (m)
                q = m;
        }

        /* copy string to here */
        z = q - str;
        s = xs_insert_m(s, o, str, z);
        o += z;

        /* if q points to the end, nothing more to do */
        if (q == e)
            break;

        /* insert the escaped char */
        char tmp[8];
        snprintf(tmp, sizeof(tmp), "&#%d;", *q);

        z = strlen(tmp);
        s = xs_insert_m(s, o, tmp, z);
        o += z;

        str = q + 1;
    }

    return s;
}


#define XS_HTML_NEW() memset(xs_realloc(NULL, sizeof(xs_html)), '\0', sizeof(xs_html))

xs_html *xs_html_attr(char *key, char *value)
/* creates an HTML block with an attribute */
{
    xs_html *a = XS_HTML_NEW();

    a->type = XS_HTML_ATTR;

    if (value) {
        xs *ev = xs_html_encode(value);
        a->content = xs_fmt("%s=\"%s\"", key, ev);
    }
    else
        a->content = xs_dup(key);

    return a;
}


xs_html *xs_html_text(char *content)
/* creates an HTML block of text, escaping it previously */
{
    xs_html *a = XS_HTML_NEW();

    a->type    = XS_HTML_TEXT;
    a->content = xs_html_encode(content);

    return a;
}


xs_html *xs_html_raw(char *content)
/* creates an HTML block without escaping (for pre-formatted HTML, comments, etc) */
{
    xs_html *a = XS_HTML_NEW();

    a->type    = XS_HTML_TEXT;
    a->content = xs_dup(content);

    return a;
}


xs_html *_xs_html_add(xs_html *tag, xs_html *var[])
/* add data (attrs, tags or text) to a tag */
{
    while (*var) {
        xs_html *data = *var++;

        xs_html **first;
        xs_html **last;

        if (data->type == XS_HTML_ATTR) {
            first = &tag->f_attr;
            last  = &tag->l_attr;
        }
        else {
            first = &tag->f_tag;
            last  = &tag->l_tag;
        }

        if (*first == NULL)
            *first = data;

        if (*last != NULL)
            (*last)->next = data;

        *last = data;
    }

    return tag;
}


static xs_html *_xs_html_tag_t(xs_html_type type, char *tag, xs_html *var[])
/* creates a tag with a variable list of attributes and subtags */
{
    xs_html *a = XS_HTML_NEW();

    a->type    = type;
    a->content = xs_dup(tag);

    _xs_html_add(a, var);

    return a;
}


xs_html *_xs_html_tag(char *tag, xs_html *var[])
{
    return _xs_html_tag_t(XS_HTML_TAG, tag, var);
}


xs_html *_xs_html_sctag(char *tag, xs_html *var[])
{
    return _xs_html_tag_t(XS_HTML_SCTAG, tag, var);
}


xs_str *xs_html_render_s(xs_html *h, xs_str *s)
/* renders the tag and its subtags into s */
{
    xs_html *st;

    switch (h->type) {
    case XS_HTML_TAG:
    case XS_HTML_SCTAG:
        s = xs_str_cat(s, "<", h->content);

        /* render the attributes */
        st = h->f_attr;
        while (st) {
            xs_html *nst = st->next;
            s = xs_html_render_s(st, s);
            st = nst;
        }

        if (h->type == XS_HTML_SCTAG) {
            /* self-closing tags should not have subtags */
            s = xs_str_cat(s, "/>\n");
        }
        else {
            s = xs_str_cat(s, ">");

            /* render the subtags */
            st = h->f_tag;
            while (st) {
                xs_html *nst = st->next;
                s = xs_html_render_s(st, s);
                st = nst;
            }

            s = xs_str_cat(s, "</", h->content, ">");
        }

        break;

    case XS_HTML_ATTR:
        s = xs_str_cat(s, " ", h->content);
        break;

    case XS_HTML_TEXT:
        s = xs_str_cat(s, h->content);
        break;
    }

    xs_free(h->content);
    xs_free(h);

    return s;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_HTML_H */
