/* copyright (c) 2022 - 2023 grunfink et al. / MIT license */

#ifndef _XS_JSON_H

#define _XS_JSON_H

int xs_json_dump_pp(const xs_val *data, int indent, FILE *f);
xs_str *xs_json_dumps_pp(const xs_val *data, int indent);
#define xs_json_dumps(data) xs_json_dumps_pp(data, 0)
#define xs_json_dump(data, f) xs_json_dumps_pp(data, 0, f)
xs_val *xs_json_loads(const xs_str *json);
xs_val *xs_json_load(FILE *f);


#ifdef XS_IMPLEMENTATION

/** IMPLEMENTATION **/

/** JSON dumps **/

static void _xs_json_dump_str(const char *data, FILE *f)
/* dumps a string in JSON format */
{
    unsigned char c;
    fputs("\"", f);

    while ((c = *data)) {
        if (c == '\n')
            fputs("\\n", f);
        else
        if (c == '\r')
            fputs("\\r", f);
        else
        if (c == '\t')
            fputs("\\t", f);
        else
        if (c == '\\')
            fputs("\\\\", f);
        else
        if (c == '"')
            fputs("\\\"", f);
        else
        if (c < 32)
            fprintf(f, "\\u%04x", (unsigned int) c);
        else
            fputc(c, f);

        data++;
    }

    fputs("\"", f);
}


static void _xs_json_indent(int level, int indent, FILE *f)
/* adds indentation */
{
    if (indent) {
        int n;

        fputc('\n', f);

        for (n = 0; n < level * indent; n++)
            fputc(' ', f);
    }
}


static void _xs_json_dump(const xs_val *s_data, int level, int indent, FILE *f)
/* dumps partial data as JSON */
{
    int c = 0;
    xs_val *v;
    xs_val *data = (xs_val *)s_data;

    switch (xs_type(data)) {
    case XSTYPE_NULL:
        fputs("null", f);
        break;

    case XSTYPE_TRUE:
        fputs("true", f);
        break;

    case XSTYPE_FALSE:
        fputs("false", f);
        break;

    case XSTYPE_NUMBER:
        fputs(xs_number_str(data), f);
        break;

    case XSTYPE_LIST:
        fputc('[', f);

        while (xs_list_iter(&data, &v)) {
            if (c != 0)
                fputc(',', f);

            _xs_json_indent(level + 1, indent, f);
            _xs_json_dump(v, level + 1, indent, f);

            c++;
        }

        _xs_json_indent(level, indent, f);
        fputc(']', f);

        break;

    case XSTYPE_DICT:
        fputc('{', f);

        xs_str *k;
        while (xs_dict_iter(&data, &k, &v)) {
            if (c != 0)
                fputc(',', f);

            _xs_json_indent(level + 1, indent, f);

            _xs_json_dump_str(k, f);
            fputc(':', f);

            if (indent)
                fputc(' ', f);

            _xs_json_dump(v, level + 1, indent, f);

            c++;
        }

        _xs_json_indent(level, indent, f);
        fputc('}', f);
        break;

    case XSTYPE_STRING:
        _xs_json_dump_str(data, f);
        break;

    default:
        break;
    }
}


xs_str *xs_json_dumps_pp(const xs_val *data, int indent)
/* dumps data as a JSON string */
{
    xs_str *s = NULL;
    size_t sz;
    FILE *f;

    if ((f = open_memstream(&s, &sz)) != NULL) {
        int r = xs_json_dump_pp(data, indent, f);
        fclose(f);

        if (!r)
            s = xs_free(s);
    }

    return s;
}


int xs_json_dump_pp(const xs_val *data, int indent, FILE *f)
/* dumps data into a file as JSON */
{
    xstype t = xs_type(data);

    if (t == XSTYPE_LIST || t == XSTYPE_DICT) {
        _xs_json_dump(data, 0, indent, f);
        return 1;
    }

    return 0;
}


/** JSON loads **/

/* this code comes mostly from the Minimum Profit Text Editor (MPDM) */

typedef enum {
    JS_ERROR = -1,
    JS_INCOMPLETE,
    JS_OCURLY,
    JS_OBRACK,
    JS_CCURLY,
    JS_CBRACK,
    JS_COMMA,
    JS_COLON,
    JS_VALUE,
    JS_STRING,
    JS_INTEGER,
    JS_REAL,
    JS_TRUE,
    JS_FALSE,
    JS_NULL,
    JS_ARRAY,
    JS_OBJECT
} js_type;


static xs_val *_xs_json_loads_lexer(const char **json, js_type *t)
{
    char c;
    const char *s = *json;
    xs_val *v = NULL;

    /* skip blanks */
    while (*s == L' ' || *s == L'\t' || *s == L'\n' || *s == L'\r')
        s++;

    c = *s++;

    if (c == '{')
        *t = JS_OCURLY;
    else
    if (c == '}')
        *t = JS_CCURLY;
    else
    if (c == '[')
        *t = JS_OBRACK;
    else
    if (c == ']')
        *t = JS_CBRACK;
    else
    if (c == ',')
        *t = JS_COMMA;
    else
    if (c == ':')
        *t = JS_COLON;
    else
    if (c == '"') {
        *t = JS_STRING;

        v = xs_str_new(NULL);

        while ((c = *s) != '"' && c != '\0') {
            char tmp[5];
            int cp, i;

            if (c == '\\') {
                s++;
                c = *s;
                switch (c) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u': /* Unicode codepoint as an hex char */
                    s++;
                    strncpy(tmp, s, 4);
                    tmp[4] = '\0';

                    if (strlen(tmp) != 4) {
                        *t = JS_ERROR;
                        break;
                    }

                    s += 3; /* skip as it was one byte */

                    sscanf(tmp, "%04x", &i);

                    if (i >= 0xd800 && i <= 0xdfff) {
                        /* it's a surrogate pair */
                        cp = (i & 0x3ff) << 10;

                        /* skip to the next value (last char + \ + u)  */
                        s++;
                        if (memcmp(s, "\\u", 2) != 0) {
                            *t = JS_ERROR;
                            break;
                        }
                        s += 2;

                        strncpy(tmp, s, 4);
                        tmp[4] = '\0';

                        if (strlen(tmp) != 4) {
                            *t = JS_ERROR;
                            break;
                        }

                        s += 3; /* skip as it was one byte */

                        sscanf(tmp, "%04x", &i);
                        cp |= (i & 0x3ff);
                        cp += 0x10000;
                    }
                    else
                        cp = i;

                    /* replace dangerous control codes with their visual representations */
                    if (cp >= '\0' && cp < ' ' && !strchr("\r\n\t", cp))
                        cp += 0x2400;

                    v = xs_utf8_enc(v, cp);
                    c = '\0';

                    break;
                }
            }

            if (c)
                v = xs_append_m(v, &c, 1);

            s++;
        }

        if (c != '\0')
            s++;
    }
    else
    if (c == '-' || (c >= '0' && c <= '9') || c == '.') {
        xs *vn = NULL;

        *t = JS_INTEGER;

        vn = xs_str_new(NULL);
        vn = xs_append_m(vn, &c, 1);

        while (((c = *s) >= '0' && c <= '9') || c == '.') {
            if (c == '.')
                *t = JS_REAL;

            vn = xs_append_m(vn, &c, 1);
            s++;
        }

        /* convert to XSTYPE_NUMBER */
        v = xs_number_new(atof(vn));
    }
    else
    if (c == 't' && strncmp(s, "rue", 3) == 0) {
        s += 3;
        *t = JS_TRUE;

        v = xs_val_new(XSTYPE_TRUE);
    }
    else
    if (c == 'f' && strncmp(s, "alse", 4) == 0) {
        s += 4;
        *t = JS_FALSE;

        v = xs_val_new(XSTYPE_FALSE);
    }
    else
    if (c == 'n' && strncmp(s, "ull", 3) == 0) {
        s += 3;
        *t = JS_NULL;

        v = xs_val_new(XSTYPE_NULL);
    }
    else
        *t = JS_ERROR;

    *json = s;

    return v;
}


static xs_list *_xs_json_loads_array(const char **json, js_type *t);
static xs_dict *_xs_json_loads_object(const char **json, js_type *t);

static xs_val *_xs_json_loads_value(const char **json, js_type *t, xs_val *v)
/* parses a JSON value */
{
    if (*t == JS_OBRACK)
        v = _xs_json_loads_array(json, t);
    else
    if (*t == JS_OCURLY)
        v = _xs_json_loads_object(json, t);

    if (*t >= JS_VALUE)
        *t = JS_VALUE;
    else
        *t = JS_ERROR;

    return v;
}


static xs_list *_xs_json_loads_array(const char **json, js_type *t)
/* parses a JSON array */
{
    const char *s = *json;
    xs *v;
    xs_list *l;
    js_type tt;

    l = xs_list_new();

    *t = JS_INCOMPLETE;

    v = _xs_json_loads_lexer(&s, &tt);

    if (tt == JS_CBRACK)
        *t = JS_ARRAY;
    else {
        v = _xs_json_loads_value(&s, &tt, v);

        if (tt == JS_VALUE) {
            l = xs_list_append(l, v);

            while (*t == JS_INCOMPLETE) {
                xs_free(_xs_json_loads_lexer(&s, &tt));

                if (tt == JS_CBRACK)
                    *t = JS_ARRAY;
                else
                if (tt == JS_COMMA) {
                    xs *v2;

                    v2 = _xs_json_loads_lexer(&s, &tt);
                    v2 = _xs_json_loads_value(&s, &tt, v2);

                    if (tt == JS_VALUE)
                        l = xs_list_append(l, v2);
                    else
                        *t = JS_ERROR;
                }
                else
                    *t = JS_ERROR;
            }
        }
        else
            *t = JS_ERROR;
    }

    if (*t == JS_ERROR)
        l = xs_free(l);

    *json = s;

    return l;
}


static xs_dict *_xs_json_loads_object(const char **json, js_type *t)
/* parses a JSON object */
{
    const char *s = *json;
    xs *k1;
    xs_dict *d;
    js_type tt;

    d = xs_dict_new();

    *t = JS_INCOMPLETE;

    k1 = _xs_json_loads_lexer(&s, &tt);

    if (tt == JS_CCURLY)
        *t = JS_OBJECT;
    else
    if (tt == JS_STRING) {
        xs_free(_xs_json_loads_lexer(&s, &tt));

        if (tt == JS_COLON) {
            xs *v1;

            v1 = _xs_json_loads_lexer(&s, &tt);
            v1 = _xs_json_loads_value(&s, &tt, v1);

            if (tt == JS_VALUE) {
                d = xs_dict_append(d, k1, v1);

                while (*t == JS_INCOMPLETE) {
                    xs_free(_xs_json_loads_lexer(&s, &tt));

                    if (tt == JS_CCURLY)
                        *t = JS_OBJECT;
                    else
                    if (tt == JS_COMMA) {
                        xs *k = _xs_json_loads_lexer(&s, &tt);

                        if (tt == JS_STRING) {
                            xs_free(_xs_json_loads_lexer(&s, &tt));

                            if (tt == JS_COLON) {
                                xs *v;

                                v = _xs_json_loads_lexer(&s, &tt);
                                v = _xs_json_loads_value(&s, &tt, v);

                                if (tt == JS_VALUE)
                                    d = xs_dict_append(d, k, v);
                                else
                                    *t = JS_ERROR;
                            }
                            else
                                *t = JS_ERROR;
                        }
                        else
                            *t = JS_ERROR;
                    }
                    else
                        *t = JS_ERROR;
                }
            }
            else
                *t = JS_ERROR;
        }
        else
            *t = JS_ERROR;
    }
    else
        *t = JS_ERROR;

    if (*t == JS_ERROR)
        d = xs_free(d);

    *json = s;

    return d;
}


xs_val *xs_json_loads(const xs_str *json)
/* loads a string in JSON format and converts to a multiple data */
{
    xs_val *v = NULL;
    js_type t;

    xs_free(_xs_json_loads_lexer(&json, &t));

    if (t == JS_OBRACK)
        v = _xs_json_loads_array(&json, &t);
    else
    if (t == JS_OCURLY)
        v = _xs_json_loads_object(&json, &t);
    else
        t = JS_ERROR;

    return v;
}


xs_val *xs_json_load(FILE *f)
/* loads a JSON file */
{
    xs *o = xs_readall(f);
    return o ? xs_json_loads(o) : NULL;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_JSON_H */
