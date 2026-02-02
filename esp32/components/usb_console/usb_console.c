/* Minimal USB CDC console for ESP32-S3 (TinyUSB).
 * Implements a small REPL for instance management.
 */

#include "usb_console.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"

#include "esp_heap_caps.h"

#include "xs.h"
#include "snac.h"
#include "xs_http.h"
#include "xs_openssl.h"

#include "tinyusb.h"
#include "tusb.h"

static const char *TAG = "usb_console";

static usb_console_sync_cb_t s_sync_cb = NULL;
static void *s_sync_cb_arg = NULL;

static sdmmc_card_t *s_msc_card = NULL;
static unsigned int s_msc_start_lba = 0;
static unsigned int s_msc_lba_count = 0;

static void cdc_write_str(const char *s);

static volatile int s_worker_busy = 0;

typedef struct {
    char uid[64];
} adduser_args_t;

enum {
    USBCMD_DELUSER = 1,
    USBCMD_RESETPWD,
    USBCMD_QUEUE_USER,
    USBCMD_QUEUE_ALL,
    USBCMD_PURGE_ALL,
    USBCMD_UPDATE,
    USBCMD_VERIFY_LINKS,
    USBCMD_EXPORT_CSV,
    USBCMD_EXPORT_POSTS,
    USBCMD_IMPORT_CSV,
    USBCMD_MIGRATE,
    USBCMD_LISTS,
    USBCMD_MUTED,
    USBCMD_TOP_TEN,
    USBCMD_REFRESH,
    USBCMD_FOLLOW,
    USBCMD_UNFOLLOW,
    USBCMD_REQUEST,
    USBCMD_INSERT,
    USBCMD_ACTOR,
    USBCMD_NOTE,
    USBCMD_BOOST,
    USBCMD_UNBOOST,
    USBCMD_PING,
    USBCMD_PIN,
    USBCMD_UNPIN,
    USBCMD_BOOKMARK,
    USBCMD_UNBOOKMARK,
    USBCMD_LIMIT,
    USBCMD_UNLIMIT,
    USBCMD_UNMUTE,
    USBCMD_ALIAS,
    USBCMD_LIST_CREATE,
    USBCMD_LIST_REMOVE,
    USBCMD_LIST_ADD,
    USBCMD_LIST_DEL,
    USBCMD_WEBFINGER,
    USBCMD_COLLECT_REPLIES,
    USBCMD_SEARCH,
    USBCMD_BLOCK_INSTANCE,
    USBCMD_UNBLOCK_INSTANCE,
    USBCMD_LIST_MEMBERS,
};

typedef struct {
    int op;
    char uid[64];
    char arg1[256];
    char arg2[256];
    int num_arg;
} usercmd_args_t;

static void adduser_worker_task(void *arg)
{
    adduser_args_t *a = (adduser_args_t *)arg;

    cdc_write_str("\r\nadduser: running (worker)\r\n");
    int ret = adduser(a->uid);
    if (ret == 0)
        cdc_write_str("\r\nadduser: done (password printed on log)\r\n");
    else {
        char msg[160];
        int e = errno;
        snprintf(msg, sizeof(msg), "\r\nadduser: failed (errno=%d: %s)\r\n", e, strerror(e));
        cdc_write_str(msg);
    }

    xs_free(a);
    s_worker_busy = 0;
    vTaskDelete(NULL);
}

static void usercmd_worker_task(void *arg)
{
    usercmd_args_t *a = (usercmd_args_t *)arg;
    int opened = 0;
    int ret = 0;

    const char *op = "cmd";
    if (a->op == USBCMD_DELUSER) op = "deluser";
    else if (a->op == USBCMD_RESETPWD) op = "resetpwd";
    else if (a->op == USBCMD_QUEUE_USER) op = "queue";
    else if (a->op == USBCMD_QUEUE_ALL) op = "queue_all";
    else if (a->op == USBCMD_PURGE_ALL) op = "purge_all";
    else if (a->op == USBCMD_UPDATE) op = "update";
    else if (a->op == USBCMD_VERIFY_LINKS) op = "verify_links";
    else if (a->op == USBCMD_EXPORT_CSV) op = "export_csv";
    else if (a->op == USBCMD_EXPORT_POSTS) op = "export_posts";
    else if (a->op == USBCMD_IMPORT_CSV) op = "import_csv";
    else if (a->op == USBCMD_MIGRATE) op = "migrate";
    else if (a->op == USBCMD_LISTS) op = "lists";
    else if (a->op == USBCMD_MUTED) op = "muted";
    else if (a->op == USBCMD_TOP_TEN) op = "top_ten";
    else if (a->op == USBCMD_REFRESH) op = "refresh";
    else if (a->op == USBCMD_FOLLOW) op = "follow";
    else if (a->op == USBCMD_UNFOLLOW) op = "unfollow";
    else if (a->op == USBCMD_REQUEST) op = "request";
    else if (a->op == USBCMD_INSERT) op = "insert";
    else if (a->op == USBCMD_ACTOR) op = "actor";
    else if (a->op == USBCMD_NOTE) op = "note";
    else if (a->op == USBCMD_BOOST) op = "boost";
    else if (a->op == USBCMD_UNBOOST) op = "unboost";
    else if (a->op == USBCMD_PING) op = "ping";
    else if (a->op == USBCMD_PIN) op = "pin";
    else if (a->op == USBCMD_UNPIN) op = "unpin";
    else if (a->op == USBCMD_BOOKMARK) op = "bookmark";
    else if (a->op == USBCMD_UNBOOKMARK) op = "unbookmark";
    else if (a->op == USBCMD_LIMIT) op = "limit";
    else if (a->op == USBCMD_UNLIMIT) op = "unlimit";
    else if (a->op == USBCMD_UNMUTE) op = "unmute";
    else if (a->op == USBCMD_ALIAS) op = "alias";
    else if (a->op == USBCMD_LIST_CREATE) op = "list_create";
    else if (a->op == USBCMD_LIST_REMOVE) op = "list_remove";
    else if (a->op == USBCMD_LIST_ADD) op = "list_add";
    else if (a->op == USBCMD_LIST_DEL) op = "list_del";
    else if (a->op == USBCMD_WEBFINGER) op = "webfinger";
    else if (a->op == USBCMD_COLLECT_REPLIES) op = "collect_replies";
    else if (a->op == USBCMD_SEARCH) op = "search";
    else if (a->op == USBCMD_BLOCK_INSTANCE) op = "block";
    else if (a->op == USBCMD_UNBLOCK_INSTANCE) op = "unblock";
    else if (a->op == USBCMD_LIST_MEMBERS) op = "list_members";

    {
        char msg[96];
        snprintf(msg, sizeof(msg), "\r\n%s: running (worker)\r\n", op);
        cdc_write_str(msg);
    }

    if (p_state != NULL && p_state->srv_running) {
        /* Server already running; reuse open globals. */
        if (srv_config == NULL || srv_basedir == NULL) {
            cdc_write_str("\r\ncmd: server context not ready yet (try again in a moment)\r\n");
            ret = 1;
            goto done;
        }
    }
    else {
        ret = srv_open("/data/snac", 1);
        if (!ret || srv_config == NULL) {
            char msg[160];
            int e = errno;
            snprintf(msg, sizeof(msg),
                "\r\ncmd: srv_open failed (ret=%d, errno=%d: %s)\r\n",
                ret, e, strerror(e));
            cdc_write_str(msg);
            ret = 1;
            goto done;
        }
        opened = 1;
    }

    if (a->op == USBCMD_PURGE_ALL) {
        purge_all();
        ret = 0;
    }
    else if (a->op == USBCMD_QUEUE_ALL) {
        int cnt = process_queue();
        char msg[96];
        snprintf(msg, sizeof(msg), "\r\nqueue_all: processed %d items\r\n", cnt);
        cdc_write_str(msg);
        ret = 0;
    }
    else {
        snac user;
        memset(&user, 0, sizeof(user));

        if (!user_open(&user, a->uid)) {
            char msg[160];
            snprintf(msg, sizeof(msg), "\r\n%s: invalid user '%s'\r\n", op, a->uid);
            cdc_write_str(msg);
            ret = 1;
        }
        else {
            if (a->op == USBCMD_DELUSER)
                ret = deluser(&user);
            else if (a->op == USBCMD_RESETPWD)
                ret = resetpwd(&user);
            else if (a->op == USBCMD_QUEUE_USER) {
                int cnt = process_user_queue(&user);
                char msg[96];
                snprintf(msg, sizeof(msg), "\r\nqueue: processed %d items\r\n", cnt);
                cdc_write_str(msg);
                ret = 0;
            }
            else if (a->op == USBCMD_UPDATE) {
                xs *a_msg = msg_actor(&user);
                xs *u_msg = msg_update(&user, a_msg);
                enqueue_message(&user, u_msg);
                ret = 0;
            }
            else if (a->op == USBCMD_VERIFY_LINKS) {
                verify_links(&user);
                ret = 0;
            }
            else if (a->op == USBCMD_EXPORT_CSV) {
                export_csv(&user);
                ret = 0;
            }
            else if (a->op == USBCMD_EXPORT_POSTS) {
                export_posts(&user);
                ret = 0;
            }
            else if (a->op == USBCMD_IMPORT_CSV) {
                import_csv(&user);
                ret = 0;
            }
            else if (a->op == USBCMD_MIGRATE) {
                ret = migrate_account(&user);
            }
            else if (a->op == USBCMD_LISTS) {
                xs *lol = list_maint(&user, NULL, 0);
                const xs_list *l;
                xs_list_foreach(lol, l) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "\r\n%s (%s)\r\n", xs_list_get(l, 1), xs_list_get(l, 0));
                    cdc_write_str(msg);
                }
                ret = 0;
            }
            else if (a->op == USBCMD_MUTED) {
                xs *l = muted_list(&user);
                const char *v;
                xs_list_foreach(l, v) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "\r\n%s\r\n", v);
                    cdc_write_str(msg);
                }
                ret = 0;
            }
            else if (a->op == USBCMD_TOP_TEN) {
                int count = a->num_arg > 0 ? a->num_arg : 10;
                xs *l = user_top_ten(&user, count);
                const xs_list *i;
                xs_list_foreach(l, i) {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "\r\n%s %ld★ %ld↺\r\n", xs_list_get(i, 0),
                        xs_number_get_l(xs_list_get(i, 1)),
                        xs_number_get_l(xs_list_get(i, 2)));
                    cdc_write_str(msg);
                }
                ret = 0;
            }
            else if (a->op == USBCMD_REFRESH) {
                xs *fwers = follower_list(&user);
                xs *fwing = following_list(&user);
                const char *id;
                xs_list_foreach(fwers, id)
                    enqueue_actor_refresh(&user, id, 0);
                xs_list_foreach(fwing, id)
                    enqueue_actor_refresh(&user, id, 0);
                ret = 0;
            }
            else if (a->op == USBCMD_FOLLOW) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nfollow: missing actor\r\n");
                    ret = 1;
                } else {
                    xs *msg = msg_follow(&user, a->arg1);
                    if (msg != NULL) {
                        const char *actor = xs_dict_get(msg, "object");
                        following_add(&user, actor, msg);
                        enqueue_output_by_actor(&user, msg, actor, 0);
                        ret = 0;
                    } else
                        ret = 1;
                }
            }
            else if (a->op == USBCMD_UNFOLLOW) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nunfollow: missing actor\r\n");
                    ret = 1;
                } else {
                    xs *object = NULL;
                    if (valid_status(following_get(&user, a->arg1, &object))) {
                        xs *msg = msg_undo(&user, xs_dict_get(object, "object"));
                        following_del(&user, a->arg1);
                        enqueue_output_by_actor(&user, msg, a->arg1, 0);
                        ret = 0;
                    } else
                        ret = 1;
                }
            }
            else if (a->op == USBCMD_REQUEST) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nrequest: missing URL\r\n");
                    ret = 1;
                } else {
                    xs *data = NULL;
                    int status = activitypub_request(&user, a->arg1, &data);
                    char msg[256];
                    snprintf(msg, sizeof(msg), "\r\nrequest: status %d\r\n", status);
                    cdc_write_str(msg);
                    ret = valid_status(status) ? 0 : 1;
                }
            }
            else if (a->op == USBCMD_INSERT) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\ninsert: missing URL\r\n");
                    ret = 1;
                } else {
                    xs *data = NULL;
                    int status = activitypub_request(&user, a->arg1, &data);
                    if (valid_status(status) && data != NULL) {
                        enqueue_actor_refresh(&user, xs_dict_get(data, "attributedTo"), 0);
                        if (!timeline_here(&user, a->arg1))
                            timeline_add(&user, a->arg1, data);
                        ret = 0;
                    } else
                        ret = 1;
                }
            }
            else if (a->op == USBCMD_ACTOR) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nactor: missing URL\r\n");
                    ret = 1;
                } else {
                    xs *actor = NULL;
                    int status = actor_request(&user, a->arg1, &actor);
                    char msg[256];
                    snprintf(msg, sizeof(msg), "\r\nactor: status %d\r\n", status);
                    cdc_write_str(msg);
                    ret = valid_status(status) ? 0 : 1;
                }
            }
            else if (a->op == USBCMD_NOTE) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nnote: missing text\r\n");
                    ret = 1;
                } else {
                    xs *content = xs_dup(a->arg1);
                    xs *msg = msg_note(&user, content, NULL, NULL, NULL, SCOPE_PUBLIC, NULL, NULL);
                    if (msg != NULL) {
                        xs *c_msg = msg_create(&user, msg);
                        enqueue_message(&user, c_msg);
                        timeline_add(&user, xs_dict_get(msg, "id"), msg);
                        ret = 0;
                    } else
                        ret = 1;
                }
            }
            else if (a->op == USBCMD_BOOST) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nboost: missing URL\r\n");
                    ret = 1;
                } else {
                    xs *msg = msg_admiration(&user, a->arg1, "Announce");
                    if (msg != NULL) {
                        enqueue_message(&user, msg);
                        timeline_admire(&user, xs_dict_get(msg, "object"), user.actor, 0, "");
                        ret = 0;
                    } else
                        ret = 1;
                }
            }
            else if (a->op == USBCMD_UNBOOST) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nunboost: missing URL\r\n");
                    ret = 1;
                } else {
                    xs *msg = msg_repulsion(&user, a->arg1, "Announce");
                    if (msg != NULL) {
                        enqueue_message(&user, msg);
                        ret = 0;
                    } else
                        ret = 1;
                }
            }
            else if (a->op == USBCMD_PING) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nping: missing actor\r\n");
                    ret = 1;
                } else {
                    xs *actor = NULL;
                    int status = actor_request(&user, a->arg1, &actor);
                    if (valid_status(status)) {
                        xs *msg = msg_ping(&user, xs_dict_get(actor, "id"));
                        if (msg != NULL) {
                            enqueue_output_by_actor(&user, msg, xs_dict_get(actor, "id"), 0);
                            ret = 0;
                        }
                    }
                }
            }
            else if (a->op == USBCMD_PIN) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\npin: missing URL\r\n");
                    ret = 1;
                } else {
                    ret = pin(&user, a->arg1);
                }
            }
            else if (a->op == USBCMD_UNPIN) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nunpin: missing URL\r\n");
                    ret = 1;
                } else {
                    ret = unpin(&user, a->arg1);
                }
            }
            else if (a->op == USBCMD_BOOKMARK) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nbookmark: missing URL\r\n");
                    ret = 1;
                } else {
                    ret = bookmark(&user, a->arg1) ? 0 : 1;
                }
            }
            else if (a->op == USBCMD_UNBOOKMARK) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nunbookmark: missing URL\r\n");
                    ret = 1;
                } else {
                    ret = unbookmark(&user, a->arg1) ? 0 : 1;
                }
            }
            else if (a->op == USBCMD_LIMIT) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nlimit: missing actor\r\n");
                    ret = 1;
                } else {
                    ret = limit(&user, a->arg1);
                }
            }
            else if (a->op == USBCMD_UNLIMIT) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nunlimit: missing actor\r\n");
                    ret = 1;
                } else {
                    ret = unlimit(&user, a->arg1);
                }
            }
            else if (a->op == USBCMD_UNMUTE) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nunmute: missing actor\r\n");
                    ret = 1;
                } else {
                    if (is_muted(&user, a->arg1)) {
                        unmute(&user, a->arg1);
                        ret = 0;
                    } else {
                        cdc_write_str("\r\nunmute: actor is not muted\r\n");
                        ret = 1;
                    }
                }
            }
            else if (a->op == USBCMD_ALIAS) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nalias: missing account\r\n");
                    ret = 1;
                } else {
                    xs *actor = NULL;
                    xs *uid = NULL;
                    int status = webfinger_request(a->arg1, &actor, &uid);
                    if (valid_status(status) && actor != NULL) {
                        user.config = xs_dict_set(user.config, "alias", actor);
                        user.config = xs_dict_set(user.config, "alias_raw", a->arg1);
                        user_persist(&user, 1);
                        ret = 0;
                    } else {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "\r\nalias: webfinger failed (status %d)\r\n", status);
                        cdc_write_str(msg);
                        ret = 1;
                    }
                }
            }
            else if (a->op == USBCMD_LIST_CREATE) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nlist_create: missing name\r\n");
                    ret = 1;
                } else {
                    xs *list = list_maint(&user, a->arg1, 1);
                    ret = list != NULL ? 0 : 1;
                }
            }
            else if (a->op == USBCMD_LIST_REMOVE) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nlist_remove: missing name\r\n");
                    ret = 1;
                } else {
                    xs *list = list_maint(&user, a->arg1, 2);
                    ret = list != NULL ? 0 : 1;
                }
            }
            else if (a->op == USBCMD_LIST_ADD) {
                if (a->arg1[0] == '\0' || a->arg2[0] == '\0') {
                    cdc_write_str("\r\nlist_add: missing name or account\r\n");
                    ret = 1;
                } else {
                    xs *actor = NULL;
                    xs *uid = NULL;
                    int status = webfinger_request(a->arg2, &actor, &uid);
                    if (valid_status(status) && actor != NULL) {
                        xs *lid = list_maint(&user, a->arg1, 4);
                        if (lid != NULL) {
                            xs *md5 = xs_md5_hex(actor, strlen(actor));
                            list_members(&user, lid, md5, 1);
                            ret = 0;
                        } else {
                            cdc_write_str("\r\nlist_add: list not found\r\n");
                            ret = 1;
                        }
                    } else
                        ret = 1;
                }
            }
            else if (a->op == USBCMD_LIST_DEL) {
                if (a->arg1[0] == '\0' || a->arg2[0] == '\0') {
                    cdc_write_str("\r\nlist_del: missing name or actor\r\n");
                    ret = 1;
                } else {
                    xs *lid = list_maint(&user, a->arg1, 4);
                    if (lid != NULL) {
                        xs *md5 = xs_md5_hex(a->arg2, strlen(a->arg2));
                        list_members(&user, lid, md5, 2);
                        ret = 0;
                    } else {
                        cdc_write_str("\r\nlist_del: list not found\r\n");
                        ret = 1;
                    }
                }
            }
            else if (a->op == USBCMD_COLLECT_REPLIES) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\ncollect_replies: missing URL\r\n");
                    ret = 1;
                } else {
                    enqueue_collect_replies(&user, a->arg1);
                    ret = 0;
                }
            }
            else if (a->op == USBCMD_SEARCH) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nsearch: missing regex\r\n");
                    ret = 1;
                } else {
                    int to;
                    xs *r = content_search(&user, a->arg1, 1, 0, XS_ALL, 10, &to);
                    int c = 0;
                    const char *v;
                    while (xs_list_next(r, &v, &c)) {
                        char buf[512];
                        snprintf(buf, sizeof(buf), "\r\n%s/admin/p/%s\r\n", user.actor, v);
                        cdc_write_str(buf);
                    }
                    ret = 0;
                }
            }
            else if (a->op == USBCMD_LIST_MEMBERS) {
                if (a->arg1[0] == '\0') {
                    cdc_write_str("\r\nlist_members: missing name\r\n");
                    ret = 1;
                } else {
                    xs *lid = list_maint(&user, a->arg1, 4);
                    if (lid != NULL) {
                        xs *lcont = list_members(&user, lid, NULL, 0);
                        const char *md5;
                        xs_list_foreach(lcont, md5) {
                            xs *actor = NULL;
                            if (valid_status(object_get_by_md5(md5, &actor))) {
                                char buf[256];
                                snprintf(buf, sizeof(buf), "\r\n%s\r\n", xs_dict_get(actor, "id"));
                                cdc_write_str(buf);
                            }
                        }
                        ret = 0;
                    } else
                        ret = 1;
                }
            }

            user_free(&user);
        }
    }
    
    if (a->op == USBCMD_WEBFINGER) {
        xs *actor = NULL;
        xs *uid = NULL;
        int status = webfinger_request(a->uid, &actor, &uid);
        char msg[512];
        snprintf(msg, sizeof(msg), "\r\nwebfinger: status %d\r\n", status);
        cdc_write_str(msg);
        if (actor != NULL) {
            snprintf(msg, sizeof(msg), "actor: %s\r\n", actor);
            cdc_write_str(msg);
        }
        if (uid != NULL) {
            snprintf(msg, sizeof(msg), "uid: %s\r\n", uid);
            cdc_write_str(msg);
        }
        ret = 0;
    }
    
    if (a->op == USBCMD_BLOCK_INSTANCE) {
        ret = instance_block(a->uid);
        if (ret < 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "\r\nblock: error blocking instance %s: %d\r\n", a->uid, ret);
            cdc_write_str(msg);
            ret = 1;
        } else
            ret = 0;
    }
    
    if (a->op == USBCMD_UNBLOCK_INSTANCE) {
        ret = instance_unblock(a->uid);
        if (ret < 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "\r\nunblock: error unblocking instance %s: %d\r\n", a->uid, ret);
            cdc_write_str(msg);
            ret = 1;
        } else
            ret = 0;
    }

done:
    if (opened)
        srv_free();

    if (ret == 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "\r\n%s: done\r\n", op);
        cdc_write_str(msg);
    }
    else {
        char msg[160];
        int e = errno;
        snprintf(msg, sizeof(msg), "\r\n%s: failed (errno=%d: %s)\r\n", op, e, strerror(e));
        cdc_write_str(msg);
    }

    xs_free(a);
    s_worker_busy = 0;
    vTaskDelete(NULL);
}

void usb_console_set_sync_callback(usb_console_sync_cb_t cb, void *arg)
{
    s_sync_cb = cb;
    s_sync_cb_arg = arg;
}

void usb_console_set_msc_sdmmc(sdmmc_card_t *card, unsigned int start_lba, unsigned int lba_count)
{
    s_msc_card = card;
    s_msc_start_lba = start_lba;
    s_msc_lba_count = lba_count;
}

static void cdc_write_str(const char *s)
{
    if (s == NULL)
        return;

    if (tud_cdc_connected()) {
        const char *p = s;
        size_t left = strlen(s);

        while (left > 0 && tud_cdc_connected()) {
            uint32_t n = tud_cdc_write(p, (uint32_t)left);
            if (n == 0) {
                vTaskDelay(1);
                continue;
            }

            p += n;
            left -= n;
        }

        tud_cdc_write_flush();
    }
}

static void prompt(void)
{
    cdc_write_str("\r\nsnac2> ");
}

static int parse_args(char *line, int cmdlen, char **uid, char **arg1, char **arg2)
{
    char *p = line + cmdlen;
    while (*p == ' ' || *p == '\t') p++;
    *uid = p;
    if (*p == '\0') return 0;
    
    while (*p != '\0' && *p != ' ' && *p != '\t') p++;
    if (*p != '\0') {
        *p = '\0';
        p++;
        while (*p == ' ' || *p == '\t') p++;
        *arg1 = p;
        if (*p == '\0') return 1;
        
        while (*p != '\0' && *p != ' ' && *p != '\t') p++;
        if (*p != '\0') {
            *p = '\0';
            p++;
            while (*p == ' ' || *p == '\t') p++;
            *arg2 = p;
            return 3;
        }
        return 2;
    }
    return 1;
}

static void snac_httpd_task(void *arg)
{
    (void)arg;

    if (!srv_open("/data/snac", 1)) {
        srv_log(xs_dup("ESP32: srv_open failed"));
        vTaskDelete(NULL);
    }

    httpd();
    srv_free();
    vTaskDelete(NULL);
}

static void handle_line(char *line)
{
    while (*line == ' ' || *line == '\t')
        line++;

    if (*line == '\0')
        return;

    if (strcmp(line, "help") == 0) {
        cdc_write_str(
            "\r\nCommands:\r\n"
            "  help               Show this help\r\n"
            "  init               Initialize /data/snac from /config/server.json\r\n"
            "  adduser UID        Add a user\r\n"
            "  deluser UID        Delete a user\r\n"
            "  update UID         Send user's updated profile\r\n"
            "  resetpwd UID       Reset user password\r\n"
            "  queue UID          Process user queue\r\n"
            "  queue              Process global queue\r\n"
            "  purge              Purge all old data\r\n"
            "  state              Print server state\r\n"
            "  webfinger ACCOUNT  Query about an account\r\n"
            "  follow UID ACTOR   Follow an actor\r\n"
            "  unfollow UID ACTOR Unfollow an actor\r\n"
            "  request UID URL    Request an object\r\n"
            "  insert UID URL     Request and insert object\r\n"
            "  collect_replies UID URL Collect all replies\r\n"
            "  actor UID URL      Request actor info\r\n"
            "  note UID TEXT      Send a note\r\n"
            "  boost UID URL      Boost a post\r\n"
            "  unboost UID URL    Unboost a post\r\n"
            "  ping UID ACTOR     Ping an actor\r\n"
            "  pin UID MSG_URL    Pin a message\r\n"
            "  unpin UID MSG_URL  Unpin a message\r\n"
            "  bookmark UID URL   Bookmark a message\r\n"
            "  unbookmark UID URL Unbookmark a message\r\n"
            "  block INST_URL     Block an instance\r\n"
            "  unblock INST_URL   Unblock an instance\r\n"
            "  limit UID ACTOR    Limit an actor\r\n"
            "  unlimit UID ACTOR  Unlimit an actor\r\n"
            "  muted UID          List muted actors\r\n"
            "  unmute UID ACTOR   Unmute an actor\r\n"
            "  verify_links UID   Verify user's links\r\n"
            "  search UID REGEX   Search posts by content\r\n"
            "  export_csv UID     Export followers/lists\r\n"
            "  export_posts UID   Export posts to JSON\r\n"
            "  import_csv UID     Import data from CSV\r\n"
            "  alias UID ACCOUNT  Set account as alias\r\n"
            "  migrate UID        Migrate to alias account\r\n"
            "  lists UID          Show user's lists\r\n"
            "  list_members UID N Show list members\r\n"
            "  list_create UID N  Create a list\r\n"
            "  list_remove UID N  Remove a list\r\n"
            "  list_add UID N ACT Add account to list\r\n"
            "  list_del UID N ACT Delete actor from list\r\n"
            "  top_ten UID [N]    Show popular posts\r\n"
            "  refresh UID        Refresh all actors\r\n"
            "  start              Start HTTP server\r\n"
            "  sync_config        Sync config files\r\n"
            "  reboot             Restart ESP32\r\n"
        );
        return;
    }

    if (strcmp(line, "init") == 0) {
        cdc_write_str("\r\ninit: creating /data/snac\r\n");

        struct stat st;
        if (stat("/data", &st) != 0 || !S_ISDIR(st.st_mode)) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                "\r\ninit: /data is missing (DATA partition not mounted?) (errno=%d: %s)\r\n",
                errno, strerror(errno));
            cdc_write_str(msg);
            return;
        }
        if (stat("/config", &st) != 0 || !S_ISDIR(st.st_mode)) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                "\r\ninit: /config is missing (CONFIG partition not mounted?) (errno=%d: %s)\r\n",
                errno, strerror(errno));
            cdc_write_str(msg);
            return;
        }

        int ret = snac_init_esp32("/data/snac", "/config/server.json");
        if (ret == 0)
            cdc_write_str("\r\ninit: done\r\n");
        else {
            char msg[160];
            int e = errno;
            snprintf(msg, sizeof(msg),
                "\r\ninit: failed (ret=%d, errno=%d: %s)\r\n",
                ret, e, strerror(e));
            cdc_write_str(msg);
        }
        return;
    }

    if (strncmp(line, "adduser", 7) == 0 && (line[7] == '\0' || line[7] == ' ' || line[7] == '\t')) {
        char *uid = line + 7;
        while (*uid == ' ' || *uid == '\t')
            uid++;

        if (*uid == '\0') {
            cdc_write_str("\r\nadduser: missing UID\r\n");
            return;
        }

        int ret = 0;

        if (p_state != NULL && p_state->srv_running) {
            /* Server already running (auto-start). Reuse open globals; do NOT call srv_open/srv_free. */
            if (srv_config == NULL || srv_basedir == NULL) {
                cdc_write_str("\r\nadduser: server context not ready yet (try again in a moment)\r\n");
                return;
            }

            if (s_worker_busy) {
                cdc_write_str("\r\nadduser: busy\r\n");
                return;
            }

            adduser_args_t *a = xs_realloc(NULL, sizeof(adduser_args_t));
            memset(a, 0, sizeof(*a));
            strncpy(a->uid, uid, sizeof(a->uid) - 1);

            s_worker_busy = 1;
            xTaskCreate(adduser_worker_task, "adduser", 16384, a, 5, NULL);
            return;
        }

        cdc_write_str("\r\nadduser: opening /data/snac\r\n");
        ret = srv_open("/data/snac", 1);
        if (!ret || srv_config == NULL) {
            char msg[160];
            int e = errno;
            snprintf(msg, sizeof(msg),
                "\r\nadduser: srv_open failed (ret=%d, errno=%d: %s)\r\n",
                ret, e, strerror(e));
            cdc_write_str(msg);
            srv_free();
            return;
        }

        cdc_write_str("\r\nadduser: running\r\n");
        ret = adduser(uid);
        if (ret == 0)
            cdc_write_str("\r\nadduser: done (password printed on log)\r\n");
        else
            cdc_write_str("\r\nadduser: failed\r\n");

        srv_free();
        return;
    }

    if (strcmp(line, "state") == 0) {
        if (p_state == NULL) {
            cdc_write_str("\r\nstate: not available\r\n");
            return;
        }

        char msg[320];
        size_t free_8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        snprintf(msg, sizeof(msg),
            "\r\nstate: running=%d threads=%d job_fifo=%d peak_job_fifo=%d heap8_free=%lu psram_free=%lu\r\n",
            p_state->srv_running,
            p_state->n_threads,
            p_state->job_fifo_size,
            p_state->peak_job_fifo_size,
            (unsigned long)free_8,
            (unsigned long)free_psram);
        cdc_write_str(msg);
        return;
    }

    if (strncmp(line, "deluser", 7) == 0 && (line[7] == '\0' || line[7] == ' ' || line[7] == '\t')) {
        char *uid = line + 7;
        while (*uid == ' ' || *uid == '\t')
            uid++;

        if (*uid == '\0') {
            cdc_write_str("\r\ndeluser: missing UID\r\n");
            return;
        }

        if (s_worker_busy) {
            cdc_write_str("\r\ndeluser: busy\r\n");
            return;
        }

        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_DELUSER;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);

        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "deluser", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "resetpwd", 8) == 0 && (line[8] == '\0' || line[8] == ' ' || line[8] == '\t')) {
        char *uid = line + 8;
        while (*uid == ' ' || *uid == '\t')
            uid++;

        if (*uid == '\0') {
            cdc_write_str("\r\nresetpwd: missing UID\r\n");
            return;
        }

        if (s_worker_busy) {
            cdc_write_str("\r\nresetpwd: busy\r\n");
            return;
        }

        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_RESETPWD;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);

        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "resetpwd", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "queue", 5) == 0 && (line[5] == '\0' || line[5] == ' ' || line[5] == '\t')) {
        char *uid = line + 5;
        while (*uid == ' ' || *uid == '\t')
            uid++;

        if (s_worker_busy) {
            cdc_write_str("\r\nqueue: busy\r\n");
            return;
        }

        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        if (*uid == '\0')
            a->op = USBCMD_QUEUE_ALL;
        else {
            a->op = USBCMD_QUEUE_USER;
            strncpy(a->uid, uid, sizeof(a->uid) - 1);
        }

        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "queue", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "purge", 5) == 0 && (line[5] == '\0' || line[5] == ' ' || line[5] == '\t')) {
        char *uid = line + 5;
        while (*uid == ' ' || *uid == '\t')
            uid++;

        if (s_worker_busy) {
            cdc_write_str("\r\npurge: busy\r\n");
            return;
        }

        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        if (*uid != '\0') {
            cdc_write_str("\r\npurge: per-user purge not supported\r\n");
            xs_free(a);
            return;
        }

        a->op = USBCMD_PURGE_ALL;

        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "purge", 16384, a, 5, NULL);
        return;
    }

    if (strcmp(line, "start") == 0) {
        if (p_state != NULL && p_state->srv_running) {
            cdc_write_str("\r\nstart: already running\r\n");
            return;
        }
        cdc_write_str("\r\nstart: starting snac2 httpd task\r\n");
        xTaskCreate(snac_httpd_task, "snac2_httpd", 8192, NULL, 5, NULL);
        return;
    }

    if (strcmp(line, "sync_config") == 0) {
        if (s_sync_cb != NULL) {
            cdc_write_str("\r\nsync_config: running\r\n");
            s_sync_cb(s_sync_cb_arg);
            cdc_write_str("\r\nsync_config: done\r\n");
        } else
            cdc_write_str("\r\nsync_config: not configured\r\n");
        return;
    }

    if (strcmp(line, "reboot") == 0) {
        cdc_write_str("\r\nrebooting...\r\n");
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_restart();
        return;
    }

    if (strncmp(line, "update", 6) == 0 && (line[6] == '\0' || line[6] == ' ' || line[6] == '\t')) {
        char *uid = line + 6;
        while (*uid == ' ' || *uid == '\t') uid++;
        if (*uid == '\0') {
            cdc_write_str("\r\nupdate: missing UID\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nupdate: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_UPDATE;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "update", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "verify_links", 12) == 0 && (line[12] == '\0' || line[12] == ' ' || line[12] == '\t')) {
        char *uid = line + 12;
        while (*uid == ' ' || *uid == '\t') uid++;
        if (*uid == '\0') {
            cdc_write_str("\r\nverify_links: missing UID\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nverify_links: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_VERIFY_LINKS;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "verify_links", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "export_csv", 10) == 0 && (line[10] == '\0' || line[10] == ' ' || line[10] == '\t')) {
        char *uid = line + 10;
        while (*uid == ' ' || *uid == '\t') uid++;
        if (*uid == '\0') {
            cdc_write_str("\r\nexport_csv: missing UID\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nexport_csv: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_EXPORT_CSV;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "export_csv", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "export_posts", 12) == 0 && (line[12] == '\0' || line[12] == ' ' || line[12] == '\t')) {
        char *uid = line + 12;
        while (*uid == ' ' || *uid == '\t') uid++;
        if (*uid == '\0') {
            cdc_write_str("\r\nexport_posts: missing UID\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nexport_posts: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_EXPORT_POSTS;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "export_posts", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "import_csv", 10) == 0 && (line[10] == '\0' || line[10] == ' ' || line[10] == '\t')) {
        char *uid = line + 10;
        while (*uid == ' ' || *uid == '\t') uid++;
        if (*uid == '\0') {
            cdc_write_str("\r\nimport_csv: missing UID\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nimport_csv: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_IMPORT_CSV;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "import_csv", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "migrate", 7) == 0 && (line[7] == '\0' || line[7] == ' ' || line[7] == '\t')) {
        char *uid = line + 7;
        while (*uid == ' ' || *uid == '\t') uid++;
        if (*uid == '\0') {
            cdc_write_str("\r\nmigrate: missing UID\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nmigrate: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_MIGRATE;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "migrate", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "lists", 5) == 0 && (line[5] == '\0' || line[5] == ' ' || line[5] == '\t')) {
        char *uid = line + 5;
        while (*uid == ' ' || *uid == '\t') uid++;
        if (*uid == '\0') {
            cdc_write_str("\r\nlists: missing UID\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nlists: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_LISTS;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "lists", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "muted", 5) == 0 && (line[5] == '\0' || line[5] == ' ' || line[5] == '\t')) {
        char *uid = line + 5;
        while (*uid == ' ' || *uid == '\t') uid++;
        if (*uid == '\0') {
            cdc_write_str("\r\nmuted: missing UID\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nmuted: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_MUTED;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "muted", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "top_ten", 7) == 0 && (line[7] == '\0' || line[7] == ' ' || line[7] == '\t')) {
        char *uid = line + 7;
        while (*uid == ' ' || *uid == '\t') uid++;
        char *end = uid;
        while (*end != '\0' && *end != ' ' && *end != '\t') end++;
        char *num_str = NULL;
        if (*end != '\0') {
            *end = '\0';
            num_str = end + 1;
            while (*num_str == ' ' || *num_str == '\t') num_str++;
        }
        if (*uid == '\0') {
            cdc_write_str("\r\ntop_ten: missing UID\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\ntop_ten: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_TOP_TEN;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        if (num_str != NULL && *num_str != '\0')
            a->num_arg = atoi(num_str);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "top_ten", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "refresh", 7) == 0 && (line[7] == '\0' || line[7] == ' ' || line[7] == '\t')) {
        char *uid = line + 7;
        while (*uid == ' ' || *uid == '\t') uid++;
        if (*uid == '\0') {
            cdc_write_str("\r\nrefresh: missing UID\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nrefresh: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_REFRESH;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "refresh", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "webfinger", 9) == 0 && (line[9] == '\0' || line[9] == ' ' || line[9] == '\t')) {
        char *account = line + 9;
        while (*account == ' ' || *account == '\t') account++;
        if (*account == '\0') {
            cdc_write_str("\r\nwebfinger: missing account\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nwebfinger: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_WEBFINGER;
        strncpy(a->uid, account, sizeof(a->uid) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "webfinger", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "follow", 6) == 0 && (line[6] == '\0' || line[6] == ' ' || line[6] == '\t')) {
        char *uid = NULL, *actor = NULL, *unused = NULL;
        if (parse_args(line, 6, &uid, &actor, &unused) < 2 || uid[0] == '\0' || actor[0] == '\0') {
            cdc_write_str("\r\nfollow: usage: follow UID ACTOR\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nfollow: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_FOLLOW;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, actor, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "follow", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "unfollow", 8) == 0 && (line[8] == '\0' || line[8] == ' ' || line[8] == '\t')) {
        char *uid = NULL, *actor = NULL, *unused = NULL;
        if (parse_args(line, 8, &uid, &actor, &unused) < 2 || uid[0] == '\0' || actor[0] == '\0') {
            cdc_write_str("\r\nunfollow: usage: unfollow UID ACTOR\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nunfollow: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_UNFOLLOW;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, actor, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "unfollow", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "request", 7) == 0 && (line[7] == '\0' || line[7] == ' ' || line[7] == '\t')) {
        char *uid = NULL, *url = NULL, *unused = NULL;
        if (parse_args(line, 7, &uid, &url, &unused) < 2 || uid[0] == '\0' || url[0] == '\0') {
            cdc_write_str("\r\nrequest: usage: request UID URL\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nrequest: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_REQUEST;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, url, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "request", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "insert", 6) == 0 && (line[6] == '\0' || line[6] == ' ' || line[6] == '\t')) {
        char *uid = NULL, *url = NULL, *unused = NULL;
        if (parse_args(line, 6, &uid, &url, &unused) < 2 || uid[0] == '\0' || url[0] == '\0') {
            cdc_write_str("\r\ninsert: usage: insert UID URL\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\ninsert: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_INSERT;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, url, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "insert", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "actor", 5) == 0 && (line[5] == '\0' || line[5] == ' ' || line[5] == '\t')) {
        char *uid = NULL, *url = NULL, *unused = NULL;
        if (parse_args(line, 5, &uid, &url, &unused) < 2 || uid[0] == '\0' || url[0] == '\0') {
            cdc_write_str("\r\nactor: usage: actor UID URL\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nactor: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_ACTOR;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, url, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "actor", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "note", 4) == 0 && (line[4] == '\0' || line[4] == ' ' || line[4] == '\t')) {
        char *uid = line + 4;
        while (*uid == ' ' || *uid == '\t') uid++;
        char *text = uid;
        while (*text != '\0' && *text != ' ' && *text != '\t') text++;
        if (*text != '\0') {
            *text = '\0';
            text++;
            while (*text == ' ' || *text == '\t') text++;
        }
        if (uid[0] == '\0' || text[0] == '\0') {
            cdc_write_str("\r\nnote: usage: note UID TEXT\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nnote: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_NOTE;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, text, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "note", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "boost", 5) == 0 && (line[5] == '\0' || line[5] == ' ' || line[5] == '\t')) {
        char *uid = NULL, *url = NULL, *unused = NULL;
        if (parse_args(line, 5, &uid, &url, &unused) < 2 || uid[0] == '\0' || url[0] == '\0') {
            cdc_write_str("\r\nboost: usage: boost UID URL\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nboost: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_BOOST;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, url, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "boost", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "unboost", 7) == 0 && (line[7] == '\0' || line[7] == ' ' || line[7] == '\t')) {
        char *uid = NULL, *url = NULL, *unused = NULL;
        if (parse_args(line, 7, &uid, &url, &unused) < 2 || uid[0] == '\0' || url[0] == '\0') {
            cdc_write_str("\r\nunboost: usage: unboost UID URL\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nunboost: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_UNBOOST;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, url, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "unboost", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "ping", 4) == 0 && (line[4] == '\0' || line[4] == ' ' || line[4] == '\t')) {
        char *uid = NULL, *actor = NULL, *unused = NULL;
        if (parse_args(line, 4, &uid, &actor, &unused) < 2 || uid[0] == '\0' || actor[0] == '\0') {
            cdc_write_str("\r\nping: usage: ping UID ACTOR\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nping: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_PING;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, actor, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "ping", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "pin", 3) == 0 && (line[3] == '\0' || line[3] == ' ' || line[3] == '\t')) {
        char *uid = NULL, *url = NULL, *unused = NULL;
        if (parse_args(line, 3, &uid, &url, &unused) < 2 || uid[0] == '\0' || url[0] == '\0') {
            cdc_write_str("\r\npin: usage: pin UID MSG_URL\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\npin: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_PIN;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, url, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "pin", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "unpin", 5) == 0 && (line[5] == '\0' || line[5] == ' ' || line[5] == '\t')) {
        char *uid = NULL, *url = NULL, *unused = NULL;
        if (parse_args(line, 5, &uid, &url, &unused) < 2 || uid[0] == '\0' || url[0] == '\0') {
            cdc_write_str("\r\nunpin: usage: unpin UID MSG_URL\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nunpin: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_UNPIN;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, url, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "unpin", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "bookmark", 8) == 0 && (line[8] == '\0' || line[8] == ' ' || line[8] == '\t')) {
        char *uid = NULL, *url = NULL, *unused = NULL;
        if (parse_args(line, 8, &uid, &url, &unused) < 2 || uid[0] == '\0' || url[0] == '\0') {
            cdc_write_str("\r\nbookmark: usage: bookmark UID URL\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nbookmark: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_BOOKMARK;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, url, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "bookmark", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "unbookmark", 10) == 0 && (line[10] == '\0' || line[10] == ' ' || line[10] == '\t')) {
        char *uid = NULL, *url = NULL, *unused = NULL;
        if (parse_args(line, 10, &uid, &url, &unused) < 2 || uid[0] == '\0' || url[0] == '\0') {
            cdc_write_str("\r\nunbookmark: usage: unbookmark UID URL\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nunbookmark: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_UNBOOKMARK;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, url, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "unbookmark", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "limit", 5) == 0 && (line[5] == '\0' || line[5] == ' ' || line[5] == '\t')) {
        char *uid = NULL, *actor = NULL, *unused = NULL;
        if (parse_args(line, 5, &uid, &actor, &unused) < 2 || uid[0] == '\0' || actor[0] == '\0') {
            cdc_write_str("\r\nlimit: usage: limit UID ACTOR\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nlimit: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_LIMIT;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, actor, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "limit", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "unlimit", 7) == 0 && (line[7] == '\0' || line[7] == ' ' || line[7] == '\t')) {
        char *uid = NULL, *actor = NULL, *unused = NULL;
        if (parse_args(line, 7, &uid, &actor, &unused) < 2 || uid[0] == '\0' || actor[0] == '\0') {
            cdc_write_str("\r\nunlimit: usage: unlimit UID ACTOR\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nunlimit: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_UNLIMIT;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, actor, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "unlimit", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "unmute", 6) == 0 && (line[6] == '\0' || line[6] == ' ' || line[6] == '\t')) {
        char *uid = NULL, *actor = NULL, *unused = NULL;
        if (parse_args(line, 6, &uid, &actor, &unused) < 2 || uid[0] == '\0' || actor[0] == '\0') {
            cdc_write_str("\r\nunmute: usage: unmute UID ACTOR\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nunmute: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_UNMUTE;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, actor, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "unmute", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "alias", 5) == 0 && (line[5] == '\0' || line[5] == ' ' || line[5] == '\t')) {
        char *uid = NULL, *account = NULL, *unused = NULL;
        if (parse_args(line, 5, &uid, &account, &unused) < 2 || uid[0] == '\0' || account[0] == '\0') {
            cdc_write_str("\r\nalias: usage: alias UID ACCOUNT\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nalias: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_ALIAS;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, account, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "alias", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "list_create", 11) == 0 && (line[11] == '\0' || line[11] == ' ' || line[11] == '\t')) {
        char *uid = NULL, *name = NULL, *unused = NULL;
        if (parse_args(line, 11, &uid, &name, &unused) < 2 || uid[0] == '\0' || name[0] == '\0') {
            cdc_write_str("\r\nlist_create: usage: list_create UID NAME\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nlist_create: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_LIST_CREATE;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, name, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "list_create", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "list_remove", 11) == 0 && (line[11] == '\0' || line[11] == ' ' || line[11] == '\t')) {
        char *uid = NULL, *name = NULL, *unused = NULL;
        if (parse_args(line, 11, &uid, &name, &unused) < 2 || uid[0] == '\0' || name[0] == '\0') {
            cdc_write_str("\r\nlist_remove: usage: list_remove UID NAME\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nlist_remove: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_LIST_REMOVE;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, name, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "list_remove", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "list_add", 8) == 0 && (line[8] == '\0' || line[8] == ' ' || line[8] == '\t')) {
        char *uid = NULL, *name = NULL, *account = NULL;
        if (parse_args(line, 8, &uid, &name, &account) < 3 || uid[0] == '\0' || name[0] == '\0' || account[0] == '\0') {
            cdc_write_str("\r\nlist_add: usage: list_add UID NAME ACCOUNT\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nlist_add: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_LIST_ADD;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, name, sizeof(a->arg1) - 1);
        strncpy(a->arg2, account, sizeof(a->arg2) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "list_add", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "list_del", 8) == 0 && (line[8] == '\0' || line[8] == ' ' || line[8] == '\t')) {
        char *uid = NULL, *name = NULL, *actor = NULL;
        if (parse_args(line, 8, &uid, &name, &actor) < 3 || uid[0] == '\0' || name[0] == '\0' || actor[0] == '\0') {
            cdc_write_str("\r\nlist_del: usage: list_del UID NAME ACTOR\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nlist_del: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_LIST_DEL;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, name, sizeof(a->arg1) - 1);
        strncpy(a->arg2, actor, sizeof(a->arg2) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "list_del", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "collect_replies", 15) == 0 && (line[15] == '\0' || line[15] == ' ' || line[15] == '\t')) {
        char *uid = NULL, *url = NULL, *unused = NULL;
        if (parse_args(line, 15, &uid, &url, &unused) < 2 || uid[0] == '\0' || url[0] == '\0') {
            cdc_write_str("\r\ncollect_replies: usage: collect_replies UID URL\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\ncollect_replies: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_COLLECT_REPLIES;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, url, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "collect_replies", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "search", 6) == 0 && (line[6] == '\0' || line[6] == ' ' || line[6] == '\t')) {
        char *uid = line + 6;
        while (*uid == ' ' || *uid == '\t') uid++;
        char *regex = uid;
        while (*regex != '\0' && *regex != ' ' && *regex != '\t') regex++;
        if (*regex != '\0') {
            *regex = '\0';
            regex++;
            while (*regex == ' ' || *regex == '\t') regex++;
        }
        if (uid[0] == '\0' || regex[0] == '\0') {
            cdc_write_str("\r\nsearch: usage: search UID REGEX\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nsearch: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_SEARCH;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, regex, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "search", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "block", 5) == 0 && (line[5] == '\0' || line[5] == ' ' || line[5] == '\t')) {
        char *inst_url = line + 5;
        while (*inst_url == ' ' || *inst_url == '\t') inst_url++;
        if (*inst_url == '\0') {
            cdc_write_str("\r\nblock: missing instance URL\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nblock: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_BLOCK_INSTANCE;
        strncpy(a->uid, inst_url, sizeof(a->uid) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "block", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "unblock", 7) == 0 && (line[7] == '\0' || line[7] == ' ' || line[7] == '\t')) {
        char *inst_url = line + 7;
        while (*inst_url == ' ' || *inst_url == '\t') inst_url++;
        if (*inst_url == '\0') {
            cdc_write_str("\r\nunblock: missing instance URL\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nunblock: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_UNBLOCK_INSTANCE;
        strncpy(a->uid, inst_url, sizeof(a->uid) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "unblock", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "list_members", 12) == 0 && (line[12] == '\0' || line[12] == ' ' || line[12] == '\t')) {
        char *uid = NULL, *name = NULL, *unused = NULL;
        if (parse_args(line, 12, &uid, &name, &unused) < 2 || uid[0] == '\0' || name[0] == '\0') {
            cdc_write_str("\r\nlist_members: usage: list_members UID NAME\r\n");
            return;
        }
        if (s_worker_busy) {
            cdc_write_str("\r\nlist_members: busy\r\n");
            return;
        }
        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_LIST_MEMBERS;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);
        strncpy(a->arg1, name, sizeof(a->arg1) - 1);
        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "list_members", 16384, a, 5, NULL);
        return;
    }

    cdc_write_str("\r\nunknown command (try 'help')\r\n");
}

static void usb_console_task(void *arg)
{
    (void)arg;

    cdc_write_str("\r\nsnac2 USB console ready (type 'help')\r\n");
    prompt();

    char line[256];
    size_t n = 0;

    for (;;) {
        if (tud_cdc_available()) {
            uint8_t ch;
            if (tud_cdc_read(&ch, 1) == 1) {
                if (ch == '\r' || ch == '\n') {
                    line[n] = '\0';
                    handle_line(line);
                    n = 0;
                    prompt();
                }
                else if (ch == 0x08 || ch == 0x7f) {
                    if (n > 0)
                        n--;
                }
                else if (n + 1 < sizeof(line)) {
                    line[n++] = (char)ch;
                }
            }
        }

        /* NOTE: pdMS_TO_TICKS(1) can be 0 depending on tick rate, which would
           starve IDLE and trigger the task watchdog. */
        vTaskDelay(1);
    }
}

void usb_console_init(void)
{
    // Install TinyUSB driver (CDC enabled by default in TinyUSB device descriptors).
    tinyusb_config_t cfg = { 0 };
    cfg.task.size = 16384;
    cfg.task.priority = 5;
    cfg.task.xCoreID = 0;
    esp_err_t err = tinyusb_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %d", (int)err);
        return;
    }

    ESP_LOGI(TAG, "TinyUSB installed");

    xTaskCreate(usb_console_task, "usb_console", 8192, NULL, 5, NULL);
}

#if 1
/* TinyUSB MSC callbacks.
 * These are compiled only when MSC is enabled in the ESP-IDF TinyUSB configuration.
 * They expose a contiguous LBA range (intended for the CONFIG partition).
 */

#include "sdmmc_cmd.h"

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;
    return s_msc_card != NULL && s_msc_lba_count > 0;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void)lun;
    *block_size = 512;
    *block_count = s_msc_lba_count;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    (void)lun;
    memcpy(vendor_id, "SNAC2   ", 8);
    memcpy(product_id, "CONFIG_VOL      ", 16);
    memcpy(product_rev, "0001", 4);
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    (void)lun;
    if (s_msc_card == NULL || offset != 0 || (bufsize % 512) != 0)
        return -1;

    uint32_t count = bufsize / 512;
    if (lba + count > s_msc_lba_count)
        return -1;

    esp_err_t err = sdmmc_read_sectors(s_msc_card, buffer, s_msc_start_lba + lba, count);
    return err == ESP_OK ? (int32_t)bufsize : -1;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    (void)lun;
    if (s_msc_card == NULL || offset != 0 || (bufsize % 512) != 0)
        return -1;

    uint32_t count = bufsize / 512;
    if (lba + count > s_msc_lba_count)
        return -1;

    esp_err_t err = sdmmc_write_sectors(s_msc_card, buffer, s_msc_start_lba + lba, count);
    return err == ESP_OK ? (int32_t)bufsize : -1;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    (void)lun;
    (void)buffer;
    (void)bufsize;

    /* Let TinyUSB handle the default SCSI commands. */
    uint8_t const cmd = scsi_cmd[0];
    ESP_LOGD(TAG, "MSC SCSI cmd 0x%02x", cmd);
    return -1;
}
#endif

