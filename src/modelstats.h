/* modelstats.h - Per-model statistics (data\model_stats.json).
   Tracks how a model behaves with this client - tool calls, invalid tool arguments, edit_file misses and
   whitespace-tolerant matches, replies cut at max_tokens - and calibrates characters per token from the server's
   prompt token counts, which the request fitting and the context estimates use. */
#ifndef LLAMAC_MODELSTATS_H
#define LLAMAC_MODELSTATS_H

typedef struct
{
    char *model;
    double cpt; /* characters per prompt token (0 = not calibrated) */
    long ncal, calls, badargs, edits, editmiss, editfuzzy, gens, lenstops;
} MStat;
static MStat *g_ms;
static int g_nms, g_capms, g_ms_loaded, g_ms_dirty;
static CRITICAL_SECTION g_ms_cs;
static int g_ms_init;
static void ms_lock(void)
{
    if (!g_ms_init)
    {
        InitializeCriticalSection(&g_ms_cs);
        g_ms_init = 1;
    }
    EnterCriticalSection(&g_ms_cs);
}
static void ms_unlock(void) { LeaveCriticalSection(&g_ms_cs); }
static MStat *ms_find_locked(const char *model, int create)
{
    wchar_t p[MAX_PATH];
    if (!g_ms_loaded)
    {
        size_t len;
        char *b;
        g_ms_loaded = 1;
        data_path(L"model_stats.json", p);
        b = (char *)read_file(p, &len);
        if (b)
        {
            J *r = json_parse(b), *a = j_get(r, "models");
            for (J *e = a ? a->child : NULL; e; e = e->next)
            {
                const char *m = j_str(j_get(e, "model"));
                MStat *s;
                if (!m)
                    continue;
                if (g_nms == g_capms)
                {
                    g_capms = g_capms ? g_capms * 2 : 8;
                    g_ms = (MStat *)realloc(g_ms, (size_t)g_capms * sizeof(MStat));
                }
                s = &g_ms[g_nms++];
                memset(s, 0, sizeof *s);
                s->model = xstrdup(m);
                s->cpt = j_num(j_get(e, "cpt"), 0);
                s->ncal = (long)j_num(j_get(e, "ncal"), 0);
                s->calls = (long)j_num(j_get(e, "calls"), 0);
                s->badargs = (long)j_num(j_get(e, "badargs"), 0);
                s->edits = (long)j_num(j_get(e, "edits"), 0);
                s->editmiss = (long)j_num(j_get(e, "editmiss"), 0);
                s->editfuzzy = (long)j_num(j_get(e, "editfuzzy"), 0);
                s->gens = (long)j_num(j_get(e, "gens"), 0);
                s->lenstops = (long)j_num(j_get(e, "lenstops"), 0);
            }
            j_free(r);
            free(b);
        }
    }
    for (int i = 0; i < g_nms; i++)
        if (!strcmp(g_ms[i].model, model))
            return &g_ms[i];
    if (!create)
        return NULL;
    if (g_nms == g_capms)
    {
        g_capms = g_capms ? g_capms * 2 : 8;
        g_ms = (MStat *)realloc(g_ms, (size_t)g_capms * sizeof(MStat));
    }
    memset(&g_ms[g_nms], 0, sizeof(MStat));
    g_ms[g_nms].model = xstrdup(model);
    return &g_ms[g_nms++];
}
static void ms_save(void)
{
    wchar_t p[MAX_PATH];
    SB o = {0};
    ms_lock();
    if (!g_ms_dirty)
    {
        ms_unlock();
        return;
    }
    sb_puts(&o, "{\"models\":[");
    for (int i = 0; i < g_nms; i++)
    {
        MStat *s = &g_ms[i];
        char num[40];
        sb_puts(&o, i ? ",\n{\"model\":" : "\n{\"model\":");
        sb_json(&o, s->model);
        fmt_num(num, s->cpt);
        sb_printf(&o, ",\"cpt\":%s,\"ncal\":%ld,\"calls\":%ld,\"badargs\":%ld,\"edits\":%ld,\"editmiss\":%ld,\"editfuzzy\":%ld,\"gens\":%ld,\"lenstops\":%ld}",
                  num, s->ncal, s->calls, s->badargs, s->edits, s->editmiss, s->editfuzzy, s->gens, s->lenstops);
    }
    sb_puts(&o, "\n]}\n");
    g_ms_dirty = 0;
    ms_unlock();
    data_path(L"model_stats.json", p);
    write_atomic(p, o.p, o.n);
    free(o.p);
}
/* characters per token for the current model (3.0 until calibrated) */
static double ms_cpt(void)
{
    double c = 3.0;
    ms_lock();
    {
        MStat *s = G.model[0] ? ms_find_locked(G.model, 0) : NULL;
        if (s && s->ncal >= 1 && s->cpt > 0)
            c = s->cpt;
    }
    ms_unlock();
    return c;
}
/* a generation finished: calibrate from the prompt size the server measured for the request we built */
static void ms_note_gen(int prompt_n, size_t chars, int lenstop)
{
    if (!G.model[0])
        return;
    ms_lock();
    {
        MStat *s = ms_find_locked(G.model, 1);
        s->gens++;
        s->lenstops += lenstop != 0;
        if (prompt_n >= 400 && chars > 0)
        {
            double r = (double)chars / prompt_n;
            if (r > 1.2 && r < 8)
            {
                s->cpt = s->ncal ? s->cpt * 0.8 + r * 0.2 : r;
                s->ncal++;
            }
        }
        g_ms_dirty = 1;
    }
    ms_unlock();
}
/* from the tool runner thread */
static void ms_note_tool(const char *rname, const char *res, int badargs)
{
    if (!G.model[0])
        return;
    ms_lock();
    {
        MStat *s = ms_find_locked(G.model, 1);
        s->calls++;
        s->badargs += badargs != 0;
        if (rname && !strcmp(rname, "edit_file"))
        {
            s->edits++;
            if (res && !strncmp(res, "Error: old_text was not found", 29))
                s->editmiss++;
            if (res && strstr(res, "(matched ignoring whitespace"))
                s->editfuzzy++;
        }
        g_ms_dirty = 1;
    }
    ms_unlock();
}
static void ms_report(SB *o)
{
    ms_lock();
    {
        MStat *s = G.model[0] ? ms_find_locked(G.model, 0) : NULL;
        if (!s)
            sb_puts(o, "No statistics for this model yet.\n");
        else
        {
            sb_printf(o, "Replies: %ld (cut at max tokens: %ld)\n", s->gens, s->lenstops);
            sb_printf(o, "Tool calls: %ld (invalid JSON arguments: %ld)\n", s->calls, s->badargs);
            sb_printf(o, "edit_file: %ld calls, %ld not found, %ld matched only ignoring whitespace\n", s->edits, s->editmiss, s->editfuzzy);
            if (s->ncal)
                sb_printf(o, "Characters per prompt token: %.2f (calibrated from %ld replies)\n", s->cpt, s->ncal);
            else
                sb_puts(o, "Characters per prompt token: 3.00 (default, not calibrated yet)\n");
        }
    }
    ms_unlock();
}

#endif /* LLAMAC_MODELSTATS_H */
