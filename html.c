/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink et al. / MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_json.h"
#include "xs_regex.h"
#include "xs_set.h"
#include "xs_openssl.h"
#include "xs_time.h"
#include "xs_mime.h"
#include "xs_match.h"
#include "xs_html.h"

#include "snac.h"

int login(snac *snac, const xs_dict *headers)
/* tries a login */
{
    int logged_in = 0;
    const char *auth = xs_dict_get(headers, "authorization");

    if (auth && xs_startswith(auth, "Basic ")) {
        int sz;
        xs *s1 = xs_crop_i(xs_dup(auth), 6, 0);
        xs *s2 = xs_base64_dec(s1, &sz);

        xs *l1 = xs_split_n(s2, ":", 1);

        if (xs_list_len(l1) == 2) {
            logged_in = check_password(
                xs_list_get(l1, 0), xs_list_get(l1, 1),
                xs_dict_get(snac->config, "passwd"));
        }
    }

    if (logged_in)
        lastlog_write(snac, "web");

    return logged_in;
}


xs_str *actor_name(xs_dict *actor)
/* gets the actor name */
{
    xs_list *p;
    char *v;
    xs_str *name;

    if (xs_is_null((v = xs_dict_get(actor, "name"))) || *v == '\0') {
        if (xs_is_null(v = xs_dict_get(actor, "preferredUsername")) || *v == '\0') {
            v = "anonymous";
        }
    }

    name = encode_html(v);

    /* replace the :shortnames: */
    if (!xs_is_null(p = xs_dict_get(actor, "tag"))) {
        xs *tag = NULL;
        if (xs_type(p) == XSTYPE_DICT) {
            /* not a list */
            tag = xs_list_new();
            tag = xs_list_append(tag, p);
        } else {
            /* is a list */
            tag = xs_dup(p);
        }

        xs_list *tags = tag;

        /* iterate the tags */
        while (xs_list_iter(&tags, &v)) {
            char *t = xs_dict_get(v, "type");

            if (t && strcmp(t, "Emoji") == 0) {
                char *n = xs_dict_get(v, "name");
                char *i = xs_dict_get(v, "icon");

                if (n && i) {
                    char *u = xs_dict_get(i, "url");
                    xs *img = xs_fmt("<img loading=\"lazy\" src=\"%s\" style=\"height: 1em; vertical-align: middle;\"/>", u);

                    name = xs_replace_i(name, n, img);
                }
            }
        }
    }

    return name;
}


xs_html *html_actor_icon(xs_dict *actor, const char *date,
                        const char *udate, const char *url, int priv)
{
    xs_html *actor_icon = xs_html_tag("p", NULL);

    xs *avatar = NULL;
    char *v;

    xs *name = actor_name(actor);

    /* get the avatar */
    if ((v = xs_dict_get(actor, "icon")) != NULL &&
        (v = xs_dict_get(v, "url")) != NULL) {
        avatar = xs_dup(v);
    }

    if (avatar == NULL)
        avatar = xs_fmt("data:image/png;base64, %s", default_avatar_base64());

    xs_html_add(actor_icon,
        xs_html_sctag("img",
            xs_html_attr("loading", "lazy"),
            xs_html_attr("class",   "snac-avatar"),
            xs_html_attr("src",     avatar),
            xs_html_attr("alt",     "")),
        xs_html_tag("a",
            xs_html_attr("href",    xs_dict_get(actor, "id")),
            xs_html_attr("class",   "p-author h-card snac-author"),
            xs_html_raw(name))); /* name is already html-escaped */


    if (!xs_is_null(url)) {
        xs_html_add(actor_icon,
            xs_html_text(" "),
            xs_html_tag("a",
                xs_html_attr("href", (char *)url),
                xs_html_text("»")));
    }

    if (priv) {
        xs_html_add(actor_icon,
            xs_html_text(" "),
            xs_html_tag("span",
                xs_html_attr("title", "private"),
                xs_html_raw("&#128274;")));
    }

    if (strcmp(xs_dict_get(actor, "type"), "Service") == 0) {
        xs_html_add(actor_icon,
            xs_html_text(" "),
            xs_html_tag("span",
                xs_html_attr("title", "bot"),
                xs_html_raw("&#129302;")));
    }

    if (xs_is_null(date)) {
        xs_html_add(actor_icon,
            xs_html_raw("&nbsp;"));
    }
    else {
        xs *date_label = xs_crop_i(xs_dup(date), 0, 10);
        xs *date_title = xs_dup(date);

        if (!xs_is_null(udate)) {
            xs *sd = xs_crop_i(xs_dup(udate), 0, 10);

            date_label = xs_str_cat(date_label, " / ", sd);

            date_title = xs_str_cat(date_title, " / ", udate);
        }

        xs_html_add(actor_icon,
            xs_html_text(" "),
            xs_html_tag("time",
                xs_html_attr("class", "dt-published snac-pubdate"),
                xs_html_attr("title", date_title),
                xs_html_text(date_label)));
    }

    {
        char *username, *id;

        if (xs_is_null(username = xs_dict_get(actor, "preferredUsername")) || *username == '\0') {
            /* This should never be reached */
            username = "anonymous";
        }

        if (xs_is_null(id = xs_dict_get(actor, "id")) || *id == '\0') {
            /* This should never be reached */
            id = "https://social.example.org/anonymous";
        }

        /* "LIKE AN ANIMAL" */
        xs *domain = xs_split(id, "/");
        xs *user   = xs_fmt("@%s@%s", username, xs_list_get(domain, 2));

        xs_html_add(actor_icon,
            xs_html_sctag("br", NULL),
            xs_html_tag("a",
                xs_html_attr("href",  xs_dict_get(actor, "id")),
                xs_html_attr("class", "p-author-tag h-card snac-author-tag"),
                xs_html_text(user)));
    }

    return actor_icon;
}


xs_html *html_msg_icon(const xs_dict *msg)
{
    char *actor_id;
    xs *actor = NULL;
    xs_html *actor_icon = NULL;

    if ((actor_id = xs_dict_get(msg, "attributedTo")) == NULL)
        actor_id = xs_dict_get(msg, "actor");

    if (actor_id && valid_status(actor_get(actor_id, &actor))) {
        char *date  = NULL;
        char *udate = NULL;
        char *url   = NULL;
        int priv    = 0;
        const char *type = xs_dict_get(msg, "type");

        if (xs_match(type, "Note|Question|Page|Article"))
            url = xs_dict_get(msg, "id");

        priv = !is_msg_public(msg);

        date  = xs_dict_get(msg, "published");
        udate = xs_dict_get(msg, "updated");

        actor_icon = html_actor_icon(actor, date, udate, url, priv);
    }

    return actor_icon;
}


xs_html *html_note(snac *user, char *summary,
                   char *div_id, char *form_id,
                   char *ta_plh, char *ta_content,
                   char *edit_id, char *actor_id,
                   xs_val *cw_yn, char *cw_text,
                   xs_val *mnt_only, char *redir,
                   char *in_reply_to, int poll)
{
    xs *action = xs_fmt("%s/admin/note", user->actor);

    xs_html *form;

    xs_html *note = xs_html_tag("div",
        xs_html_tag("details",
            xs_html_tag("summary",
                xs_html_text(summary)),
                xs_html_tag("p", NULL),
                xs_html_tag("div",
                    xs_html_attr("class", "snac-note"),
                    xs_html_attr("id",    div_id),
                    form = xs_html_tag("form",
                        xs_html_attr("autocomplete", "off"),
                        xs_html_attr("method",       "post"),
                        xs_html_attr("action",       action),
                        xs_html_attr("enctype",      "multipart/form-data"),
                        xs_html_attr("id",           form_id),
                        xs_html_tag("textarea",
                            xs_html_attr("class",    "snac-textarea"),
                            xs_html_attr("name",     "content"),
                            xs_html_attr("rows",     "4"),
                            xs_html_attr("wrap",     "virtual"),
                            xs_html_attr("required", "required"),
                            xs_html_attr("placeholder", ta_plh),
                            xs_html_text(ta_content)),
                        xs_html_tag("p", NULL),
                        xs_html_text(L("Sensitive content: ")),
                        xs_html_sctag("input",
                            xs_html_attr("type", "checkbox"),
                            xs_html_attr("name", "sensitive"),
                            xs_html_attr(xs_type(cw_yn) == XSTYPE_TRUE ? "checked" : "", NULL)),
                        xs_html_sctag("input",
                            xs_html_attr("type", "text"),
                            xs_html_attr("name", "summary"),
                            xs_html_attr("placeholder", L("Sensitive content description")),
                            xs_html_attr("value", xs_is_null(cw_text) ? "" : cw_text))))));

    if (actor_id)
        xs_html_add(form,
            xs_html_sctag("input",
                xs_html_attr("type",  "hidden"),
                xs_html_attr("name",  "to"),
                xs_html_attr("value", actor_id)));
    else {
        /* no actor_id; ask for mentioned_only */
        xs_html_add(form,
            xs_html_tag("p", NULL),
            xs_html_text(L("Only for mentioned people: ")),
            xs_html_sctag("input",
                xs_html_attr("type",  "checkbox"),
                xs_html_attr("name",  "mentioned_only"),
                xs_html_attr(xs_type(mnt_only) == XSTYPE_TRUE ? "checked" : "", NULL)));
    }

    if (redir)
        xs_html_add(form,
            xs_html_sctag("input",
                xs_html_attr("type",  "hidden"),
                xs_html_attr("name",  "redir"),
                xs_html_attr("value", redir)));

    if (in_reply_to)
        xs_html_add(form,
            xs_html_sctag("input",
                xs_html_attr("type",  "hidden"),
                xs_html_attr("name",  "in_reply_to"),
                xs_html_attr("value", in_reply_to)));

    if (edit_id)
        xs_html_add(form,
            xs_html_sctag("input",
                xs_html_attr("type",  "hidden"),
                xs_html_attr("name",  "edit_id"),
                xs_html_attr("value", edit_id)));

    xs_html_add(form,
        xs_html_tag("p", NULL),
        xs_html_tag("details",
            xs_html_tag("summary",
                xs_html_text(L("Attachment..."))),
                xs_html_tag("p", NULL),
                xs_html_sctag("input",
                    xs_html_attr("type",    "file"),
                    xs_html_attr("name",    "attach")),
                xs_html_sctag("input",
                    xs_html_attr("type",    "text"),
                    xs_html_attr("name",    "alt_text"),
                    xs_html_attr("placeholder", L("Attachment description")))));

    /* add poll controls */
    if (poll) {
        xs_html_add(form,
            xs_html_tag("p", NULL),
            xs_html_tag("details",
                xs_html_tag("summary",
                    xs_html_text(L("Poll..."))),
                xs_html_tag("p",
                    xs_html_text(L("Poll options (one per line, up to 8):")),
                    xs_html_sctag("br", NULL),
                    xs_html_tag("textarea",
                        xs_html_attr("class",    "snac-textarea"),
                        xs_html_attr("name",     "poll_options"),
                        xs_html_attr("rows",     "4"),
                        xs_html_attr("wrap",     "virtual"),
                        xs_html_attr("placeholder", "Option 1...\nOption 2...\nOption 3...\n..."))),
                xs_html_tag("select",
                    xs_html_attr("name",    "poll_multiple"),
                    xs_html_tag("option",
                        xs_html_attr("value", "off"),
                        xs_html_text(L("One choice"))),
                    xs_html_tag("option",
                        xs_html_attr("value", "on"),
                        xs_html_text(L("Multiple choices")))),
                xs_html_text(" "),
                xs_html_tag("select",
                    xs_html_attr("name",    "poll_end_secs"),
                    xs_html_tag("option",
                        xs_html_attr("value", "300"),
                        xs_html_text(L("End in 5 minutes"))),
                    xs_html_tag("option",
                        xs_html_attr("value", "3600"),
                        xs_html_attr("selected", NULL),
                        xs_html_text(L("End in 1 hour"))),
                    xs_html_tag("option",
                        xs_html_attr("value", "86400"),
                        xs_html_text(L("End in 1 day"))))));
    }

    xs_html_add(form,
        xs_html_tag("p", NULL),
        xs_html_sctag("input",
            xs_html_attr("type",     "submit"),
            xs_html_attr("class",    "button"),
            xs_html_attr("value",    L("Post"))),
        xs_html_tag("p", NULL));

    return note;
}


static xs_html *html_base_head(void)
{
    xs_html *head = xs_html_tag("head",
        xs_html_sctag("meta",
            xs_html_attr("name", "viewport"),
            xs_html_attr("content", "width=device-width, initial-scale=1")),
        xs_html_sctag("meta",
            xs_html_attr("name", "generator"),
            xs_html_attr("content", USER_AGENT)));

    /* add server CSS */
    xs_list *p = xs_dict_get(srv_config, "cssurls");
    char *v;
    while (xs_list_iter(&p, &v)) {
        xs_html_add(head,
            xs_html_sctag("link",
                xs_html_attr("rel",  "stylesheet"),
                xs_html_attr("type", "text/css"),
                xs_html_attr("href", v)));
    }

    return head;
}


xs_html *html_instance_head(void)
{
    xs_html *head = html_base_head();

    {
        FILE *f;
        xs *g_css_fn = xs_fmt("%s/style.css", srv_basedir);

        if ((f = fopen(g_css_fn, "r")) != NULL) {
            xs *css = xs_readall(f);
            fclose(f);

            xs_html_add(head,
                xs_html_tag("style",
                    xs_html_text(css)));
        }
    }

    char *host  = xs_dict_get(srv_config, "host");
    char *title = xs_dict_get(srv_config, "title");

    xs_html_add(head,
        xs_html_tag("title",
            xs_html_text(title && *title ? title : host)));

    return head;
}


static xs_html *html_instance_body(char *tag)
{
    char *host  = xs_dict_get(srv_config, "host");
    char *sdesc = xs_dict_get(srv_config, "short_description");
    char *email = xs_dict_get(srv_config, "admin_email");
    char *acct  = xs_dict_get(srv_config, "admin_account");

    xs *blurb = xs_replace(snac_blurb, "%host%", host);

    xs_html *dl;

    xs_html *body = xs_html_tag("body",
        xs_html_tag("div",
            xs_html_attr("class", "snac-instance-blurb"),
            xs_html_raw(blurb), /* pure html */
            dl = xs_html_tag("dl", NULL)));

    if (sdesc && *sdesc) {
        xs_html_add(dl,
            xs_html_tag("di",
                xs_html_tag("dt",
                    xs_html_text(L("Site description"))),
                xs_html_tag("dd",
                    xs_html_text(sdesc))));
    }
    if (email && *email) {
        xs *mailto = xs_fmt("mailto:%s", email);

        xs_html_add(dl,
            xs_html_tag("di",
                xs_html_tag("dt",
                    xs_html_text(L("Admin email"))),
                xs_html_tag("dd",
                    xs_html_tag("a",
                        xs_html_attr("href", mailto),
                        xs_html_text(email)))));
    }
    if (acct && *acct) {
        xs *url    = xs_fmt("%s/%s", srv_baseurl, acct);
        xs *handle = xs_fmt("@%s@%s", acct, host);

        xs_html_add(dl,
            xs_html_tag("di",
                xs_html_tag("dt",
                    xs_html_text(L("Admin account"))),
                xs_html_tag("dd",
                    xs_html_tag("a",
                        xs_html_attr("href", url),
                        xs_html_text(handle)))));
    }

    {
        xs *l = tag ? xs_fmt(L("Search results for #%s"), tag) :
            xs_dup(L("Recent posts by users in this instance"));

        xs_html_add(body,
            xs_html_tag("h2",
                xs_html_attr("class", "snac-header"),
                xs_html_text(l)));
    }

    return body;
}


static xs_str *html_instance_header(xs_str *s, char *tag)
/* TO BE REPLACED BY html_instance_body() */
{
    xs_html *head = html_instance_head();

    char *host  = xs_dict_get(srv_config, "host");
    char *sdesc = xs_dict_get(srv_config, "short_description");
    char *email = xs_dict_get(srv_config, "admin_email");
    char *acct  = xs_dict_get(srv_config, "admin_account");

    {
        xs *s1 = xs_html_render(head);
        s = xs_str_cat(s, "<!DOCTYPE html><html>\n", s1);
    }

    s = xs_str_cat(s, "<body>\n");

    s = xs_str_cat(s, "<div class=\"snac-instance-blurb\">\n");

    {
        xs *s1 = xs_replace(snac_blurb, "%host%", host);
        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "<dl>\n");

    if (sdesc && *sdesc) {
        xs *s1 = xs_fmt("<di><dt>%s</dt><dd>%s</dd></di>\n", L("Site description"), sdesc);
        s = xs_str_cat(s, s1);
    }
    if (email && *email) {
        xs *s1 = xs_fmt("<di><dt>%s</dt><dd>"
                "<a href=\"mailto:%s\">%s</a></dd></di>\n",
                L("Admin email"), email, email);

        s = xs_str_cat(s, s1);
    }
    if (acct && *acct) {
        xs *s1 = xs_fmt("<di><dt>%s</dt><dd>"
                "<a href=\"%s/%s\">@%s@%s</a></dd></di>\n",
                L("Admin account"), srv_baseurl, acct, acct, host);

        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "</dl>\n");

    s = xs_str_cat(s, "</div>\n");

    {
        xs *l = tag ? xs_fmt(L("Search results for #%s"), tag) :
            xs_dup(L("Recent posts by users in this instance"));

        xs *s1 = xs_fmt("<h2 class=\"snac-header\">%s</h2>\n", l);
        s = xs_str_cat(s, s1);
    }

    return s;
}


xs_html *html_user_head(snac *user)
{
    xs_html *head = html_base_head();

    /* add the user CSS */
    {
        xs *css = NULL;
        int size;

        /* try to open the user css */
        if (!valid_status(static_get(user, "style.css", &css, &size, NULL, NULL))) {
            /* it's not there; try to open the server-wide css */
            FILE *f;
            xs *g_css_fn = xs_fmt("%s/style.css", srv_basedir);

            if ((f = fopen(g_css_fn, "r")) != NULL) {
                css = xs_readall(f);
                fclose(f);
            }
        }

        if (css != NULL) {
            xs_html_add(head,
                xs_html_tag("style",
                    xs_html_text(css)));
        }
    }

    /* title */
    xs *title = xs_fmt("%s (@%s@%s)", xs_dict_get(user->config, "name"),
        user->uid, xs_dict_get(srv_config, "host"));

    xs_html_add(head,
        xs_html_tag("title",
            xs_html_text(title)));

    xs *avatar = xs_dup(xs_dict_get(user->config, "avatar"));

    if (avatar == NULL || *avatar == '\0') {
        xs_free(avatar);
        avatar = xs_fmt("data:image/png;base64, %s", default_avatar_base64());
    }

    {
        xs *s_bio = xs_dup(xs_dict_get(user->config, "bio"));
        int n;

        /* shorten the bio */
        for (n = 0; s_bio[n] && s_bio[n] != '&' && s_bio[n] != '.' &&
                    s_bio[n] != '\r' && s_bio[n] != '\n' && n < 128; n++);
        s_bio[n] = '\0';

        xs *s_avatar = xs_dup(avatar);

        /* don't inline an empty avatar: create a real link */
        if (xs_startswith(s_avatar, "data:")) {
            xs_free(s_avatar);
            s_avatar = xs_fmt("%s/susie.png", srv_baseurl);
        }

        /* og properties */
        xs_html_add(head,
            xs_html_sctag("meta",
                xs_html_attr("property", "og:site_name"),
                xs_html_attr("content", xs_dict_get(srv_config, "host"))),
            xs_html_sctag("meta",
                xs_html_attr("property", "og:title"),
                xs_html_attr("content", title)),
            xs_html_sctag("meta",
                xs_html_attr("property", "og:description"),
                xs_html_attr("content", s_bio)),
            xs_html_sctag("meta",
                xs_html_attr("property", "og:image"),
                xs_html_attr("content", s_avatar)),
            xs_html_sctag("meta",
                xs_html_attr("property", "og:width"),
                xs_html_attr("content", "300")),
            xs_html_sctag("meta",
                xs_html_attr("property", "og:height"),
                xs_html_attr("content", "300")));
    }

    /* RSS link */
    xs *rss_url = xs_fmt("%s.rss", user->actor);
    xs_html_add(head,
        xs_html_sctag("link",
            xs_html_attr("rel", "alternate"),
            xs_html_attr("type", "application/rss+xml"),
            xs_html_attr("title", "RSS"),
            xs_html_attr("href", rss_url)));

    return head;
}


static xs_html *html_user_body(snac *user, int local)
{
    xs_html *body = xs_html_tag("body", NULL);

    /* top nav */
    xs_html *top_nav = xs_html_tag("nav",
        xs_html_attr("class", "snac-top-nav"));

    xs *avatar = xs_dup(xs_dict_get(user->config, "avatar"));

    if (avatar == NULL || *avatar == '\0') {
        xs_free(avatar);
        avatar = xs_fmt("data:image/png;base64, %s", default_avatar_base64());
    }

    xs_html_add(top_nav,
        xs_html_sctag("img",
            xs_html_attr("src", avatar),
            xs_html_attr("class", "snac-avatar"),
            xs_html_attr("alt", "")));

    if (local) {
        xs *rss_url = xs_fmt("%s.rss", user->actor);
        xs *admin_url = xs_fmt("%s/admin", user->actor);

        xs_html_add(top_nav,
            xs_html_tag("a",
                xs_html_attr("href", rss_url),
                xs_html_text(L("RSS"))),
            xs_html_text(" - "),
            xs_html_tag("a",
                xs_html_attr("href", admin_url),
                xs_html_attr("rel", "nofollow"),
                xs_html_text(L("private"))));
    }
    else {
        xs *n_list = notify_list(user, 1);
        int n_len  = xs_list_len(n_list);
        xs *n_str  = NULL;
        xs_html *notify_count = NULL;

        /* show the number of new notifications, if there are any */
        if (n_len) {
            xs *n_len_str = xs_fmt(" %d ", n_len);
            notify_count = xs_html_tag("sup",
                xs_html_attr("style", "background-color: red; color: white;"),
                xs_html_text(n_len_str));
        }
        else
            notify_count = xs_html_text("");

        xs *admin_url = xs_fmt("%s/admin", user->actor);
        xs *notify_url = xs_fmt("%s/notifications", user->actor);
        xs *people_url = xs_fmt("%s/people", user->actor);
        xs_html_add(top_nav,
            xs_html_tag("a",
                xs_html_attr("href", user->actor),
                xs_html_text(L("public"))),
            xs_html_text(" - "),
            xs_html_tag("a",
                xs_html_attr("href", admin_url),
                xs_html_text(L("private"))),
            xs_html_text(" - "),
            xs_html_tag("a",
                xs_html_attr("href", notify_url),
                xs_html_text(L("notifications"))),
            notify_count,
            xs_html_text(" - "),
            xs_html_tag("a",
                xs_html_attr("href", people_url),
                xs_html_text(L("people"))));
    }

    xs_html_add(body,
        top_nav);

    /* user info */
    xs_html *top_user = xs_html_tag("div",
        xs_html_attr("class", "h-card snac-top-user"));

    if (local) {
        char *header = xs_dict_get(user->config, "header");
        if (header && *header) {
            xs_html_add(top_user,
                xs_html_tag("div",
                    xs_html_attr("class", "snac-top-user-banner"),
                    xs_html_attr("style", "clear: both"),
                    xs_html_sctag("br", NULL),
                    xs_html_sctag("img",
                        xs_html_attr("src", header))));
        }
    }

    xs *handle = xs_fmt("@%s@%s",
        xs_dict_get(user->config, "uid"),
        xs_dict_get(srv_config, "host"));

    xs_html_add(top_user,
        xs_html_tag("p",
            xs_html_attr("class", "p-name snac-top-user-name"),
            xs_html_text(xs_dict_get(user->config, "name"))),
        xs_html_tag("p",
            xs_html_attr("class", "snac-top-user-id"),
            xs_html_text(handle)));

    if (local) {
        xs *es1  = encode_html(xs_dict_get(user->config, "bio"));
        xs *bio1 = not_really_markdown(es1, NULL);
        xs *tags = xs_list_new();
        xs *bio2 = process_tags(user, bio1, &tags);

        xs_html *top_user_bio = xs_html_tag("div",
            xs_html_attr("class", "p-note snac-top-user-bio"),
            xs_html_raw(bio2)); /* already sanitized */

        xs_html_add(top_user,
            top_user_bio);

        xs_dict *metadata = xs_dict_get(user->config, "metadata");
        if (xs_type(metadata) == XSTYPE_DICT) {
            xs_str *k;
            xs_str *v;

            xs_html *snac_metadata = xs_html_tag("div",
                xs_html_attr("class", "snac-metadata"));

            while (xs_dict_iter(&metadata, &k, &v)) {
                xs_html *value;

                if (xs_startswith(v, "https:/" "/"))
                    value = xs_html_tag("a",
                        xs_html_attr("href", v),
                        xs_html_text(v));
                else
                    value = xs_html_text(v);

                xs_html_add(snac_metadata,
                    xs_html_tag("span",
                        xs_html_attr("class", "snac-property-name"),
                        xs_html_text(k)),
                    xs_html_text(":"),
                    xs_html_sctag("br", NULL),
                    xs_html_tag("span",
                        xs_html_attr("class", "snac-property-value"),
                        value),
                    xs_html_sctag("br", NULL));
            }

            xs_html_add(top_user,
                snac_metadata);
        }
    }

    xs_html_add(body,
        top_user);

    return body;
}


static xs_str *html_user_header(snac *snac, xs_str *s, int local)
/* TO BE REPLACED BY html_user_body() */
{
    xs_html *head = html_user_head(snac);

    {
        xs *s1 = xs_html_render(head);
        s = xs_str_cat(s, "<!DOCTYPE html><html>\n", s1);
    }

    s = xs_str_cat(s, "\n<body>\n");

    /* top nav */
    xs_html *top_nav = xs_html_tag("nav",
        xs_html_attr("class", "snac-top-nav"));

    xs *avatar = xs_dup(xs_dict_get(snac->config, "avatar"));

    if (avatar == NULL || *avatar == '\0') {
        xs_free(avatar);
        avatar = xs_fmt("data:image/png;base64, %s", default_avatar_base64());
    }

    xs_html_add(top_nav,
        xs_html_sctag("img",
            xs_html_attr("src", avatar),
            xs_html_attr("class", "snac-avatar"),
            xs_html_attr("alt", "")));

    {
        if (local) {
            xs *rss_url = xs_fmt("%s.rss", snac->actor);
            xs *admin_url = xs_fmt("%s/admin", snac->actor);

            xs_html_add(top_nav,
                xs_html_tag("a",
                    xs_html_attr("href", rss_url),
                    xs_html_text(L("RSS"))),
                xs_html_text(" - "),
                xs_html_tag("a",
                    xs_html_attr("href", admin_url),
                    xs_html_attr("rel", "nofollow"),
                    xs_html_text(L("private"))));
        }
        else {
            xs *n_list = notify_list(snac, 1);
            int n_len  = xs_list_len(n_list);
            xs *n_str  = NULL;
            xs_html *notify_count = NULL;

            /* show the number of new notifications, if there are any */
            if (n_len) {
                xs *n_len_str = xs_fmt(" %d ", n_len);
                notify_count = xs_html_tag("sup",
                    xs_html_attr("style", "background-color: red; color: white;"),
                    xs_html_text(n_len_str));
            }
            else
                notify_count = xs_html_text("");

            xs *admin_url = xs_fmt("%s/admin", snac->actor);
            xs *notify_url = xs_fmt("%s/notifications", snac->actor);
            xs *people_url = xs_fmt("%s/people", snac->actor);
            xs_html_add(top_nav,
                xs_html_tag("a",
                    xs_html_attr("href", snac->actor),
                    xs_html_text(L("public"))),
                xs_html_text(" - "),
                xs_html_tag("a",
                    xs_html_attr("href", admin_url),
                    xs_html_text(L("private"))),
                xs_html_text(" - "),
                xs_html_tag("a",
                    xs_html_attr("href", notify_url),
                    xs_html_text(L("notifications"))),
                notify_count,
                xs_html_text(" - "),
                xs_html_tag("a",
                    xs_html_attr("href", people_url),
                    xs_html_text(L("people"))));
        }
    }

    {
        xs *s1 = xs_html_render(top_nav);
        s = xs_str_cat(s, s1);
    }

    /* user info */
    {
        xs_html *top_user = xs_html_tag("div",
            xs_html_attr("class", "h-card snac-top-user"));

        if (local) {
            char *header = xs_dict_get(snac->config, "header");
            if (header && *header) {
                xs_html_add(top_user,
                    xs_html_tag("div",
                        xs_html_attr("class", "snac-top-user-banner"),
                        xs_html_attr("style", "clear: both"),
                        xs_html_sctag("br", NULL),
                        xs_html_sctag("img",
                            xs_html_attr("src", header))));
            }
        }

        xs *handle = xs_fmt("@%s@%s",
            xs_dict_get(snac->config, "uid"),
            xs_dict_get(srv_config, "host"));

        xs_html_add(top_user,
            xs_html_tag("p",
                xs_html_attr("class", "p-name snac-top-user-name"),
                xs_html_text(xs_dict_get(snac->config, "name"))),
            xs_html_tag("p",
                xs_html_attr("class", "snac-top-user-id"),
                xs_html_text(handle)));

        if (local) {
            xs *es1  = encode_html(xs_dict_get(snac->config, "bio"));
            xs *bio1 = not_really_markdown(es1, NULL);
            xs *tags = xs_list_new();
            xs *bio2 = process_tags(snac, bio1, &tags);

            xs_html *top_user_bio = xs_html_tag("div",
                xs_html_attr("class", "p-note snac-top-user-bio"),
                xs_html_raw(bio2)); /* already sanitized */

            xs_html_add(top_user,
                top_user_bio);

            xs_dict *metadata = xs_dict_get(snac->config, "metadata");
            if (xs_type(metadata) == XSTYPE_DICT) {
                xs_str *k;
                xs_str *v;

                xs_html *snac_metadata = xs_html_tag("div",
                    xs_html_attr("class", "snac-metadata"));

                while (xs_dict_iter(&metadata, &k, &v)) {
                    xs_html *value;

                    if (xs_startswith(v, "https:/" "/"))
                        value = xs_html_tag("a",
                            xs_html_attr("href", v),
                            xs_html_text(v));
                    else
                        value = xs_html_text(v);

                    xs_html_add(snac_metadata,
                        xs_html_tag("span",
                            xs_html_attr("class", "snac-property-name"),
                            xs_html_text(k)),
                        xs_html_text(":"),
                        xs_html_sctag("br", NULL),
                        xs_html_tag("span",
                            xs_html_attr("class", "snac-property-value"),
                            value),
                        xs_html_sctag("br", NULL));
                }

                xs_html_add(top_user,
                    snac_metadata);
            }
        }

        {
            xs *s1 = xs_html_render(top_user);
            s = xs_str_cat(s, s1);
        }
    }

    return s;
}


xs_html *html_top_controls(snac *snac)
/* generates the top controls */
{
    xs *ops_action = xs_fmt("%s/admin/action", snac->actor);

    xs_html *top_controls = xs_html_tag("div",
        xs_html_attr("class", "snac-top-controls"),

        /** new post **/
        html_note(snac, L("New Post..."),
            "new_post_div", "new_post_form",
            L("What's on your mind?"), "",
            NULL, NULL,
            xs_stock_false, "",
            xs_stock_false, NULL,
            NULL, 1),

        /** operations **/
        xs_html_tag("details",
            xs_html_tag("summary",
                xs_html_text(L("Operations..."))),
            xs_html_tag("p", NULL),
            xs_html_tag("form",
                xs_html_attr("autocomplete", "off"),
                xs_html_attr("method",       "post"),
                xs_html_attr("action",       ops_action),
                xs_html_sctag("input",
                    xs_html_attr("type",     "text"),
                    xs_html_attr("name",     "actor"),
                    xs_html_attr("required", "required"),
                    xs_html_attr("placeholder", "bob@example.com")),
                xs_html_sctag("input",
                    xs_html_attr("type",    "submit"),
                    xs_html_attr("name",    "action"),
                    xs_html_attr("value",   L("Follow"))),
            xs_html_text(L("(by URL or user@host)"))),
            xs_html_tag("p", NULL),
            xs_html_tag("form",
                xs_html_attr("autocomplete", "off"),
                xs_html_attr("method",       "post"),
                xs_html_attr("action",       ops_action),
                xs_html_sctag("input",
                    xs_html_attr("type",     "text"),
                    xs_html_attr("name",     "id"),
                    xs_html_attr("required", "required"),
                    xs_html_attr("placeholder", "https:/" "/fedi.example.com/bob/...")),
                xs_html_sctag("input",
                    xs_html_attr("type",    "submit"),
                    xs_html_attr("name",    "action"),
                    xs_html_attr("value",   L("Boost"))),
                xs_html_text(L("(by URL)"))),
            xs_html_tag("p", NULL)));

    /** user settings **/

    char *email = "[disabled by admin]";

    if (xs_type(xs_dict_get(srv_config, "disable_email_notifications")) != XSTYPE_TRUE) {
        email = xs_dict_get(snac->config_o, "email");
        if (xs_is_null(email)) {
            email = xs_dict_get(snac->config, "email");

            if (xs_is_null(email))
                email = "";
        }
    }

    char *cw = xs_dict_get(snac->config, "cw");
    if (xs_is_null(cw))
        cw = "";

    char *telegram_bot = xs_dict_get(snac->config, "telegram_bot");
    if (xs_is_null(telegram_bot))
        telegram_bot = "";

    char *telegram_chat_id = xs_dict_get(snac->config, "telegram_chat_id");
    if (xs_is_null(telegram_chat_id))
        telegram_chat_id = "";

    char *purge_days = xs_dict_get(snac->config, "purge_days");
    if (!xs_is_null(purge_days) && xs_type(purge_days) == XSTYPE_NUMBER)
        purge_days = (char *)xs_number_str(purge_days);
    else
        purge_days = "0";

    xs_val *d_dm_f_u  = xs_dict_get(snac->config, "drop_dm_from_unknown");
    xs_val *bot       = xs_dict_get(snac->config, "bot");
    xs_val *a_private = xs_dict_get(snac->config, "private");

    xs *metadata = xs_str_new(NULL);
    xs_dict *md = xs_dict_get(snac->config, "metadata");
    xs_str *k;
    xs_str *v;

    while (xs_dict_iter(&md, &k, &v)) {
        xs *kp = xs_fmt("%s=%s", k, v);

        if (*metadata)
            metadata = xs_str_cat(metadata, "\n");
        metadata = xs_str_cat(metadata, kp);
    }

    xs *user_setup_action = xs_fmt("%s/admin/user-setup", snac->actor);

    xs_html_add(top_controls,
        xs_html_tag("details",
        xs_html_tag("summary",
            xs_html_text(L("User Settings..."))),
        xs_html_tag("div",
            xs_html_attr("class", "snac-user-setup"),
            xs_html_tag("form",
                xs_html_attr("autocomplete", "off"),
                xs_html_attr("method",       "post"),
                xs_html_attr("action",       user_setup_action),
                xs_html_attr("enctype",      "multipart/form-data"),
                xs_html_tag("p",
                    xs_html_text(L("Display name:")),
                    xs_html_sctag("br", NULL),
                    xs_html_sctag("input",
                        xs_html_attr("type", "text"),
                        xs_html_attr("name", "name"),
                        xs_html_attr("value", xs_dict_get(snac->config, "name")),
                        xs_html_attr("placeholder", L("Your name")))),
                xs_html_tag("p",
                    xs_html_text(L("Avatar: ")),
                    xs_html_sctag("input",
                        xs_html_attr("type", "file"),
                        xs_html_attr("name", "avatar_file"))),
                xs_html_tag("p",
                    xs_html_text(L("Header image (banner): ")),
                    xs_html_sctag("input",
                        xs_html_attr("type", "file"),
                        xs_html_attr("name", "header_file"))),
                xs_html_tag("p",
                    xs_html_text(L("Bio:")),
                    xs_html_sctag("br", NULL),
                    xs_html_tag("textarea",
                        xs_html_attr("name", "bio"),
                        xs_html_attr("cols", "40"),
                        xs_html_attr("rows", "4"),
                        xs_html_attr("placeholder", L("Write about yourself here...")),
                        xs_html_text(xs_dict_get(snac->config, "bio")))),
                xs_html_sctag("input",
                    xs_html_attr("type", "checkbox"),
                    xs_html_attr("name", "cw"),
                    xs_html_attr("id",   "cw"),
                    xs_html_attr(strcmp(cw, "open") == 0 ? "checked" : "", NULL)),
                xs_html_tag("label",
                    xs_html_attr("for", "cw"),
                    xs_html_text(L("Always show sensitive content"))),
                xs_html_tag("p",
                    xs_html_text(L("Email address for notifications:")),
                    xs_html_sctag("br", NULL),
                    xs_html_sctag("input",
                        xs_html_attr("type", "text"),
                        xs_html_attr("name", "email"),
                        xs_html_attr("value", email),
                        xs_html_attr("placeholder", "bob@example.com"))),
                xs_html_tag("p",
                    xs_html_text(L("Telegram notifications (bot key and chat id):")),
                    xs_html_sctag("br", NULL),
                    xs_html_sctag("input",
                        xs_html_attr("type", "text"),
                        xs_html_attr("name", "telegram_bot"),
                        xs_html_attr("value", telegram_bot),
                        xs_html_attr("placeholder", "Bot API key")),
                    xs_html_text(" "),
                    xs_html_sctag("input",
                        xs_html_attr("type", "text"),
                        xs_html_attr("name", "telegram_chat_id"),
                        xs_html_attr("value", telegram_chat_id),
                        xs_html_attr("placeholder", "Chat id"))),
                xs_html_tag("p",
                    xs_html_text(L("Maximum days to keep posts (0: server settings):")),
                    xs_html_sctag("br", NULL),
                    xs_html_sctag("input",
                        xs_html_attr("type", "number"),
                        xs_html_attr("name", "purge_days"),
                        xs_html_attr("value", purge_days))),
                xs_html_tag("p",
                    xs_html_sctag("input",
                        xs_html_attr("type", "checkbox"),
                        xs_html_attr("name", "drop_dm_from_unknown"),
                        xs_html_attr("id", "drop_dm_from_unknown"),
                        xs_html_attr(xs_type(d_dm_f_u) == XSTYPE_TRUE ? "checked" : "", NULL)),
                    xs_html_tag("label",
                        xs_html_attr("for", "drop_dm_from_unknown"),
                        xs_html_text(L("Drop direct messages from people you don't follow")))),
                xs_html_tag("p",
                    xs_html_sctag("input",
                        xs_html_attr("type", "checkbox"),
                        xs_html_attr("name", "bot"),
                        xs_html_attr("id", "bot"),
                        xs_html_attr(xs_type(bot) == XSTYPE_TRUE ? "checked" : "", NULL)),
                    xs_html_tag("label",
                        xs_html_attr("for", "bot"),
                        xs_html_text(L("This account is a bot")))),
                xs_html_tag("p",
                    xs_html_sctag("input",
                        xs_html_attr("type", "checkbox"),
                        xs_html_attr("name", "private"),
                        xs_html_attr("id", "private"),
                        xs_html_attr(xs_type(a_private) == XSTYPE_TRUE ? "checked" : "", NULL)),
                    xs_html_tag("label",
                        xs_html_attr("for", "private"),
                        xs_html_text(L("This account is private (posts are not shown through the web)")))),
                xs_html_tag("p",
                    xs_html_text(L("Profile metadata (key=value pairs in each line):")),
                    xs_html_sctag("br", NULL),
                    xs_html_tag("textarea",
                        xs_html_attr("name", "metadata"),
                        xs_html_attr("cols", "40"),
                        xs_html_attr("rows", "4"),
                        xs_html_attr("placeholder", "Blog=https:/" "/example.com/my-blog\nGPG Key=1FA54\n..."),
                    xs_html_text(metadata))),

                xs_html_tag("p",
                    xs_html_text(L("New password:")),
                    xs_html_sctag("br", NULL),
                    xs_html_sctag("input",
                        xs_html_attr("type", "password"),
                        xs_html_attr("name", "passwd1"),
                        xs_html_attr("value", ""))),
                xs_html_tag("p",
                    xs_html_text(L("Repeat new password:")),
                    xs_html_sctag("br", NULL),
                    xs_html_sctag("input",
                        xs_html_attr("type", "password"),
                        xs_html_attr("name", "passwd2"),
                        xs_html_attr("value", ""))),

                xs_html_sctag("input",
                    xs_html_attr("type", "submit"),
                    xs_html_attr("class", "button"),
                    xs_html_attr("value", L("Update user info")))))));

    return top_controls;
}


static xs_html *html_button(char *clss, char *label, char *hint)
{
    xs *c = xs_fmt("snac-btn-%s", clss);

    return xs_html_sctag("input",
        xs_html_attr("type",    "submit"),
        xs_html_attr("name",    "action"),
        xs_html_attr("class",   c),
        xs_html_attr("value",   label),
        xs_html_attr("title",   hint));
}


xs_str *build_mentions(snac *snac, const xs_dict *msg)
/* returns a string with the mentions in msg */
{
    xs_str *s = xs_str_new(NULL);
    char *list = xs_dict_get(msg, "tag");
    char *v;

    while (xs_list_iter(&list, &v)) {
        char *type = xs_dict_get(v, "type");
        char *href = xs_dict_get(v, "href");
        char *name = xs_dict_get(v, "name");

        if (type && strcmp(type, "Mention") == 0 &&
            href && strcmp(href, snac->actor) != 0 && name) {
            xs *s1 = NULL;

            if (name[0] != '@') {
                s1 = xs_fmt("@%s", name);
                name = s1;
            }

            xs *l = xs_split(name, "@");

            /* is it a name without a host? */
            if (xs_list_len(l) < 3) {
                /* split the href and pick the host name LIKE AN ANIMAL */
                /* would be better to query the webfinger but *won't do that* here */
                xs *l2 = xs_split(href, "/");

                if (xs_list_len(l2) >= 3) {
                    xs *s1 = xs_fmt("%s@%s ", name, xs_list_get(l2, 2));
                    s = xs_str_cat(s, s1);
                }
            }
            else {
                s = xs_str_cat(s, name);
                s = xs_str_cat(s, " ");
            }
        }
    }

    if (*s) {
        xs *s1 = s;
        s = xs_fmt("\n\n\nCC: %s", s1);
    }

    return s;
}


xs_html *html_entry_controls(snac *snac, const xs_dict *msg, const char *md5)
{
    char *id    = xs_dict_get(msg, "id");
    char *actor = xs_dict_get(msg, "attributedTo");
    char *group = xs_dict_get(msg, "audience");

    xs *likes   = object_likes(id);
    xs *boosts  = object_announces(id);

    xs *action = xs_fmt("%s/admin/action", snac->actor);
    xs *redir  = xs_fmt("%s_entry", md5);

    xs_html *form;
    xs_html *controls = xs_html_tag("div",
        xs_html_attr("class", "snac-controls"),
        form = xs_html_tag("form",
            xs_html_attr("autocomplete", "off"),
            xs_html_attr("method",       "post"),
            xs_html_attr("action",       action),
            xs_html_sctag("input",
                xs_html_attr("type",     "hidden"),
                xs_html_attr("name",     "id"),
                xs_html_attr("value",    id)),
            xs_html_sctag("input",
                xs_html_attr("type",     "hidden"),
                xs_html_attr("name",     "actor"),
                xs_html_attr("value",    actor)),
            xs_html_sctag("input",
                xs_html_attr("type",     "hidden"),
                xs_html_attr("name",     "group"),
                xs_html_attr("value",    xs_is_null(group) ? "" : group)),
            xs_html_sctag("input",
                xs_html_attr("type",     "hidden"),
                xs_html_attr("name",     "redir"),
                xs_html_attr("value",    redir))));

    if (!xs_startswith(id, snac->actor)) {
        if (xs_list_in(likes, snac->md5) == -1) {
            /* not already liked; add button */
            xs_html_add(form,
                html_button("like", L("Like"), L("Say you like this post")));
        }
    }
    else {
        if (is_pinned(snac, id))
            xs_html_add(form,
                html_button("unpin", L("Unpin"), L("Unpin this post from your timeline")));
        else
            xs_html_add(form,
                html_button("pin", L("Pin"), L("Pin this post to the top of your timeline")));
    }

    if (is_msg_public(msg)) {
        if (strcmp(actor, snac->actor) == 0 || xs_list_in(boosts, snac->md5) == -1) {
            /* not already boosted or us; add button */
            xs_html_add(form,
                html_button("boost", L("Boost"), L("Announce this post to your followers")));
        }
    }

    if (strcmp(actor, snac->actor) != 0) {
        /* controls for other actors than this one */
        if (following_check(snac, actor)) {
            xs_html_add(form,
                html_button("unfollow", L("Unfollow"), L("Stop following this user's activity")));
        }
        else {
            xs_html_add(form,
                html_button("follow", L("Follow"), L("Start following this user's activity")));
        }

        if (!xs_is_null(group)) {
            if (following_check(snac, group)) {
                xs_html_add(form,
                    html_button("unfollow", L("Unfollow Group"),
                        L("Stop following this group or channel")));
            }
            else {
                xs_html_add(form,
                    html_button("follow", L("Follow Group"),
                        L("Start following this group or channel")));
            }
        }

        xs_html_add(form,
            html_button("mute", L("MUTE"),
                L("Block any activity from this user forever")));
    }

    xs_html_add(form,
        html_button("delete", L("Delete"), L("Delete this post")),
        html_button("hide",   L("Hide"), L("Hide this post and its children")));

    char *prev_src = xs_dict_get(msg, "sourceContent");

    if (!xs_is_null(prev_src) && strcmp(actor, snac->actor) == 0) { /** edit **/
        /* post can be edited */
        xs *div_id  = xs_fmt("%s_edit", md5);
        xs *form_id = xs_fmt("%s_edit_form", md5);
        xs *redir   = xs_fmt("%s_entry", md5);

        xs_html_add(controls, xs_html_tag("div",
            xs_html_tag("p", NULL),
            html_note(snac, L("Edit..."),
                div_id, form_id,
                "", prev_src,
                id, NULL,
                xs_dict_get(msg, "sensitive"), xs_dict_get(msg, "summary"),
                xs_stock_false, redir,
                NULL, 0)),
            xs_html_tag("p", NULL));
    }

    { /** reply **/
        /* the post textarea */
        xs *ct      = build_mentions(snac, msg);
        xs *div_id  = xs_fmt("%s_reply", md5);
        xs *form_id = xs_fmt("%s_reply_form", md5);
        xs *redir   = xs_fmt("%s_entry", md5);

        xs_html_add(controls, xs_html_tag("div",
            xs_html_tag("p", NULL),
            html_note(snac, L("Reply..."),
                div_id, form_id,
                "", ct,
                NULL, NULL,
                xs_dict_get(msg, "sensitive"), xs_dict_get(msg, "summary"),
                xs_stock_false, redir,
                id, 0)),
            xs_html_tag("p", NULL));
    }

    return controls;
}


xs_str *html_entry(snac *user, xs_str *os, const xs_dict *msg, int local,
                   int level, const char *md5, int hide_children)
{
    char *id    = xs_dict_get(msg, "id");
    char *type  = xs_dict_get(msg, "type");
    char *actor;
    int sensitive = 0;
    char *v;
    xs *boosts = NULL;

    /* do not show non-public messages in the public timeline */
    if ((local || !user) && !is_msg_public(msg))
        return os;

    /* hidden? do nothing more for this conversation */
    if (user && is_hidden(user, id))
        return os;

    /* avoid too deep nesting, as it may be a loop */
    if (level >= 256)
        return os;

    if (strcmp(type, "Follow") == 0) {
        xs_html *h = xs_html_tag("div",
            xs_html_attr("class", "snac-post"),
            xs_html_tag("div",
                xs_html_attr("class", "snac-post-header"),
                xs_html_tag("div",
                    xs_html_attr("class", "snac-origin"),
                    xs_html_text(L("follows you"))),
                html_msg_icon(msg)));

        xs *s1 = xs_html_render(h);
        return xs_str_cat(os, s1);
    }
    else
    if (!xs_match(type, "Note|Question|Page|Article")) {
        /* skip oddities */
        return os;
    }

    /* ignore notes with "name", as they are votes to Questions */
    if (strcmp(type, "Note") == 0 && !xs_is_null(xs_dict_get(msg, "name")))
        return os;

    /* bring the main actor */
    if ((actor = xs_dict_get(msg, "attributedTo")) == NULL)
        return os;

    /* ignore muted morons immediately */
    if (user && is_muted(user, actor))
        return os;

    if ((user == NULL || strcmp(actor, user->actor) != 0)
        && !valid_status(actor_get(actor, NULL)))
        return os;

    xs *s = xs_str_new("<div>\n");

    {
        xs *s1 = xs_fmt("<a name=\"%s_entry\"></a>\n", md5);

        s = xs_str_cat(s, s1);
    }

    if (level == 0)
        s = xs_str_cat(s, "<div class=\"snac-post\">\n"); /** **/
    else
        s = xs_str_cat(s, "<div class=\"snac-child\">\n"); /** **/

    /** post header **/

    xs_html *score;
    xs_html *post_header = xs_html_tag("div",
        xs_html_attr("class", "snac-post-header"),
        score = xs_html_tag("div",
            xs_html_attr("class", "snac-score")));

    if (user && is_pinned(user, id)) {
        /* add a pin emoji */
        xs_html_add(score,
            xs_html_tag("span",
                xs_html_attr("title", L("Pinned")),
                xs_html_raw(" &#128204; ")));
    }

    if (strcmp(type, "Question") == 0) {
        /* add the ballot box emoji */
        xs_html_add(score,
            xs_html_tag("span",
                xs_html_attr("title", L("Poll")),
                xs_html_raw(" &#128499; ")));

        if (user && was_question_voted(user, id)) {
            /* add a check to show this poll was voted */
            xs_html_add(score,
                xs_html_tag("span",
                    xs_html_attr("title", L("Voted")),
                    xs_html_raw(" &#10003; ")));
        }
    }

    /* if it's a user from this same instance, add the score */
    if (xs_startswith(id, srv_baseurl)) {
        int n_likes  = object_likes_len(id);
        int n_boosts = object_announces_len(id);

        /* alternate emojis: %d &#128077; %d &#128257; */
        xs *s1 = xs_fmt("%d &#9733; %d &#8634;\n", n_likes, n_boosts);

        xs_html_add(score,
            xs_html_raw(s1));
    }

    if (boosts == NULL)
        boosts = object_announces(id);

    if (xs_list_len(boosts)) {
        /* if somebody boosted this, show as origin */
        char *p = xs_list_get(boosts, -1);
        xs *actor_r = NULL;

        if (user && xs_list_in(boosts, user->md5) != -1) {
            /* we boosted this */
            xs_html_add(post_header,
                xs_html_tag("div",
                    xs_html_attr("class", "snac-origin"),
                    xs_html_tag("a",
                        xs_html_attr("href", user->actor),
                        xs_html_text(xs_dict_get(user->config, "name"))),
                        xs_html_text(" "),
                        xs_html_text(L("boosted"))));
        }
        else
        if (valid_status(object_get_by_md5(p, &actor_r))) {
            xs *name = actor_name(actor_r);

            if (!xs_is_null(name)) {
                xs_html_add(post_header,
                    xs_html_tag("div",
                        xs_html_attr("class", "snac-origin"),
                        xs_html_tag("a",
                            xs_html_attr("href", xs_dict_get(actor_r, "id")),
                            xs_html_raw(name)), /* already sanitized */
                            xs_html_text(" "),
                            xs_html_text(L("boosted"))));
            }
        }
    }
    else
    if (strcmp(type, "Note") == 0) {
        if (level == 0) {
            /* is the parent not here? */
            char *parent = xs_dict_get(msg, "inReplyTo");

            if (user && !xs_is_null(parent) && *parent && !timeline_here(user, parent)) {
                xs_html_add(post_header,
                    xs_html_tag("div",
                        xs_html_attr("class", "snac-origin"),
                        xs_html_text(L("in reply to")),
                        xs_html_text(" "),
                        xs_html_tag("a",
                            xs_html_attr("href", parent),
                            xs_html_text("»"))));
            }
        }
    }

    xs_html_add(post_header,
        html_msg_icon(msg));

    {
        xs *s1 = xs_html_render(post_header);
        s = xs_str_cat(s, s1);
    }

    /** post content **/

    s = xs_str_cat(s, "\n<div class=\"e-content snac-content\">\n"); /** **/

    if (!xs_is_null(v = xs_dict_get(msg, "name"))) {
        xs *es1 = encode_html(v);
        xs *s1  = xs_fmt("<h3 class=\"snac-entry-title\">%s</h3>\n", es1);
        s = xs_str_cat(s, s1);
    }

    /* is it sensitive? */
    if (user && xs_type(xs_dict_get(msg, "sensitive")) == XSTYPE_TRUE) {
        if (xs_is_null(v = xs_dict_get(msg, "summary")) || *v == '\0')
            v = "...";
        /* only show it when not in the public timeline and the config setting is "open" */
        char *cw = xs_dict_get(user->config, "cw");
        if (xs_is_null(cw) || local)
            cw = "";
        xs *es1 = encode_html(v);
        xs *s1 = xs_fmt("<details %s><summary>%s [%s]</summary>\n", cw, es1, L("SENSITIVE CONTENT"));
        s = xs_str_cat(s, s1);
        sensitive = 1;
    }

#if 0
    {
        xs *md5 = xs_md5_hex(id, strlen(id));
        xs *s1  = xs_fmt("<p><code>%s</code></p>\n", md5);
        s = xs_str_cat(s, s1);
    }
#endif

    {
        /** build the content string **/

        char *content = xs_dict_get(msg, "content");

        xs *c = sanitize(xs_is_null(content) ? "" : content);
        char *p, *v;

        /* do some tweaks to the content */
        c = xs_replace_i(c, "\r", "");

        while (xs_endswith(c, "<br><br>"))
            c = xs_crop_i(c, 0, -4);

        c = xs_replace_i(c, "<br><br>", "<p>");

        if (!xs_startswith(c, "<p>")) {
            xs *s1 = c;
            c = xs_fmt("<p>%s</p>", s1);
        }

        /* replace the :shortnames: */
        if (!xs_is_null(p = xs_dict_get(msg, "tag"))) {
            xs *tag = NULL;
            if (xs_type(p) == XSTYPE_DICT) {
                /* not a list */
                tag = xs_list_new();
                tag = xs_list_append(tag, p);
            }
            else
            if (xs_type(p) == XSTYPE_LIST)
                tag = xs_dup(p);
            else
                tag = xs_list_new();

            xs_list *tags = tag;

            /* iterate the tags */
            while (xs_list_iter(&tags, &v)) {
                char *t = xs_dict_get(v, "type");

                if (t && strcmp(t, "Emoji") == 0) {
                    char *n = xs_dict_get(v, "name");
                    char *i = xs_dict_get(v, "icon");

                    if (n && i) {
                        char *u = xs_dict_get(i, "url");
                        xs *img = xs_fmt("<img loading=\"lazy\" src=\"%s\" "
                                        "style=\"height: 2em; vertical-align: middle;\" "
                                        "title=\"%s\"/>", u, n);

                        c = xs_replace_i(c, n, img);
                    }
                }
            }
        }

        /* c contains sanitized HTML */
        s = xs_str_cat(s, c);
    }

    if (strcmp(type, "Question") == 0) { /** question content **/
        xs_list *oo = xs_dict_get(msg, "oneOf");
        xs_list *ao = xs_dict_get(msg, "anyOf");
        xs_list *p;
        xs_dict *v;
        int closed = 0;

        xs_html *poll = xs_html_tag("div", NULL);

        if (xs_dict_get(msg, "closed"))
            closed = 2;
        else
        if (user && xs_startswith(id, user->actor))
            closed = 1; /* we questioned; closed for us */
        else
        if (user && was_question_voted(user, id))
            closed = 1; /* we already voted; closed for us */

        /* get the appropriate list of options */
        p = oo != NULL ? oo : ao;

        if (closed || user == NULL) {
            /* closed poll */
            xs_html *poll_result = xs_html_tag("table",
                xs_html_attr("class", "snac-poll-result"));

            while (xs_list_iter(&p, &v)) {
                char *name       = xs_dict_get(v, "name");
                xs_dict *replies = xs_dict_get(v, "replies");

                if (name && replies) {
                    char *ti = (char *)xs_number_str(xs_dict_get(replies, "totalItems"));

                    xs_html_add(poll_result,
                        xs_html_tag("tr",
                            xs_html_tag("td",
                                xs_html_text(name),
                                xs_html_text(":")),
                            xs_html_tag("td",
                                xs_html_text(ti))));
                }
            }

            xs_html_add(poll,
                poll_result);
        }
        else {
            /* poll still active */
            xs *vote_action = xs_fmt("%s/admin/vote", user->actor);
            xs_html *form;
            xs_html *poll_form = xs_html_tag("div",
                xs_html_attr("class", "snac-poll-form"),
                form = xs_html_tag("form",
                    xs_html_attr("autocomplete", "off"),
                    xs_html_attr("method", "post"),
                    xs_html_attr("action", vote_action),
                    xs_html_sctag("input",
                        xs_html_attr("type", "hidden"),
                        xs_html_attr("name", "actor"),
                        xs_html_attr("value", actor)),
                    xs_html_sctag("input",
                        xs_html_attr("type", "hidden"),
                        xs_html_attr("name", "irt"),
                        xs_html_attr("value", id))));

            while (xs_list_iter(&p, &v)) {
                char *name = xs_dict_get(v, "name");
                xs_dict *replies = xs_dict_get(v, "replies");

                if (name) {
                    char *ti = (char *)xs_number_str(xs_dict_get(replies, "totalItems"));

                    xs_html_add(form,
                        xs_html_sctag("input",
                            xs_html_attr("type", !xs_is_null(oo) ? "radio" : "checkbox"),
                            xs_html_attr("id", name),
                            xs_html_attr("value", name),
                            xs_html_attr("name", "question")),
                        xs_html_text(" "),
                        xs_html_tag("span",
                            xs_html_attr("title", ti),
                            xs_html_text(name)),
                        xs_html_sctag("br", NULL));
                }
            }

            xs_html_add(form,
                xs_html_tag("p", NULL),
                xs_html_sctag("input",
                    xs_html_attr("type", "submit"),
                    xs_html_attr("class", "button"),
                    xs_html_attr("value", L("Vote"))));

            xs_html_add(poll,
                poll_form);
        }

        /* if it's *really* closed, say it */
        if (closed == 2) {
            xs_html_add(poll,
                xs_html_tag("p",
                    xs_html_text(L("Closed"))));
        }
        else {
            /* show when the poll closes */
            char *end_time = xs_dict_get(msg, "endTime");
            if (!xs_is_null(end_time)) {
                time_t t0 = time(NULL);
                time_t t1 = xs_parse_iso_date(end_time, 0);

                if (t1 > 0 && t1 > t0) {
                    time_t diff_time = t1 - t0;
                    xs *tf = xs_str_time_diff(diff_time);
                    char *p = tf;

                    /* skip leading zeros */
                    for (; *p == '0' || *p == ':'; p++);

                    xs_html_add(poll,
                        xs_html_tag("p",
                            xs_html_text(L("Closes in")),
                            xs_html_text(" "),
                            xs_html_text(p)));
                }
            }
        }

        xs *s1 = xs_html_render(poll);
        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "\n");

    /* add the attachments */
    v = xs_dict_get(msg, "attachment");

    if (!xs_is_null(v)) { /** attachments **/
        xs *attach = NULL;

        /* ensure it's a list */
        if (xs_type(v) == XSTYPE_DICT) {
            attach = xs_list_new();
            attach = xs_list_append(attach, v);
        }
        else
        if (xs_type(v) == XSTYPE_LIST)
            attach = xs_dup(v);
        else
            attach = xs_list_new();

        /* does the message have an image? */
        if (xs_type(v = xs_dict_get(msg, "image")) == XSTYPE_DICT) {
            /* add it to the attachment list */
            attach = xs_list_append(attach, v);
        }

        /* make custom css for attachments easier */
        xs_html *content_attachments = xs_html_tag("div",
            xs_html_attr("class", "snac-content-attachments"));

        xs_list *p = attach;

        while (xs_list_iter(&p, &v)) {
            char *t = xs_dict_get(v, "mediaType");

            if (xs_is_null(t))
                t = xs_dict_get(v, "type");

            if (xs_is_null(t))
                continue;

            char *url = xs_dict_get(v, "url");
            if (xs_is_null(url))
                url = xs_dict_get(v, "href");
            if (xs_is_null(url))
                continue;

            /* infer MIME type from non-specific attachments */
            if (xs_list_len(attach) < 2 && xs_match(t, "Link|Document")) {
                char *mt = (char *)xs_mime_by_ext(url);

                if (xs_match(mt, "image/*|audio/*|video/*")) /* */
                    t = mt;
            }

            char *name = xs_dict_get(v, "name");
            if (xs_is_null(name))
                name = xs_dict_get(msg, "name");
            if (xs_is_null(name))
                name = L("No description");

            if (xs_startswith(t, "image/") || strcmp(t, "Image") == 0) {
                xs_html_add(content_attachments,
                    xs_html_tag("a",
                        xs_html_attr("href", url),
                        xs_html_attr("target", "_blank"),
                        xs_html_sctag("img",
                            xs_html_attr("loading", "lazy"),
                            xs_html_attr("src", url),
                            xs_html_attr("alt", name),
                            xs_html_attr("title", name))));
            }
            else
            if (xs_startswith(t, "video/")) {
                xs_html_add(content_attachments,
                    xs_html_tag("video",
                        xs_html_attr("style", "width: 100%"),
                        xs_html_attr("class", "snac-embedded-video"),
                        xs_html_attr("controls", NULL),
                        xs_html_attr("src", url),
                        xs_html_text(L("Video")),
                        xs_html_text(": "),
                        xs_html_tag("a",
                            xs_html_attr("href", url),
                            xs_html_text(name))));
            }
            else
            if (xs_startswith(t, "audio/")) {
                xs_html_add(content_attachments,
                    xs_html_tag("audio",
                        xs_html_attr("style", "width: 100%"),
                        xs_html_attr("class", "snac-embedded-audio"),
                        xs_html_attr("controls", NULL),
                        xs_html_attr("src", url),
                        xs_html_text(L("Audio")),
                        xs_html_text(": "),
                        xs_html_tag("a",
                            xs_html_attr("href", url),
                            xs_html_text(name))));
            }
            else
            if (strcmp(t, "Link") == 0) {
                xs_html_add(content_attachments,
                    xs_html_tag("p",
                        xs_html_tag("a",
                            xs_html_attr("href", url),
                            xs_html_text(url))));
            }
            else {
                xs_html_add(content_attachments,
                    xs_html_tag("p",
                        xs_html_tag("a",
                            xs_html_attr("href", url),
                            xs_html_text(L("Attachment")),
                            xs_html_text(": "),
                            xs_html_text(url))));
            }
        }

        {
            xs *s1 = xs_html_render(content_attachments);
            s = xs_str_cat(s, s1);
        }
    }

    /* has this message an audience (i.e., comes from a channel or community)? */
    char *audience = xs_dict_get(msg, "audience");
    if (strcmp(type, "Page") == 0 && !xs_is_null(audience)) {
        xs_html *au_tag = xs_html_tag("p",
            xs_html_text("("),
            xs_html_tag("a",
                xs_html_attr("href", audience),
                xs_html_attr("title", L("Source channel or community")),
                xs_html_text(audience)),
            xs_html_text(")"));

        xs *s1 = xs_html_render(au_tag);
        s = xs_str_cat(s, s1);
    }

    if (sensitive)
        s = xs_str_cat(s, "</details><p>\n");

    s = xs_str_cat(s, "</div>\n");

    /** controls **/

    if (!local && user) {
        xs_html *h = html_entry_controls(user, msg, md5);
        xs *s1 = xs_html_render(h);
        s = xs_str_cat(s, s1);
    }

    /** children **/
    if (!hide_children) {
        xs *children = object_children(id);
        int left     = xs_list_len(children);

        if (left) {
            xs_html *ch_details = xs_html_tag("details",
                xs_html_attr("open", NULL),
                xs_html_tag("summary",
                    xs_html_text("...")));

            xs_html *ch_container = xs_html_tag("div",
                xs_html_attr("class", level < 4 ? "snac-children" : "snac-children-too-deep"));

            xs_html_add(ch_details,
                ch_container);

            xs_html *ch_older = NULL;
            if (left > 3) {
                xs_html_add(ch_container,
                    ch_older = xs_html_tag("details",
                        xs_html_tag("summary",
                            xs_html_text(L("Older...")))));
            }

            xs_list *p = children;
            char *cmd5;
            while (xs_list_iter(&p, &cmd5)) {
                xs *chd = NULL;

                if (user)
                    timeline_get_by_md5(user, cmd5, &chd);
                else
                    object_get_by_md5(cmd5, &chd);

                if (chd != NULL && xs_is_null(xs_dict_get(chd, "name"))) {
                    xs *s1 = xs_str_new(NULL);
                    s1 = html_entry(user, s1, chd, local, level + 1, cmd5, hide_children);

                    if (left > 3)
                        xs_html_add(ch_older,
                            xs_html_raw(s1));
                    else
                        xs_html_add(ch_container,
                            xs_html_raw(s1));
                }
                else
                    srv_debug(2, xs_fmt("cannot read child %s", cmd5));

                left--;
            }

            xs *s1 = xs_html_render(ch_details);
            s = xs_str_cat(s, s1);
        }
    }

    s = xs_str_cat(s, "</div>\n</div>\n");

    return xs_str_cat(os, s);
}


xs_html *html_footer(void)
{
    return xs_html_tag("div",
        xs_html_attr("class", "snac-footer"),
        xs_html_tag("a",
            xs_html_attr("href", srv_baseurl),
            xs_html_text(L("about this site"))),
        xs_html_text(" - "),
        xs_html_text(L("powered by ")),
        xs_html_tag("a",
            xs_html_attr("href", WHAT_IS_SNAC_URL),
            xs_html_tag("abbr",
                xs_html_attr("title", "Social Network Are Crap"),
                xs_html_text("snac"))));
}


xs_str *html_timeline(snac *user, const xs_list *list, int local,
                      int skip, int show, int show_more, char *tag)
/* returns the HTML for the timeline */
{
    xs_str *s = xs_str_new(NULL);
    xs_list *p = (xs_list *)list;
    char *v;
    double t = ftime();

    if (user)
        s = html_user_header(user, s, local);
    else
        s = html_instance_header(s, tag);

    if (user && !local) {
        xs_html *h = html_top_controls(user);
        xs *s1 = xs_html_render(h);
        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "<a name=\"snac-posts\"></a>\n");
    s = xs_str_cat(s, "<div class=\"snac-posts\">\n");

    while (xs_list_iter(&p, &v)) {
        xs *msg = NULL;
        int status;

        if (user && !is_pinned_by_md5(user, v))
            status = timeline_get_by_md5(user, v, &msg);
        else
            status = object_get_by_md5(v, &msg);

        if (!valid_status(status))
            continue;

        /* if it's an instance page, discard private users */
        if (user == NULL && xs_startswith(xs_dict_get(msg, "id"), srv_baseurl)) {
            const char *atto = xs_dict_get(msg, "attributedTo");
            xs *l = xs_split(atto, "/");
            const char *uid = xs_list_get(l, -1);
            snac user;
            int skip = 1;

            if (uid && user_open(&user, uid)) {
                if (xs_type(xs_dict_get(user.config, "private")) != XSTYPE_TRUE)
                    skip = 0;

                user_free(&user);
            }

            if (skip)
                continue;
        }

        s = html_entry(user, s, msg, local, 0, v, user ? 0 : 1);
    }

    s = xs_str_cat(s, "</div>\n");

    if (list && user && local) {
        if (xs_type(xs_dict_get(srv_config, "disable_history")) == XSTYPE_TRUE) {
            s = xs_str_cat(s, "<!-- history disabled -->\n");
        }
        else {
            xs *s1 = xs_fmt(
                "<div class=\"snac-history\">\n"
                "<p class=\"snac-history-title\">%s</p><ul>\n",
                L("History")
            );

            s = xs_str_cat(s, s1);

            xs *list = history_list(user);
            char *p, *v;

            p = list;
            while (xs_list_iter(&p, &v)) {
                xs *fn = xs_replace(v, ".html", "");
                xs *s1 = xs_fmt(
                        "<li><a href=\"%s/h/%s\">%s</a></li>\n",
                        user->actor, v, fn);

                s = xs_str_cat(s, s1);
            }

            s = xs_str_cat(s, "</ul></div>\n");
        }
    }

    {
        xs *s1 = xs_fmt("<!-- %lf seconds -->\n", ftime() - t);
        s = xs_str_cat(s, s1);
    }

    if (show_more) {
        xs *t  = NULL;
        xs *m  = NULL;
        xs *ss = xs_fmt("skip=%d&show=%d", skip + show, show);

        if (tag) {
            t = xs_fmt("%s?t=%s", srv_baseurl, tag);
            m = xs_fmt("%s&%s", t, ss);
        }
        else {
            t = xs_fmt("%s%s", user ? user->actor : srv_baseurl, local ? "" : "/admin");
            m = xs_fmt("%s?%s", t, ss);
        }

        xs *s1 = xs_fmt(
            "<p>"
            "<a href=\"%s\" name=\"snac-more\">%s</a> - "
            "<a href=\"%s\" name=\"snac-more\">%s</a>"
            "</p>\n",
            t, L("Back to top"),
            m, L("More...")
        );

        s = xs_str_cat(s, s1);
    }

    {
        xs_html *h = html_footer();
        xs *s1 = xs_html_render(h);
        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "</body>\n</html>\n");

    return s;
}


xs_html *html_people_list(snac *snac, xs_list *list, char *header, char *t)
{
    xs_html *snac_posts;
    xs_html *people = xs_html_tag("div",
        xs_html_tag("h2",
            xs_html_attr("class", "snac-header"),
            xs_html_text(header)),
        snac_posts = xs_html_tag("div",
            xs_html_attr("class", "snac-posts")));

    xs_list *p = list;
    char *actor_id;

    while (xs_list_iter(&p, &actor_id)) {
        xs *md5 = xs_md5_hex(actor_id, strlen(actor_id));
        xs *actor = NULL;

        if (valid_status(actor_get(actor_id, &actor))) {
            xs_html *snac_post = xs_html_tag("div",
                xs_html_attr("class", "snac-post"),
                xs_html_tag("div",
                    xs_html_attr("class", "snac-post-header"),
                    html_actor_icon(actor, xs_dict_get(actor, "published"), NULL, NULL, 0)));

            /* content (user bio) */
            char *c = xs_dict_get(actor, "summary");

            if (!xs_is_null(c)) {
                xs *sc = sanitize(c);

                xs_html *snac_content = xs_html_tag("div",
                    xs_html_attr("class", "snac-content"));

                if (xs_startswith(sc, "<p>"))
                    xs_html_add(snac_content,
                        xs_html_raw(sc)); /* already sanitized */
                else
                    xs_html_add(snac_content,
                        xs_html_tag("p",
                            xs_html_raw(sc))); /* already sanitized */

                xs_html_add(snac_post, snac_content);
            }

            /* buttons */
            xs *btn_form_action = xs_fmt("%s/admin/action", snac->actor);

            xs_html *snac_controls = xs_html_tag("div",
                xs_html_attr("class",   "snac-controls"));

            xs_html *form = xs_html_tag("form",
                xs_html_attr("autocomplete",    "off"),
                xs_html_attr("method",          "post"),
                xs_html_attr("action",          btn_form_action),
                xs_html_sctag("input",
                    xs_html_attr("type",    "hidden"),
                    xs_html_attr("name",    "actor"),
                    xs_html_attr("value",   actor_id)),
                xs_html_sctag("input",
                    xs_html_attr("type",    "hidden"),
                    xs_html_attr("name",    "actor-form"),
                    xs_html_attr("value",   "yes")));

            xs_html_add(snac_controls, form);

            if (following_check(snac, actor_id)) {
                xs_html_add(form,
                    html_button("unfollow", L("Unfollow"),
                                L("Stop following this user's activity")));

                if (is_limited(snac, actor_id))
                    xs_html_add(form,
                        html_button("unlimit", L("Unlimit"),
                                L("Allow announces (boosts) from this user")));
                else
                    xs_html_add(form,
                        html_button("limit", L("Limit"),
                                L("Block announces (boosts) from this user")));
            }
            else {
                xs_html_add(form,
                    html_button("follow", L("Follow"),
                                L("Start following this user's activity")));

                if (follower_check(snac, actor_id))
                    xs_html_add(form,
                        html_button("delete", L("Delete"), L("Delete this user")));
            }

            if (is_muted(snac, actor_id))
                xs_html_add(form,
                    html_button("unmute", L("Unmute"),
                                L("Stop blocking activities from this user")));
            else
                xs_html_add(form,
                    html_button("mute", L("MUTE"),
                                L("Block any activity from this user")));

            /* the post textarea */
            xs *dm_div_id  = xs_fmt("%s_%s_dm", md5, t);
            xs *dm_form_id = xs_fmt("%s_reply_form", md5);

            xs_html_add(snac_controls,
                xs_html_tag("p", NULL),
                html_note(snac, L("Direct Message..."),
                    dm_div_id, dm_form_id,
                    "", "",
                    NULL, actor_id,
                    xs_stock_false, "",
                    xs_stock_false, NULL,
                    NULL, 0),
                xs_html_tag("p", NULL));

            xs_html_add(snac_post, snac_controls);

            xs_html_add(snac_posts, snac_post);
        }
    }

    return people;
}


xs_str *html_people(snac *user)
{
    xs *wing = following_list(user);
    xs *wers = follower_list(user);

    xs_html *html = xs_html_tag("html",
        html_user_head(user),
        xs_html_add(html_user_body(user, 0),
            html_people_list(user, wing, L("People you follow"), "i"),
            html_people_list(user, wers, L("People that follow you"), "e"),
            html_footer()));

    return xs_html_render_s(html, "<!DOCTYPE html>\n");
}


xs_str *html_notifications(snac *snac)
{
    xs_str *s  = xs_str_new(NULL);
    xs *n_list = notify_list(snac, 0);
    xs *n_time = notify_check_time(snac, 0);
    xs_list *p = n_list;
    xs_str *v;
    enum { NHDR_NONE, NHDR_NEW, NHDR_OLD } stage = NHDR_NONE;

    s = html_user_header(snac, s, 0);

    xs *clear_all_action = xs_fmt("%s/admin/clear-notifications", snac->actor);
    xs_html *clear_all_form = xs_html_tag("form",
        xs_html_attr("autocomplete", "off"),
        xs_html_attr("method",       "post"),
        xs_html_attr("action",       clear_all_action),
        xs_html_attr("id",           "clear"),
        xs_html_sctag("input",
            xs_html_attr("type",     "submit"),
            xs_html_attr("class",    "snac-btn-like"),
            xs_html_attr("value",    L("Clear all"))));

    {
        xs *s1 = xs_html_render(clear_all_form);
        s = xs_str_cat(s, s1);
    }

    while (xs_list_iter(&p, &v)) {
        xs *noti = notify_get(snac, v);

        if (noti == NULL)
            continue;

        xs *obj = NULL;
        const char *type  = xs_dict_get(noti, "type");
        const char *utype = xs_dict_get(noti, "utype");
        const char *id    = xs_dict_get(noti, "objid");

        if (xs_is_null(id) || !valid_status(object_get(id, &obj)))
            continue;

        if (is_hidden(snac, id))
            continue;

        const char *actor_id = xs_dict_get(noti, "actor");
        xs *actor = NULL;

        if (!valid_status(actor_get(actor_id, &actor)))
            continue;

        xs *a_name = actor_name(actor);

        if (strcmp(v, n_time) > 0) {
            /* unseen notification */
            if (stage == NHDR_NONE) {
                xs *s1 = xs_fmt("<h2 class=\"snac-header\">%s</h2>\n", L("New"));
                s = xs_str_cat(s, s1);

                s = xs_str_cat(s, "<div class=\"snac-posts\">\n");

                stage = NHDR_NEW;
            }
        }
        else {
            /* already seen notification */
            if (stage != NHDR_OLD) {
                if (stage == NHDR_NEW)
                    s = xs_str_cat(s, "</div>\n");

                xs *s1 = xs_fmt("<h2 class=\"snac-header\">%s</h2>\n", L("Already seen"));
                s = xs_str_cat(s, s1);

                s = xs_str_cat(s, "<div class=\"snac-posts\">\n");

                stage = NHDR_OLD;
            }
        }

        const char *label = type;

        if (strcmp(type, "Create") == 0)
            label = L("Mention");
        else
        if (strcmp(type, "Update") == 0 && strcmp(utype, "Question") == 0)
            label = L("Finished poll");
        else
        if (strcmp(type, "Undo") == 0 && strcmp(utype, "Follow") == 0)
            label = L("Unfollow");

        xs *es1 = encode_html(label);
        xs *s1 = xs_fmt("<div class=\"snac-post-with-desc\">\n"
                        "<p><b>%s by <a href=\"%s\">%s</a></b>:</p>\n",
            es1, actor_id, a_name);
        s = xs_str_cat(s, s1);

        if (strcmp(type, "Follow") == 0 || strcmp(utype, "Follow") == 0) {
            xs_html *div = xs_html_tag("div",
                xs_html_attr("class", "snac-post"),
                html_actor_icon(actor, NULL, NULL, NULL, 0));

            xs *s1 = xs_html_render(div);
            s = xs_str_cat(s, s1);
        }
        else {
            xs *md5 = xs_md5_hex(id, strlen(id));

            s = html_entry(snac, s, obj, 0, 0, md5, 1);
        }

        s = xs_str_cat(s, "</div>\n");
    }

    if (stage == NHDR_NONE) {
        xs *s1 = xs_fmt("<h2 class=\"snac-header\">%s</h2>\n", L("None"));
        s = xs_str_cat(s, s1);
    }
    else
        s = xs_str_cat(s, "</div>\n");

    {
        xs_html *h = html_footer();
        xs *s1 = xs_html_render(h);
        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "</body>\n</html>\n");

    /* set the check time to now */
    xs *dummy = notify_check_time(snac, 1);
    dummy = xs_free(dummy);

    timeline_touch(snac);

    return s;
}


int html_get_handler(const xs_dict *req, const char *q_path,
                     char **body, int *b_size, char **ctype, xs_str **etag)
{
    char *accept = xs_dict_get(req, "accept");
    int status = 404;
    snac snac;
    xs *uid = NULL;
    char *p_path;
    int cache = 1;
    int save = 1;
    char *v;

    xs *l = xs_split_n(q_path, "/", 2);
    v = xs_list_get(l, 1);

    if (xs_is_null(v)) {
        srv_log(xs_fmt("html_get_handler bad query '%s'", q_path));
        return 404;
    }

    uid = xs_dup(v);

    /* rss extension? */
    if (xs_endswith(uid, ".rss")) {
        uid = xs_crop_i(uid, 0, -4);
        p_path = ".rss";
    }
    else
        p_path = xs_list_get(l, 2);

    if (!uid || !user_open(&snac, uid)) {
        /* invalid user */
        srv_debug(1, xs_fmt("html_get_handler bad user %s", uid));
        return 404;
    }

    /* return the RSS if requested by Accept header */
    if (accept != NULL) {
        if (xs_str_in(accept, "text/xml") != -1 ||
            xs_str_in(accept, "application/rss+xml") != -1)
            p_path = ".rss";
    }

    /* check if server config variable 'disable_cache' is set */
    if ((v = xs_dict_get(srv_config, "disable_cache")) && xs_type(v) == XSTYPE_TRUE)
        cache = 0;

    int skip = 0;
    int show = xs_number_get(xs_dict_get(srv_config, "max_timeline_entries"));
    char *q_vars = xs_dict_get(req, "q_vars");
    if ((v = xs_dict_get(q_vars, "skip")) != NULL)
        skip = atoi(v), cache = 0, save = 0;
    if ((v = xs_dict_get(q_vars, "show")) != NULL)
        show = atoi(v), cache = 0, save = 0;

    if (p_path == NULL) { /** public timeline **/
        xs *h = xs_str_localtime(0, "%Y-%m.html");

        if (xs_type(xs_dict_get(snac.config, "private")) == XSTYPE_TRUE) {
            *body = html_timeline(&snac, NULL, 1, 0, 0, 0, NULL);
            *b_size = strlen(*body);
            status  = 200;
        }
        else
        if (cache && history_mtime(&snac, h) > timeline_mtime(&snac)) {
            snac_debug(&snac, 1, xs_fmt("serving cached local timeline"));

            status = history_get(&snac, h, body, b_size,
                        xs_dict_get(req, "if-none-match"), etag);
        }
        else {
            xs *list = timeline_list(&snac, "public", skip, show);
            xs *next = timeline_list(&snac, "public", skip + show, 1);

            xs *pins = pinned_list(&snac);
            pins = xs_list_cat(pins, list);

            *body = html_timeline(&snac, pins, 1, skip, show, xs_list_len(next), NULL);

            *b_size = strlen(*body);
            status  = 200;

            if (save)
                history_add(&snac, h, *body, *b_size, etag);
        }
    }
    else
    if (strcmp(p_path, "admin") == 0) { /** private timeline **/
        if (!login(&snac, req)) {
            *body  = xs_dup(uid);
            status = 401;
        }
        else {
            if (cache && history_mtime(&snac, "timeline.html_") > timeline_mtime(&snac)) {
                snac_debug(&snac, 1, xs_fmt("serving cached timeline"));

                status = history_get(&snac, "timeline.html_", body, b_size,
                            xs_dict_get(req, "if-none-match"), etag);
            }
            else {
                snac_debug(&snac, 1, xs_fmt("building timeline"));

                xs *list = timeline_list(&snac, "private", skip, show);
                xs *next = timeline_list(&snac, "private", skip + show, 1);

                xs *pins = pinned_list(&snac);
                pins = xs_list_cat(pins, list);

                *body = html_timeline(&snac, pins, 0, skip, show, xs_list_len(next), NULL);

                *b_size = strlen(*body);
                status  = 200;

                if (save)
                    history_add(&snac, "timeline.html_", *body, *b_size, etag);
            }
        }
    }
    else
    if (strcmp(p_path, "people") == 0) { /** the list of people **/
        if (!login(&snac, req)) {
            *body  = xs_dup(uid);
            status = 401;
        }
        else {
            *body   = html_people(&snac);
            *b_size = strlen(*body);
            status  = 200;
        }
    }
    else
    if (strcmp(p_path, "notifications") == 0) { /** the list of notifications **/
        if (!login(&snac, req)) {
            *body  = xs_dup(uid);
            status = 401;
        }
        else {
            *body   = html_notifications(&snac);
            *b_size = strlen(*body);
            status  = 200;
        }
    }
    else
    if (xs_startswith(p_path, "p/")) { /** a timeline with just one entry **/
        if (xs_type(xs_dict_get(snac.config, "private")) == XSTYPE_TRUE)
            return 403;

        xs *id  = xs_fmt("%s/%s", snac.actor, p_path);
        xs *msg = NULL;

        if (valid_status(object_get(id, &msg))) {
            xs *md5  = xs_md5_hex(id, strlen(id));
            xs *list = xs_list_new();

            list = xs_list_append(list, md5);

            *body   = html_timeline(&snac, list, 1, 0, 0, 0, NULL);
            *b_size = strlen(*body);
            status  = 200;
        }
    }
    else
    if (xs_startswith(p_path, "s/")) { /** a static file **/
        xs *l    = xs_split(p_path, "/");
        char *id = xs_list_get(l, 1);
        int sz;

        if (id && *id) {
            status = static_get(&snac, id, body, &sz,
                        xs_dict_get(req, "if-none-match"), etag);

            if (valid_status(status)) {
                *b_size = sz;
                *ctype  = (char *)xs_mime_by_ext(id);
            }
        }
    }
    else
    if (xs_startswith(p_path, "h/")) { /** an entry from the history **/
        if (xs_type(xs_dict_get(snac.config, "private")) == XSTYPE_TRUE)
            return 403;

        if (xs_type(xs_dict_get(srv_config, "disable_history")) == XSTYPE_TRUE)
            return 403;

        xs *l    = xs_split(p_path, "/");
        char *id = xs_list_get(l, 1);

        if (id && *id) {
            if (xs_endswith(id, "timeline.html_")) {
                /* Don't let them in */
                *b_size = 0;
                status = 404;
            }
            else
                status = history_get(&snac, id, body, b_size,
                            xs_dict_get(req, "if-none-match"), etag);
        }
    }
    else
    if (strcmp(p_path, ".rss") == 0) { /** public timeline in RSS format **/
        if (xs_type(xs_dict_get(snac.config, "private")) == XSTYPE_TRUE)
            return 403;

        xs *elems = timeline_simple_list(&snac, "public", 0, 20);
        xs *bio   = not_really_markdown(xs_dict_get(snac.config, "bio"), NULL);

        xs *rss_title = xs_fmt("%s (@%s@%s)",
            xs_dict_get(snac.config, "name"),
            snac.uid,
            xs_dict_get(srv_config, "host"));
        xs *rss_link = xs_fmt("%s.rss", snac.actor);

        xs_html *rss = xs_html_tag("rss",
            xs_html_attr("version", "0.91"));

        xs_html *channel = xs_html_tag("channel",
            xs_html_tag("title",
                xs_html_text(rss_title)),
            xs_html_tag("language",
                xs_html_text("en")),
            xs_html_tag("link",
                xs_html_text(rss_link)),
            xs_html_tag("description",
                xs_html_text(bio)));

        xs_html_add(rss, channel);

        xs_list *p = elems;
        char *v;

        while (xs_list_iter(&p, &v)) {
            xs *msg  = NULL;

            if (!valid_status(timeline_get_by_md5(&snac, v, &msg)))
                continue;

            char *id = xs_dict_get(msg, "id");
            char *content = xs_dict_get(msg, "content");

            if (!xs_startswith(id, snac.actor))
                continue;

            /* create a title with the first line of the content */
            xs *es_title = xs_replace(content, "<br>", "\n");
            xs *title   = xs_str_new(NULL);
            int i;

            for (i = 0; es_title[i] && es_title[i] != '\n' && es_title[i] != '&' && i < 50; i++)
                title = xs_append_m(title, &es_title[i], 1);

            xs_html_add(channel,
                xs_html_tag("item",
                    xs_html_tag("title",
                        xs_html_text(title)),
                    xs_html_tag("link",
                        xs_html_text(id)),
                    xs_html_tag("description",
                        xs_html_text(content))));
        }

        *body   = xs_html_render_s(rss, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        *b_size = strlen(*body);
        *ctype  = "application/rss+xml; charset=utf-8";
        status  = 200;

        snac_debug(&snac, 1, xs_fmt("serving RSS"));
    }
    else
        status = 404;

    user_free(&snac);

    if (valid_status(status) && *ctype == NULL) {
        *ctype = "text/html; charset=utf-8";
    }

    return status;
}


int html_post_handler(const xs_dict *req, const char *q_path,
                      char *payload, int p_size,
                      char **body, int *b_size, char **ctype)
{
    (void)p_size;
    (void)ctype;

    int status = 0;
    snac snac;
    char *uid, *p_path;
    xs_dict *p_vars;

    xs *l = xs_split_n(q_path, "/", 2);

    uid = xs_list_get(l, 1);
    if (!uid || !user_open(&snac, uid)) {
        /* invalid user */
        srv_debug(1, xs_fmt("html_post_handler bad user %s", uid));
        return 404;
    }

    p_path = xs_list_get(l, 2);

    /* all posts must be authenticated */
    if (!login(&snac, req)) {
        user_free(&snac);
        *body  = xs_dup(uid);
        return 401;
    }

    p_vars = xs_dict_get(req, "p_vars");

#if 0
    xs_json_dump(p_vars, 4, stdout);
#endif

    if (p_path && strcmp(p_path, "admin/note") == 0) { /** **/
        /* post note */
        xs_str *content      = xs_dict_get(p_vars, "content");
        xs_str *in_reply_to  = xs_dict_get(p_vars, "in_reply_to");
        xs_str *attach_url   = xs_dict_get(p_vars, "attach_url");
        xs_list *attach_file = xs_dict_get(p_vars, "attach");
        xs_str *to           = xs_dict_get(p_vars, "to");
        xs_str *sensitive    = xs_dict_get(p_vars, "sensitive");
        xs_str *summary      = xs_dict_get(p_vars, "summary");
        xs_str *edit_id      = xs_dict_get(p_vars, "edit_id");
        xs_str *alt_text     = xs_dict_get(p_vars, "alt_text");
        int priv             = !xs_is_null(xs_dict_get(p_vars, "mentioned_only"));
        xs *attach_list      = xs_list_new();

        /* default alt text */
        if (xs_is_null(alt_text))
            alt_text = "";

        /* is attach_url set? */
        if (!xs_is_null(attach_url) && *attach_url != '\0') {
            xs *l = xs_list_new();

            l = xs_list_append(l, attach_url);
            l = xs_list_append(l, alt_text);

            attach_list = xs_list_append(attach_list, l);
        }

        /* is attach_file set? */
        if (!xs_is_null(attach_file) && xs_type(attach_file) == XSTYPE_LIST) {
            char *fn = xs_list_get(attach_file, 0);

            if (*fn != '\0') {
                char *ext = strrchr(fn, '.');
                xs *hash  = xs_md5_hex(fn, strlen(fn));
                xs *id    = xs_fmt("%s%s", hash, ext);
                xs *url   = xs_fmt("%s/s/%s", snac.actor, id);
                int fo    = xs_number_get(xs_list_get(attach_file, 1));
                int fs    = xs_number_get(xs_list_get(attach_file, 2));

                /* store */
                static_put(&snac, id, payload + fo, fs);

                xs *l = xs_list_new();

                l = xs_list_append(l, url);
                l = xs_list_append(l, alt_text);

                attach_list = xs_list_append(attach_list, l);
            }
        }

        if (content != NULL) {
            xs *msg       = NULL;
            xs *c_msg     = NULL;
            xs *content_2 = xs_replace(content, "\r", "");
            xs *poll_opts = NULL;

            /* is there a valid set of poll options? */
            const char *v = xs_dict_get(p_vars, "poll_options");
            if (!xs_is_null(v) && *v) {
                xs *v2 = xs_strip_i(xs_replace(v, "\r", ""));

                poll_opts = xs_split(v2, "\n");
            }

            if (!xs_is_null(poll_opts) && xs_list_len(poll_opts)) {
                /* get the rest of poll configuration */
                const char *p_multiple = xs_dict_get(p_vars, "poll_multiple");
                const char *p_end_secs = xs_dict_get(p_vars, "poll_end_secs");
                int multiple = 0;

                int end_secs = atoi(!xs_is_null(p_end_secs) ? p_end_secs : "60");

                if (!xs_is_null(p_multiple) && strcmp(p_multiple, "on") == 0)
                    multiple = 1;

                msg = msg_question(&snac, content_2, attach_list,
                                   poll_opts, multiple, end_secs);

                enqueue_close_question(&snac, xs_dict_get(msg, "id"), end_secs);
            }
            else
                msg = msg_note(&snac, content_2, to, in_reply_to, attach_list, priv);

            if (sensitive != NULL) {
                msg = xs_dict_set(msg, "sensitive", xs_stock_true);
                msg = xs_dict_set(msg, "summary",   xs_is_null(summary) ? "..." : summary);
            }

            if (xs_is_null(edit_id)) {
                /* new message */
                c_msg = msg_create(&snac, msg);
                timeline_add(&snac, xs_dict_get(msg, "id"), msg);
            }
            else {
                /* an edition of a previous message */
                xs *p_msg = NULL;

                if (valid_status(object_get(edit_id, &p_msg))) {
                    /* copy relevant fields from previous version */
                    char *fields[] = { "id", "context", "url", "published",
                                       "to", "inReplyTo", NULL };
                    int n;

                    for (n = 0; fields[n]; n++) {
                        char *v = xs_dict_get(p_msg, fields[n]);
                        msg = xs_dict_set(msg, fields[n], v);
                    }

                    /* set the updated field */
                    xs *updated = xs_str_utctime(0, ISO_DATE_SPEC);
                    msg = xs_dict_set(msg, "updated", updated);

                    /* overwrite object, not updating the indexes */
                    object_add_ow(edit_id, msg);

                    /* update message */
                    c_msg = msg_update(&snac, msg);
                }
                else
                    snac_log(&snac, xs_fmt("cannot get object '%s' for editing", edit_id));
            }

            if (c_msg != NULL)
                enqueue_message(&snac, c_msg);

            history_del(&snac, "timeline.html_");
        }

        status = 303;
    }
    else
    if (p_path && strcmp(p_path, "admin/action") == 0) { /** **/
        /* action on an entry */
        char *id     = xs_dict_get(p_vars, "id");
        char *actor  = xs_dict_get(p_vars, "actor");
        char *action = xs_dict_get(p_vars, "action");
        char *group  = xs_dict_get(p_vars, "group");

        if (action == NULL)
            return 404;

        snac_debug(&snac, 1, xs_fmt("web action '%s' received", action));

        status = 303;

        if (strcmp(action, L("Like")) == 0) { /** **/
            xs *msg = msg_admiration(&snac, id, "Like");

            if (msg != NULL) {
                enqueue_message(&snac, msg);
                timeline_admire(&snac, xs_dict_get(msg, "object"), snac.actor, 1);
            }
        }
        else
        if (strcmp(action, L("Boost")) == 0) { /** **/
            xs *msg = msg_admiration(&snac, id, "Announce");

            if (msg != NULL) {
                enqueue_message(&snac, msg);
                timeline_admire(&snac, xs_dict_get(msg, "object"), snac.actor, 0);
            }
        }
        else
        if (strcmp(action, L("MUTE")) == 0) { /** **/
            mute(&snac, actor);
        }
        else
        if (strcmp(action, L("Unmute")) == 0) { /** **/
            unmute(&snac, actor);
        }
        else
        if (strcmp(action, L("Hide")) == 0) { /** **/
            hide(&snac, id);
        }
        else
        if (strcmp(action, L("Limit")) == 0) { /** **/
            limit(&snac, actor);
        }
        else
        if (strcmp(action, L("Unlimit")) == 0) { /** **/
            unlimit(&snac, actor);
        }
        else
        if (strcmp(action, L("Follow")) == 0) { /** **/
            xs *msg = msg_follow(&snac, actor);

            if (msg != NULL) {
                /* reload the actor from the message, in may be different */
                actor = xs_dict_get(msg, "object");

                following_add(&snac, actor, msg);

                enqueue_output_by_actor(&snac, msg, actor, 0);
            }
        }
        else
        if (strcmp(action, L("Unfollow")) == 0) { /** **/
            /* get the following object */
            xs *object = NULL;

            if (valid_status(following_get(&snac, actor, &object))) {
                xs *msg = msg_undo(&snac, xs_dict_get(object, "object"));

                following_del(&snac, actor);

                enqueue_output_by_actor(&snac, msg, actor, 0);

                snac_log(&snac, xs_fmt("unfollowed actor %s", actor));
            }
            else
                snac_log(&snac, xs_fmt("actor is not being followed %s", actor));
        }
        else
        if (strcmp(action, L("Follow Group")) == 0) { /** **/
            xs *msg = msg_follow(&snac, group);

            if (msg != NULL) {
                /* reload the group from the message, in may be different */
                group = xs_dict_get(msg, "object");

                following_add(&snac, group, msg);

                enqueue_output_by_actor(&snac, msg, group, 0);
            }
        }
        else
        if (strcmp(action, L("Unfollow Group")) == 0) { /** **/
            /* get the following object */
            xs *object = NULL;

            if (valid_status(following_get(&snac, group, &object))) {
                xs *msg = msg_undo(&snac, xs_dict_get(object, "object"));

                following_del(&snac, group);

                enqueue_output_by_actor(&snac, msg, group, 0);

                snac_log(&snac, xs_fmt("unfollowed group %s", group));
            }
            else
                snac_log(&snac, xs_fmt("actor is not being followed %s", actor));
        }
        else
        if (strcmp(action, L("Delete")) == 0) { /** **/
            char *actor_form = xs_dict_get(p_vars, "actor-form");
            if (actor_form != NULL) {
                /* delete follower */
                if (valid_status(follower_del(&snac, actor)))
                    snac_log(&snac, xs_fmt("deleted follower %s", actor));
                else
                    snac_log(&snac, xs_fmt("error deleting follower %s", actor));
            }
            else {
                /* delete an entry */
                if (xs_startswith(id, snac.actor)) {
                    /* it's a post by us: generate a delete */
                    xs *msg = msg_delete(&snac, id);

                    enqueue_message(&snac, msg);

                    snac_log(&snac, xs_fmt("posted tombstone for %s", id));
                }

                timeline_del(&snac, id);

                snac_log(&snac, xs_fmt("deleted entry %s", id));
            }
        }
        else
        if (strcmp(action, L("Pin")) == 0) { /** **/
            pin(&snac, id);
            timeline_touch(&snac);
        }
        else
        if (strcmp(action, L("Unpin")) == 0) { /** **/
            unpin(&snac, id);
            timeline_touch(&snac);
        }
        else
            status = 404;

        /* delete the cached timeline */
        if (status == 303)
            history_del(&snac, "timeline.html_");
    }
    else
    if (p_path && strcmp(p_path, "admin/user-setup") == 0) { /** **/
        /* change of user data */
        char *v;
        char *p1, *p2;

        if ((v = xs_dict_get(p_vars, "name")) != NULL)
            snac.config = xs_dict_set(snac.config, "name", v);
        if ((v = xs_dict_get(p_vars, "avatar")) != NULL)
            snac.config = xs_dict_set(snac.config, "avatar", v);
        if ((v = xs_dict_get(p_vars, "bio")) != NULL)
            snac.config = xs_dict_set(snac.config, "bio", v);
        if ((v = xs_dict_get(p_vars, "cw")) != NULL &&
            strcmp(v, "on") == 0) {
            snac.config = xs_dict_set(snac.config, "cw", "open");
        } else { /* if the checkbox is not set, the parameter is missing */
            snac.config = xs_dict_set(snac.config, "cw", "");
        }
        if ((v = xs_dict_get(p_vars, "email")) != NULL)
            snac.config = xs_dict_set(snac.config, "email", v);
        if ((v = xs_dict_get(p_vars, "telegram_bot")) != NULL)
            snac.config = xs_dict_set(snac.config, "telegram_bot", v);
        if ((v = xs_dict_get(p_vars, "telegram_chat_id")) != NULL)
            snac.config = xs_dict_set(snac.config, "telegram_chat_id", v);
        if ((v = xs_dict_get(p_vars, "purge_days")) != NULL) {
            xs *days    = xs_number_new(atof(v));
            snac.config = xs_dict_set(snac.config, "purge_days", days);
        }
        if ((v = xs_dict_get(p_vars, "drop_dm_from_unknown")) != NULL && strcmp(v, "on") == 0)
            snac.config = xs_dict_set(snac.config, "drop_dm_from_unknown", xs_stock_true);
        else
            snac.config = xs_dict_set(snac.config, "drop_dm_from_unknown", xs_stock_false);
        if ((v = xs_dict_get(p_vars, "bot")) != NULL && strcmp(v, "on") == 0)
            snac.config = xs_dict_set(snac.config, "bot", xs_stock_true);
        else
            snac.config = xs_dict_set(snac.config, "bot", xs_stock_false);
        if ((v = xs_dict_get(p_vars, "private")) != NULL && strcmp(v, "on") == 0)
            snac.config = xs_dict_set(snac.config, "private", xs_stock_true);
        else
            snac.config = xs_dict_set(snac.config, "private", xs_stock_false);
        if ((v = xs_dict_get(p_vars, "metadata")) != NULL) {
            /* split the metadata and store it as a dict */
            xs_dict *md = xs_dict_new();
            xs *l = xs_split(v, "\n");
            xs_list *p = l;
            xs_str *kp;

            while (xs_list_iter(&p, &kp)) {
                xs *kpl = xs_split_n(kp, "=", 1);
                if (xs_list_len(kpl) == 2) {
                    xs *k2 = xs_strip_i(xs_dup(xs_list_get(kpl, 0)));
                    xs *v2 = xs_strip_i(xs_dup(xs_list_get(kpl, 1)));

                    md = xs_dict_set(md, k2, v2);
                }
            }

            snac.config = xs_dict_set(snac.config, "metadata", md);
        }

        /* uploads */
        const char *uploads[] = { "avatar", "header", NULL };
        int n;
        for (n = 0; uploads[n]; n++) {
            xs *var_name = xs_fmt("%s_file", uploads[n]);

            xs_list *uploaded_file = xs_dict_get(p_vars, var_name);
            if (xs_type(uploaded_file) == XSTYPE_LIST) {
                const char *fn = xs_list_get(uploaded_file, 0);

                if (fn && *fn) {
                    const char *mimetype = xs_mime_by_ext(fn);

                    if (xs_startswith(mimetype, "image/")) {
                        const char *ext = strrchr(fn, '.');
                        xs *hash        = xs_md5_hex(fn, strlen(fn));
                        xs *id          = xs_fmt("%s%s", hash, ext);
                        xs *url         = xs_fmt("%s/s/%s", snac.actor, id);
                        int fo          = xs_number_get(xs_list_get(uploaded_file, 1));
                        int fs          = xs_number_get(xs_list_get(uploaded_file, 2));

                        /* store */
                        static_put(&snac, id, payload + fo, fs);

                        snac.config = xs_dict_set(snac.config, uploads[n], url);
                    }
                }
            }
        }

        /* password change? */
        if ((p1 = xs_dict_get(p_vars, "passwd1")) != NULL &&
            (p2 = xs_dict_get(p_vars, "passwd2")) != NULL &&
            *p1 && strcmp(p1, p2) == 0) {
            xs *pw = hash_password(snac.uid, p1, NULL);
            snac.config = xs_dict_set(snac.config, "passwd", pw);
        }

        xs *fn  = xs_fmt("%s/user.json", snac.basedir);
        xs *bfn = xs_fmt("%s.bak", fn);
        FILE *f;

        rename(fn, bfn);

        if ((f = fopen(fn, "w")) != NULL) {
            xs_json_dump(snac.config, 4, f);
            fclose(f);
        }
        else
            rename(bfn, fn);

        history_del(&snac, "timeline.html_");

        xs *a_msg = msg_actor(&snac);
        xs *u_msg = msg_update(&snac, a_msg);

        enqueue_message(&snac, u_msg);

        status = 303;
    }
    else
    if (p_path && strcmp(p_path, "admin/clear-notifications") == 0) { /** **/
        notify_clear(&snac);
        timeline_touch(&snac);

        status = 303;
    }
    else
    if (p_path && strcmp(p_path, "admin/vote") == 0) { /** **/
        char *irt         = xs_dict_get(p_vars, "irt");
        const char *opt   = xs_dict_get(p_vars, "question");
        const char *actor = xs_dict_get(p_vars, "actor");

        xs *ls = NULL;

        /* multiple choices? */
        if (xs_type(opt) == XSTYPE_LIST)
            ls = xs_dup(opt);
        else {
            ls = xs_list_new();
            ls = xs_list_append(ls, opt);
        }

        xs_list *p = ls;
        xs_str *v;

        while (xs_list_iter(&p, &v)) {
            xs *msg = msg_note(&snac, "", actor, irt, NULL, 1);

            /* set the option */
            msg = xs_dict_append(msg, "name", v);

            xs *c_msg = msg_create(&snac, msg);

            enqueue_message(&snac, c_msg);

            timeline_add(&snac, xs_dict_get(msg, "id"), msg);
        }

        status = 303;
    }

    if (status == 303) {
        char *redir = xs_dict_get(p_vars, "redir");

        if (xs_is_null(redir))
            redir = "top";

        *body   = xs_fmt("%s/admin#%s", snac.actor, redir);
        *b_size = strlen(*body);
    }

    user_free(&snac);

    return status;
}
