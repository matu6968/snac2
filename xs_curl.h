/* copyright (c) 2022 - 2026 grunfink et al. / MIT license */

#ifndef _XS_CURL_H

#define _XS_CURL_H

xs_dict *xs_http_request(const char *method, const char *url,
                        const xs_dict *headers,
                        const xs_str *body, int b_size, int *status,
                        xs_str **payload, int *p_size, int timeout);

int xs_smtp_request(const char *url, const char *user, const char *pass,
                   const char *from, const char *to, const xs_str *body,
                   int use_ssl);

const char *xs_curl_strerr(int errnum);

#ifdef XS_IMPLEMENTATION

#ifdef SNAC_ESP32

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "mbedtls/base64.h"

struct _payload_data {
    char *data;
    int size;
    int offset;
};

struct _xs_esp32_req_ctx {
    xs_dict *response;
    struct _payload_data pd;
};

typedef struct {
    int use_tls;
    esp_tls_t *tls;
    int sockfd;
} xs_smtp_conn_t;

static int _xs_smtp_conn_write(xs_smtp_conn_t *c, const void *data, size_t len)
{
    if (c->use_tls)
        return (int)esp_tls_conn_write(c->tls, data, len);
    else
        return (int)send(c->sockfd, data, len, 0);
}

static int _xs_smtp_conn_read(xs_smtp_conn_t *c, void *data, size_t len)
{
    if (c->use_tls)
        return (int)esp_tls_conn_read(c->tls, data, len);
    else
        return (int)recv(c->sockfd, data, len, 0);
}

static int _xs_smtp_read_line(xs_smtp_conn_t *c, xs_str **out)
{
    xs_str *s = xs_str_new(NULL);
    char ch = 0;

    for (;;) {
        int r = _xs_smtp_conn_read(c, &ch, 1);
        if (r <= 0) {
            xs_free(s);
            return -1;
        }
        s = xs_append_m(s, &ch, 1);
        if (ch == '\n')
            break;
        if (xs_size(s) > 4096)
            break;
    }

    *out = s;
    return 0;
}

static int _xs_smtp_read_reply(xs_smtp_conn_t *c, int *code)
{
    int ccode = -1;

    for (;;) {
        xs_str *line = NULL;
        if (_xs_smtp_read_line(c, &line) != 0)
            return -1;

        if (xs_size(line) >= 4 &&
            line[0] >= '0' && line[0] <= '9' &&
            line[1] >= '0' && line[1] <= '9' &&
            line[2] >= '0' && line[2] <= '9') {
            ccode = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
            char cont = line[3];
            xs_free(line);
            if (cont == '-')
                continue;
            break;
        }

        xs_free(line);
    }

    if (code)
        *code = ccode;
    return 0;
}

static int _xs_smtp_cmd_expect(xs_smtp_conn_t *c, const char *cmd, int expect_code)
{
    if (cmd) {
        xs *s = xs_fmt("%s\r\n", cmd);
        if (_xs_smtp_conn_write(c, s, strlen(s)) <= 0)
            return -1;
    }

    int rc = 0;
    if (_xs_smtp_read_reply(c, &rc) != 0)
        return -1;
    return rc == expect_code ? 0 : -1;
}

static esp_err_t _xs_esp32_http_event(esp_http_client_event_t *evt)
{
    struct _xs_esp32_req_ctx *ctx = (struct _xs_esp32_req_ctx *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key && evt->header_value) {
        xs *k = xs_tolower_i(xs_str_new(evt->header_key));
        ctx->response = xs_dict_set(ctx->response, k, evt->header_value);
    }
    else
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        struct _payload_data *pd = &ctx->pd;
        pd->size += evt->data_len;
        pd->data = xs_realloc(pd->data, _xs_blk_size(pd->size + 1));
        memcpy(pd->data + pd->offset, evt->data, evt->data_len);
        pd->offset += evt->data_len;
    }

    return ESP_OK;
}

xs_dict *xs_http_request(const char *method, const char *url,
                        const xs_dict *headers,
                        const xs_str *body, int b_size, int *status,
                        xs_str **payload, int *p_size, int timeout)
/* does an HTTP request (ESP-IDF) */
{
    xs_dict *response = xs_dict_new();

    if (timeout <= 0)
        timeout = (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0) ? 60 : 30;

    struct _xs_esp32_req_ctx ctx = { 0 };
    ctx.response = response;
    ctx.pd = (struct _payload_data){ NULL, 0, 0 };

    int tx_buffer = 2048;
    if ((strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0) && body != NULL) {
        int body_len = (b_size <= 0) ? xs_size(body) : b_size;
        /* Ensure TX buffer is large enough for the body, minimum 8KB for ActivityPub */
        if (body_len > tx_buffer)
            tx_buffer = (body_len + 1023) & ~1023;  /* Round up to nearest KB */
        if (tx_buffer < 8192)
            tx_buffer = 8192;
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = timeout * 1000,
        .event_handler = _xs_esp32_http_event,
        .user_data = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 8192,
        .buffer_size_tx = tx_buffer,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        if (status) *status = 599;
        if (p_size) *p_size = 0;
        if (payload) *payload = NULL;
        return response;
    }

    if (strcmp(method, "GET") == 0)        esp_http_client_set_method(client, HTTP_METHOD_GET);
    else if (strcmp(method, "POST") == 0)  esp_http_client_set_method(client, HTTP_METHOD_POST);
    else if (strcmp(method, "PUT") == 0)   esp_http_client_set_method(client, HTTP_METHOD_PUT);
    else if (strcmp(method, "HEAD") == 0)  esp_http_client_set_method(client, HTTP_METHOD_HEAD);
    else                                   esp_http_client_set_method(client, HTTP_METHOD_GET);

    const xs_str *k;
    const xs_val *v;
    xs_dict_foreach(headers, k, v) {
        esp_http_client_set_header(client, k, v);
    }

    if ((strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0) && body != NULL) {
        if (b_size <= 0)
            b_size = xs_size(body);

        esp_http_client_set_post_field(client, body, b_size);
    }

    esp_err_t err = esp_http_client_perform(client);

    int st = esp_http_client_get_status_code(client);
    if (status) {
        if (err == ESP_ERR_HTTP_EAGAIN)
            *status = 599;
        else
        if (st == 0 && err != ESP_OK)
            *status = 599;
        else
            *status = st;
    }

    response = ctx.response;
    if (err != ESP_OK)
        response = xs_dict_set(response, "_esp_err", esp_err_to_name(err));

    if (p_size != NULL)
        *p_size = ctx.pd.size;

    if (payload != NULL) {
        *payload = ctx.pd.data;
        if (ctx.pd.data != NULL)
            ctx.pd.data[ctx.pd.size] = '\0';
    }
    else
        xs_free(ctx.pd.data);

    esp_http_client_cleanup(client);

    return response;
}

int xs_smtp_request(const char *url, const char *user, const char *pass,
                   const char *from, const char *to, const xs_str *body,
                   int use_ssl)
{
    if (!xs_is_string(url) || !xs_is_string(from) || !xs_is_string(to) || !xs_is_string(body)) {
        errno = EINVAL;
        return -EINVAL;
    }

    /* Parse scheme://host[:port] */
    const char *p = strstr(url, "://");
    const char *scheme = url;
    const char *rest = p ? (p + 3) : url;
    int implicit_tls = 0;

    if (p) {
        if (strncmp(scheme, "smtps", 5) == 0)
            implicit_tls = 1;
    }

    const char *host_start = rest;
    const char *host_end = host_start;
    while (*host_end && *host_end != ':' && *host_end != '/')
        host_end++;

    if (host_end == host_start) {
        errno = EINVAL;
        return -EINVAL;
    }

    xs *host = xs_str_new_sz(host_start, (int)(host_end - host_start));

    int port = implicit_tls ? 465 : 25;
    if (*host_end == ':') {
        const char *q = host_end + 1;
        int n = 0;
        while (q[n] && q[n] >= '0' && q[n] <= '9')
            n++;
        if (n > 0)
            port = atoi(q);
    }

    /* use_ssl is set by caller for 465/587; on ESP32 we only support implicit TLS (SMTPS/465) */
    if (use_ssl && !implicit_tls && port != 465) {
        errno = ENOTSUP;
        return -ENOTSUP;
    }

    xs_smtp_conn_t c = { 0 };

    /* Connect */
    if (implicit_tls || port == 465) {
        c.use_tls = 1;
        c.tls = esp_tls_init();
        if (c.tls == NULL) {
            errno = ENOMEM;
            return -ENOMEM;
        }

        esp_tls_cfg_t cfg = {
            .timeout_ms = 10000,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };

        if (esp_tls_conn_new_sync(host, strlen(host), port, &cfg, c.tls) != 1) {
            esp_tls_conn_destroy(c.tls);
            c.tls = NULL;
            errno = EIO;
            return -EIO;
        }
    }
    else {
        c.use_tls = 0;
        esp_tls_cfg_t cfg = {
            .timeout_ms = 10000,
            .is_plain_tcp = true,
        };

        if (esp_tls_plain_tcp_connect(host, strlen(host), port, &cfg, NULL, &c.sockfd) != ESP_OK) {
            errno = EIO;
            return -EIO;
        }
    }

    int code = 0;
    if (_xs_smtp_read_reply(&c, &code) != 0 || code != 220) {
        errno = EPROTO;
        goto fail;
    }

    if (_xs_smtp_cmd_expect(&c, "EHLO snac2", 250) != 0) {
        /* fallback */
        if (_xs_smtp_cmd_expect(&c, "HELO snac2", 250) != 0) {
            errno = EPROTO;
            goto fail;
        }
    }

    /* AUTH (optional) */
    if (xs_is_string(user) && *user) {
        if (!xs_is_string(pass))
            pass = "";

        int ulen = (int)strlen(user);
        int plen = (int)strlen(pass);
        int blen = 1 + ulen + 1 + plen;
        char *buf = xs_realloc(NULL, blen);
        memset(buf, 0, blen);
        memcpy(buf + 1, user, ulen);
        memcpy(buf + 1 + ulen + 1, pass, plen);
        size_t olen = 0;
        mbedtls_base64_encode(NULL, 0, &olen, (const unsigned char *)buf, (size_t)blen);
        xs *b64 = xs_realloc(NULL, (int)olen + 1);
        if (mbedtls_base64_encode((unsigned char *)b64, olen, &olen,
                                  (const unsigned char *)buf, (size_t)blen) != 0) {
            xs_free(buf);
            xs_free(b64);
            errno = EIO;
            goto fail;
        }
        b64[olen] = '\0';
        xs_free(buf);

        xs *cmd = xs_fmt("AUTH PLAIN %s", b64);
        if (_xs_smtp_cmd_expect(&c, cmd, 235) != 0) {
            errno = EACCES;
            goto fail;
        }
    }

    /* Envelope */
    xs *cmd_from = xs_fmt("MAIL FROM:<%s>", from);
    if (_xs_smtp_cmd_expect(&c, cmd_from, 250) != 0) { errno = EPROTO; goto fail; }

    xs *cmd_to = xs_fmt("RCPT TO:<%s>", to);
    if (_xs_smtp_cmd_expect(&c, cmd_to, 250) != 0) { errno = EPROTO; goto fail; }

    if (_xs_smtp_cmd_expect(&c, "DATA", 354) != 0) { errno = EPROTO; goto fail; }

    /* DATA: normalize LF->CRLF and dot-stuff */
    const char *bp = body;
    xs *out = xs_str_new(NULL);
    int bol = 1;
    while (*bp) {
        char ch = *bp++;
        if (bol && ch == '.')
            out = xs_str_cat(out, ".");
        bol = 0;
        if (ch == '\n') {
            out = xs_append_m(out, "\r\n", 2);
            bol = 1;
        }
        else if (ch == '\r') {
            out = xs_append_m(out, "\r", 1);
        }
        else {
            out = xs_append_m(out, &ch, 1);
        }
    }

    /* ensure ends with CRLF */
    int osz = xs_size(out);
    if (osz < 2 || !(out[osz - 2] == '\r' && out[osz - 1] == '\n'))
        out = xs_str_cat(out, "\r\n");

    out = xs_str_cat(out, ".\r\n");
    if (_xs_smtp_conn_write(&c, out, xs_size(out)) <= 0) { errno = EIO; goto fail; }

    if (_xs_smtp_read_reply(&c, &code) != 0 || code != 250) { errno = EPROTO; goto fail; }

    (void)_xs_smtp_cmd_expect(&c, "QUIT", 221);

    if (c.use_tls)
        esp_tls_conn_destroy(c.tls);
    else
        close(c.sockfd);

    return 0;

fail:
    if (c.use_tls && c.tls)
        esp_tls_conn_destroy(c.tls);
    else if (!c.use_tls && c.sockfd > 0)
        close(c.sockfd);
    return -errno;
}

const char *xs_curl_strerr(int errnum)
{
    (void)errnum;
    return "esp32";
}

#else /* SNAC_ESP32 */

#include <curl/curl.h>

static size_t _header_callback(char *buffer, size_t size,
                               size_t nitems, xs_dict **userdata)
{
    xs_dict *headers = *userdata;
    xs *l;

    /* get the line */
    l = xs_str_new(NULL);
    l = xs_append_m(l, buffer, size * nitems);
    l = xs_strip_i(l);

    /* only the HTTP/x 200 line and the last one doesn't have ': ' */
    if (xs_str_in(l, ": ") != -1) {
        xs *knv = xs_split_n(l, ": ", 1);

        xs_tolower_i((xs_str *)xs_list_get(knv, 0));

        headers = xs_dict_set(headers, xs_list_get(knv, 0), xs_list_get(knv, 1));
    }
    else
    if (xs_startswith(l, "HTTP/"))
        headers = xs_dict_set(headers, "_proto", l);

    *userdata = headers;

    return nitems * size;
}


struct _payload_data {
    char *data;
    int size;
    int offset;
};

static size_t _data_callback(void *buffer, size_t size,
                          size_t nitems, struct _payload_data *pd)
{
    size_t sz = size * nitems;

    /* open space */
    pd->size += sz;
    pd->data = xs_realloc(pd->data, _xs_blk_size(pd->size + 1));

    /* copy data */
    memcpy(pd->data + pd->offset, buffer, sz);
    pd->offset += sz;

    return sz;
}


static size_t _post_callback(char *buffer, size_t size,
                          size_t nitems, struct _payload_data *pd)
{
    /* size of data left */
    size_t sz = pd->size - pd->offset;

    /* if it's still bigger than the provided space, trim */
    if (sz > (size_t) (size * nitems))
        sz = size * nitems;

    memcpy(buffer, pd->data + pd->offset, sz);

    /* skip sent data */
    pd->offset += sz;

    return sz;
}


xs_dict *xs_http_request(const char *method, const char *url,
                        const xs_dict *headers,
                        const xs_str *body, int b_size, int *status,
                        xs_str **payload, int *p_size, int timeout)
/* does an HTTP request */
{
    xs_dict *response;
    CURL *curl;
    struct curl_slist *list = NULL;
    const xs_str *k;
    const xs_val *v;
    long lstatus = 0;
    struct _payload_data pd;

    response = xs_dict_new();

    curl = curl_easy_init();

    curl_easy_setopt(curl, CURLOPT_URL, url);

    if (timeout <= 0)
        timeout = 8;

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long) timeout);

#ifdef FORCE_HTTP_1_1
    /* force HTTP/1.1 */
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
#endif

    /* obey redirections */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    /* store response headers here */
    curl_easy_setopt(curl, CURLOPT_HEADERDATA,     &response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, (curl_read_callback) _header_callback);

    struct _payload_data ipd = { NULL, 0, 0 };
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &ipd);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (curl_read_callback) _data_callback);

    if (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0) {
        CURLoption curl_method = method[1] == 'O' ? CURLOPT_POST : CURLOPT_UPLOAD;
        curl_easy_setopt(curl, curl_method, 1L);

        if (body != NULL) {
            if (b_size <= 0)
                b_size = xs_size(body);

            /* add the content-length header */
            curl_easy_setopt(curl, curl_method == CURLOPT_POST ? CURLOPT_POSTFIELDSIZE : CURLOPT_INFILESIZE, b_size);

            pd.data = (char *)body;
            pd.size = b_size;
            pd.offset = 0;

            curl_easy_setopt(curl, CURLOPT_READDATA,     &pd);
            curl_easy_setopt(curl, CURLOPT_READFUNCTION, (curl_read_callback) _post_callback);
        }
    }

    /* fill the request headers */
    xs_dict_foreach(headers, k, v) {
        xs *h = xs_fmt("%s: %s", k, v);

        list = curl_slist_append(list, h);
    }

    /* disable server support for 100-continue */
    list = curl_slist_append(list, "Expect:");

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

    /* do it */
    CURLcode cc = curl_easy_perform(curl);

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &lstatus);

    curl_easy_cleanup(curl);

    curl_slist_free_all(list);

    if (status != NULL) {
        if (lstatus == 0) {
            /* set the timeout error to a fake HTTP status, or propagate as is */
            if (cc == CURLE_OPERATION_TIMEDOUT)
                lstatus = 599;
            else
                lstatus = -cc;
        }

        *status = (int) lstatus;
    }

    if (p_size != NULL)
        *p_size = ipd.size;

    if (payload != NULL) {
        *payload = ipd.data;

        /* add an asciiz just in case (but not touching p_size) */
        if (ipd.data != NULL)
            ipd.data[ipd.size] = '\0';
    }
    else
        xs_free(ipd.data);

    return response;
}


int xs_smtp_request(const char *url, const char *user, const char *pass,
                   const char *from, const char *to, const xs_str *body,
                   int use_ssl)
{
    CURL *curl;
    CURLcode res = CURLE_OK;
    struct curl_slist *rcpt = NULL;
    struct _payload_data pd = {
        .data = (char *)body,
        .size = strlen(body),
        .offset = 0
    };

    curl = curl_easy_init();

    curl_easy_setopt(curl, CURLOPT_URL, url);
    if (user && pass) {
        /* allow authless connections, to, e.g. localhost */
        curl_easy_setopt(curl, CURLOPT_USERNAME, user);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, pass);
    }

    if (use_ssl)
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);

    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from);

    rcpt = curl_slist_append(rcpt, to);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, rcpt);

    curl_easy_setopt(curl, CURLOPT_READDATA, &pd);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, (curl_read_callback) _post_callback);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(rcpt);

    return (int)res;
}


const char *xs_curl_strerr(int errnum)
{
    CURLcode cc = errnum < 0 ? -errnum : errnum;

    return curl_easy_strerror(cc);
}

#endif /* SNAC_ESP32 */


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_CURL_H */
