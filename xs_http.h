/* copyright (c) 2022 - 2025 grunfink et al. / MIT license */

#ifndef _XS_HTTP_H

#define _XS_HTTP_H

typedef enum {
#define HTTP_STATUS(code, name, text) HTTP_STATUS_ ## name = code,
#include "xs_http_codes.h"
#undef HTTP_STATUS
} http_status;


int xs_http_valid_status(int status);
const char *xs_http_status_text(int status);


#ifdef XS_IMPLEMENTATION

int xs_http_valid_status(int status)
/* is this HTTP status valid? */
{
    return status >= 200 && status <= 299;
}


const char *xs_http_status_text(int status)
/* translate status codes to canonical status texts */
{
    switch (status) {
        case 599: return "Timeout";
#define HTTP_STATUS(code, name, text) case HTTP_STATUS_ ## name: return #text;
#include "xs_http_codes.h"
#undef HTTP_STATUS
        default: return "Unknown";
    }
}


#endif /* XS_IMPLEMENTATION */

#endif /* XS_HTTP_H */
