/* copyright (c) 2025 - 2026 grunfink et al. / MIT license */

#ifndef _XS_WEBMENTION_H

#define _XS_WEBMENTION_H

int xs_webmention_send(const char *source, const char *target, const char *user_agent);
int xs_webmention_hook(const char *source, const char *target, const char *user_agent);


#ifdef XS_IMPLEMENTATION

#include "xs_http.h"

int xs_webmention_send(const char *source, const char *target, const char *user_agent)
/* sends a Webmention to target.
   Returns: < 0, error; 0, no Webmention endpoint; > 0, Webmention sent */
{
    int status = 0;
    xs *endpoint = NULL;

    xs *ua = xs_fmt("%s (Webmention)", user_agent ? user_agent : "xs_webmention");
    xs *headers = xs_dict_new();
    headers = xs_dict_set(headers, "accept", "text/html");
    headers = xs_dict_set(headers, "user-agent", ua);

    xs *h_req = NULL;
    int p_size = 0;

    /* try first a HEAD, to see if there is a Webmention Link header */
    h_req = xs_http_request("HEAD", target, headers, NULL, 0, &status, NULL, &p_size, 0);

    /* return immediate failures */
    if (!xs_http_valid_status(status))
        return -1;

    const char *link = xs_dict_get(h_req, "link");

    if (xs_is_string(link) && xs_regex_match(link, "rel *= *(\"|')?webmention")) {
        /* endpoint is between < and > */
        xs *r = xs_regex_select_n(link, "<[^>]+>", 1);

        if (xs_list_len(r) == 1) {
            endpoint = xs_dup(xs_list_get(r, 0));
            endpoint = xs_strip_chars_i(endpoint, "<>");
        }
    }

    if (endpoint == NULL) {
        /* no Link header; get the content */
        xs *g_req = NULL;
        xs *payload = NULL;

        g_req = xs_http_request("GET", target, headers, NULL, 0, &status, &payload, &p_size, 0);

        if (!xs_http_valid_status(status))
            return -1;

        const char *ctype = xs_dict_get(g_req, "content-type");

        /* not HTML? no point in looking inside */
        if (!xs_is_string(ctype) || xs_str_in(ctype, "text/html") == -1)
            return -2;

        if (!xs_is_string(payload))
            return -3;

        xs *links = xs_regex_select(payload, "<(a +|link +)[^>]+>");
        const char *link;

        xs_list_foreach(links, link) {
            if (xs_regex_match(link, "rel *= *(\"|')?webmention")) {
                /* found; extract the href */
                xs *r = xs_regex_select_n(link, "href *= *(\"|')?[^\"]+(\"|')", 1);

                if (xs_list_len(r) == 1) {
                    xs *l = xs_split_n(xs_list_get(r, 0), "=", 1);

                    if (xs_list_len(l) == 2) {
                        endpoint = xs_dup(xs_list_get(l, 1));
                        endpoint = xs_strip_chars_i(endpoint, " \"'");

                        break;
                    }
                }
            }
        }
    }

    /* is it a relative endpoint? */
    if (xs_is_string(endpoint)) {
        if (!xs_startswith(endpoint, "https://") && !xs_startswith(endpoint, "http://")) {
            xs *l = xs_split(target, "/");

            if (xs_list_len(l) < 3)
                endpoint = xs_free(endpoint);
            else {
                xs *s = xs_fmt("%s/" "/%s", xs_list_get(l, 0), xs_list_get(l, 2));
                endpoint = xs_str_wrap_i(s, endpoint, NULL);
            }
        }
    }

    if (xs_is_string(endpoint)) {
        /* got it! */
        headers = xs_dict_set(headers, "content-type", "application/x-www-form-urlencoded");

        xs *body = xs_fmt("source=%s&target=%s", source, target);

        xs *rsp = xs_http_request("POST", endpoint, headers, body, strlen(body), &status, NULL, 0, 0);

        if (!xs_http_valid_status(status))
            status = -4;
        else
            status = 1;
    }
    else
        status = 0;

    return status;
}


int xs_webmention_hook(const char *source, const char *target, const char *user_agent)
/* a Webmention has been received for a target that is ours; check if the source
   really contains a link to our target */
{
    int status = 0;

    xs *ua = xs_fmt("%s (Webmention)", user_agent ? user_agent : "xs_webmention");
    xs *headers = xs_dict_new();
    headers = xs_dict_set(headers, "accept", "text/html");
    headers = xs_dict_set(headers, "user-agent", ua);

    xs *g_req = NULL;
    xs *payload = NULL;
    int p_size = 0;

    g_req = xs_http_request("GET", source, headers, NULL, 0, &status, &payload, &p_size, 0);

    if (!xs_http_valid_status(status))
        return -1;

    if (!xs_is_string(payload))
        return -2;

    /* note: a "rogue" webmention can include a link to our target in commented-out HTML code */

    xs *links = xs_regex_select(payload, "<(a +|link +)[^>]+>");
    const char *link;

    status = 0;
    xs_list_foreach(links, link) {
        /* if the link contains our target, it's valid */
        if (xs_str_in(link, target) != -1) {
            status = 1;
            break;
        }
    }

    return status;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_WEBMENTION_H */
