/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */
#include "xs.h"

#ifndef _XS_FORMDATA_H

#define _XS_FORMDATA_H

xs_val *xs_formdata_loads(const xs_str *formdata);

#ifdef XS_IMPLEMENTATION

/** IMPLEMENTATION **/

xs_val *xs_formdata_loads(const xs_str *formdata)
/* loads a string in formdata format and converts to a multiple data */
{
    xs_val *v = NULL;
    xs_list *args = xs_split(formdata, "&");
    int i = 0;
    while (){}
    printf("args: %s\r\n", args); fflush(stdout);
    printf("data: %s\r\n", formdata); fflush(stdout);
}

#endif /* XS_IMPLEMENTATION */

#endif /* _XS_FORMDATA_H */
