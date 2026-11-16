/* config.h - Settings struct, field table, serialisation (portable) */
#ifndef LLAMAC_CONFIG_H
#define LLAMAC_CONFIG_H

/* ======================= settings ======================= */
typedef struct
{
    char url[256], key[256], model[512], sys[4096], sysname[80], stop[256], custom[2048], kwargs[512], rfmt[24];
    /* sampling overrides are strings: EMPTY = "not set -> use the llama-server value" */
    char temp[24], top_k[24], top_p[24], min_p[24], rep[24], pres[24], freq[24], dry[24], maxtok[24], seed[24];
    int dark, thoughts, stats, smartpaste;
    char fsroot[512], toolmax[24];           /* file-tool root folder, max tool rounds per message */
    int tools_on, fswrite, autoappr, mem_on; /* enable tools, allow file writes, auto-approve every call */
    int ctx_limit;                           /* 0 = unknown/disabled */
    int autocompact;                         /* 1 = compress when free context is low */
    int compact_pct;                         /* compress at this % of the context, 0 = auto */
    int shell;                               /* 1 = offer the run_command tool (runs commands as the user) */
    int fade;                                /* 1 = fade-in animation for streamed text */
    char remind[1024];                       /* appended to the latest user message at send time (not stored) */
    char verify[512];                        /* command run after a tool round that changed files */
    char rbud[24];                           /* reasoning budget override (reasoning_budget_tokens), empty = not sent */
    int toolset;                             /* 0 all tools, 1 chat tools only (date, memory, guides, MCP), 2 none */
    int kvslot;                              /* 1 = save/restore the server KV cache per chat (--slot-save-path) */
    int logprobs;                            /* 1 = request token probabilities (only possible without tools) */
    int precompact;                          /* 1 = prepare compression while idle, before the limit is reached */
    int budget;                              /* 1 = fit each request into the context (elide / clip old tool output) */
    int sumbud;                              /* reasoning budget for the compression summary, 0 = not sent */
    char kvdir[512];                         /* the server's --slot-save-path as seen from here (relative = exe folder) */
    int kvmax;                               /* GB the llamac-*.bin files in kvdir may use, 0 = no limit */
} Cfg;
/* kind: 0 text, 1 number override (string, empty = server default), 2 integer, 3 checkbox */
typedef struct
{
    const char *key, *label;
    int kind;
    size_t off, size;
    const char *rkey, *pkey;
} Field;
#define FLD(k, l, t, m, rk, pk) {k, l, t, offsetof(Cfg, m), sizeof(((Cfg *)0)->m), rk, pk}
static const Field FIELDS[] = {
    FLD("url", "Server URL", 0, url, NULL, NULL),
    FLD("key", "API key", 0, key, NULL, NULL),
    FLD("model", NULL, 0, model, NULL, NULL),
    FLD("sys", "System prompt", 0, sys, NULL, NULL),
    FLD("sysname", NULL, 0, sysname, NULL, NULL), /* name of the selected saved prompt */
    FLD("kwargs", "Jinja template kwargs (JSON)", 0, kwargs, NULL, NULL),
    FLD("rfmt", "Reasoning format (auto/none/deepseek)", 0, rfmt, NULL, NULL),
    FLD("stop", "Stop strings (separate with |)", 0, stop, NULL, NULL),
    FLD("custom", "Extra request JSON  {\"k\":v}", 0, custom, NULL, NULL),
    FLD("dark", "Dark theme", 3, dark, NULL, NULL),
    FLD("thoughts", "Show reasoning blocks", 3, thoughts, NULL, NULL),
    FLD("stats", "Show generation speed", 3, stats, NULL, NULL),
    FLD("smartpaste", "Long paste becomes attachment", 3, smartpaste, NULL, NULL),
    FLD("tools", "Enable tools (file access + MCP)", 3, tools_on, NULL, NULL),
    FLD("mem", "Long-term memory (saved facts)", 3, mem_on, NULL, NULL),
    FLD("fsroot", "File tools root folder", 0, fsroot, NULL, NULL),
    FLD("fswrite", "Allow file write / edit tools", 3, fswrite, NULL, NULL),
    FLD("shell", "Allow shell command tool (NOT sandboxed)", 3, shell, NULL, NULL),
    FLD("fade", "Fade-in animation while streaming", 3, fade, NULL, NULL),
    FLD("autoappr", "Auto-approve ALL tool calls (unsafe)", 3, autoappr, NULL, NULL),
    FLD("toolmax", "Max tool rounds per message", 0, toolmax, NULL, NULL),
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
    FLD("ctxlimit", "Context limit (tokens, 0 = server)", 2, ctx_limit, NULL, NULL), /* int: kind 1 would write text into it */
    FLD("autocompact", "Auto-compress when context is low", 3, autocompact, NULL, NULL),
    FLD("compactpct", "Compress at % of context (0 = auto)", 2, compact_pct, NULL, NULL),
    FLD("rbud", "Reasoning budget", 1, rbud, "reasoning_budget_tokens", NULL),
    FLD("remind", "Reminder", 0, remind, NULL, NULL),
    FLD("verify", "Verify command", 0, verify, NULL, NULL),
    FLD("toolset", "Tool set", 2, toolset, NULL, NULL),
    FLD("kvslot", "Keep KV cache per chat (server needs --slot-save-path)", 3, kvslot, NULL, NULL),
    FLD("logprobs", "Token confidence shading (only without tools)", 3, logprobs, NULL, NULL),
    FLD("precompact", "Prepare compression early, while idle", 3, precompact, NULL, NULL),
    FLD("budget", "Fit requests into the context", 3, budget, NULL, NULL),
    FLD("sumbud", "Summary reasoning budget (tokens, 0 = none)", 2, sumbud, NULL, NULL),
    FLD("kvdir", "KV slot folder", 0, kvdir, NULL, NULL),
    FLD("kvmax", "KV folder limit (GB)", 2, kvmax, NULL, NULL),
};
#define NFIELDS ((int)(sizeof FIELDS / sizeof FIELDS[0]))
static int fidx(const char *key)
{
    for (int i = 0; i < NFIELDS; i++)
        if (!strcmp(FIELDS[i].key, key))
            return i;
    return -1;
}

static void cfg_defaults(Cfg *g)
{
    memset(g, 0, sizeof *g);
    strcpy(g->url, "http://127.0.0.1:8080");
    g->dark = 1;
    g->thoughts = 1;
    g->stats = 1;
    g->smartpaste = 1;
    g->tools_on = 1;
    strcpy(g->toolmax, "25");
    g->fade = 1;
    g->mem_on = 1;
    g->ctx_limit = 0;
    g->autocompact = 1;
    g->precompact = 1;
    g->budget = 1;
    g->sumbud = 1024;
    g->kvmax = 10;
}
/* default_generation_settings.params from GET /props -> out[NFIELDS] ("" = unknown) */
static void srv_parse(J *props, char out[][32])
{
    J *dgs = j_get(props, "default_generation_settings"), *params = j_get(dgs, "params");
    if (!params)
        params = dgs;
    for (int i = 0; i < NFIELDS; i++)
    {
        out[i][0] = 0;
        if (FIELDS[i].kind != 1 || !FIELDS[i].pkey)
            continue;
        J *v = j_get(params, FIELDS[i].pkey);
        if (v && v->t == J_NUM)
        {
            double d = v->num;
            if (!strcmp(FIELDS[i].key, "seed") && d > 2147483647.0)
                d = -1;
            fmt_num(out[i], d);
        }
    }
}
static void esc_to(SB *o, const char *s)
{
    for (; *s; s++)
    {
        if (*s == '\\')
            sb_puts(o, "\\\\");
        else if (*s == '\n')
            sb_puts(o, "\\n");
        else if (*s != '\r')
            sb_putc(o, *s);
    }
}
static void unesc_to(char *dst, size_t cap, const char *s)
{
    size_t n = 0;
    for (; *s && n + 1 < cap; s++)
    {
        if (*s == '\\' && s[1] == 'n')
        {
            dst[n++] = '\n';
            s++;
        }
        else if (*s == '\\' && s[1] == '\\')
        {
            dst[n++] = '\\';
            s++;
        }
        else
            dst[n++] = *s;
    }
    dst[n] = 0;
}
static void field_set(Cfg *g, const Field *f, const char *v)
{
    char *base = (char *)g + f->off;
    if (f->kind <= 1)
        unesc_to(base, f->size, v);
    else
        *(int *)base = atoi(v);
}
static void field_get(Cfg *g, const Field *f, SB *o)
{
    char *base = (char *)g + f->off;
    if (f->kind <= 1)
        esc_to(o, base);
    else
        sb_printf(o, "%d", *(int *)base);
}
static char *cfg_serialize(Cfg *g)
{
    SB o = {0};
    for (int i = 0; i < NFIELDS; i++)
    {
        sb_printf(&o, "%s=", FIELDS[i].key);
        field_get(g, &FIELDS[i], &o);
        sb_putc(&o, '\n');
    }
    return o.p;
}
static void cfg_parse(Cfg *g, char *txt)
{
    for (char *ln = strtok(txt, "\n"); ln; ln = strtok(NULL, "\n"))
    {
        char *eq = strchr(ln, '=');
        if (!eq)
            continue;
        *eq = 0;
        for (int i = 0; i < NFIELDS; i++)
            if (!strcmp(FIELDS[i].key, ln))
                field_set(g, &FIELDS[i], eq + 1);
    }
    /* older versions stored ctxlimit as clipped text (e.g. "200" for 20000): no real context is that small */
    if (g->ctx_limit < 1024)
        g->ctx_limit = 0;
    if (g->compact_pct < 0)
        g->compact_pct = 0;
    if (g->toolset < 0 || g->toolset > 2)
        g->toolset = 0;
    if (g->sumbud < 0)
        g->sumbud = 0;
    if (g->kvmax < 0)
        g->kvmax = 0;
}

#endif /* LLAMAC_CONFIG_H */
