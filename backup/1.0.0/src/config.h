/* config.h - Settings struct, field table, serialisation (portable) */
#ifndef LLAMAC_CONFIG_H
#define LLAMAC_CONFIG_H

/* ======================= settings ======================= */
typedef struct {
    char url[256], key[256], model[160], sys[2048], stop[256], custom[2048], kwargs[512], rfmt[24];
    /* sampling overrides are strings: EMPTY = "not set -> use the llama-server value" */
    char temp[24], top_k[24], top_p[24], min_p[24], rep[24], pres[24], freq[24], dry[24], maxtok[24], seed[24];
    int dark, thoughts, stats, smartpaste;
} Cfg;
/* kind: 0 text, 1 number override (empty = server default), 3 checkbox */
typedef struct { const char *key, *label; int kind; size_t off, size; const char *rkey, *pkey; } Field;
#define FLD(k, l, t, m, rk, pk) { k, l, t, offsetof(Cfg, m), sizeof(((Cfg *)0)->m), rk, pk }
static const Field FIELDS[] = {
    FLD("url", "Server URL", 0, url, NULL, NULL),
    FLD("key", "API key", 0, key, NULL, NULL),
    FLD("model", NULL, 0, model, NULL, NULL),
    FLD("sys", "System message", 0, sys, NULL, NULL),
    FLD("kwargs", "Jinja template kwargs (JSON)", 0, kwargs, NULL, NULL),
    FLD("rfmt", "Reasoning format (auto/none/deepseek)", 0, rfmt, NULL, NULL),
    FLD("stop", "Stop strings (separate with |)", 0, stop, NULL, NULL),
    FLD("custom", "Extra request JSON  {\"k\":v}", 0, custom, NULL, NULL),
    FLD("dark", "Dark theme", 3, dark, NULL, NULL),
    FLD("thoughts", "Show reasoning blocks", 3, thoughts, NULL, NULL),
    FLD("stats", "Show generation speed", 3, stats, NULL, NULL),
    FLD("smartpaste", "Long paste becomes attachment", 3, smartpaste, NULL, NULL),
    FLD("temp", "Temperature", 1, temp, "temperature", "temperature"),
    FLD("top_k", "Top K", 1, top_k, "top_k", "top_k"),
    FLD("top_p", "Top P", 1, top_p, "top_p", "top_p"),
    FLD("min_p", "Min P", 1, min_p, "min_p", "min_p"),
    FLD("rep", "Repeat penalty", 1, rep, "repeat_penalty", "repeat_penalty"),
    FLD("pres", "Presence penalty", 1, pres, "presence_penalty", "presence_penalty"),
    FLD("freq", "Frequency penalty", 1, freq, "frequency_penalty", "frequency_penalty"),
    FLD("dry", "DRY multiplier", 1, dry, "dry_multiplier", "dry_multiplier"),
    FLD("maxtok", "Max tokens", 1, maxtok, "max_tokens", "n_predict"),
    FLD("seed", "Seed", 1, seed, "seed", "seed"),
};
#define NFIELDS ((int)(sizeof FIELDS / sizeof FIELDS[0]))
static int fidx(const char *key) { for (int i = 0; i < NFIELDS; i++) if (!strcmp(FIELDS[i].key, key)) return i; return -1; }

static void cfg_defaults(Cfg *g) {
    memset(g, 0, sizeof *g);
    strcpy(g->url, "http://127.0.0.1:8080");
    g->dark = 1; g->thoughts = 1; g->stats = 1; g->smartpaste = 1;
}
/* default_generation_settings.params from GET /props -> out[NFIELDS] ("" = unknown) */
static void srv_parse(J *props, char out[][32]) {
    J *dgs = j_get(props, "default_generation_settings"), *params = j_get(dgs, "params");
    if (!params) params = dgs;
    for (int i = 0; i < NFIELDS; i++) {
        out[i][0] = 0;
        if (FIELDS[i].kind != 1 || !FIELDS[i].pkey) continue;
        J *v = j_get(params, FIELDS[i].pkey);
        if (v && v->t == J_NUM) {
            double d = v->num;
            if (!strcmp(FIELDS[i].key, "seed") && d > 2147483647.0) d = -1;
            fmt_num(out[i], d);
        }
    }
}
static void esc_to(SB *o, const char *s) {
    for (; *s; s++) {
        if (*s == '\\') sb_puts(o, "\\\\"); else if (*s == '\n') sb_puts(o, "\\n");
        else if (*s != '\r') sb_putc(o, *s);
    }
}
static void unesc_to(char *dst, size_t cap, const char *s) {
    size_t n = 0;
    for (; *s && n + 1 < cap; s++) {
        if (*s == '\\' && s[1] == 'n') { dst[n++] = '\n'; s++; }
        else if (*s == '\\' && s[1] == '\\') { dst[n++] = '\\'; s++; }
        else dst[n++] = *s;
    }
    dst[n] = 0;
}
static void field_set(Cfg *g, const Field *f, const char *v) {
    char *base = (char *)g + f->off;
    if (f->kind <= 1) unesc_to(base, f->size, v); else *(int *)base = atoi(v);
}
static void field_get(Cfg *g, const Field *f, SB *o) {
    char *base = (char *)g + f->off;
    if (f->kind <= 1) esc_to(o, base); else sb_printf(o, "%d", *(int *)base);
}
static char *cfg_serialize(Cfg *g) {
    SB o = {0};
    for (int i = 0; i < NFIELDS; i++) { sb_printf(&o, "%s=", FIELDS[i].key); field_get(g, &FIELDS[i], &o); sb_putc(&o, '\n'); }
    return o.p;
}
static void cfg_parse(Cfg *g, char *txt) {
    for (char *ln = strtok(txt, "\n"); ln; ln = strtok(NULL, "\n")) {
        char *eq = strchr(ln, '='); if (!eq) continue; *eq = 0;
        for (int i = 0; i < NFIELDS; i++) if (!strcmp(FIELDS[i].key, ln)) field_set(g, &FIELDS[i], eq + 1);
    }
}

#endif /* LLAMAC_CONFIG_H */
