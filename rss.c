/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2025 grunfink et al. / MIT license */

#include "xs.h"
#include "xs_html.h"
#include "xs_regex.h"
#include "xs_time.h"

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
