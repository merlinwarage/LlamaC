/* tools_core.h - Tool-call helpers: JSON dump, streamed tool_calls accumulator, UTF-8 sanitiser (portable) */
#ifndef LLAMAC_TOOLS_CORE_H
#define LLAMAC_TOOLS_CORE_H

/* serialise a JSON DOM node back to text */
static void j_dump(SB *s, J *j) {
    if (!j) { sb_puts(s, "null"); return; }
    switch (j->t) {
    case J_NULL: sb_puts(s, "null"); break;
    case J_BOOL: sb_puts(s, j->num != 0 ? "true" : "false"); break;
    case J_NUM: { char b[40]; fmt_num(b, j->num); sb_puts(s, b); break; }
    case J_STR: sb_json(s, j->str ? j->str : ""); break;
    case J_ARR:
        sb_putc(s, '[');
        for (J *c = j->child; c; c = c->next) { if (c != j->child) sb_putc(s, ','); j_dump(s, c); }
        sb_putc(s, ']'); break;
    case J_OBJ:
        sb_putc(s, '{');
        for (J *c = j->child; c; c = c->next) { if (c != j->child) sb_putc(s, ','); sb_json(s, c->key ? c->key : ""); sb_putc(s, ':'); j_dump(s, c); }
        sb_putc(s, '}'); break;
    }
}

/* replace invalid UTF-8 bytes with '?' (llama-server rejects invalid UTF-8 in JSON bodies) */
static void utf8_sanitize(char *s) {
    unsigned char *p = (unsigned char *)s;
    while (*p) {
        int n = *p < 0x80 ? 1 : (*p >= 0xC2 && *p <= 0xDF) ? 2 : (*p >= 0xE0 && *p <= 0xEF) ? 3 : (*p >= 0xF0 && *p <= 0xF4) ? 4 : 0;
        int ok = n > 0;
        for (int i = 1; ok && i < n; i++) if ((p[i] & 0xC0) != 0x80) ok = 0;
        if (ok && n == 3) { if (p[0] == 0xE0 && p[1] < 0xA0) ok = 0; if (p[0] == 0xED && p[1] >= 0xA0) ok = 0; }
        if (ok && n == 4) { if (p[0] == 0xF0 && p[1] < 0x90) ok = 0; if (p[0] == 0xF4 && p[1] >= 0x90) ok = 0; }
        if (ok) p += n; else *p++ = '?';
    }
}

/* accumulates delta.tool_calls fragments of one streamed response (used by the single worker thread) */
#define MAXTC 16
typedef struct { char id[80]; char name[128]; SB args; int used; } TcAcc;
static TcAcc g_tc[MAXTC]; static int g_ntc;
static void tc_reset(void) {
    for (int i = 0; i < MAXTC; i++) free(g_tc[i].args.p);
    memset(g_tc, 0, sizeof g_tc); g_ntc = 0;
}
static void tc_delta(J *arr) {
    for (J *e = arr ? arr->child : NULL; e; e = e->next) {
        J *ix = j_get(e, "index"); const char *id = j_str(j_get(e, "id"));
        int idx = ix ? (int)ix->num : -1;
        if (idx < 0) idx = (id && *id) ? g_ntc : (g_ntc ? g_ntc - 1 : 0);
        if (idx < 0 || idx >= MAXTC) continue;
        TcAcc *a = &g_tc[idx]; a->used = 1; if (idx + 1 > g_ntc) g_ntc = idx + 1;
        if (id && *id && !a->id[0]) { strncpy(a->id, id, sizeof a->id - 1); }
        J *fn = j_get(e, "function"); const char *nm = j_str(j_get(fn, "name"));
        if (nm && *nm && !a->name[0]) strncpy(a->name, nm, sizeof a->name - 1);
        J *ar = j_get(fn, "arguments");
        if (ar && ar->t == J_STR && ar->str) sb_puts(&a->args, ar->str);
        else if (ar && ar->t == J_OBJ) { a->args.n = 0; j_dump(&a->args, ar); }
    }
}
/* final OpenAI-style tool_calls array as JSON text, or NULL when the model made no call */
static char *tc_build(void) {
    SB s = {0}; int n = 0; sb_putc(&s, '[');
    for (int i = 0; i < g_ntc; i++) {
        TcAcc *a = &g_tc[i]; char gen[40];
        if (!a->used || !a->name[0]) continue;
        if (n++) sb_putc(&s, ',');
        if (!a->id[0]) { mkid(gen); snprintf(a->id, sizeof a->id, "call_%s", gen); }
        sb_puts(&s, "{\"id\":"); sb_json(&s, a->id);
        sb_puts(&s, ",\"type\":\"function\",\"function\":{\"name\":"); sb_json(&s, a->name);
        sb_puts(&s, ",\"arguments\":"); sb_json(&s, a->args.n ? sbs(&a->args) : "{}");
        sb_puts(&s, "}}");
    }
    sb_putc(&s, ']');
    if (!n) { free(s.p); return NULL; }
    return s.p;
}
static int tc_count(const char *json) {
    J *a = json_parse(json); int n = 0;
    if (a && a->t == J_ARR) for (J *c = a->child; c; c = c->next) n++;
    j_free(a); return n;
}

#endif /* LLAMAC_TOOLS_CORE_H */
