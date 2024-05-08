/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#define VERSION "2.52"

#define USER_AGENT "snac/" VERSION

#define WHAT_IS_SNAC_URL "https:/" "/comam.es/what-is-snac"

#define DIR_PERM 00770
#define DIR_PERM_ADD 02770

#define ISO_DATE_SPEC "%Y-%m-%dT%H:%M:%SZ"

#ifndef MAX_THREADS
#define MAX_THREADS 256
#endif

#ifndef MAX_CONVERSATION_LEVELS
#define MAX_CONVERSATION_LEVELS 48
#endif

extern double disk_layout;
extern xs_str *srv_basedir;
extern xs_dict *srv_config;
extern xs_str *srv_baseurl;

extern int dbglevel;

#define L(s) (s)

int mkdirx(const char *pathname);

int valid_status(int status);
xs_str *tid(int offset);
double ftime(void);

void srv_log(xs_str *str);
#define srv_debug(level, str) do { if (dbglevel >= (level)) \
    { srv_log((str)); } } while (0)

typedef struct {
    xs_str *uid;        /* uid */
    xs_str *basedir;    /* user base directory */
    xs_dict *config;    /* user configuration */
    xs_dict *config_o;  /* user configuration admin override */
    xs_dict *key;       /* keypair */
    xs_dict *links;     /* validated links */
    xs_str *actor;      /* actor url */
    xs_str *md5;        /* actor url md5 */
} snac;

typedef struct {
    int s_size;             /* struct size (for double checking) */
    int srv_running;        /* server running on/off */
    int use_fcgi;           /* FastCGI use on/off */
    time_t srv_start_time;  /* start time */
    int job_fifo_size;      /* job fifo size */
    int peak_job_fifo_size; /* maximum job fifo size seen */
    int n_threads;          /* number of configured threads */
    enum { THST_STOP, THST_WAIT, THST_IN, THST_QUEUE } th_state[MAX_THREADS];
} srv_state;

extern srv_state *p_state;

void snac_log(snac *user, xs_str *str);
#define snac_debug(user, level, str) do { if (dbglevel >= (level)) \
    { snac_log((user), (str)); } } while (0)

int srv_open(char *basedir, int auto_upgrade);
void srv_free(void);

int user_open(snac *snac, const char *uid);
void user_free(snac *snac);
xs_list *user_list(void);
int user_open_by_md5(snac *snac, const char *md5);

int validate_uid(const char *uid);

xs_str *hash_password(const char *uid, const char *passwd, const char *nonce);
int check_password(const char *uid, const char *passwd, const char *hash);

void srv_archive(const char *direction, const char *url, xs_dict *req,
                 const char *payload, int p_size,
                 int status, xs_dict *headers,
                 const char *body, int b_size);
void srv_archive_error(const char *prefix, const xs_str *err,
                       const xs_dict *req, const xs_val *data);
void srv_archive_qitem(char *prefix, xs_dict *q_item);

double mtime_nl(const char *fn, int *n_link);
#define mtime(fn) mtime_nl(fn, NULL)
double f_ctime(const char *fn);

int index_add_md5(const char *fn, const char *md5);
int index_add(const char *fn, const char *id);
int index_gc(const char *fn);
int index_first(const char *fn, char *buf, int size);
int index_len(const char *fn);
xs_list *index_list(const char *fn, int max);
xs_list *index_list_desc(const char *fn, int skip, int show);

int object_add(const char *id, const xs_dict *obj);
int object_add_ow(const char *id, const xs_dict *obj);
int object_here_by_md5(const char *id);
int object_here(const char *id);
int object_get_by_md5(const char *md5, xs_dict **obj);
int object_get(const char *id, xs_dict **obj);
int object_del(const char *id);
int object_del_if_unref(const char *id);
double object_ctime_by_md5(const char *md5);
double object_ctime(const char *id);
double object_mtime_by_md5(const char *md5);
double object_mtime(const char *id);
void object_touch(const char *id);

int object_admire(const char *id, const char *actor, int like);
int object_unadmire(const char *id, const char *actor, int like);

int object_likes_len(const char *id);
int object_announces_len(const char *id);

xs_list *object_children(const char *id);
xs_list *object_likes(const char *id);
xs_list *object_announces(const char *id);
int object_parent(const char *id, char *buf, int size);

int object_user_cache_add(snac *snac, const char *id, const char *cachedir);
int object_user_cache_del(snac *snac, const char *id, const char *cachedir);

int follower_add(snac *snac, const char *actor);
int follower_del(snac *snac, const char *actor);
int follower_check(snac *snac, const char *actor);
xs_list *follower_list(snac *snac);

double timeline_mtime(snac *snac);
int timeline_touch(snac *snac);
int timeline_here(snac *snac, const char *md5);
int timeline_get_by_md5(snac *snac, const char *md5, xs_dict **msg);
int timeline_del(snac *snac, char *id);
xs_list *timeline_simple_list(snac *snac, const char *idx_name, int skip, int show);
xs_list *timeline_list(snac *snac, const char *idx_name, int skip, int show);
int timeline_add(snac *snac, const char *id, const xs_dict *o_msg);
int timeline_admire(snac *snac, const char *id, const char *admirer, int like);

xs_list *timeline_top_level(snac *snac, xs_list *list);
xs_list *local_list(snac *snac, int max);
xs_list *timeline_instance_list(int skip, int show);

int following_add(snac *snac, const char *actor, const xs_dict *msg);
int following_del(snac *snac, const char *actor);
int following_check(snac *snac, const char *actor);
int following_get(snac *snac, const char *actor, xs_dict **data);
xs_list *following_list(snac *snac);

void mute(snac *snac, const char *actor);
void unmute(snac *snac, const char *actor);
int is_muted(snac *snac, const char *actor);

int pin(snac *user, const char *id);
int unpin(snac *user, const char *id);
int is_pinned(snac *user, const char *id);
int is_pinned_by_md5(snac *user, const char *md5);
xs_list *pinned_list(snac *user);

int limited(snac *user, const char *id, int cmd);
#define is_limited(user, id) limited((user), (id), 0)
#define limit(user, id) limited((user), (id), 1)
#define unlimit(user, id) limited((user), (id), 2)

void hide(snac *snac, const char *id);
int is_hidden(snac *snac, const char *id);

void tag_index(const char *id, const xs_dict *obj);
xs_list *tag_search(char *tag, int skip, int show);

xs_val *list_maint(snac *user, const char *list, int op);
xs_list *list_timeline(snac *user, const char *list, int skip, int show);
xs_val *list_content(snac *user, const char *list_id, const char *actor_md5, int op);
void list_distribute(snac *user, const char *who, const xs_dict *post);

xs_list *search_by_content(snac *user, const xs_list *timeline,
                            const char *regex, int max_secs, int *timeout);

int actor_add(const char *actor, xs_dict *msg);
int actor_get(const char *actor, xs_dict **data);
int actor_get_refresh(snac *user, const char *actor, xs_dict **data);

int static_get(snac *snac, const char *id, xs_val **data, int *size, const char *inm, xs_str **etag);
void static_put(snac *snac, const char *id, const char *data, int size);
void static_put_meta(snac *snac, const char *id, const char *str);
xs_str *static_get_meta(snac *snac, const char *id);

double history_mtime(snac *snac, const char *id);
void history_add(snac *snac, const char *id, const char *content, int size,
                    xs_str **etag);
int history_get(snac *snac, const char *id, xs_str **content, int *size,
                const char *inm, xs_str **etag);
int history_del(snac *snac, const char *id);
xs_list *history_list(snac *snac);

void lastlog_write(snac *snac, const char *source);

xs_str *notify_check_time(snac *snac, int reset);
void notify_add(snac *snac, const char *type, const char *utype,
                const char *actor, const char *objid);
xs_dict *notify_get(snac *snac, const char *id);
int notify_new_num(snac *snac);
xs_list *notify_list(snac *snac, int skip, int show);
void notify_clear(snac *snac);

void inbox_add(const char *inbox);
void inbox_add_by_actor(const xs_dict *actor);
xs_list *inbox_list(void);

int is_instance_blocked(const char *instance);
int instance_block(const char *instance);
int instance_unblock(const char *instance);

int content_check(const char *file, const xs_dict *msg);

void enqueue_input(snac *snac, const xs_dict *msg, const xs_dict *req, int retries);
void enqueue_shared_input(const xs_dict *msg, const xs_dict *req, int retries);
void enqueue_output_raw(const char *keyid, const char *seckey,
                        xs_dict *msg, xs_str *inbox, int retries, int p_status);
void enqueue_output(snac *snac, xs_dict *msg, xs_str *inbox, int retries, int p_status);
void enqueue_output_by_actor(snac *snac, xs_dict *msg, const xs_str *actor, int retries);
void enqueue_email(xs_str *msg, int retries);
void enqueue_telegram(const xs_str *msg, const char *bot, const char *chat_id);
void enqueue_ntfy(const xs_str *msg, const char *ntfy_server, const char *ntfy_token);
void enqueue_message(snac *snac, const xs_dict *msg);
void enqueue_close_question(snac *user, const char *id, int end_secs);
void enqueue_object_request(snac *user, const char *id, int forward_secs);
void enqueue_verify_links(snac *user);
void enqueue_actor_refresh(snac *user, const char *actor);
void enqueue_request_replies(snac *user, const char *id);
int was_question_voted(snac *user, const char *id);

xs_list *user_queue(snac *snac);
xs_list *queue(void);
xs_dict *queue_get(const char *fn);
xs_dict *dequeue(const char *fn);

void purge(snac *snac);
void purge_all(void);

xs_dict *http_signed_request_raw(const char *keyid, const char *seckey,
                            const char *method, const char *url,
                            xs_dict *headers,
                            const char *body, int b_size,
                            int *status, xs_str **payload, int *p_size,
                            int timeout);
xs_dict *http_signed_request(snac *snac, const char *method, const char *url,
                            xs_dict *headers,
                            const char *body, int b_size,
                            int *status, xs_str **payload, int *p_size,
                            int timeout);
int check_signature(xs_dict *req, xs_str **err);

srv_state *srv_state_op(xs_str **fname, int op);
void httpd(void);

int webfinger_request_signed(snac *snac, const char *qs, char **actor, char **user);
int webfinger_request(const char *qs, char **actor, char **user);
int webfinger_get_handler(xs_dict *req, char *q_path,
                          char **body, int *b_size, char **ctype);

const char *default_avatar_base64(void);

xs_str *process_tags(snac *snac, const char *content, xs_list **tag);

char *get_atto(const xs_dict *msg);
xs_list *get_attachments(const xs_dict *msg);

xs_dict *msg_admiration(snac *snac, char *object, char *type);
xs_dict *msg_repulsion(snac *user, char *id, char *type);
xs_dict *msg_create(snac *snac, const xs_dict *object);
xs_dict *msg_follow(snac *snac, const char *actor);

xs_dict *msg_note(snac *snac, const xs_str *content, const xs_val *rcpts,
                  xs_str *in_reply_to, xs_list *attach, int priv);

xs_dict *msg_undo(snac *snac, char *object);
xs_dict *msg_delete(snac *snac, char *id);
xs_dict *msg_actor(snac *snac);
xs_dict *msg_update(snac *snac, xs_dict *object);
xs_dict *msg_ping(snac *user, const char *rcpt);
xs_dict *msg_pong(snac *user, const char *rcpt, const char *object);
xs_dict *msg_question(snac *user, const char *content, xs_list *attach,
                      const xs_list *opts, int multiple, int end_secs);

int activitypub_request(snac *snac, const char *url, xs_dict **data);
int actor_request(snac *user, const char *actor, xs_dict **data);
void timeline_request_replies(snac *user, const char *id);
int send_to_inbox_raw(const char *keyid, const char *seckey,
                  const xs_str *inbox, const xs_dict *msg,
                  xs_val **payload, int *p_size, int timeout);
int send_to_inbox(snac *snac, const xs_str *inbox, const xs_dict *msg,
                  xs_val **payload, int *p_size, int timeout);
xs_str *get_actor_inbox(const char *actor);
int send_to_actor(snac *snac, const char *actor, const xs_dict *msg,
                  xs_val **payload, int *p_size, int timeout);
int is_msg_public(const xs_dict *msg);
int is_msg_from_private_user(const xs_dict *msg);
int is_msg_for_me(snac *snac, const xs_dict *msg);

int process_user_queue(snac *snac);
void process_queue_item(xs_dict *q_item);
int process_queue(void);

int activitypub_get_handler(const xs_dict *req, const char *q_path,
                            char **body, int *b_size, char **ctype);
int activitypub_post_handler(const xs_dict *req, const char *q_path,
                             char *payload, int p_size,
                             char **body, int *b_size, char **ctype);

xs_dict *emojis(void);
xs_str *not_really_markdown(const char *content, xs_list **attach, xs_list **tag);
xs_str *sanitize(const char *content);
xs_str *encode_html(const char *str);

xs_str *html_timeline(snac *user, const xs_list *list, int read_only,
                      int skip, int show, int show_more,
                      char *title, char *page, int utl);

int html_get_handler(const xs_dict *req, const char *q_path,
                     char **body, int *b_size, char **ctype, xs_str **etag);
int html_post_handler(const xs_dict *req, const char *q_path,
                      char *payload, int p_size,
                      char **body, int *b_size, char **ctype);
xs_str *timeline_to_rss(snac *user, const xs_list *timeline, char *title, char *link, char *desc);

int snac_init(const char *_basedir);
int adduser(const char *uid);
int resetpwd(snac *snac);
int deluser(snac *user);

extern const char *snac_blurb;

void job_post(const xs_val *job, int urgent);
void job_wait(xs_val **job);

int oauth_get_handler(const xs_dict *req, const char *q_path,
                      char **body, int *b_size, char **ctype);
int oauth_post_handler(const xs_dict *req, const char *q_path,
                       const char *payload, int p_size,
                       char **body, int *b_size, char **ctype);
int mastoapi_get_handler(const xs_dict *req, const char *q_path,
                         char **body, int *b_size, char **ctype);
int mastoapi_post_handler(const xs_dict *req, const char *q_path,
                          const char *payload, int p_size,
                          char **body, int *b_size, char **ctype);
int mastoapi_delete_handler(const xs_dict *req, const char *q_path,
                          const char *payload, int p_size,
                          char **body, int *b_size, char **ctype);
int mastoapi_put_handler(const xs_dict *req, const char *q_path,
                          const char *payload, int p_size,
                          char **body, int *b_size, char **ctype);
void mastoapi_purge(void);

void verify_links(snac *user);
