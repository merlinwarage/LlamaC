/* json.h - Tiny JSON DOM parser (portable) */
#ifndef LLAMAC_JSON_H
#define LLAMAC_JSON_H

/* ======================= tiny JSON DOM ======================= */
typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JT;
typedef struct J { JT t; double num; char *str; char *key; struct J *child, *next; } J;

static void j_free(J *j) {
    while (j) { J *n = j->next; j_free(j->child); free(j->str); free(j->key); free(j); j = n; }
}
static J *j_get(J *o, const char *k) {
    if (!o || o->t != J_OBJ) return NULL;
    for (J *c = o->child; c; c = c->next) if (c->key && !strcmp(c->key, k)) return c;
    return NULL;
}
static J *j_idx(J *a, int i) {
    if (!a || a->t != J_ARR) return NULL;
    J *c = a->child; while (c && i--) c = c->next; return c;
}
static const char *j_str(J *j) { return (j && j->t == J_STR) ? j->str : NULL; }
static double j_num(J *j, double d) { return (j && j->t == J_NUM) ? j->num : d; }

static void jws(const char **p) { while (**p == ' ' || **p == '\n' || **p == '\r' || **p == '\t') (*p)++; }
static void put_utf8(SB *s, unsigned cp) {
    if (cp < 0x80) sb_putc(s, (char)cp);
    else if (cp < 0x800) { sb_putc(s, (char)(0xC0 | (cp >> 6))); sb_putc(s, (char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { sb_putc(s, (char)(0xE0 | (cp >> 12))); sb_putc(s, (char)(0x80 | ((cp >> 6) & 0x3F))); sb_putc(s, (char)(0x80 | (cp & 0x3F))); }
    else { sb_putc(s, (char)(0xF0 | (cp >> 18))); sb_putc(s, (char)(0x80 | ((cp >> 12) & 0x3F))); sb_putc(s, (char)(0x80 | ((cp >> 6) & 0x3F))); sb_putc(s, (char)(0x80 | (cp & 0x3F))); }
}
static int hex4(const char *p, unsigned *out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i]; v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    *out = v; return 1;
}
static char *parse_str(const char **p) {
    SB s = {0}; (*p)++;
    while (**p && **p != '"') {
        if (**p == '\\') {
            (*p)++;
            switch (**p) {
            case 'n': sb_putc(&s, '\n'); break;
            case 't': sb_putc(&s, '\t'); break;
            case 'r': sb_putc(&s, '\r'); break;
            case 'b': sb_putc(&s, '\b'); break;
            case 'f': sb_putc(&s, '\f'); break;
            case '/': sb_putc(&s, '/'); break;
            case '\\': sb_putc(&s, '\\'); break;
            case '"': sb_putc(&s, '"'); break;
            case 'u': {
                unsigned cp, lo;
                if (!hex4(*p + 1, &cp)) { free(s.p); return NULL; }
                *p += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && (*p)[1] == '\\' && (*p)[2] == 'u' && hex4(*p + 3, &lo) && lo >= 0xDC00 && lo < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); *p += 6;
                }
                put_utf8(&s, cp); break;
            }
            default: free(s.p); return NULL;
            }
            if (**p) (*p)++;
        } else sb_putc(&s, *(*p)++);
    }
    if (**p != '"') { free(s.p); return NULL; }
    (*p)++;
    if (!s.p) return xstrdup("");
    return s.p;
}
static J *jnew(JT t) { J *j = (J *)calloc(1, sizeof(J)); j->t = t; return j; }
static J *parse_val(const char **p, int depth) {
    jws(p);
    if (depth > 64) return NULL;
    char c = **p;
    if (c == '{' || c == '[') {
        J *n = jnew(c == '{' ? J_OBJ : J_ARR); char close = (c == '{') ? '}' : ']'; J *tail = NULL;
        (*p)++; jws(p);
        if (**p == close) { (*p)++; return n; }
        for (;;) {
            char *key = NULL; J *v;
            jws(p);
            if (c == '{') {
                if (**p != '"') goto bad;
                key = parse_str(p); if (!key) goto bad;
                jws(p);
                if (**p != ':') { free(key); goto bad; }
                (*p)++;
            }
            v = parse_val(p, depth + 1);
            if (!v) { free(key); goto bad; }
            v->key = key;
            if (tail) tail->next = v; else n->child = v;
            tail = v; jws(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == close) { (*p)++; return n; }
            goto bad;
        }
    bad:
        j_free(n); return NULL;
    }
    if (c == '"') { char *s = parse_str(p); if (!s) return NULL; J *n = jnew(J_STR); n->str = s; return n; }
    if (!strncmp(*p, "true", 4)) { *p += 4; J *n = jnew(J_BOOL); n->num = 1; return n; }
    if (!strncmp(*p, "false", 5)) { *p += 5; return jnew(J_BOOL); }
    if (!strncmp(*p, "null", 4)) { *p += 4; return jnew(J_NULL); }
    { char *e; double d = strtod(*p, &e); if (e == *p) return NULL; *p = e; J *n = jnew(J_NUM); n->num = d; return n; }
}
static J *json_parse(const char *s) { const char *p = s; return parse_val(&p, 0); }
static J *json_parse_strict(const char *s) {
    const char *p = s; J *j = parse_val(&p, 0);
    if (!j) return NULL;
    jws(&p);
    if (*p) { j_free(j); return NULL; }
    return j;
}
static void fmt_num(char *b, double v) {
    if (v == floor(v) && fabs(v) < 1e15) sprintf(b, "%.0f", v); else sprintf(b, "%g", v);
}

#endif /* LLAMAC_JSON_H */
