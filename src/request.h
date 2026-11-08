/* request.h - llama-server request body builder (portable) */
#ifndef LLAMAC_REQUEST_H
#define LLAMAC_REQUEST_H

/* ======================= request builder ======================= */
static const char *role_name(int r) { return r == ROLE_USER ? "user" : r == ROLE_ASSIST ? "assistant"
                                                                   : r == ROLE_TOOL     ? "tool"
                                                                                        : "system"; }
static void put_msg_content(SB *s, Node *m)
{
    int media = 0;
    for (int i = 0; i < m->na; i++)
        if (m->att[i].kind != ATT_TEXT)
            media = 1;
    SB t = {0};
    sb_puts(&t, m->content);
    for (int i = 0; i < m->na; i++)
        if (m->att[i].kind == ATT_TEXT)
        {
            sb_puts(&t, "\n\n--- File: ");
            sb_puts(&t, m->att[i].name);
            sb_puts(&t, " ---\n");
            sb_puts(&t, m->att[i].data);
        }
    if (!media)
    {
        sb_json(s, sbs(&t));
        free(t.p);
        return;
    }
    sb_puts(s, "[{\"type\":\"text\",\"text\":");
    sb_json(s, sbs(&t));
    sb_putc(s, '}');
    free(t.p);
    for (int i = 0; i < m->na; i++)
    {
        Att *a = &m->att[i];
        if (a->kind == ATT_IMAGE)
        {
            sb_puts(s, ",{\"type\":\"image_url\",\"image_url\":{\"url\":");
            sb_json(s, a->data);
            sb_puts(s, "}}");
        }
        else if (a->kind == ATT_AUDIO)
        {
            sb_puts(s, ",{\"type\":\"input_audio\",\"input_audio\":{\"data\":");
            sb_json(s, a->data);
            sb_printf(s, ",\"format\":\"%s\"}}", strstr(a->mime, "wav") ? "wav" : "mp3");
        }
    }
    sb_putc(s, ']');
}
static char *tools_schema_build(void); /* defined in tools.h: JSON array for the "tools" request field, or NULL */
static char *mem_digest(void);         /* memory.h */
/* The memory digest is snapshotted per conversation: a memory_save in the middle of a chat must not change
   the system prompt, because that invalidates llama-server's KV prefix cache for the whole history. */
static char *g_dig_txt;
static int g_dig_on = -1;
static char *digest_cached(Conv *c, const Cfg *g)
{
    if (c != g_dig_conv || c->cnt <= 3 || g_dig_on != g->mem_on)
    {
        free(g_dig_txt);
        g_dig_txt = mem_digest();
        g_dig_conv = c;
        g_dig_on = g->mem_on;
    }
    return g_dig_txt ? xstrdup(g_dig_txt) : NULL;
}
static void build_messages(SB *s, Conv *c, const Cfg *g)
{
    int first = 1;
    int *p;
    int n;
    int tools_left = 0, i0 = 0;
    char *md = digest_cached(c, g);
    SB sy = {0};
    sb_puts(s, "\"messages\":[");
    sb_puts(&sy, g->sys);
    if (md)
    {
        if (sy.n)
            sb_puts(&sy, "\n\n");
        sb_puts(&sy, md);
        free(md);
    }
    n = conv_path(c, &p);
    /* A compression summary is always the first node of the path (a child of root). It is merged into the
       one system message: many chat templates reject a second system message or one that is not first. */
    if (n > 0 && c->n[p[0]].role == ROLE_SYS)
    {
        if (sy.n)
            sb_puts(&sy, "\n\n");
        sb_puts(&sy, "Summary of the earlier part of this conversation (older messages were compressed):\n");
        sb_puts(&sy, c->n[p[0]].content);
        i0 = 1;
    }
    if (sy.n)
    {
        sb_puts(s, "{\"role\":\"system\",\"content\":");
        sb_json(s, sbs(&sy));
        sb_putc(s, '}');
        first = 0;
    }
    free(sy.p);
    if (i0)
    { /* the kept part may start with an assistant turn (cut inside a tool loop); templates that require
         user/assistant alternation need a user turn first */
        int j = i0;
        while (j < n && c->n[p[j]].role == ROLE_ASSIST && !c->n[p[j]].content[0] && !(c->n[p[j]].tcalls && *c->n[p[j]].tcalls))
            j++;
        if (j < n && c->n[p[j]].role != ROLE_USER)
        {
            if (!first)
                sb_putc(s, ',');
            first = 0;
            sb_puts(s, "{\"role\":\"user\",\"content\":\"Continue from the summary above.\"}");
        }
    }
    for (int i = i0; i < n; i++)
    {
        Node *m = &c->n[p[i]];
        int tc = 0;
        if (m->role == ROLE_TOOL)
        { /* only valid directly after an assistant tool_calls message */
            if (tools_left <= 0)
                continue;
            tools_left--;
            if (!first)
                sb_putc(s, ',');
            first = 0;
            sb_puts(s, "{\"role\":\"tool\",\"tool_call_id\":");
            sb_json(s, m->tcid ? m->tcid : "");
            sb_puts(s, ",\"content\":");
            sb_json(s, m->content);
            sb_putc(s, '}');
            continue;
        }
        tools_left = 0;
        if (m->role == ROLE_ASSIST && m->tcalls && *m->tcalls)
        { /* send tool_calls only when every call has a result */
            int need = tc_count(m->tcalls), have = 0;
            while (i + 1 + have < n && c->n[p[i + 1 + have]].role == ROLE_TOOL)
                have++;
            if (need > 0 && have >= need)
            {
                tc = 1;
                tools_left = need;
            }
        }
        if (m->role == ROLE_SYS) /* a summary is only valid as the first node, merged above */
            continue;
        if (m->role == ROLE_ASSIST && !m->content[0] && !tc)
            continue;
        if (!first)
            sb_putc(s, ',');
        first = 0;
        sb_printf(s, "{\"role\":\"%s\",\"content\":", role_name(m->role));
        if (tc && !m->content[0])
            sb_puts(s, "null");
        else
            put_msg_content(s, m);
        if (tc)
        {
            sb_puts(s, ",\"tool_calls\":");
            sb_puts(s, m->tcalls);
        }
        sb_putc(s, '}');
    }
    free(p);
    sb_putc(s, ']');
}
/* POST /apply-template body: renders the server's Jinja chat template without inference */
static char *build_template_body(Conv *c, const Cfg *g)
{
    SB s = {0};
    sb_putc(&s, '{');
    build_messages(&s, c, g);
    sb_putc(&s, '}');
    return s.p;
}
/* Only parameters SET in the client are sent; all others fall back to llama-server's own values. */
static char *build_request(Conv *c, const Cfg *g)
{
    SB s = {0};
    sb_putc(&s, '{');
    build_messages(&s, c, g);
    sb_puts(&s, ",\"stream\":true,\"timings_per_token\":true");
    {
        char *tl = tools_schema_build();
        if (tl)
        {
            sb_puts(&s, ",\"tools\":");
            sb_puts(&s, tl);
            free(tl);
        }
    }
    if (g->model[0])
    {
        sb_puts(&s, ",\"model\":");
        sb_json(&s, g->model);
    }
    for (int i = 0; i < NFIELDS; i++)
    {
        const Field *f = &FIELDS[i];
        const char *v = (const char *)g + f->off;
        char *e, nb[40];
        if (f->kind != 1 || !f->rkey || !*v)
            continue;
        double d = strtod(v, &e);
        if (e == v || !isfinite(d))
            continue;
        if (!strcmp(f->rkey, "max_tokens") && d <= 0)
            continue;
        if (!strcmp(f->rkey, "seed") && d < 0)
            continue;
        fmt_num(nb, d);
        sb_printf(&s, ",\"%s\":%s", f->rkey, nb);
    }
    if (g->rfmt[0])
    {
        sb_puts(&s, ",\"reasoning_format\":");
        sb_json(&s, g->rfmt);
    }
    if (g->kwargs[0])
    {
        J *k = json_parse_strict(g->kwargs);
        if (k && k->t == J_OBJ)
            sb_printf(&s, ",\"chat_template_kwargs\":%s", g->kwargs);
        j_free(k);
    }
    if (g->stop[0])
    {
        char tmp[256];
        strcpy(tmp, g->stop);
        int any = 0;
        sb_puts(&s, ",\"stop\":[");
        for (char *t = strtok(tmp, "|"); t; t = strtok(NULL, "|"))
        {
            if (any)
                sb_putc(&s, ',');
            sb_json(&s, t);
            any = 1;
        }
        sb_putc(&s, ']');
    }
    {
        const char *a = g->custom;
        while (*a == ' ' || *a == '\n')
            a++;
        size_t len = strlen(a);
        while (len && (a[len - 1] == ' ' || a[len - 1] == '\n'))
            len--;
        if (len > 2 && a[0] == '{' && a[len - 1] == '}')
        {
            sb_putc(&s, ',');
            sb_add(&s, a + 1, len - 2);
        }
    }
    sb_putc(&s, '}');
    return s.p;
}

#endif /* LLAMAC_REQUEST_H */
