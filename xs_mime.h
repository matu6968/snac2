/* copyright (c) 2022 - 2024 grunfink et al. / MIT license */

#ifndef _XS_MIME_H

#define _XS_MIME_H

const char *xs_mime_by_ext(const char *file);
const char *xs_ext_by_mime(const char *mime);

extern const char *xs_mime_types[];

#ifdef XS_IMPLEMENTATION

/* intentionally brain-dead simple */
/* CAUTION: sorted by extension */

const char *xs_mime_types[] = {
    "3gp",      "video/3gpp",
    "aac",      "audio/aac",
    "audio",    "audio/unknown",
    "avif",     "image/avif",
    "css",      "text/css",
    "flac",     "audio/flac",
    "flv",      "video/flv",
    "gif",      "image/gif",
    "gmi",      "text/gemini",
    "html",     "text/html",
    "image",    "image/unknown",
    "jpeg",     "image/jpeg",
    "jpg",      "image/jpeg",
    "json",     "application/json",
    "m4a",      "audio/aac",
    "m4v",      "video/mp4",
    "md",       "text/markdown",
    "media",    "media/unknown",
    "mov",      "video/quicktime",
    "mp3",      "audio/mp3",
    "mp4",      "video/mp4",
    "mpg4",     "video/mp4",
    "oga",      "audio/ogg",
    "ogg",      "audio/ogg",
    "ogv",      "video/ogg",
    "opus",     "audio/ogg",
    "png",      "image/png",
    "svg",      "image/svg+xml",
    "svgz",     "image/svg+xml",
    "txt",      "text/plain",
    "video",    "video/unknown",
    "wav",      "audio/wav",
    "webm",     "video/webm",
    "webp",     "image/webp",
    "wma",      "audio/wma",
    "xml",      "text/xml",
    NULL,       NULL,
};

/* reverse table, sorted by mime */
const char *xs_mime_extensions[] = {
    "application/json", "json",
    "audio/aac",        "aac",
    "audio/flac",       "flac",
    "audio/mp3",        "mp3",
    "audio/mp4",        "m4a",
    "audio/mpeg",       "mp3",
    "audio/ogg",        "ogg",
    "audio/wav",        "wav",
    "audio/wma",        "wma",
    "image/avif",       "avif",
    "image/gif",        "gif",
    "image/jpeg",       "jpg",
    "image/png",        "png",
    "image/svg+xml",    "svg",
    "image/webp",       "webp",
    "media/mp4",        "mp4",
    "media/ogg",        "ogv",
    "text/css",         "css",
    "text/gemini",      "gmi",
    "text/html",        "html",
    "text/markdown",    "md",
    "text/plain",       "txt",
    "text/xml",         "xml",
    "video/3gpp",       "3gp",
    "video/flv",        "flv",
    "video/mp4",        "m4v",
    "video/mp4",        "mp4",
    "video/ogg",        "ogv",
    "video/quicktime",  "mov",
    "video/webm",       "webm",
    NULL,               NULL,
};


const char *xs_mime_by_ext(const char *file)
/* returns the MIME type by file extension */
{
    const char *ext = strrchr(file, '.');

    if (ext) {
        xs *uext = xs_tolower_i(xs_dup(ext + 1));
        int b = 0;
        int t = xs_countof(xs_mime_types) / 2 - 2;

        while (t >= b) {
            int n = (b + t) / 2;
            const char *p = xs_mime_types[n * 2];

            int c = strcmp(uext, p);

            if (c < 0)
                t = n - 1;
            else
            if (c > 0)
                b = n + 1;
            else
                return xs_mime_types[(n * 2) + 1];
        }
    }

    return "application/octet-stream";
}

const char *xs_ext_by_mime(const char *mime)
/* returns the extension type by MIME type */
{
    if (mime) {
        int b = 0;
        int t = xs_countof(xs_mime_extensions) / 2 - 2;

        while (t >= b) {
            int n = (b + t) / 2;
            const char *p = xs_mime_extensions[n * 2];

            int c = strcmp(mime, p);

            if (c < 0)
                t = n - 1;
            else
            if (c > 0)
                b = n + 1;
            else
                return xs_mime_extensions[(n * 2) + 1];
        }

        /* special extensions to determine attachment type, not real content type */
        if(xs_startswith(mime, "image/"))
            return "image";
        if(xs_startswith(mime, "video/"))
            return "video";
        if(xs_startswith(mime, "media/"))
            return "video";
        if(xs_startswith(mime, "audio/"))
            return "audio";
    }

    return NULL;
}


#endif /* XS_IMPLEMENTATION */

#endif /* XS_MIME_H */
