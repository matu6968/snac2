/* copyright (c) 2022 - 2025 grunfink et al. / MIT license */

#ifndef _XS_TIME_H

#define _XS_TIME_H

#include <time.h>

xs_str *xs_str_time(time_t t, const char *fmt, int local);
#define xs_str_localtime(t, fmt) xs_str_time(t, fmt, 1)
#define xs_str_utctime(t, fmt)   xs_str_time(t, fmt, 0)
#define xs_str_iso_date(t) xs_str_time(t, "%Y-%m-%dT%H:%M:%SZ", 0)
time_t xs_parse_iso_date(const char *iso_date, int local);
time_t xs_parse_time(const char *str, const char *fmt, int local);
#define xs_parse_localtime(str, fmt) xs_parse_time(str, fmt, 1)
#define xs_parse_utctime(str, fmt) xs_parse_time(str, fmt, 0)
xs_str *xs_str_time_diff(time_t time_diff);
xs_list *xs_tz_list(void);
int xs_tz_offset(const char *tz);

#ifdef XS_IMPLEMENTATION

xs_str *xs_str_time(time_t t, const char *fmt, int local)
/* returns a string with a formated time */
{
    struct tm tm;
    char tmp[64];

    if (t == 0)
        t = time(NULL);

    if (local)
        localtime_r(&t, &tm);
    else
        gmtime_r(&t, &tm);

    strftime(tmp, sizeof(tmp), fmt, &tm);

    return xs_str_new(tmp);
}


xs_str *xs_str_time_diff(time_t time_diff)
/* returns time_diff in seconds to 'human' units (d:hh:mm:ss) */
{
    int secs  = time_diff % 60;
    int mins  = (time_diff /= 60) % 60;
    int hours = (time_diff /= 60) % 24;
    int days  = (time_diff /= 24);

    return xs_fmt("%d:%02d:%02d:%02d", days, hours, mins, secs);
}


char *strptime(const char *s, const char *format, struct tm *tm);

time_t xs_parse_time(const char *str, const char *fmt, int local)
{
    time_t t = 0;

#ifndef WITHOUT_STRPTIME

    struct tm tm = {0};

    strptime(str, fmt, &tm);

    /* try to guess the Daylight Saving Time */
    if (local)
        tm.tm_isdst = -1;

    t = local ? mktime(&tm) : timegm(&tm);

#endif /* WITHOUT_STRPTIME */

    return t;
}


time_t xs_parse_iso_date(const char *iso_date, int local)
/* parses a YYYY-MM-DDTHH:MM:SS date string */
{
    time_t t = 0;

#ifndef WITHOUT_STRPTIME

    t = xs_parse_time(iso_date, "%Y-%m-%dT%H:%M:%S", local);

#else /* WITHOUT_STRPTIME */

    struct tm tm = {0};

    if (sscanf(iso_date, "%d-%d-%dT%d:%d:%d",
        &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
        &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {

        tm.tm_year -= 1900;
        tm.tm_mon -= 1;

        if (local)
            tm.tm_isdst = -1;

        t = local ? mktime(&tm) : timegm(&tm);
    }

#endif /* WITHOUT_STRPTIME */

    return t;
}


/** timezones **/

/* intentionally dead simple */

struct {
    const char *tz; /* timezone name */
    float h_offset; /* hour offset */
} xs_tz[] = {
    { "UTC",                                     0 },
    { "WET (Western European Time)",             0 },
    { "WEST (Western European Summer Time)",     1 },
    { "CET (Central European Time)",             1 },
    { "CEST (Central European Summer Time)",     2 },
    { "EET (Eastern European Time)",             2 },
    { "EEST (Eastern European Summer Time)",     3 },
    { "MSK (Moskow Time Zone)",                  3 },
    { "EST (Eastern Time Zone)",                 -5 },
    { "AST (Atlantic Time Zone)",                -4 },
    { "ADT (Atlantic Daylight Time Zone)",       -3 },
    { "CST (Central Time Zone)",                 -6 },
    { "CDT (Central Daylight Time Zone)",        -5 },
    { "MST (Mountain Time Zone)",                -7 },
    { "MDT (Mountain Daylight Time Zone)",       -6 },
    { "PST (Pacific Time Zone)",                 -8 },
    { "PDT (Pacific Daylight Time Zone)",        -7 },
    { "AKST (Alaska Time Zone)",                 -9 },
    { "AKDT (Alaska Daylight Time Zone)",        -8 },
    { "China Time Zone",                         8 },
    { "IST (Israel Standard Time)",              2 },
    { "IDT (Israel Daylight Standard Time)",     3 },
    { "WIB (Western Indonesia Time)",            7 },
    { "WITA (Central Indonesia Time)",           8 },
    { "WIT (Eastern Indonesia Time)",            9 },
    { "AWST (Australian Western Time)",          8 },
    { "ACST (Australian Eastern Time)",          9.5 },
    { "ACDT (Australian Daylight Eastern Time)", 10.5 },
    { "AEST (Australian Eastern Time)",          10 },
    { "AEDT (Australian Daylight Eastern Time)", 11 },
    { "NZST (New Zealand Time)",                 12 },
    { "NZDT (New Zealand Daylight Time)",        13 },
    { "UTC",      0 },
    { "UTC+1",    1 },
    { "UTC+2",    2 },
    { "UTC+3",    3 },
    { "UTC+4",    4 },
    { "UTC+5",    5 },
    { "UTC+6",    6 },
    { "UTC+7",    7 },
    { "UTC+8",    8 },
    { "UTC+9",    9 },
    { "UTC+10",   10 },
    { "UTC+11",   11 },
    { "UTC+12",   12 },
    { "UTC-1",    -1 },
    { "UTC-2",    -2 },
    { "UTC-3",    -3 },
    { "UTC-4",    -4 },
    { "UTC-5",    -5 },
    { "UTC-6",    -6 },
    { "UTC-7",    -7 },
    { "UTC-8",    -8 },
    { "UTC-9",    -9 },
    { "UTC-10",   -10 },
    { "UTC-11",   -11 },
    { "UTC-12",   -12 },
    { "UTC-13",   -13 },
    { "UTC-14",   -14 },
    { "GMT",      0 },
    { "GMT+1",    -1 },
    { "GMT+2",    -2 },
    { "GMT+3",    -3 },
    { "GMT+4",    -4 },
    { "GMT+5",    -5 },
    { "GMT+6",    -6 },
    { "GMT+7",    -7 },
    { "GMT+8",    -8 },
    { "GMT+9",    -9 },
    { "GMT+10",   -10 },
    { "GMT+11",   -11 },
    { "GMT+12",   -12 },
    { "GMT-1",    1 },
    { "GMT-2",    2 },
    { "GMT-3",    3 },
    { "GMT-4",    4 },
    { "GMT-5",    5 },
    { "GMT-6",    6 },
    { "GMT-7",    7 },
    { "GMT-8",    8 },
    { "GMT-9",    9 },
    { "GMT-10",   10 },
    { "GMT-11",   11 },
    { "GMT-12",   12 },
    { "GMT-13",   13 },
    { "GMT-14",   14 },
    { NULL,       0 }
};


xs_list *xs_tz_list(void)
/* returns the list of supported timezones */
{
    xs_list *l = xs_list_new();

    for (int n = 0; xs_tz[n].tz != NULL; n++)
        l = xs_list_append(l, xs_tz[n].tz);

    return l;
}


int xs_tz_offset(const char *tz)
/* returns the offset in seconds from the specified Time Zone to UTC */
{
    for (int n = 0; xs_tz[n].tz != NULL; n++) {
        if (strcmp(xs_tz[n].tz, tz) == 0)
            return xs_tz[n].h_offset * 3600;
    }

    return 0;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_TIME_H */
