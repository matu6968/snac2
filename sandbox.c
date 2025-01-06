#include "xs.h"

#include "snac.h"

#if defined(__OpenBSD__)

void sbox_enter(const char *basedir)
{
    const char *address = xs_dict_get(srv_config, "address");

    int smail = !xs_is_true(xs_dict_get(srv_config, "disable_email_notifications"));

    if (xs_is_true(xs_dict_get(srv_config, "disable_openbsd_security"))) {
        srv_log(xs_dup("OpenBSD security disabled by admin"));
        return;
    }

    srv_debug(1, xs_fmt("Calling unveil()"));
    unveil(basedir,                "rwc");
    unveil("/tmp",                 "rwc");
    unveil("/etc/resolv.conf",     "r");
    unveil("/etc/hosts",           "r");
    unveil("/etc/ssl/openssl.cnf", "r");
    unveil("/etc/ssl/cert.pem",    "r");
    unveil("/usr/share/zoneinfo",  "r");

    if (smail)
        unveil("/usr/sbin/sendmail",   "x");

    if (*address == '/')
        unveil(address, "rwc");

    unveil(NULL,                   NULL);

    srv_debug(1, xs_fmt("Calling pledge()"));

    xs *p = xs_str_new("stdio rpath wpath cpath flock inet proc dns fattr");

    if (smail)
        p = xs_str_cat(p, " exec");

    if (*address == '/')
        p = xs_str_cat(p, " unix");

    pledge(p, NULL);
}

#elif defined(__linux__)

#if defined(WITH_LINUX_SANDBOX)

#include <unistd.h>

#define LL_PRINTERR(fmt, ...) srv_debug(0, xs_fmt(fmt, __VA_ARGS__))
#include "landloc.h"

static
LL_BEGIN(sbox_enter_linux_, const char* basedir, const char *address, int smail) {

    const unsigned long long
        rd = LANDLOCK_ACCESS_FS_READ_DIR,
        rf = LANDLOCK_ACCESS_FS_READ_FILE,
        w  = LANDLOCK_ACCESS_FS_WRITE_FILE      |
             LANDLOCK_ACCESS_FS_TRUNCATE_COMPAT,
        c  = LANDLOCK_ACCESS_FS_MAKE_DIR        |
             LANDLOCK_ACCESS_FS_MAKE_REG        |
             LANDLOCK_ACCESS_FS_TRUNCATE_COMPAT |
             LANDLOCK_ACCESS_FS_MAKE_SYM        |
             LANDLOCK_ACCESS_FS_REMOVE_DIR      |
             LANDLOCK_ACCESS_FS_REMOVE_FILE     |
             LANDLOCK_ACCESS_FS_REFER_COMPAT,
        s  = LANDLOCK_ACCESS_FS_MAKE_SOCK,
        x  = LANDLOCK_ACCESS_FS_EXECUTE;

    LL_PATH(basedir,                rf|rd|w|c);
    LL_PATH("/tmp",                 rf|rd|w|c);
#ifndef WITHOUT_SHM
    LL_PATH("/dev/shm",             rf|w|c   );
#endif
    LL_PATH("/etc/resolv.conf",     rf       );
    LL_PATH("/etc/hosts",           rf       );
    LL_PATH("/etc/ssl",             rf       );
    LL_PATH("/usr/share/zoneinfo",  rf       );

    if (mtime("/etc/pki") > 0)
        LL_PATH("/etc/pki",         rf       );

    if (*address == '/') {
        /* the directory holding the socket must be allowed */
        xs *l = xs_split(address, "/");
        l = xs_list_del(l, -1);
        xs *sdir = xs_join(l, "/");

        LL_PATH(sdir, s);
    }

    if (smail)
        LL_PATH("/usr/sbin/sendmail", x);

    if (*address != '/') {
        unsigned short listen_port = xs_number_get(xs_dict_get(srv_config, "port"));
        LL_PORT(listen_port, LANDLOCK_ACCESS_NET_BIND_TCP_COMPAT);
    }

    LL_PORT(80,  LANDLOCK_ACCESS_NET_CONNECT_TCP_COMPAT);
    LL_PORT(443, LANDLOCK_ACCESS_NET_CONNECT_TCP_COMPAT);

} LL_END

void sbox_enter(const char *basedir)
{
    const char *address = xs_dict_get(srv_config, "address");

    int smail = !xs_is_true(xs_dict_get(srv_config, "disable_email_notifications"));

    if (xs_is_true(xs_dict_get(srv_config, "disable_sandbox"))) {
        srv_debug(1, xs_dup("Linux sandbox disabled by admin"));
        return;
    }

    if (sbox_enter_linux_(basedir, address, smail) == 0)
        srv_debug(1, xs_dup("Linux sandbox enabled"));
    else
        srv_debug(1, xs_dup("Linux sandbox failed"));
}

#else /* defined(WITH_LINUX_SANDBOX) */

void sbox_enter(const char *basedir)
{
    (void)basedir;

    srv_debug(1, xs_fmt("Linux sandbox not compiled in"));
}

#endif

#else

/* other OSs: dummy sbox_enter() */

void sbox_enter(const char *basedir)
{
    (void)basedir;
}


#endif /* __OpenBSD__ */
