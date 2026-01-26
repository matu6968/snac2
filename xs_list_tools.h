/* copyright (c) 2022 - 2026 grunfink et al. / MIT license */

#ifndef _XS_LIST_TOOLS_H

#define _XS_LIST_TOOLS_H

 xs_list *xs_list_insert_sorted(xs_list *list, const xs_val *nv);
 xs_list *xs_list_reverse(const xs_list *l);
 xs_val **xs_list_to_array(const xs_list *l, int *len);
 int xs_list_sort_cmp(const void *p1, const void *p2);
 int xs_list_sort_inv_cmp(const void *p1, const void *p2);
 int xs_list_sort_dict_cmp(const char *field, const void *p1, const void *p2);
 xs_list *xs_list_sort(const xs_list *l, int (*cmp)(const void *, const void *));
 xs_list *xs_list_shuffle(const xs_list *l);

#ifdef XS_IMPLEMENTATION

#include "xs_random.h"

xs_list *xs_list_insert_sorted(xs_list *list, const xs_val *nv)
/* inserts a string in the list in its ordered position */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    int offset = xs_size(list);

    const xs_val *v;
    xs_list_foreach(list, v) {
        /* if this element is greater or equal, insert here */
        if (xs_cmp(v, nv) >= 0) {
            offset = v - list;
            break;
        }
    }

    return _xs_list_write_litem(list, offset - 1, nv, xs_size(nv));
}


xs_list *xs_list_reverse(const xs_list *l)
/* creates a new list as a reverse version of l */
{
    xs_list *n = xs_dup(l);
    const xs_val *v;

    /* move to one byte before the EOM */
    char *p = n + xs_size(n) - 1;

    xs_list_foreach(l, v) {
        /* size of v, plus the LITEM */
        int z = xs_size(v) + 1;

        p -= z;

        /* copy v, including its LITEM */
        memcpy(p, v - 1, z);
    }

    return n;
}


xs_val **xs_list_to_array(const xs_list *l, int *len)
/* converts a list to an array of values */
/* must be freed after use */
{
    *len = xs_list_len(l);
    xs_val **a = xs_realloc(NULL, *len * sizeof(xs_val *));
    const xs_val *v;
    int n = 0;

    xs_list_foreach(l, v)
        a[n++] = (xs_val *)v;

    return a;
}


int xs_list_sort_cmp(const void *p1, const void *p2)
/* default list sorting function */
{
    const xs_val *v1 = *(xs_val **)p1;
    const xs_val *v2 = *(xs_val **)p2;

    return xs_cmp(v1, v2);
}


int xs_list_sort_inv_cmp(const void *p1, const void *p2)
/* default list inverse sorting function */
{
    const xs_val *v1 = *(xs_val **)p1;
    const xs_val *v2 = *(xs_val **)p2;

    return xs_cmp(v2, v1);
}


int xs_list_sort_dict_cmp(const char *field, const void *p1, const void *p2)
/* compare sorting function for a field an array of dicts */
{
    const xs_dict *d1 = *(xs_val **)p1;
    const xs_dict *d2 = *(xs_val **)p2;

    if (xs_type(d1) != XSTYPE_DICT || xs_type(d2) != XSTYPE_DICT)
        return 0;

    return xs_cmp(xs_dict_get_def(d1, field, ""),
                  xs_dict_get_def(d2, field, ""));
}


xs_list *xs_list_sort(const xs_list *l, int (*cmp)(const void *, const void *))
/* returns a sorted copy of l. cmp can be null for standard sorting */
{
    int sz;
    xs_val **a = xs_list_to_array(l, &sz);
    xs_list *nl = xs_dup(l);
    char *p = nl + 1 + _XS_TYPE_SIZE;

    /* sort the array */
    qsort(a, sz, sizeof(xs_val *), cmp ? cmp : xs_list_sort_cmp);

    /* transfer the sorted list over the copy */
    for (int n = 0; n < sz; n++) {
        /* get the litem */
        const char *e = a[n] - 1;
        int z = xs_size(e);

        memcpy(p, e, z);
        p += z;
    }

    xs_free(a);

    return nl;
}


xs_list *xs_list_shuffle(const xs_list *l)
/* returns a shuffled list */
{
    int sz;
    xs_val **a = xs_list_to_array(l, &sz);
    xs_list *nl = xs_list_new();
    unsigned int seed = 0;

    xs_rnd_buf(&seed, sizeof(seed));

    /* shuffle */
    for (int n = sz - 1; n > 0; n--) {
        int m = xs_rnd_int32_d(&seed) % n;
        void *p = a[n];
        a[n] = a[m];
        a[m] = p;
    }

    for (int n = 0; n < sz; n++)
        nl = xs_list_append(nl, a[n]);

    xs_free(a);

    return nl;
}


#endif /* XS_IMPLEMENTATION */

#endif /* XS_LIST_TOOLS_H */
