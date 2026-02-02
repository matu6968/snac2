/* ESP32 compatibility shims for Snac2 (ESP-IDF / newlib). */

#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#ifdef SNAC_ESP32

int utimes(const char *filename, const struct timeval times[2])
{
    (void)filename;
    (void)times;
    return 0;
}

/* dup() implementation for ESP32
   Minimal stub since ESP32 newlib doesn't provide it. Note: actual code
   should use fcntl(F_GETFD) to check fd validity instead of dup/close. */
int dup(int oldfd)
{
    if (oldfd < 0) {
        errno = EBADF;
        return -1;
    }
    
    /* ESP32 doesn't support true fd duplication. This stub returns -1
       to indicate operation not supported. Callers should use fcntl instead. */
    errno = ENOTSUP;
    return -1;
}

#endif /* SNAC_ESP32 */

