/**
 *  Zero-Clause BSD
 *  ===============
 *
 *  Copyright 2024 shtrophic <christoph@liebender.dev>
 *
 *  Permission to use, copy, modify, and/or distribute this software for
 *  any purpose with or without fee is hereby granted.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS” AND THE AUTHOR DISCLAIMS ALL
 *  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 *  OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE
 *  FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY
 *  DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 *  AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 *  OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

/**
 * Usage example:
 *

LL_BEGIN(my_sandbox_function, const char *rw_path) {

    LL_PATH(rw_path, LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR | LANDLOCK_ACCESS_FS_EXECUTE);
    LL_PORT(443, LANDLOCK_ACCESS_NET_CONNECT_TCP);

} LL_END

int main(void) {

    int status = my_sandbox_function("some/path");

    if (status != 0) {
        // error
    }

}
 */

#ifndef __LANDLOC_H__
#define __LANDLOC_H__

#ifndef __linux__
#error "no landlock without linux"
#endif

#include <unistd.h>
#include <linux/landlock.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <fcntl.h>

#ifndef O_PATH
#define O_PATH		010000000
#endif

#ifndef LL_PRINTERR
#define LL_PRINTERR(fmt, ...) (void)fmt;
#else
#include <string.h>
#include <errno.h>
#endif

#define LL_FS_ALL                   (\
    LANDLOCK_ACCESS_FS_EXECUTE      |\
    LANDLOCK_ACCESS_FS_WRITE_FILE   |\
    LANDLOCK_ACCESS_FS_READ_FILE    |\
    LANDLOCK_ACCESS_FS_READ_DIR     |\
    LANDLOCK_ACCESS_FS_REMOVE_DIR   |\
    LANDLOCK_ACCESS_FS_REMOVE_FILE  |\
    LANDLOCK_ACCESS_FS_MAKE_CHAR    |\
    LANDLOCK_ACCESS_FS_MAKE_DIR     |\
    LANDLOCK_ACCESS_FS_MAKE_REG     |\
    LANDLOCK_ACCESS_FS_MAKE_SOCK    |\
    LANDLOCK_ACCESS_FS_MAKE_FIFO    |\
    LANDLOCK_ACCESS_FS_MAKE_BLOCK   |\
    LANDLOCK_ACCESS_FS_MAKE_SYM     |\
    LANDLOCK_ACCESS_FS_REFER        |\
    LANDLOCK_ACCESS_FS_TRUNCATE     |\
    LANDLOCK_ACCESS_FS_IOCTL_DEV    )

#define LL_NET_ALL                  (\
    LANDLOCK_ACCESS_NET_BIND_TCP    |\
    LANDLOCK_ACCESS_NET_CONNECT_TCP )

#define LL_BEGIN(function, ...) int function(__VA_ARGS__) {\
    int ll_rule_fd, ll_abi;\
    struct landlock_ruleset_attr      __rattr = {0};\
    struct landlock_path_beneath_attr __pattr = {0};\
    struct landlock_net_port_attr     __nattr = {0};\
    int __err = 0;\
    __rattr.handled_access_fs  = LL_FS_ALL;\
    __rattr.handled_access_net = LL_NET_ALL;\
    ll_abi = (int)syscall(SYS_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);\
    switch (ll_abi) {\
    case -1: return -1;\
    case  1: __rattr.handled_access_fs  &= ~LANDLOCK_ACCESS_FS_REFER; __attribute__((fallthrough));\
    case  2: __rattr.handled_access_fs  &= ~LANDLOCK_ACCESS_FS_TRUNCATE; __attribute__((fallthrough));\
    case  3: __rattr.handled_access_net &= ~(LANDLOCK_ACCESS_NET_BIND_TCP | LANDLOCK_ACCESS_NET_CONNECT_TCP); __attribute__((fallthrough));\
    case  4: __rattr.handled_access_fs &= ~LANDLOCK_ACCESS_FS_IOCTL_DEV;\
    default: break;\
    }\
    ll_rule_fd = (int)syscall(SYS_landlock_create_ruleset, &__rattr, sizeof(struct landlock_ruleset_attr), 0);\
    if (-1 == ll_rule_fd) {\
        LL_PRINTERR("landlock_create_ruleset: %s", strerror(errno));\
        return -1;\
    }

#define LL_END \
    __err = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);\
    if (-1 == __err) {\
        LL_PRINTERR("set_no_new_privs: %s", strerror(errno));\
        goto __close;\
    }\
    __err = (int)syscall(SYS_landlock_restrict_self, ll_rule_fd, 0);\
    if (__err)\
        LL_PRINTERR("landlock_restrict_self: %s", strerror(errno));\
    __close: close(ll_rule_fd);\
    return __err; }

#define LL_PATH(p, rules) do {\
    const char *__path = (p);\
    __pattr.allowed_access = (rules) & __rattr.handled_access_fs;\
    __pattr.parent_fd = open(__path, O_PATH | O_CLOEXEC);\
    if (-1 == __pattr.parent_fd) {\
        LL_PRINTERR("open(%s): %s", __path, strerror(errno));\
        __err = -1;\
        goto __close;\
    }\
    __err = (int)syscall(SYS_landlock_add_rule, ll_rule_fd, LANDLOCK_RULE_PATH_BENEATH, &__pattr, 0);\
    if (__err) {\
        LL_PRINTERR("landlock_add_rule(%s): %s", __path, strerror(errno));\
        goto __close;\
    }\
    close(__pattr.parent_fd);\
} while (0)

#define LL_PORT(p, rules) do {\
    if (ll_abi > 3) {\
        unsigned short __port = (p);\
        __nattr.allowed_access = (rules);\
        __nattr.port = __port;\
        __err = (int)syscall(SYS_landlock_add_rule, ll_rule_fd, LANDLOCK_RULE_NET_PORT, &__nattr, 0);\
        if (__err) {\
            LL_PRINTERR("landlock_add_rule(%u): %s", __port, strerror(errno));\
            goto __close;\
        }\
    }\
} while (0)

#endif /* __LANDLOC_H__ */
