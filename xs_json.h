/* copyright (c) 2022 - 2023 grunfink et al. / MIT license */

#ifndef _XS_JSON_H

#define _XS_JSON_H

int xs_json_dump_pp(const xs_val *data, int indent, FILE *f);
xs_str *xs_json_dumps_pp(const xs_val *data, int indent);
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


static xs_val *_xs_json_load_lexer(FILE *f, js_type *t)
{
    int c;
    xs_val *v = NULL;

    *t = JS_ERROR;

    /* skip blanks */
    while ((c = fgetc(f)) == L' ' || c == L'\t' || c == L'\n' || c == L'\r');

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

        while ((c = fgetc(f)) != '"' && c != EOF && *t != JS_ERROR) {
            if (c == '\\') {
                unsigned int cp = fgetc(f);

                switch (cp) {
                case 'n': cp = '\n'; break;
                case 'r': cp = '\r'; break;
                case 't': cp = '\t'; break;
                case 'u': /* Unicode codepoint as an hex char */
                    if (fscanf(f, "%04x", &cp) != 1) {
                        *t = JS_ERROR;
                        break;
                    }

                    if (cp >= 0xd800 && cp <= 0xdfff) {
                        /* it's a surrogate pair */
                        cp = (cp & 0x3ff) << 10;

                        /* \u must follow */
                        if (fgetc(f) != '\\' || fgetc(f) != 'u') {
                            *t = JS_ERROR;
                            break;
                        }

                        unsigned int i;
                        if (fscanf(f, "%04x", &i) != 1) {
                            *t = JS_ERROR;
                            break;
                        }

                        cp |= (i & 0x3ff);
                        cp += 0x10000;
                    }

                    /* replace dangerous control codes with their visual representations */
                    if (cp < ' ' && !strchr("\r\n\t", cp))
                        cp += 0x2400;

                    break;
                }

                v = xs_utf8_enc(v, cp);
            }
            else {
                char cc = c;
                v = xs_append_m(v, &cc, 1);
            }
        }
    }
    else
    if (c == '-' || (c >= '0' && c <= '9') || c == '.') {
        double d;

        ungetc(c, f);
        if (fscanf(f, "%lf", &d) == 1) {
            *t = JS_REAL;
            v = xs_number_new(d);
        }
    }
    else
    if (c == 't') {
        if (fgetc(f) == 'r' && fgetc(f) == 'u' && fgetc(f) == 'e') {
            *t = JS_TRUE;
            v = xs_val_new(XSTYPE_TRUE);
        }
    }
    else
    if (c == 'f') {
        if (fgetc(f) == 'a' && fgetc(f) == 'l' &&
            fgetc(f) == 's' && fgetc(f) == 'e') {
            *t = JS_FALSE;
            v = xs_val_new(XSTYPE_FALSE);
        }
    }
    else
    if (c == 'n') {
        if (fgetc(f) == 'u' && fgetc(f) == 'l' && fgetc(f) == 'l') {
            *t = JS_NULL;
            v = xs_val_new(XSTYPE_NULL);
        }
    }

    return v;
}


static xs_list *_xs_json_load_array(FILE *f, js_type *t);
static xs_dict *_xs_json_load_object(FILE *f, js_type *t);

static xs_val *_xs_json_load_value(FILE *f, js_type *t, xs_val *v)
/* parses a JSON value */
{
    if (*t == JS_OBRACK)
        v = _xs_json_load_array(f, t);
    else
    if (*t == JS_OCURLY)
        v = _xs_json_load_object(f, t);

    if (*t >= JS_VALUE)
        *t = JS_VALUE;
    else
        *t = JS_ERROR;

    return v;
}


static xs_list *_xs_json_load_array(FILE *f, js_type *t)
/* parses a JSON array */
{
    xs *v;
    xs_list *l;
    js_type tt;

    l = xs_list_new();

    *t = JS_INCOMPLETE;

    v = _xs_json_load_lexer(f, &tt);

    if (tt == JS_CBRACK)
        *t = JS_ARRAY;
    else {
        v = _xs_json_load_value(f, &tt, v);

        if (tt == JS_VALUE) {
            l = xs_list_append(l, v);

            while (*t == JS_INCOMPLETE) {
                xs_free(_xs_json_load_lexer(f, &tt));

                if (tt == JS_CBRACK)
                    *t = JS_ARRAY;
                else
                if (tt == JS_COMMA) {
                    xs *v2;

                    v2 = _xs_json_load_lexer(f, &tt);
                    v2 = _xs_json_load_value(f, &tt, v2);

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

    return l;
}


static xs_dict *_xs_json_load_object(FILE *f, js_type *t)
/* parses a JSON object */
{
    xs *k1;
    xs_dict *d;
    js_type tt;

    d = xs_dict_new();

    *t = JS_INCOMPLETE;

    k1 = _xs_json_load_lexer(f, &tt);

    if (tt == JS_CCURLY)
        *t = JS_OBJECT;
    else
    if (tt == JS_STRING) {
        xs_free(_xs_json_load_lexer(f, &tt));

        if (tt == JS_COLON) {
            xs *v1;

            v1 = _xs_json_load_lexer(f, &tt);
            v1 = _xs_json_load_value(f, &tt, v1);

            if (tt == JS_VALUE) {
                d = xs_dict_append(d, k1, v1);

                while (*t == JS_INCOMPLETE) {
                    xs_free(_xs_json_load_lexer(f, &tt));

                    if (tt == JS_CCURLY)
                        *t = JS_OBJECT;
                    else
                    if (tt == JS_COMMA) {
                        xs *k = _xs_json_load_lexer(f, &tt);

                        if (tt == JS_STRING) {
                            xs_free(_xs_json_load_lexer(f, &tt));

                            if (tt == JS_COLON) {
                                xs *v;

                                v = _xs_json_load_lexer(f, &tt);
                                v = _xs_json_load_value(f, &tt, v);

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

    return d;
}


xs_val *xs_json_loads(const xs_str *json)
/* loads a string in JSON format and converts to a multiple data */
{
    FILE *f;
    xs_val *v = NULL;

    if ((f = fmemopen((char *)json, strlen(json), "r")) != NULL) {
        v = xs_json_load(f);
        fclose(f);
    }

    return v;
}


xs_val *xs_json_load(FILE *f)
/* loads a JSON file */
{
    xs_val *v = NULL;
    js_type t;

    xs_free(_xs_json_load_lexer(f, &t));

    if (t == JS_OBRACK)
        v = _xs_json_load_array(f, &t);
    else
    if (t == JS_OCURLY)
        v = _xs_json_load_object(f, &t);
    else
        t = JS_ERROR;

    return v;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_JSON_H */
