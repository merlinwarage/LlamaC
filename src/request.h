/* request.h - llama-server request body builder (portable) */
#ifndef LLAMAC_REQUEST_H
#define LLAMAC_REQUEST_H

/* ======================= request builder ======================= */
static const char *role_name(int r) { return r == ROLE_USER ? "user" : r == ROLE_ASSIST ? "assistant" : "system"; }
static void put_msg_content(SB *s, Node *m) {
    int media = 0;
    for (int i = 0; i < m->na; i++) if (m->att[i].kind != ATT_TEXT) media = 1;
    SB t = {0}; sb_puts(&t, m->content);
    for (int i = 0; i < m->na; i++) if (m->att[i].kind == ATT_TEXT) {
        sb_puts(&t, "\n\n--- File: "); sb_puts(&t, m->att[i].name); sb_puts(&t, " ---\n"); sb_puts(&t, m->att[i].data);
    }
    if (!media) { sb_json(s, sbs(&t)); free(t.p); return; }
    sb_puts(s, "[{\"type\":\"text\",\"text\":"); sb_json(s, sbs(&t)); sb_putc(s, '}'); free(t.p);
    for (int i = 0; i < m->na; i++) {
        Att *a = &m->att[i];
        if (a->kind == ATT_IMAGE) { sb_puts(s, ",{\"type\":\"image_url\",\"image_url\":{\"url\":"); sb_json(s, a->data); sb_puts(s, "}}"); }
        else if (a->kind == ATT_AUDIO) {
            sb_puts(s, ",{\"type\":\"input_audio\",\"input_audio\":{\"data\":"); sb_json(s, a->data);
            sb_printf(s, ",\"format\":\"%s\"}}", strstr(a->mime, "wav") ? "wav" : "mp3");
        }
    }
    sb_putc(s, ']');
}
static void build_messages(SB *s, Conv *c, const Cfg *g) {
    int first = 1; int *p; int n;
    sb_puts(s, "\"messages\":[");
    if (g->sys[0]) { sb_puts(s, "{\"role\":\"system\",\"content\":"); sb_json(s, g->sys); sb_putc(s, '}'); first = 0; }
    n = conv_path(c, &p);
    for (int i = 0; i < n; i++) {
        Node *m = &c->n[p[i]];
        if (m->role == ROLE_ASSIST && !m->content[0]) continue;
        if (!first) sb_putc(s, ',');
        first = 0;
        sb_printf(s, "{\"role\":\"%s\",\"content\":", role_name(m->role));
        put_msg_content(s, m); sb_putc(s, '}');
    }
    free(p);
    sb_putc(s, ']');
}
/* POST /apply-template body: renders the server's Jinja chat template without inference */
static char *build_template_body(Conv *c, const Cfg *g) {
    SB s = {0}; sb_putc(&s, '{'); build_messages(&s, c, g); sb_putc(&s, '}'); return s.p;
}
/* Only parameters SET in the client are sent; all others fall back to llama-server's own values. */
static char *build_request(Conv *c, const Cfg *g) {
    SB s = {0};
    sb_putc(&s, '{'); build_messages(&s, c, g);
    sb_puts(&s, ",\"stream\":true,\"timings_per_token\":true");
    if (g->model[0]) { sb_puts(&s, ",\"model\":"); sb_json(&s, g->model); }
    for (int i = 0; i < NFIELDS; i++) {
        const Field *f = &FIELDS[i]; const char *v = (const char *)g + f->off; char *e, nb[40];
        if (f->kind != 1 || !f->rkey || !*v) continue;
        double d = strtod(v, &e);
        if (e == v || !isfinite(d)) continue;
        if (!strcmp(f->rkey, "max_tokens") && d <= 0) continue;
        if (!strcmp(f->rkey, "seed") && d < 0) continue;
        fmt_num(nb, d); sb_printf(&s, ",\"%s\":%s", f->rkey, nb);
    }
    if (g->rfmt[0]) { sb_puts(&s, ",\"reasoning_format\":"); sb_json(&s, g->rfmt); }
    if (g->kwargs[0]) {
        J *k = json_parse_strict(g->kwargs);
        if (k && k->t == J_OBJ) sb_printf(&s, ",\"chat_template_kwargs\":%s", g->kwargs);
        j_free(k);
    }
    if (g->stop[0]) {
        char tmp[256]; strcpy(tmp, g->stop); int any = 0;
        sb_puts(&s, ",\"stop\":[");
        for (char *t = strtok(tmp, "|"); t; t = strtok(NULL, "|")) { if (any) sb_putc(&s, ','); sb_json(&s, t); any = 1; }
        sb_putc(&s, ']');
    }
    {
        const char *a = g->custom; while (*a == ' ' || *a == '\n') a++;
        size_t len = strlen(a); while (len && (a[len - 1] == ' ' || a[len - 1] == '\n')) len--;
        if (len > 2 && a[0] == '{' && a[len - 1] == '}') { sb_putc(&s, ','); sb_add(&s, a + 1, len - 2); }
    }
    sb_putc(&s, '}');
    return s.p;
}

#endif /* LLAMAC_REQUEST_H */
