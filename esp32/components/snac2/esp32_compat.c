/* ESP32 compatibility shims for Snac2 (ESP-IDF / newlib). */

#include <sys/time.h>

#ifdef SNAC_ESP32

int utimes(const char *filename, const struct timeval times[2])
{
    (void)filename;
    (void)times;
    return 0;
}

#endif /* SNAC_ESP32 */

