/* request.h - llama-server request body builder (portable) */
#ifndef LLAMAC_REQUEST_H
#define LLAMAC_REQUEST_H

/* ======================= request builder ======================= */
static const char *role_name(int r) { return r == ROLE_USER ? "user" : r == ROLE_ASSIST ? "assistant"
                                                                   : r == ROLE_TOOL     ? "tool"
                                                                                        : "system"; }
/* Set by the caller before build_request(): the context size the request has to fit (0 = unknown, no fitting). */
static int g_req_lim;
static double g_cpt = 3.0;                      /* characters per token, calibrated from server timings */
static size_t g_req_chars;                      /* characters of the last request's messages + tools */
static int g_req_fit_elided, g_req_fit_clipped; /* what fitting did to the last request (status line) */
static int g_req_fit_dropped;

/* head (2/3) + tail (1/3) of s, at most cap bytes, UTF-8 safe */
static void put_clipped(SB *o, const char *s, size_t cap)
{
    size_t n = strlen(s), h, t;
    if (n <= cap)
    {
        sb_add(o, s, n);
        return;
    }
    h = tx_cut(s, n, cap * 2 / 3);
    t = n - (cap - h);
    while (t < n && ((unsigned char)s[t] & 0xC0) == 0x80)
        t++;
    sb_add(o, s, h);
    sb_printf(o, "\n[... %llu characters left out to fit the context; ask again for a narrower range ...]\n", (unsigned long long)(t - h));
    sb_puts(o, s + t);
}
static void put_msg_content(SB *s, Node *m, const char *suffix)
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
    if (suffix)
        sb_puts(&t, suffix);
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
/* Short replacement for an old tool result: names the call so the model can repeat it if it needs the data. */
static void tool_stub(SB *o, Conv *c, const int *p, int i)
{
    Node *m = &c->n[p[i]];
    const char *args = NULL;
    J *a = NULL;
    size_t lines = 1, n = strlen(m->content);
    for (int k = i - 1; k >= 0; k--)
    {
        Node *x = &c->n[p[k]];
        if (x->role != ROLE_ASSIST)
            continue;
        if (x->tcalls && m->tcid)
        {
            a = json_parse(x->tcalls);
            for (J *e = a ? a->child : NULL; e; e = e->next)
            {
                const char *id = j_str(j_get(e, "id"));
                if (id && !strcmp(id, m->tcid))
                    args = j_str(j_get(j_get(e, "function"), "arguments"));
            }
        }
        break;
    }
    for (const char *q = m->content; *q; q++)
        lines += *q == '\n';
    sb_puts(o, "[Earlier result of ");
    sb_puts(o, m->tname && *m->tname ? m->tname : "a tool");
    if (args)
    {
        size_t al = tx_cut(args, strlen(args), 200);
        sb_putc(o, '(');
        sb_add(o, args, al);
        if (al < strlen(args))
            sb_puts(o, "...");
        sb_putc(o, ')');
    }
    sb_printf(o, " removed to save context: %llu lines, %llu characters. Call the tool again if you still need it.]",
              (unsigned long long)lines, (unsigned long long)n);
    j_free(a);
}
static char *tools_schema_build(void); /* defined in tools.h: JSON array for the "tools" request field, or NULL */
static char *mem_digest_for(const char *query); /* memory.h */
/* The memory digest is snapshotted per conversation: a memory_save in the middle of a chat must not change
   the system prompt, because that invalidates llama-server's KV prefix cache for the whole history. It is
   chosen by relevance to the chat's first request, so it is computed once that message exists. */
static char *g_dig_txt;
static int g_dig_on = -1;
static char *digest_cached(Conv *c, const Cfg *g, const char *query)
{
    if (c != g_dig_conv || c->cnt <= 3 || g_dig_on != g->mem_on)
    {
        free(g_dig_txt);
        g_dig_txt = mem_digest_for(query);
        g_dig_conv = c;
        g_dig_on = g->mem_on;
    }
    return g_dig_txt ? xstrdup(g_dig_txt) : NULL;
}
static size_t node_chars(Node *m)
{
    size_t n = strlen(m->content) + (m->tcalls ? strlen(m->tcalls) : 0) + 16;
    for (int a = 0; a < m->na; a++)
        if (m->att[a].kind == ATT_TEXT)
            n += strlen(m->att[a].data) + strlen(m->att[a].name) + 20;
    return n;
}
/* tokens the server will need for the reply: max_tokens if set, else max(1/8 of the context, 2048), at most half */
static int reply_reserve(const Cfg *g, int lim)
{
    double mt = g->maxtok[0] ? atof(g->maxtok) : 0;
    int r = mt > 0 ? (int)mt : lim / 8;
    if (!(mt > 0) && r < 2048)
        r = 2048;
    if (r > lim / 2)
        r = lim / 2;
    return r;
}
/* Fitting ("context compiler"): when the request would not fit, decide per path node what to send, in order of
   least loss: (1) stub old tool results from earlier turns, oldest first; (2) stub tool results of the current
   turn except the newest two; (3) clip the remaining large tool results; (4) leave out whole early turns.
   mode[i]: 0 send, 1 stub, 2 clip to cap[i], 3 leave out. Deterministic, so repeated requests stay cache-friendly. */
static void fit_plan(Conv *c, const int *p, int n, int i0, long long over, char *mode, size_t *cap)
{
    int lastu = -1, ntool = 0, seen = 0;
    for (int i = i0; i < n; i++)
    {
        if (c->n[p[i]].role == ROLE_USER)
            lastu = i;
        if (c->n[p[i]].role == ROLE_TOOL)
            ntool++;
    }
    for (int pass = 0; pass < 2 && over > 0; pass++)
    {
        seen = 0;
        for (int i = i0; i < n && over > 0; i++)
        {
            Node *m = &c->n[p[i]];
            size_t L;
            if (m->role != ROLE_TOOL)
                continue;
            seen++;
            if (mode[i] || m->elided)
                continue;
            if (pass == 0 && i > lastu)
                continue;
            if (pass == 1 && seen > ntool - 2)
                continue;
            L = strlen(m->content);
            if (L < 600)
                continue;
            mode[i] = 1;
            g_req_fit_elided++;
            over -= (long long)L - 250;
        }
    }
    while (over > 0)
    { /* clip the largest remaining tool result */
        int best = -1;
        size_t bl = 0;
        for (int i = i0; i < n; i++)
        {
            Node *m = &c->n[p[i]];
            size_t L = mode[i] == 2 ? cap[i] : strlen(m->content);
            if (m->role == ROLE_TOOL && !m->elided && (mode[i] == 0 || mode[i] == 2) && L > 4000 && L > bl)
                best = i, bl = L;
        }
        if (best < 0)
            break;
        {
            size_t want = bl - (size_t)(over < (long long)bl ? over : (long long)bl);
            if (want < 4000)
                want = 4000;
            if (mode[best] != 2)
                g_req_fit_clipped++;
            mode[best] = 2;
            cap[best] = want;
            over -= (long long)(bl - want);
        }
    }
    if (over > 0)
    { /* leave out early turns: always up to a user message, never the current turn */
        int k = i0;
        long long cut = 0;
        while (k < lastu && cut < over)
        {
            cut += (long long)(mode[k] == 1 ? 250 : mode[k] == 2 ? cap[k] : node_chars(&c->n[p[k]]));
            k++;
            while (k < lastu && c->n[p[k]].role != ROLE_USER)
            {
                cut += (long long)(mode[k] == 1 ? 250 : mode[k] == 2 ? cap[k] : node_chars(&c->n[p[k]]));
                k++;
            }
        }
        for (int i = i0; i < k; i++)
            mode[i] = 3;
        g_req_fit_dropped = k - i0;
    }
}
static void build_messages(SB *s, Conv *c, const Cfg *g, size_t tools_chars)
{
    int first = 1;
    int *p;
    int n;
    int tools_left = 0, i0 = 0, lastu = -1, nimg = 0;
    Att *imgs[16];
    char *md, *mode;
    size_t *cap;
    const char *query = "";
    SB sy = {0}, rem = {0};
    size_t start = s->n;
    n = conv_path(c, &p);
    for (int i = 0; i < n; i++)
        if (c->n[p[i]].role == ROLE_USER)
        {
            query = c->n[p[i]].content;
            break;
        }
    if (n > 0 && c->n[p[0]].role == ROLE_SYS && c->n[p[0]].reasoning[0])
        query = c->n[p[0]].reasoning; /* the original prompt kept by a compression summary */
    md = digest_cached(c, g, query);
    mode = (char *)calloc((size_t)n + 1, 1);
    cap = (size_t *)calloc((size_t)n + 1, sizeof(size_t));
    g_req_fit_elided = g_req_fit_clipped = g_req_fit_dropped = 0;
    sb_puts(s, "\"messages\":[");
    sb_puts(&sy, g->sys);
    if (md)
    {
        if (sy.n)
            sb_puts(&sy, "\n\n");
        sb_puts(&sy, md);
        free(md);
    }
    /* A compression summary is always the first node of the path (a child of root). It is merged into the
       one system message: many chat templates reject a second system message or one that is not first. */
    if (n > 0 && c->n[p[0]].role == ROLE_SYS)
    {
        if (sy.n)
            sb_puts(&sy, "\n\n");
        sb_puts(&sy, "Summary of the earlier part of this conversation (older messages were compressed):\n");
        sb_puts(&sy, c->n[p[0]].content);
        if (c->n[p[0]].reasoning[0])
        {
            sb_puts(&sy, "\n\nThe user's original prompt that started this conversation:\n");
            sb_puts(&sy, c->n[p[0]].reasoning);
        }
        i0 = 1;
    }
    for (int i = i0; i < n; i++)
        if (c->n[p[i]].role == ROLE_USER)
            lastu = i;
    if (g->remind[0] && lastu >= 0)
    { /* repeated at the end, where small models follow instructions best; not stored, so only the newest
         user message carries it and the cached prefix stays valid */
        sb_puts(&rem, "\n\n<reminder>\n");
        sb_puts(&rem, g->remind);
        sb_puts(&rem, "\n</reminder>");
    }
    if (g->budget && g_req_lim > 0)
    {
        long long total = (long long)sy.n + (long long)tools_chars + (long long)rem.n, budget;
        for (int i = i0; i < n; i++)
        {
            Node *m = &c->n[p[i]];
            total += m->role == ROLE_TOOL && m->elided ? 250 : (long long)node_chars(m);
        }
        budget = (long long)((g_req_lim - reply_reserve(g, g_req_lim)) * g_cpt);
        if (total > budget)
            fit_plan(c, p, n, i0, total - budget, mode, cap);
    }
    if (g_req_fit_dropped)
        sb_printf(&sy, "%s[%d earlier message(s) were left out of this request to fit the context window.]", sy.n ? "\n\n" : "", g_req_fit_dropped);
    if (sy.n)
    {
        sb_puts(s, "{\"role\":\"system\",\"content\":");
        sb_json(s, sbs(&sy));
        sb_putc(s, '}');
        first = 0;
    }
    free(sy.p);
    {
        int j = i0; /* the kept part may start with an assistant turn (cut inside a tool loop); templates that
                       require user/assistant alternation need a user turn first */
        while (j < n && mode[j] == 3)
            j++;
        if (i0 || j > i0)
        {
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
    }
    for (int i = i0; i < n; i++)
    {
        Node *m = &c->n[p[i]];
        int tc = 0;
        if (mode[i] == 3)
            continue;
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
            if (m->elided || mode[i] == 1)
            {
                SB t = {0};
                tool_stub(&t, c, p, i);
                sb_json(s, sbs(&t));
                free(t.p);
            }
            else if (mode[i] == 2)
            {
                SB t = {0};
                put_clipped(&t, m->content, cap[i]);
                sb_json(s, sbs(&t));
                free(t.p);
            }
            else
                sb_json(s, m->content);
            sb_putc(s, '}');
            if (!m->elided && mode[i] != 1)
                for (int k = 0; k < m->na && nimg < 16; k++)
                    if (m->att[k].kind == ATT_IMAGE)
                        imgs[nimg++] = &m->att[k];
            if (tools_left == 0 && nimg)
            { /* tool messages carry text only: images from tools (screenshots) follow as a user message */
                sb_puts(s, ",{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"Image returned by the tool call above:\"}");
                for (int k = 0; k < nimg; k++)
                {
                    sb_puts(s, ",{\"type\":\"image_url\",\"image_url\":{\"url\":");
                    sb_json(s, imgs[k]->data);
                    sb_puts(s, "}}");
                }
                sb_puts(s, "]}");
                nimg = 0;
            }
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
            put_msg_content(s, m, i == lastu && rem.n ? sbs(&rem) : NULL);
        if (tc)
        {
            sb_puts(s, ",\"tool_calls\":");
            sb_puts(s, m->tcalls);
        }
        sb_putc(s, '}');
    }
    free(p);
    free(mode);
    free(cap);
    free(rem.p);
    sb_putc(s, ']');
    g_req_chars = s->n - start + tools_chars;
}
/* POST /apply-template body: renders the server's Jinja chat template without inference */
static char *build_template_body(Conv *c, const Cfg *g)
{
    SB s = {0};
    sb_putc(&s, '{');
    build_messages(&s, c, g, 0);
    sb_putc(&s, '}');
    return s.p;
}
/* Only parameters SET in the client are sent; all others fall back to llama-server's own values. */
static char *build_request(Conv *c, const Cfg *g)
{
    SB s = {0};
    char *tl = tools_schema_build();
    sb_putc(&s, '{');
    build_messages(&s, c, g, tl ? strlen(tl) : 0);
    sb_puts(&s, ",\"stream\":true,\"timings_per_token\":true,\"return_progress\":true");
    if (tl)
    {
        sb_puts(&s, ",\"tools\":");
        sb_puts(&s, tl);
        free(tl);
    }
    else if (g->logprobs) /* llama-server rejects logprobs together with tools in streaming mode */
        sb_puts(&s, ",\"logprobs\":true,\"top_logprobs\":5");
    if (g->kvslot) /* the per-chat KV cache is saved from / restored into this slot */
        sb_puts(&s, ",\"id_slot\":0");
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
    if (g->kwargs[0] || g->nothink)
    {
        J *k = g->kwargs[0] ? json_parse_strict(g->kwargs) : NULL;
        if (g->nothink)
        { /* Think button off: enable_thinking false, merged with the user's kwargs (which may not override it) */
            sb_puts(&s, ",\"chat_template_kwargs\":{\"enable_thinking\":false");
            if (k && k->t == J_OBJ)
                for (J *c = k->child; c; c = c->next)
                    if (c->key && strcmp(c->key, "enable_thinking"))
                    {
                        sb_putc(&s, ',');
                        sb_json(&s, c->key);
                        sb_putc(&s, ':');
                        j_dump(&s, c);
                    }
            sb_putc(&s, '}');
        }
        else if (k && k->t == J_OBJ)
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
