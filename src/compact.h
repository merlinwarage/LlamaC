/* compact.h - automatic session compression when free context is low */
#ifndef LLAMAC_COMPACT_H
#define LLAMAC_COMPACT_H

static int compact_tokens(Conv *c, int upto)
{
    int n = 0;
    for (int i = 0; i <= upto && i < c->cnt; i++)
    {
        Node *m = &c->n[i];
        n += (int)(strlen(m->content) / 4) + (int)(strlen(m->reasoning) / 4);
        for (int k = 0; k < m->na; k++)
            if (m->att[k].kind == ATT_TEXT)
                n += (int)(strlen(m->att[k].data) / 4);
    }
    return n;
}

/* one-shot non-streaming summary request; returns the summary text or NULL */
static char *compact_summarize(Conv *c, int from, int to)
{
    SB body = {0}, txt = {0};
    char *raw, *out = NULL;
    J *r, *ch, *msg;
    sb_puts(&body, "{\"messages\":[");
    sb_puts(&body, "{\"role\":\"system\",\"content\":");
    sb_json(&body, "Summarize this conversation for continuity. Keep the user's goal, decisions, file paths, names, constraints, open questions and current state. Dense and factual, under 600 words. No commentary.");
    sb_puts(&body, "},{\"role\":\"user\",\"content\":\"--- CONVERSATION ---\\n");
    for (int i = from; i <= to && i < c->cnt; i++)
    {
        Node *m = &c->n[i];
        if (m->role == ROLE_ROOT || m->role == ROLE_SYS)
            continue;
        sb_puts(&txt, role_name(m->role));
        sb_puts(&txt, ": ");
        sb_puts(&txt, m->content);
        sb_putc(&txt, '\n');
        for (int k = 0; k < m->na; k++)
            if (m->att[k].kind == ATT_TEXT)
            {
                sb_puts(&txt, "[file ");
                sb_puts(&txt, m->att[k].name);
                sb_puts(&txt, "]\n");
                sb_puts(&txt, m->att[k].data);
                sb_putc(&txt, '\n');
            }
    }
    sb_json(&body, sbs(&txt));
    sb_puts(&body, "\"}],\"stream\":false,\"max_tokens\":800,\"temperature\":0.2}");
    free(txt.p);

    raw = http_req(L"POST", "/v1/chat/completions", sbs(&body));
    free(body.p);
    if (!raw)
        return NULL;
    r = json_parse(raw);
    free(raw);
    ch = j_get(r, "choices");
    msg = j_get(j_idx(ch, 0), "message");
    {
        const char *s = j_str(j_get(msg, "content"));
        if (s && *s)
            out = xstrdup(s);
    }
    j_free(r);
    return out;
}

typedef struct
{
    Conv *c;
    int from, to;
} CJob;
static DWORD WINAPI compact_worker(LPVOID arg)
{
    CJob *j = (CJob *)arg;
    char *sum = compact_summarize(j->c, j->from, j->to);
    if (sum)
    {
        SB m = {0};
        sb_printf(&m, "%d\n%d\n%s", j->from, j->to, sum);
        post_msg(10, sbs(&m));
        free(m.p);
        free(sum);
    }
    else
    {
        post_msg(10, ""); /* empty = failed, just reset the flag */
    }
    free(j);
    return 0;
}

static void compact_start(Conv *c)
{
    CJob *j;
    HANDLE h;
    int *p, n, cut;
    if (g_compacting || !G.autocompact || !g_nctx)
        return;
    n = conv_path(c, &p);
    free(p);
    if (n < 8)
        return;
    cut = n * 7 / 10;
    if (cut < 4)
        return;
    j = (CJob *)calloc(1, sizeof *j);
    j->c = c;
    j->from = 1;
    j->to = cut;
    g_compacting = 1;
    set_status("Context is low - compressing older messages ...");
    h = CreateThread(NULL, 0, compact_worker, j, 0, NULL);
    if (h)
        CloseHandle(h);
    else
    {
        g_compacting = 0;
        free(j);
    }
}

#endif /* LLAMAC_COMPACT_H */