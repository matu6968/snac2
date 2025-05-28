/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2025 grunfink et al. / MIT license */

#include "xs.h"
#include "xs_html.h"
#include "xs_regex.h"
#include "xs_time.h"
#include "xs_match.h"
#include "xs_curl.h"

#include "snac.h"

xs_str *rss_from_timeline(snac *user, const xs_list *timeline,
                        const char *title, const char *link, const char *desc)
/* converts a timeline to rss */
{
    xs_html *rss = xs_html_tag("rss",
        xs_html_attr("xmlns:content", "http:/" "/purl.org/rss/1.0/modules/content/"),
        xs_html_attr("version",       "2.0"),
        xs_html_attr("xmlns:atom",    "http:/" "/www.w3.org/2005/Atom"));

    xs_html *channel = xs_html_tag("channel",
        xs_html_tag("title",
            xs_html_text(title)),
        xs_html_tag("language",
            xs_html_text("en")),
        xs_html_tag("link",
            xs_html_text(link)),
        xs_html_sctag("atom:link",
            xs_html_attr("href", link),
            xs_html_attr("rel", "self"),
            xs_html_attr("type", "application/rss+xml")),
        xs_html_tag("generator",
            xs_html_text(USER_AGENT)),
        xs_html_tag("description",
            xs_html_text(desc)));

    xs_html_add(rss, channel);

    int cnt = 0;
    const char *v;

    xs_list_foreach(timeline, v) {
        xs *msg = NULL;

        if (user) {
            if (!valid_status(timeline_get_by_md5(user, v, &msg)))
                continue;
        }
        else {
            if (!valid_status(object_get_by_md5(v, &msg)))
                continue;
        }

        const char *id = xs_dict_get(msg, "id");
        const char *content = xs_dict_get(msg, "content");
        const char *published = xs_dict_get(msg, "published");

        if (user && !xs_startswith(id, user->actor))
            continue;

        if (!id || !content || !published)
            continue;

        /* create a title with the first line of the content */
        xs *title = xs_replace(content, "<br>", "\n");
        title = xs_regex_replace_i(title, "<[^>]+>", " ");
        title = xs_regex_replace_i(title, "&[^;]+;", " ");
        int i;

        for (i = 0; title[i] && title[i] != '\n' && i < 50; i++);

        if (title[i] != '\0') {
            title[i] = '\0';
            title = xs_str_cat(title, "...");
        }

        title = xs_strip_i(title);

        /* convert the date */
        time_t t = xs_parse_iso_date(published, 0);
        xs *rss_date = xs_str_utctime(t, "%a, %d %b %Y %T +0000");

        /* if it's the first one, add it to the header */
        if (cnt == 0)
            xs_html_add(channel,
                xs_html_tag("lastBuildDate",
                    xs_html_text(rss_date)));

        xs_html_add(channel,
            xs_html_tag("item",
                xs_html_tag("title",
                    xs_html_text(title)),
                xs_html_tag("link",
                    xs_html_text(id)),
                xs_html_tag("guid",
                    xs_html_text(id)),
                xs_html_tag("pubDate",
                    xs_html_text(rss_date)),
                xs_html_tag("description",
                    xs_html_text(content))));

        cnt++;
    }

    return xs_html_render_s(rss, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
}


void rss_to_timeline(snac *user, const char *url)
/* reads an RSS and inserts all ActivityPub posts into the user's timeline */
{
    if (!xs_startswith(url, "https:/") && !xs_startswith(url, "http:/"))
        return;

    xs *hdrs = xs_dict_new();
    hdrs = xs_dict_set(hdrs, "accept",     "application/rss+xml");
    hdrs = xs_dict_set(hdrs, "user-agent", USER_AGENT);

    xs *payload = NULL;
    int status;
    int p_size;

    xs *rsp = xs_http_request("GET", url, hdrs, NULL, 0, &status, &payload, &p_size, 0);

    if (!valid_status(status) || !xs_is_string(payload))
        return;

    /* not an RSS? done */
    const char *ctype = xs_dict_get(rsp, "content-type");
    if (!xs_is_string(ctype) || xs_str_in(ctype, "application/rss+xml") == -1)
        return;

    snac_log(user, xs_fmt("parsing RSS %s", url));

    /* yes, parsing is done with regexes (now I have two problems blah blah blah) */
    xs *links = xs_regex_select(payload, "<link>[^<]+</link>");
    const char *link;

    xs_list_foreach(links, link) {
        xs *l = xs_replace(link, "<link>", "");
        char *p = strchr(l, '<');

        if (p == NULL)
            continue;
        *p = '\0';

        /* skip this same URL */
        if (strcmp(l, url) == 0)
            continue;

        snac_debug(user, 1, xs_fmt("RSS link: %s", l));

        if (timeline_here(user, l)) {
            snac_debug(user, 1, xs_fmt("RSS entry already in timeline %s", l));
            continue;
        }

        /* special trick for Mastodon: convert from the alternate format */
        if (strchr(l, '@') != NULL) {
            xs *l2 = xs_split(l, "/");

            if (xs_list_len(l2) == 5) {
                const char *uid = xs_list_get(l2, 3);
                if (*uid == '@') {
                    xs *guessed_id = xs_fmt("https:/" "/%s/users/%s/statuses/%s",
                        xs_list_get(l2, 2), uid + 1, xs_list_get(l2, -1));

                    if (timeline_here(user, guessed_id)) {
                        snac_debug(user, 1, xs_fmt("RSS entry already in timeline (alt) %s", guessed_id));
                        continue;
                    }
                }
            }
        }

        xs *obj = NULL;

        if (!valid_status(object_get(l, &obj))) {
            /* object is not here: bring it */
            if (!valid_status(activitypub_request(user, l, &obj)))
                continue;
        }

        if (xs_is_dict(obj)) {
            const char *id      = xs_dict_get(obj, "id");
            const char *type    = xs_dict_get(obj, "type");
            const char *attr_to = get_atto(obj);

            if (!xs_is_string(id) || !xs_is_string(type) || !xs_is_string(attr_to))
                continue;

            if (!xs_match(type, POSTLIKE_OBJECT_TYPE))
                continue;

            if (timeline_here(user, id)) {
                snac_debug(user, 1, xs_fmt("RSS entry already in timeline (id) %s", id));
                continue;
            }

            if (!valid_status(actor_request(user, attr_to, NULL)))
                continue;

            timeline_add(user, id, obj);
        }
    }
}


void rss_poll_hashtags(void)
/* parses all RSS from all users */
{
    xs *list = user_list();
    const char *uid;

    xs_list_foreach(list, uid) {
        snac user;

        if (user_open(&user, uid)) {
            const xs_list *rss = xs_dict_get(user.config, "followed_hashtags");

            if (xs_is_list(rss)) {
                const char *url;

                xs_list_foreach(rss, url)
                    rss_to_timeline(&user, url);
            }

            user_free(&user);
        }
    }
}
