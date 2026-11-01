/* util.h - String builder and string helpers (portable) */
#ifndef LLAMAC_UTIL_H
#define LLAMAC_UTIL_H

/* ======================= string builder ======================= */
typedef struct { char *p; size_t n, cap; } SB;
static void sb_add(SB *s, const char *t, size_t len) {
    if (s->n + len + 1 > s->cap) {
        size_t c = s->cap ? s->cap : 256;
        while (c < s->n + len + 1) c *= 2;
        s->p = (char *)realloc(s->p, c); s->cap = c;
    }
    memcpy(s->p + s->n, t, len); s->n += len; s->p[s->n] = 0;
}
static void sb_puts(SB *s, const char *t) { sb_add(s, t, strlen(t)); }
static void sb_putc(SB *s, char c) { sb_add(s, &c, 1); }
static const char *sbs(SB *s) { return s->p ? s->p : ""; }
static void sb_printf(SB *s, const char *fmt, ...) {
    char buf[512]; va_list a; va_start(a, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, a); va_end(a);
    if (n > 0) sb_add(s, buf, (size_t)(n < (int)sizeof buf ? n : (int)sizeof buf - 1));
}
static void sb_json(SB *s, const char *t) {
    sb_putc(s, '"');
    for (; *t; t++) {
        unsigned char c = (unsigned char)*t;
        switch (c) {
        case '"': sb_puts(s, "\\\""); break;
        case '\\': sb_puts(s, "\\\\"); break;
        case '\n': sb_puts(s, "\\n"); break;
        case '\r': sb_puts(s, "\\r"); break;
        case '\t': sb_puts(s, "\\t"); break;
        default:
            if (c < 0x20) sb_printf(s, "\\u%04x", c); else sb_putc(s, (char)c);
        }
    }
    sb_putc(s, '"');
}
static char *xstrdup(const char *s) { size_t n = strlen(s); char *p = (char *)malloc(n + 1); memcpy(p, s, n + 1); return p; }
static char *xstrndup(const char *s, size_t n) { char *p = (char *)malloc(n + 1); memcpy(p, s, n); p[n] = 0; return p; }
static void setstr(char **d, const char *s) { char *n = xstrdup(s ? s : ""); free(*d); *d = n; }
static void appstr(char **d, const char *s) {
    size_t a = *d ? strlen(*d) : 0, b = strlen(s);
    *d = (char *)realloc(*d, a + b + 1); memcpy(*d + a, s, b + 1);
}

#endif /* LLAMAC_UTIL_H */
