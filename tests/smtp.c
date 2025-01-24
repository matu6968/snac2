/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2025 grunfink et al. / MIT license */

#define XS_IMPLEMENTATION
#include "../xs.h"
#include "../xs_curl.h"

#define FROM "<snac-smtp-test@locahost>"

int main(void) {
    xs *to   = xs_fmt("<%s@localhost>", getenv("USER")),
       *body = xs_fmt(""
        "To: %s \r\n"
        "From: " FROM "\r\n"
        "Subject: snac smtp test\r\n"
        "\r\n"
        "If you read this as an email, it probably worked!\r\n",
        to);

    return xs_smtp_request("smtp://localhost", NULL, NULL, 
        FROM,
        to,
        body, 0);
}