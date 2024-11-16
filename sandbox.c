#include "xs.h"

#include "snac.h"

#include <unistd.h>

#if defined (__linux__)
#   define __USE_GNU
#   include <linux/landlock.h>
#   include <linux/prctl.h>
#   include <sys/syscall.h>
#   include <sys/prctl.h>
#   include <fcntl.h>
#   include <arpa/inet.h>
#endif

void sbox_enter(const char *basedir)
{
    if (xs_is_true(xs_dict_get(srv_config, "disable_openbsd_security"))) {
        srv_log(xs_dup("disable_openbsd_security is deprecated. Use disable_sandbox instead."));
        return;
    }
    if (xs_is_true(xs_dict_get(srv_config, "disable_sandbox"))) {
        srv_debug(0, xs_dup("Sandbox disabled by admin"));
        return;
    }

    const char *address = xs_dict_get(srv_config, "address");

#if defined (__OpenBSD__)
    int smail = !xs_is_true(xs_dict_get(srv_config, "disable_email_notifications"));

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

    xs_free(p);
#elif defined (__linux__)
    int error, ruleset_fd, abi;
    struct landlock_ruleset_attr      rules = {0};
    struct landlock_path_beneath_attr path  = {0};
    struct landlock_net_port_attr     net   = {0};

    rules.handled_access_fs =
        LANDLOCK_ACCESS_FS_EXECUTE     |
        LANDLOCK_ACCESS_FS_WRITE_FILE  |
        LANDLOCK_ACCESS_FS_READ_FILE   |
        LANDLOCK_ACCESS_FS_REMOVE_DIR  |
        LANDLOCK_ACCESS_FS_REMOVE_FILE |
        LANDLOCK_ACCESS_FS_MAKE_CHAR   |
        LANDLOCK_ACCESS_FS_MAKE_DIR    |
        LANDLOCK_ACCESS_FS_MAKE_REG    |
        LANDLOCK_ACCESS_FS_MAKE_SOCK   |
        LANDLOCK_ACCESS_FS_MAKE_FIFO   |
        LANDLOCK_ACCESS_FS_MAKE_BLOCK  |
        LANDLOCK_ACCESS_FS_MAKE_SYM    |
        LANDLOCK_ACCESS_FS_REFER       |
        LANDLOCK_ACCESS_FS_TRUNCATE    |
        LANDLOCK_ACCESS_FS_IOCTL_DEV;
    rules.handled_access_net =
        LANDLOCK_ACCESS_NET_BIND_TCP |
        LANDLOCK_ACCESS_NET_CONNECT_TCP;

    abi = syscall(SYS_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    switch (abi) {
    case -1:
        srv_debug(0, xs_dup("Kernel without landlock support"));
        return;
    case 1:
        rules.handled_access_fs  &= ~LANDLOCK_ACCESS_FS_REFER;
        __attribute__((fallthrough));
    case 2:
        rules.handled_access_fs  &= ~LANDLOCK_ACCESS_FS_TRUNCATE;
        __attribute__((fallthrough));
    case 3:
        rules.handled_access_net &= ~(LANDLOCK_ACCESS_NET_BIND_TCP | LANDLOCK_ACCESS_NET_CONNECT_TCP);
        __attribute__((fallthrough));
    case 4:
        rules.handled_access_fs  &= ~LANDLOCK_ACCESS_FS_IOCTL_DEV;
    }
    srv_debug(1, xs_fmt("lanlock abi: %d", abi));

    ruleset_fd = syscall(SYS_landlock_create_ruleset, &rules, sizeof(struct landlock_ruleset_attr), 0);
    if (ruleset_fd == -1) {
        srv_debug(0, xs_fmt("landlock_create_ruleset failed: %s", strerror(errno)));
        return;
    }

#define LL_R LANDLOCK_ACCESS_FS_READ_FILE
#define LL_X LANDLOCK_ACCESS_FS_EXECUTE
#define LL_RWCF (LL_R | LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_TRUNCATE | LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_REFER)
#define LL_RWCD (LL_RWCF | LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_REMOVE_DIR)
#define LL_UNIX (LL_R | LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_MAKE_SOCK)
#define LL_CONN LANDLOCK_ACCESS_NET_CONNECT_TCP
#define LL_BIND LANDLOCK_ACCESS_NET_BIND_TCP

#define LANDLOCK_PATH(p, r) do {\
    path.allowed_access = r;\
    if (abi < 2)\
        path.allowed_access &= ~LANDLOCK_ACCESS_FS_REFER;\
    if (abi < 3)\
        path.allowed_access &= ~LANDLOCK_ACCESS_FS_TRUNCATE;\
    path.parent_fd = open(p, O_PATH | O_CLOEXEC);\
    if (path.parent_fd == -1) {\
        srv_debug(2, xs_fmt("open %s: %s", p, strerror(errno)));\
        goto close;\
    }\
    error = syscall(SYS_landlock_add_rule, ruleset_fd, LANDLOCK_RULE_PATH_BENEATH, &path, 0); \
    if (error) {\
        srv_debug(0, xs_fmt("LANDLOCK_PATH(%s): %s", p, strerror(errno)));\
        goto close;\
    }\
} while (0)

#define LANDLOCK_PORT(p, r) do {\
    uint16_t _p = p;\
    net.port = _p;\
    net.allowed_access = r;\
    error = syscall(SYS_landlock_add_rule, ruleset_fd, LANDLOCK_RULE_NET_PORT, &net, 0);\
    if (error) {\
        srv_debug(0, xs_fmt("LANDLOCK_PORT(%d): %s", _p, strerror(errno)));\
        goto close;\
    }\
} while (0)

    LANDLOCK_PATH(basedir,                LL_RWCD);
    LANDLOCK_PATH("/tmp",                 LL_RWCD);
    LANDLOCK_PATH("/dev/shm",             LL_RWCF);
    LANDLOCK_PATH("/etc/resolv.conf",     LL_R  );
    LANDLOCK_PATH("/etc/hosts",           LL_R  );
    LANDLOCK_PATH("/etc/ssl/openssl.cnf", LL_R  );
    LANDLOCK_PATH("/etc/ssl/cert.pem",    LL_R  );
    LANDLOCK_PATH("/usr/share/zoneinfo",  LL_R  );

    if (*address == '/')
        LANDLOCK_PATH(address, LL_UNIX);

    if (abi > 3) {
        if (*address != '/') {
            LANDLOCK_PORT(
                (uint16_t)xs_number_get(xs_dict_get(srv_config, "port")), LL_BIND);
        }

        LANDLOCK_PORT(80,  LL_CONN);
        LANDLOCK_PORT(443, LL_CONN);
    }
    
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
        srv_debug(0, xs_fmt("prctl SET_NO_NEW_PRIVS: %s", strerror(errno)));
        goto close;
    }

    if (syscall(SYS_landlock_restrict_self, ruleset_fd, 0))
        srv_debug(0, xs_fmt("landlock_restrict_self: %s", strerror(errno)));

    srv_log(xs_dup("landlocked"));

close:
    close(ruleset_fd);

#endif
}
