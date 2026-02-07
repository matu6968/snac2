/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2026 grunfink et al. / MIT license */

#define XS_IMPLEMENTATION

#include "xs.h"
#include "xs_hex.h"
#include "xs_io.h"
#include "xs_unicode_tbl.h"
#include "xs_unicode.h"
#include "xs_json.h"
#include "xs_curl.h"
#include "xs_openssl.h"
#include "xs_socket.h"
#include "xs_unix_socket.h"
#include "xs_url.h"
#include "xs_http.h"
#include "xs_httpd.h"
#include "xs_mime.h"
#include "xs_regex.h"
#include "xs_set.h"
#include "xs_time.h"
#include "xs_glob.h"
#include "xs_random.h"
#include "xs_match.h"
#include "xs_fcgi.h"
#include "xs_html.h"
#include "xs_po.h"
#include "xs_webmention.h"
#include "xs_list_tools.h"

#include "snac.h"

#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

xs_str *srv_basedir = NULL;
xs_dict *srv_config = NULL;
xs_str *srv_baseurl = NULL;
xs_str *srv_proxy_token_seed = NULL;
xs_dict *srv_langs = NULL;
const char *months[12] = {0};

int dbglevel = 0;


int mkdirx(const char *pathname)
/* creates a directory with special permissions */
{
    int ret;

    if ((ret = mkdir(pathname, DIR_PERM)) != -1) {
        /* try to the set the setgid bit, to allow system users
           to create files in these directories using the
           command-line tool. This may fail in some restricted
           environments, but it's of no use there anyway */
        chmod(pathname, DIR_PERM_ADD);
    }

    return ret;
}


xs_str *tid(int offset)
/* returns a time-based Id */
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return xs_fmt("%010ld.%06ld", (long)tv.tv_sec + (long)offset, (long)tv.tv_usec);
}


double ftime(void)
/* returns the UNIX time as a float */
{
    xs *ntid = tid(0);

    return atof(ntid);
}


int validate_uid(const char *uid)
/* returns if uid is a valid identifier */
{
    if (!uid || *uid == '\0')
        return 0;

    while (*uid) {
        if (!(isalnum((unsigned char)*uid) || *uid == '_'))
            return 0;

        uid++;
    }

    return 1;
}


void srv_log(xs_str *str)
/* logs a debug message */
{
    if (xs_str_in(str, srv_basedir) != -1) {
        /* replace basedir with ~ */
        str = xs_replace_i(str, srv_basedir, "~");
    }

    xs *tm = xs_str_localtime(0, "%H:%M:%S");
    
    fprintf(stderr, "%s %s\n", tm, str);

    /* if the ~/log/ folder exists, also write to a file there */
    xs *dt = xs_str_localtime(0, "%Y-%m-%d");
    xs *lf = xs_fmt("%s/log/%s.log", srv_basedir, dt);
    
    FILE *f;
    if ((f = fopen(lf, "a")) != NULL) {
        fprintf(f, "%s %s\n", tm, str);
        fclose(f);
    }

    xs_free(str);
}


void snac_log(snac *snac, xs_str *str)
/* prints a user debugging information */
{
    xs *o_str = str;
    xs_str *msg = xs_fmt("[%s] %s", snac->uid, o_str);

    if (xs_str_in(msg, snac->basedir) != -1) {
        /* replace long basedir references with ~ */
        msg = xs_replace_i(msg, snac->basedir, "~");
    }

    srv_log(msg);
}


xs_str *hash_password(const char *uid, const char *passwd, const char *nonce)
/* hashes a password */
{
    xs *d_nonce = NULL;
    xs *combi;
    xs *hash;

    if (nonce == NULL) {
        unsigned int r;
        xs_rnd_buf(&r, sizeof(r));
        d_nonce = xs_fmt("%08x", r);
        nonce = d_nonce;
    }

    combi = xs_fmt("%s:%s:%s", nonce, uid, passwd);
    hash  = xs_sha1_hex(combi, strlen(combi));

    return xs_fmt("%s:%s", nonce, hash);
}


int check_password(const char *uid, const char *passwd, const char *hash)
/* checks a password */
{
    int ret = 0;
    xs *spl = xs_split_n(hash, ":", 1);

    if (xs_list_len(spl) == 2) {
        xs *n_hash = hash_password(uid, passwd, xs_list_get(spl, 0));

        ret = (strcmp(hash, n_hash) == 0);
    }

    return ret;
}


char* findprog(const char *prog)
/* find a prog in PATH and return the first match */
{
    char *path_env, *path, *dir, filename[PATH_MAX];
    int len;
    struct stat sbuf;

    path_env = getenv("PATH");
    if (!prog || !path_env)
        return NULL;

    path_env = strdup(path_env);
    if (!path_env)
        return NULL;
    path = path_env;

    while ((dir = strsep(&path, ":")) != NULL) {
        /* empty entries as ./ instead of / */
        if (*dir == '\0')
            dir = ".";

        /* strip trailing / */
        len = strlen(dir);
        while (len > 0 && dir[len-1] == '/')
            dir[--len] = '\0';

        len = snprintf(filename, sizeof(filename), "%s/%s", dir, prog);
        if (len > 0 && len < (int) sizeof(filename) &&
            (stat(filename, &sbuf) == 0) && S_ISREG(sbuf.st_mode) &&
            access(filename, X_OK) == 0) {
            free(path_env);
            return strdup(filename);
        }
    }

    free(path_env);
    return NULL;
}


int strip_media(const char *fn)
/* strips EXIF data from a file */
{
    int ret = 0;

#ifdef SNAC_ESP32
    /* On ESP32 we don't have mogrify/ffmpeg. Media processing is expected to
       be offloaded to a host PC. For now, keep uploads working by no-op'ing. */
    (void)fn;
    return 0;
#endif

    const xs_val *v = xs_dict_get(srv_config, "strip_exif");

    if (xs_type(v) == XSTYPE_TRUE) {
        /* Heuristic: find 'user/' in the path to make it relative */
        /* This works for ~/user/..., /var/snac/user/..., etc. */
        const char *r_fn = strstr(fn, "user/");
        
        if (r_fn == NULL) {
            /* Fallback: try to strip ~/ if present */
            if (strncmp(fn, "~/", 2) == 0)
                r_fn = fn + 2;
            else
                r_fn = fn;
        }

        xs *l_fn = xs_tolower_i(xs_dup(r_fn));

        /* check image extensions */
        if (xs_endswith(l_fn, ".jpg") || xs_endswith(l_fn, ".jpeg") ||
            xs_endswith(l_fn, ".png") || xs_endswith(l_fn, ".webp") ||
            xs_endswith(l_fn, ".heic") || xs_endswith(l_fn, ".heif") ||
            xs_endswith(l_fn, ".avif") || xs_endswith(l_fn, ".tiff") ||
            xs_endswith(l_fn, ".gif")  || xs_endswith(l_fn, ".bmp")) {

            const char *mp = xs_dict_get(srv_config, "mogrify_path");

            pid_t pid = fork();
            if (pid == -1) {
                srv_log(xs_fmt("strip_media: cannot fork()"));
                return -1;
            } else if (pid == 0) {
                chdir(srv_basedir);
                execl(mp, "-auto-orient", "-strip", r_fn, (char*) NULL);
                _exit(1);
            }

            if (waitpid(pid, &ret, 0) == -1) {
                srv_log(xs_fmt("strip_media: cannot waitpid()"));
                return -1;
            }

            if (ret != 0)
                srv_log(xs_fmt("strip_media: error stripping %s %d", r_fn, ret));
            else
                srv_debug(1, xs_fmt("strip_media: stripped %s", r_fn));
        }
        else
        /* check video extensions */
        if (xs_endswith(l_fn, ".mp4") || xs_endswith(l_fn, ".m4v") ||
            xs_endswith(l_fn, ".mov") || xs_endswith(l_fn, ".webm") ||
            xs_endswith(l_fn, ".mkv") || xs_endswith(l_fn, ".avi")) {

            const char *fp = xs_dict_get(srv_config, "ffmpeg_path");

            /* ffmpeg cannot modify in-place, so we need a temp file */
            /* we must preserve valid extension for ffmpeg to guess the format */
            const char *ext = strrchr(r_fn, '.');
            if (ext == NULL) ext = "";
            xs *tmp_fn = xs_fmt("%s.tmp%s", r_fn, ext);

            pid_t pid = fork();
            if (pid == -1) {
                srv_log(xs_fmt("strip_media: cannot fork()"));
                return -1;
            } else if (pid == 0) {
                chdir(srv_basedir);
                /* -map_metadata -1 strips all global metadata */
                /* -c copy copies input streams without re-encoding */
                execl(fp, "-y", "-i", r_fn, "-map_metadata", "-1", "-c", "copy", tmp_fn, (char*) NULL);
                _exit(1);
            }

            if (waitpid(pid, &ret, 0) == -1) {
                srv_log(xs_fmt("strip_media: cannot waitpid()"));
                return -1;
            }

            if (ret != 0) {
                srv_log(xs_fmt("strip_media: error stripping %s %d", r_fn, ret));

                /* try to cleanup, just in case */
                /* unlink needs full path too if we are not in basedir */
                xs *full_tmp_fn = xs_fmt("%s/%s", srv_basedir, tmp_fn);
                unlink(full_tmp_fn);
            }
            else {
                /* rename tmp file to original */
                /* use full path for source because it was created relative to basedir */
                xs *full_tmp_fn = xs_fmt("%s/%s", srv_basedir, tmp_fn);
                
                if (rename(full_tmp_fn, fn) == 0)
                    srv_debug(1, xs_fmt("strip_media: stripped %s", fn));
                else
                    srv_log(xs_fmt("strip_media: error renaming %s to %s", full_tmp_fn, fn));
            }
        }
    }

    return ret;
}


int check_strip_tool(void)
/* check if strip_exif tools do exist and fix their absolute path */
{
    const xs_val *v = xs_dict_get(srv_config, "strip_exif");
    /* skip if unless strip_exif; return non-error */
    if (xs_type(v) != XSTYPE_TRUE)
        return 1;

    int ret = 1;
    const char *progs[] = { "ffmpeg", "mogrify" };

    for (int i = 0; i < (int)(sizeof(progs) / sizeof(progs[0])); i++) {
        xs_str *key = xs_fmt("%s_path", progs[i]);

        const char *val = xs_dict_get(srv_config, key);
        if (val == NULL) {
            val = findprog(progs[i]);
            if (val != NULL)
                srv_debug(1, xs_fmt("check_strip_tool: found %s in PATH at %s", progs[i], val));
        }

        if (val == NULL) {
            srv_log(xs_fmt("check_strip_tool: %s not found in PATH", progs[i]));
            ret = 0;
        } else if (access(val, X_OK) != 0) {
            srv_log(xs_fmt("check_strip_tool: %s '%s' is not executable", progs[i], val));
            ret = 0;
        } else {
            srv_config = xs_dict_set(srv_config, key, val);
        }

        xs_free(key);
    }

    return ret;
}
