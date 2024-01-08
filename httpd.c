/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_json.h"
#include "xs_socket.h"
#include "xs_httpd.h"
#include "xs_mime.h"
#include "xs_time.h"
#include "xs_openssl.h"
#include "xs_fcgi.h"
#include "xs_html.h"

#include "snac.h"

#include <setjmp.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <stdint.h>

#include <sys/resource.h> // for getrlimit()

#ifdef USE_POLL_FOR_SLEEP
#include <poll.h>
#endif

/** server stat **/

srv_state s_state = {0};
srv_state *p_state = NULL;


/** job control **/

/* mutex to access the lists of jobs */
static pthread_mutex_t job_mutex;

/* semaphore to trigger job processing */
static sem_t *job_sem;

typedef struct job_fifo_item {
    struct job_fifo_item *next;
    xs_val *job;
} job_fifo_item;

static job_fifo_item *job_fifo_first = NULL;
static job_fifo_item *job_fifo_last  = NULL;


/** other global data **/

static jmp_buf on_break;


/** code **/

/* nodeinfo 2.0 template */
const char *nodeinfo_2_0_template = ""
    "{\"version\":\"2.0\","
    "\"software\":{\"name\":\"snac\",\"version\":\"" VERSION "\"},"
    "\"protocols\":[\"activitypub\"],"
    "\"services\":{\"outbound\":[],\"inbound\":[]},"
    "\"usage\":{\"users\":{\"total\":%d,\"activeMonth\":%d,\"activeHalfyear\":%d},"
    "\"localPosts\":%d},"
    "\"openRegistrations\":false,\"metadata\":{}}";

xs_str *nodeinfo_2_0(void)
/* builds a nodeinfo json object */
{
    int n_utotal = 0;
    int n_umonth = 0;
    int n_uhyear = 0;
    int n_posts  = 0;
    xs *users = user_list();
    xs_list *p = users;
    char *v;
    double now = (double)time(NULL);

    while (xs_list_iter(&p, &v)) {
        /* build the full path name to the last usage log */
        xs *llfn = xs_fmt("%s/user/%s/lastlog.txt", srv_basedir, v);
        double llsecs = now - mtime(llfn);

        if (llsecs < 60 * 60 * 24 * 30 * 6) {
            n_uhyear++;

            if (llsecs < 60 * 60 * 24 * 30)
                n_umonth++;
        }

        n_utotal++;

        /* build the file to each user public.idx */
        xs *pidxfn = xs_fmt("%s/user/%s/public.idx", srv_basedir, v);
        n_posts += index_len(pidxfn);
    }

    return xs_fmt(nodeinfo_2_0_template, n_utotal, n_umonth, n_uhyear, n_posts);
}


static xs_str *greeting_html(void)
/* processes and returns greeting.html */
{
    /* try to open greeting.html */
    xs *fn = xs_fmt("%s/greeting.html", srv_basedir);
    FILE *f;
    xs_str *s = NULL;

    if ((f = fopen(fn, "r")) != NULL) {
        s = xs_readall(f);
        fclose(f);

        /* replace %host% */
        s = xs_replace_i(s, "%host%", xs_dict_get(srv_config, "host"));

        const char *adm_email = xs_dict_get(srv_config, "admin_email");
        if (xs_is_null(adm_email) || *adm_email == '\0')
            adm_email = "the administrator of this instance";

        /* replace %admin_email */
        s = xs_replace_i(s, "%admin_email%", adm_email);

        /* does it have a %userlist% mark? */
        if (xs_str_in(s, "%userlist%") != -1) {
            char *host = xs_dict_get(srv_config, "host");
            xs *list = user_list();
            xs_list *p = list;
            xs_str *uid;

            xs_html *ul = xs_html_tag("ul",
                xs_html_attr("class", "snac-user-list"));

            p = list;
            while (xs_list_iter(&p, &uid)) {
                snac user;

                if (user_open(&user, uid)) {
                    xs_html_add(ul,
                        xs_html_tag("li",
                            xs_html_tag("a",
                                xs_html_attr("href", user.actor),
                                xs_html_text("@"),
                                xs_html_text(uid),
                                xs_html_text("@"),
                                xs_html_text(host),
                                xs_html_text(" ("),
                                xs_html_text(xs_dict_get(user.config, "name")),
                                xs_html_text(")"))));

                    user_free(&user);
                }
            }

            xs *s1 = xs_html_render(ul);
            s = xs_replace_i(s, "%userlist%", s1);
        }
    }

    return s;
}


int server_get_handler(xs_dict *req, const char *q_path,
                       char **body, int *b_size, char **ctype)
/* basic server services */
{
    int status = 0;

    (void)req;

    /* is it the server root? */
    if (*q_path == '\0') {
        xs_dict *q_vars = xs_dict_get(req, "q_vars");
        char *t = NULL;

        if (xs_type(q_vars) == XSTYPE_DICT && (t = xs_dict_get(q_vars, "t"))) {
            int skip = 0;
            int show = xs_number_get(xs_dict_get(srv_config, "max_timeline_entries"));
            char *v;

            if ((v = xs_dict_get(q_vars, "skip")) != NULL)
                skip = atoi(v);
            if ((v = xs_dict_get(q_vars, "show")) != NULL)
                show = atoi(v);

            xs *tl = tag_search(t, skip, show + 1);
            int more = 0;
            if (xs_list_len(tl) >= show + 1) {
                /* drop the last one */
                tl = xs_list_del(tl, -1);
                more = 1;
            }

            *body = html_timeline(NULL, tl, 0, skip, show, more, t);
        }
        else
        if (xs_type(xs_dict_get(srv_config, "show_instance_timeline")) == XSTYPE_TRUE) {
            xs *tl = timeline_instance_list(0, 30);
            *body = html_timeline(NULL, tl, 0, 0, 0, 0, NULL);
        }
        else
            *body = greeting_html();

        if (*body)
            status = 200;
    }
    else
    if (strcmp(q_path, "/susie.png") == 0 || strcmp(q_path, "/favicon.ico") == 0 ) {
        status = 200;
        *body  = xs_base64_dec(default_avatar_base64(), b_size);
        *ctype = "image/png";
    }
    else
    if (strcmp(q_path, "/.well-known/nodeinfo") == 0) {
        status = 200;
        *ctype = "application/json; charset=utf-8";
        *body  = xs_fmt("{\"links\":["
            "{\"rel\":\"http:/" "/nodeinfo.diaspora.software/ns/schema/2.0\","
            "\"href\":\"%s/nodeinfo_2_0\"}]}",
            srv_baseurl);
    }
    else
    if (strcmp(q_path, "/nodeinfo_2_0") == 0) {
        status = 200;
        *ctype = "application/json; charset=utf-8";
        *body  = nodeinfo_2_0();
    }
    else
    if (strcmp(q_path, "/robots.txt") == 0) {
        status = 200;
        *ctype = "text/plain";
        *body  = xs_str_new("User-agent: *\n"
                            "Disallow: /\n");
    }
    else
    if (strcmp(q_path, "/status.txt") == 0) {
        status = 200;
        *ctype = "text/plain";
        *body  = xs_str_new("UP\n");

        xs *uptime = xs_str_time_diff(time(NULL) - p_state->srv_start_time);
        srv_log(xs_fmt("status: uptime: %s", uptime));
        srv_log(xs_fmt("status: job_fifo len: %d", p_state->job_fifo_size));
    }

    if (status != 0)
        srv_debug(1, xs_fmt("server_get_handler serving '%s' %d", q_path, status));

    return status;
}


void httpd_connection(FILE *f)
/* the connection processor */
{
    xs *req;
    char *method;
    int status   = 0;
    xs_str *body = NULL;
    int b_size   = 0;
    char *ctype  = NULL;
    xs *headers  = xs_dict_new();
    xs *q_path   = NULL;
    xs *payload  = NULL;
    xs *etag     = NULL;
    int p_size   = 0;
    char *p;
    int fcgi_id;

    if (p_state->use_fcgi)
        req = xs_fcgi_request(f, &payload, &p_size, &fcgi_id);
    else
        req = xs_httpd_request(f, &payload, &p_size);

    if (req == NULL) {
        /* probably because a timeout */
        fclose(f);
        return;
    }

    if (!(method = xs_dict_get(req, "method")) || !(p = xs_dict_get(req, "path"))) {
        /* missing needed headers; discard */
        fclose(f);
        return;
    }

    q_path = xs_dup(p);

    /* crop the q_path from leading / and the prefix */
    if (xs_endswith(q_path, "/"))
        q_path = xs_crop_i(q_path, 0, -1);

    p = xs_dict_get(srv_config, "prefix");
    if (xs_startswith(q_path, p))
        q_path = xs_crop_i(q_path, strlen(p), 0);

    if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) {
        /* cascade through */
        if (status == 0)
            status = server_get_handler(req, q_path, &body, &b_size, &ctype);

        if (status == 0)
            status = webfinger_get_handler(req, q_path, &body, &b_size, &ctype);

        if (status == 0)
            status = activitypub_get_handler(req, q_path, &body, &b_size, &ctype);

#ifndef NO_MASTODON_API
        if (status == 0)
            status = oauth_get_handler(req, q_path, &body, &b_size, &ctype);

        if (status == 0)
            status = mastoapi_get_handler(req, q_path, &body, &b_size, &ctype);
#endif /* NO_MASTODON_API */

        if (status == 0)
            status = html_get_handler(req, q_path, &body, &b_size, &ctype, &etag);
    }
    else
    if (strcmp(method, "POST") == 0) {

#ifndef NO_MASTODON_API
        if (status == 0)
            status = oauth_post_handler(req, q_path,
                        payload, p_size, &body, &b_size, &ctype);

        if (status == 0)
            status = mastoapi_post_handler(req, q_path,
                        payload, p_size, &body, &b_size, &ctype);
#endif

        if (status == 0)
            status = activitypub_post_handler(req, q_path,
                        payload, p_size, &body, &b_size, &ctype);

        if (status == 0)
            status = html_post_handler(req, q_path,
                        payload, p_size, &body, &b_size, &ctype);
    }
    else
    if (strcmp(method, "PUT") == 0) {

#ifndef NO_MASTODON_API
        if (status == 0)
            status = mastoapi_put_handler(req, q_path,
                        payload, p_size, &body, &b_size, &ctype);
#endif

    }
    else
    if (strcmp(method, "OPTIONS") == 0) {
        status = 200;
    }

    /* unattended? it's an error */
    if (status == 0) {
        srv_archive_error("unattended_method", "unattended method", req, payload);
        srv_debug(1, xs_fmt("httpd_connection unattended %s %s", method, q_path));
        status = 404;
    }

    if (status == 403)
        body = xs_str_new("<h1>403 Forbidden</h1>");

    if (status == 404)
        body = xs_str_new("<h1>404 Not Found</h1>");

    if (status == 400 && body != NULL)
        body = xs_str_new("<h1>400 Bad Request</h1>");

    if (status == 303)
        headers = xs_dict_append(headers, "location", body);

    if (status == 401) {
        xs *www_auth = xs_fmt("Basic realm=\"@%s@%s snac login\"",
                                body, xs_dict_get(srv_config, "host"));

        headers = xs_dict_append(headers, "WWW-Authenticate", www_auth);
    }

    if (ctype == NULL)
        ctype = "text/html; charset=utf-8";

    headers = xs_dict_append(headers, "content-type", ctype);
    headers = xs_dict_append(headers, "x-creator",    USER_AGENT);

    if (!xs_is_null(etag))
        headers = xs_dict_append(headers, "etag", etag);

    /* if there are any additional headers, add them */
    xs_dict *more_headers = xs_dict_get(srv_config, "http_headers");
    if (xs_type(more_headers) == XSTYPE_DICT) {
        char *k, *v;
        while (xs_dict_iter(&more_headers, &k, &v))
            headers = xs_dict_set(headers, k, v);
    }

    if (b_size == 0 && body != NULL)
        b_size = strlen(body);

    /* if it was a HEAD, no body will be sent */
    if (strcmp(method, "HEAD") == 0)
        body = xs_free(body);

    headers = xs_dict_append(headers, "access-control-allow-origin", "*");
    headers = xs_dict_append(headers, "access-control-allow-headers", "*");

    if (p_state->use_fcgi)
        xs_fcgi_response(f, status, headers, body, b_size, fcgi_id);
    else
        xs_httpd_response(f, status, headers, body, b_size);

    fclose(f);

    srv_archive("RECV", NULL, req, payload, p_size, status, headers, body, b_size);

    /* JSON validation check */
    if (!xs_is_null(body) && strcmp(ctype, "application/json") == 0) {
        xs *j = xs_json_loads(body);

        if (j == NULL) {
            srv_log(xs_fmt("bad JSON"));
            srv_archive_error("bad_json", "bad JSON", req, body);
        }
    }

    xs_free(body);
}


void job_post(const xs_val *job, int urgent)
/* posts a job for the threads to process it */
{
    if (job != NULL) {
        /* lock the mutex */
        pthread_mutex_lock(&job_mutex);

        job_fifo_item *i = xs_realloc(NULL, sizeof(job_fifo_item));
        *i = (job_fifo_item){ NULL, xs_dup(job) };

        if (job_fifo_first == NULL)
            job_fifo_first = job_fifo_last = i;
        else
        if (urgent) {
            /* prepend */
            i->next = job_fifo_first;
            job_fifo_first = i;
        }
        else {
            /* append */
            job_fifo_last->next = i;
            job_fifo_last = i;
        }

        p_state->job_fifo_size++;

        /* unlock the mutex */
        pthread_mutex_unlock(&job_mutex);

        /* ask for someone to attend it */
        sem_post(job_sem);
    }
}


void job_wait(xs_val **job)
/* waits for an available job */
{
    *job = NULL;

    if (sem_wait(job_sem) == 0) {
        /* lock the mutex */
        pthread_mutex_lock(&job_mutex);

        /* dequeue */
        job_fifo_item *i = job_fifo_first;

        if (i != NULL) {
            job_fifo_first = i->next;

            if (job_fifo_first == NULL)
                job_fifo_last = NULL;

            *job = i->job;
            xs_free(i);

            p_state->job_fifo_size--;
        }

        /* unlock the mutex */
        pthread_mutex_unlock(&job_mutex);
    }
}


static void *job_thread(void *arg)
/* job thread */
{
    int pid = (int)(uintptr_t)arg;

    srv_debug(1, xs_fmt("job thread %d started", pid));

    for (;;) {
        xs *job = NULL;

        job_wait(&job);

        srv_debug(2, xs_fmt("job thread %d wake up", pid));

        if (job == NULL) /* corrupted message? */
            continue;

        if (xs_type(job) == XSTYPE_FALSE) /* special message: exit */
            break;
        else
        if (xs_type(job) == XSTYPE_DATA) {
            /* it's a socket */
            FILE *f = NULL;

            xs_data_get(&f, job);

            if (f != NULL)
                httpd_connection(f);
        }
        else {
            /* it's a q_item */
            process_queue_item(job);
        }
    }

    srv_debug(1, xs_fmt("job thread %d stopped", pid));

    return NULL;
}

/* background thread sleep control */
static pthread_mutex_t sleep_mutex;
static pthread_cond_t  sleep_cond;

static void *background_thread(void *arg)
/* background thread (queue management and other things) */
{
    time_t purge_time;

    (void)arg;

    /* first purge time */
    purge_time = time(NULL) + 10 * 60;

    srv_log(xs_fmt("background thread started"));

    while (p_state->srv_running) {
        time_t t;
        int cnt = 0;

        {
            xs *list = user_list();
            char *p, *uid;

            /* process queues for all users */
            p = list;
            while (xs_list_iter(&p, &uid)) {
                snac snac;

                if (user_open(&snac, uid)) {
                    cnt += process_user_queue(&snac);
                    user_free(&snac);
                }
            }
        }

        /* global queue */
        cnt += process_queue();

        /* time to purge? */
        if ((t = time(NULL)) > purge_time) {
            /* next purge time is tomorrow */
            purge_time = t + 24 * 60 * 60;

            xs *q_item = xs_dict_new();
            q_item = xs_dict_append(q_item, "type", "purge");
            job_post(q_item, 0);
        }

        if (cnt == 0) {
            /* sleep 3 seconds */

#ifdef USE_POLL_FOR_SLEEP
            poll(NULL, 0, 3 * 1000);
#else
            struct timespec ts;

            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 3;

            pthread_mutex_lock(&sleep_mutex);
            while (pthread_cond_timedwait(&sleep_cond, &sleep_mutex, &ts) == 0);
            pthread_mutex_unlock(&sleep_mutex);
#endif
        }
    }

    srv_log(xs_fmt("background thread stopped"));

    return NULL;
}


void term_handler(int s)
{
    (void)s;

    longjmp(on_break, 1);
}


void httpd(void)
/* starts the server */
{
    const char *address;
    const char *port;
    int rs;
    pthread_t threads[MAX_THREADS] = {0};
    int n;
    xs *sem_name = NULL;
    sem_t anon_job_sem;

    /* setup the server stat structure */
    {
        p_state = &s_state;
    }

    p_state->srv_start_time = time(NULL);

    p_state->use_fcgi = xs_type(xs_dict_get(srv_config, "fastcgi")) == XSTYPE_TRUE;

    address = xs_dict_get(srv_config, "address");
    port    = xs_number_str(xs_dict_get(srv_config, "port"));

    if ((rs = xs_socket_server(address, port)) == -1) {
        srv_log(xs_fmt("cannot bind socket to %s:%s", address, port));
        return;
    }

    p_state->srv_running = 1;

    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, term_handler);
    signal(SIGINT,  term_handler);

    srv_log(xs_fmt("httpd%s start %s:%s %s", p_state->use_fcgi ? " (FastCGI)" : "",
                    address, port, USER_AGENT));

    /* show the number of usable file descriptors */
    struct rlimit r;
    getrlimit(RLIMIT_NOFILE, &r);
    srv_debug(0, xs_fmt("available (rlimit) fds: %d (cur) / %d (max)",
                        (int) r.rlim_cur, (int) r.rlim_max));

    /* initialize the job control engine */
    pthread_mutex_init(&job_mutex, NULL);
    sem_name = xs_fmt("/job_%d", getpid());
    job_sem = sem_open(sem_name, O_CREAT, 0644, 0);

    if (job_sem == NULL) {
        /* error opening a named semaphore; try with an anonymous one */
        if (sem_init(&anon_job_sem, 0, 0) != -1)
            job_sem = &anon_job_sem;
    }

    if (job_sem == NULL) {
        srv_log(xs_fmt("fatal error: cannot create semaphore -- cannot continue"));
        return;
    }

    /* initialize sleep control */
    pthread_mutex_init(&sleep_mutex, NULL);
    pthread_cond_init(&sleep_cond, NULL);

    p_state->n_threads = xs_number_get(xs_dict_get(srv_config, "num_threads"));

#ifdef _SC_NPROCESSORS_ONLN
    if (p_state->n_threads == 0) {
        /* get number of CPUs on the machine */
        p_state->n_threads = sysconf(_SC_NPROCESSORS_ONLN);
    }
#endif

    if (p_state->n_threads < 4)
        p_state->n_threads = 4;

    if (p_state->n_threads > MAX_THREADS)
        p_state->n_threads = MAX_THREADS;

    srv_debug(0, xs_fmt("using %d threads", p_state->n_threads));

    /* thread #0 is the background thread */
    pthread_create(&threads[0], NULL, background_thread, NULL);

    /* the rest of threads are for job processing */
    char *ptr = (char *) 0x1;
    for (n = 1; n < p_state->n_threads; n++)
        pthread_create(&threads[n], NULL, job_thread, ptr++);

    if (setjmp(on_break) == 0) {
        for (;;) {
            FILE *f = xs_socket_accept(rs);

            if (f != NULL) {
                xs *job = xs_data_new(&f, sizeof(FILE *));
                job_post(job, 1);
            }
            else
                break;
        }
    }

    p_state->srv_running = 0;

    /* send as many exit jobs as working threads */
    for (n = 1; n < p_state->n_threads; n++)
        job_post(xs_stock_false, 0);

    /* wait for all the threads to exit */
    for (n = 0; n < p_state->n_threads; n++)
        pthread_join(threads[n], NULL);

    sem_close(job_sem);
    sem_unlink(sem_name);

    xs *uptime = xs_str_time_diff(time(NULL) - p_state->srv_start_time);

    srv_log(xs_fmt("httpd%s stop %s:%s (run time: %s)",
                p_state->use_fcgi ? " (FastCGI)" : "",
                address, port, uptime));
}
