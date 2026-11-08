/* compact.h - automatic session compression when free context is low */
#ifndef LLAMAC_COMPACT_H
#define LLAMAC_COMPACT_H

/* The transcript is snapshotted on the UI thread (c->n may be realloc'ed by the UI while a
   background thread runs), so the worker thread never touches the Conv. */
typedef struct
{
    Conv *c;
    int keep; /* first node that stays; it is re-parented under the summary */
    char *txt;
} CJob;

/* last attempt: after a failure the same conversation is not retried until its path grows by
   CRETRY nodes, so a server that cannot summarize is not hit after every turn */
#define CRETRY 6
static Conv *g_clast_conv;
static int g_clast_n, g_clast_fail;
static void compact_clip(SB *o, const char *s, size_t cap)
{
    size_t n = strlen(s), h, t, ts;
    if (n <= cap)
    {
        sb_add(o, s, n);
        return;
    }
    h = cap * 2 / 3;
    t = cap - h;
    while (h && ((unsigned char)s[h] & 0xC0) == 0x80)
        h--;
    ts = n - t;
    while (ts < n && ((unsigned char)s[ts] & 0xC0) == 0x80)
        ts++;
    sb_add(o, s, h);
    sb_puts(o, "\n[... clipped ...]\n");
    sb_add(o, s + ts, n - ts);
}
static void compact_transcript(SB *o, Conv *c, const int *ids, int k, size_t cap)
{
    for (int i = 0; i < k; i++)
    {
        Node *m = &c->n[ids[i]];
        if (m->role == ROLE_ROOT)
            continue;
        sb_puts(o, m->role == ROLE_SYS ? "earlier summary" : role_name(m->role));
        sb_puts(o, ": ");
        compact_clip(o, m->content, cap);
        sb_putc(o, '\n');
        if (m->tcalls && *m->tcalls)
        {
            sb_puts(o, "[tool calls: ");
            compact_clip(o, m->tcalls, 600);
            sb_puts(o, "]\n");
        }
        for (int a = 0; a < m->na; a++)
            if (m->att[a].kind == ATT_TEXT)
            {
                sb_puts(o, "[file ");
                sb_puts(o, m->att[a].name);
                sb_puts(o, "]\n");
                compact_clip(o, m->att[a].data, cap);
                sb_putc(o, '\n');
            }
    }
}
/* one-shot non-streaming summary request; returns the summary text, or NULL with the reason in err */
static char *compact_summarize(const char *transcript, char *err, size_t es)
{
    SB body = {0};
    char *raw, *out = NULL;
    J *r, *ch, *msg;
    sb_puts(&body, "{\"messages\":[{\"role\":\"system\",\"content\":");
    sb_json(&body, "Summarize this conversation for continuity. Keep the user's goal, decisions, file paths, names, constraints, open questions and current state. Dense and factual, under 600 words. No commentary.");
    sb_puts(&body, "},{\"role\":\"user\",\"content\":");
    {
        SB u = {0};
        sb_puts(&u, "--- CONVERSATION ---\n");
        sb_puts(&u, transcript);
        sb_json(&body, sbs(&u));
        free(u.p);
    }
    /* Reasoning models think before the summary: measured ~2100 reasoning + ~700 summary tokens for an
       8k-token transcript (Qwen3 27B), so the budget must cover both. Thinking is not forced off: some
       models answer enable_thinking:false with an empty reply. The user's template kwargs apply instead. */
    sb_puts(&body, "}],\"stream\":false,\"max_tokens\":6144,\"temperature\":0.2");
    if (G.kwargs[0])
    {
        J *k = json_parse_strict(G.kwargs);
        if (k && k->t == J_OBJ)
            sb_printf(&body, ",\"chat_template_kwargs\":%s", G.kwargs);
        j_free(k);
    }
    if (G.model[0])
    {
        sb_puts(&body, ",\"model\":");
        sb_json(&body, G.model);
    }
    sb_putc(&body, '}');
    raw = http_req_x(L"POST", "/v1/chat/completions", sbs(&body), 900000, err, es); /* local models can be slow: 6k tokens at ~18 t/s is ~6 min */
    free(body.p);
    if (!raw)
        return NULL;
    r = json_parse(raw);
    free(raw);
    ch = j_get(r, "choices");
    msg = j_get(j_idx(ch, 0), "message");
    {
        const char *s = j_str(j_get(msg, "content"));
        const char *fr = j_str(j_get(j_idx(ch, 0), "finish_reason"));
        const char *rc = j_str(j_get(msg, "reasoning_content"));
        if (s)
        { /* reasoning_format "none": the thinking arrives inline */
            const char *e = strstr(s, "</think>");
            if (e)
                s = e + 8;
            while (*s && isspace((unsigned char)*s))
                s++;
        }
        if (s && *s)
            out = xstrdup(s);
        else if (!r || !msg)
            snprintf(err, es, "unexpected reply from the server");
        else if (rc && *rc)
            snprintf(err, es, "the model used its whole token budget on reasoning (finish_reason %s)", fr ? fr : "?");
        else
            snprintf(err, es, "the model returned an empty summary (finish_reason %s)", fr ? fr : "?");
    }
    j_free(r);
    return out;
}
static DWORD WINAPI compact_worker(LPVOID arg)
{
    CJob *j = (CJob *)arg;
    char err[400] = "";
    char *sum = compact_summarize(j->txt, err, sizeof err);
    if (sum)
    {
        SB m = {0};
        sb_printf(&m, "%llu\n%d\n", (unsigned long long)(size_t)j->c, j->keep);
        sb_puts(&m, sum);
        post_msg(10, sbs(&m));
        free(m.p);
        free(sum);
    }
    else
    { /* "!reason" = failed */
        char m[420];
        snprintf(m, sizeof m, "!%s", err[0] ? err : "unknown error");
        post_msg(10, m);
    }
    free(j->txt);
    free(j);
    return 0;
}
static void compact_start(Conv *c)
{
    CJob *j;
    HANDLE h;
    int *p, n, cut, k;
    SB t = {0};
    size_t limit;
    if (g_compacting || !G.autocompact || !g_nctx)
        return;
    n = conv_path(c, &p);
    if (n < 8 || (g_clast_fail && c == g_clast_conv && n < g_clast_n + CRETRY))
    {
        free(p);
        return;
    }
    /* Cut before a user message so assistant tool_calls never get separated from their results.
       A long agent run may have no user message after the cut point: then cut before an assistant
       message instead - its tool results follow it, so the pairing still holds. */
    cut = n * 7 / 10;
    for (k = cut; k < n && c->n[p[k]].role != ROLE_USER; k++)
        ;
    if (k >= n)
        for (k = cut; k < n && c->n[p[k]].role != ROLE_ASSIST; k++)
            ;
    if (k >= n || k < 4)
    {
        free(p);
        return;
    }
    /* keep the request well inside the server context (~3 chars per token, half the window) */
    limit = (G.ctx_limit > 0 ? (size_t)G.ctx_limit : (size_t)g_nctx) * 3 / 2;
    compact_transcript(&t, c, p, k, 6000);
    if (t.n > limit)
    {
        free(t.p);
        memset(&t, 0, sizeof t);
        compact_transcript(&t, c, p, k, 800);
    }
    if (t.n > limit)
    { /* many short messages: clip the whole transcript (keeps its start and its most recent part) */
        SB t2 = {0};
        compact_clip(&t2, t.p, limit);
        free(t.p);
        t = t2;
    }
    g_clast_conv = c;
    g_clast_n = n;
    j = (CJob *)calloc(1, sizeof *j);
    j->c = c;
    j->keep = p[k];
    j->txt = t.p ? t.p : xstrdup("");
    free(p);
    g_compacting = 1;
    set_status("Context is low - compressing older messages (can take a few minutes with reasoning models) ...");
    h = CreateThread(NULL, 0, compact_worker, j, 0, NULL);
    if (h)
        CloseHandle(h);
    else
    {
        g_compacting = 0;
        free(j->txt);
        free(j);
    }
}

#endif /* LLAMAC_COMPACT_H */
