/* memory.h - built-in long-term memory: memory.json store, 4 tools, system-prompt digest */
#ifndef LLAMAC_MEMORY_H
#define LLAMAC_MEMORY_H

#define MEM_MAX 500
#define MEM_DIGEST_CHARS 3500
typedef struct { char id[40]; char *text, *cat; int pinned, hits; long long ts; } Mem;
static Mem *g_mem; static int g_nmem, g_capmem, g_mem_loaded;
static CRITICAL_SECTION g_mcs; static int g_mcs_init;

static void mem_lock(void) { if (!g_mcs_init) { InitializeCriticalSection(&g_mcs); g_mcs_init = 1; } EnterCriticalSection(&g_mcs); }
static void mem_unlock(void) { LeaveCriticalSection(&g_mcs); }

static Mem *mem_push(const char *id, const char *text, const char *cat, int pinned, long long ts, int hits) {
    Mem *m;
    if (g_nmem == g_capmem) { g_capmem = g_capmem ? g_capmem * 2 : 64; g_mem = (Mem *)realloc(g_mem, (size_t)g_capmem * sizeof(Mem)); }
    m = &g_mem[g_nmem++]; memset(m, 0, sizeof *m);
    if (id && *id) strncpy(m->id, id, sizeof m->id - 1); else mkid(m->id);
    m->text = xstrdup(text ? text : ""); m->cat = xstrdup(cat && *cat ? cat : "general");
    m->pinned = pinned; m->ts = ts ? ts : (long long)time(NULL); m->hits = hits;
    return m;
}
static void mem_drop(int i) {
    free(g_mem[i].text); free(g_mem[i].cat);
    memmove(&g_mem[i], &g_mem[i + 1], (size_t)(g_nmem - i - 1) * sizeof(Mem)); g_nmem--;
}

static void mem_load_locked(void) {
    wchar_t p[MAX_PATH]; size_t len = 0; unsigned char *b; J *r, *arr;
    if (g_mem_loaded) return;
    g_mem_loaded = 1;
    data_path(L"memory.json", p);
    b = read_file(p, &len);
    if (!b) return;
    r = json_parse((char *)b); free(b);
    arr = j_get(r, "memories");
    for (J *e = arr ? arr->child : NULL; e && g_nmem < MEM_MAX; e = e->next) {
        const char *id = j_str(j_get(e, "id")), *tx = j_str(j_get(e, "text")), *ct = j_str(j_get(e, "cat"));
        J *pn = j_get(e, "pinned"), *ts = j_get(e, "ts"), *ht = j_get(e, "hits");
        if (!tx || !*tx) continue;
        mem_push(id, tx, ct, pn && pn->num != 0, ts ? (long long)ts->num : 0, ht ? (int)ht->num : 0);
    }
    j_free(r);
}

static int mem_save_locked(void) {
    wchar_t p[MAX_PATH]; SB s = {0}; int ok;
    data_path(L"memory.json", p);
    sb_puts(&s, "{\"memories\":[");
    for (int i = 0; i < g_nmem; i++) {
        Mem *m = &g_mem[i];
        if (i) sb_putc(&s, ',');
        sb_puts(&s, "\n{\"id\":"); sb_json(&s, m->id);
        sb_puts(&s, ",\"text\":"); sb_json(&s, m->text);
        sb_puts(&s, ",\"cat\":"); sb_json(&s, m->cat);
        sb_printf(&s, ",\"pinned\":%d,\"ts\":%lld,\"hits\":%d}", m->pinned ? 1 : 0, m->ts, m->hits);
    }
    sb_puts(&s, "\n]}\n");
    ok = write_file(p, sbs(&s), s.n);
    free(s.p);
    return ok;
}

static void mem_oneline(SB *o, const char *t) {
    for (; *t; t++) sb_putc(o, (*t == '\n' || *t == '\r') ? ' ' : *t);
}

/* text for the system prompt; NULL when memory is disabled (caller frees) */
static char *mem_digest(void) {
    SB o = {0};
    if (!G.mem_on) return NULL;
    mem_lock(); mem_load_locked();
    sb_puts(&o, "## Long-term memory\nFacts saved from earlier conversations with this user. Use them naturally; do not recite them unless asked.\n");
    if (g_nmem) {
        size_t start = o.n;
        for (int pass = 0; pass < 2; pass++)
            for (int i = g_nmem - 1; i >= 0; i--) {
                Mem *m = &g_mem[i];
                if ((m->pinned ? 0 : 1) != pass) continue;
                if (o.n - start > MEM_DIGEST_CHARS) break;
                sb_puts(&o, "- ["); sb_puts(&o, m->cat); sb_puts(&o, "] "); mem_oneline(&o, m->text); sb_putc(&o, '\n');
            }
    } else sb_puts(&o, "(nothing saved yet)\n");
    mem_unlock();
    if (G.tools_on)
        sb_puts(&o, "When you learn a durable fact about the user (identity, preferences, environment, decisions, corrections, recurring constraints) or they ask you to remember something, call memory_save immediately, without asking. Write each memory as a standalone sentence. Do not save small talk or facts already listed above. Use memory_search before answering questions about past context that is not listed.\n");
    return o.p;
}

/* ---------- tools ---------- */
static char *mem_msg(const char *a, const char *b) { SB s = {0}; sb_puts(&s, a); if (b) sb_puts(&s, b); return s.p; }

static char *mem_t_save(J *a) {
    const char *tx = j_str(j_get(a, "text")), *ct = j_str(j_get(a, "category")), *id = j_str(j_get(a, "id"));
    J *pn = j_get(a, "pinned"); int pinned = pn && pn->num != 0, idx = -1; char *r; char *t;
    size_t n;
    if (!tx) return xstrdup("Error: text is required");
    t = xstrdup(tx); n = strlen(t);
    while (n && (t[n - 1] == ' ' || t[n - 1] == '\n' || t[n - 1] == '\r')) t[--n] = 0;
    if (!*t) { free(t); return xstrdup("Error: text is empty"); }
    if (n > 1000) { n = 1000; while (n && ((unsigned char)t[n] & 0xC0) == 0x80) n--; t[n] = 0; }
    mem_lock(); mem_load_locked();
    if (id && *id) for (int i = 0; i < g_nmem; i++) if (!strcmp(g_mem[i].id, id)) { idx = i; break; }
    if (idx < 0) for (int i = 0; i < g_nmem; i++) if (!_stricmp(g_mem[i].text, t)) { idx = i; break; }
    if (idx >= 0) {
        Mem *m = &g_mem[idx];
        free(m->text); m->text = t; t = NULL;
        if (ct && *ct) { free(m->cat); m->cat = xstrdup(ct); }
        if (pn) m->pinned = pinned;
        m->ts = (long long)time(NULL);
        r = mem_msg("Updated memory ", m->id);
    } else {
        Mem *m;
        if (g_nmem >= MEM_MAX) {               /* drop the oldest unpinned entry */
            int old = -1;
            for (int i = 0; i < g_nmem; i++) if (!g_mem[i].pinned && (old < 0 || g_mem[i].ts < g_mem[old].ts)) old = i;
            if (old < 0) { mem_unlock(); free(t); return xstrdup("Error: memory is full of pinned entries"); }
            mem_drop(old);
        }
        m = mem_push(NULL, t, ct, pinned, 0, 0);
        r = mem_msg("Saved memory ", m->id);
    }
    if (!mem_save_locked()) { free(r); r = xstrdup("Error: could not write memory.json"); }
    mem_unlock(); free(t);
    return r;
}

static void mem_line(SB *o, const Mem *m) {
    sb_puts(o, m->id); sb_puts(o, m->pinned ? " | pinned | [" : " | [");
    sb_puts(o, m->cat); sb_puts(o, "] "); mem_oneline(o, m->text); sb_putc(o, '\n');
}

static char *mem_t_search(J *a) {
    const char *q = j_str(j_get(a, "query")); int lim = (int)j_num(j_get(a, "limit"), 10);
    char words[16][48]; int nw = 0; int score[MEM_MAX]; SB o = {0}; int shown = 0;
    if (!q || !*q) return xstrdup("Error: query is required");
    if (lim < 1) lim = 1;
    if (lim > 50) lim = 50;
    {
        char tmp[400]; strncpy(tmp, q, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0;
        for (char *w = strtok(tmp, " ,.;:!?\t\n"); w && nw < 16; w = strtok(NULL, " ,.;:!?\t\n"))
            if (strlen(w) > 1) { strncpy(words[nw], w, 47); words[nw][47] = 0; nw++; }
    }
    mem_lock(); mem_load_locked();
    for (int i = 0; i < g_nmem; i++) {
        score[i] = 0;
        for (int k = 0; k < nw; k++) if (StrStrIA(g_mem[i].text, words[k]) || StrStrIA(g_mem[i].cat, words[k])) score[i]++;
    }
    while (shown < lim) {
        int best = -1;
        for (int i = 0; i < g_nmem; i++) if (score[i] > 0 && (best < 0 || score[i] > score[best] || (score[i] == score[best] && g_mem[i].ts > g_mem[best].ts))) best = i;
        if (best < 0) break;
        mem_line(&o, &g_mem[best]); g_mem[best].hits++; score[best] = 0; shown++;
    }
    if (shown) mem_save_locked();
    mem_unlock();
    return o.p ? o.p : xstrdup("No matching memories.");
}

static char *mem_t_list(J *a) {
    const char *ct = j_str(j_get(a, "category")); SB o = {0}; int n = 0;
    mem_lock(); mem_load_locked();
    for (int i = g_nmem - 1; i >= 0 && n < 100; i--) {
        if (ct && *ct && _stricmp(g_mem[i].cat, ct)) continue;
        mem_line(&o, &g_mem[i]); n++;
    }
    mem_unlock();
    return o.p ? o.p : xstrdup("No memories saved.");
}

static char *mem_t_delete(J *a) {
    const char *id = j_str(j_get(a, "id")); char *r = NULL;
    if (!id || !*id) return xstrdup("Error: id is required");
    mem_lock(); mem_load_locked();
    for (int i = 0; i < g_nmem; i++) if (!strcmp(g_mem[i].id, id)) { mem_drop(i); mem_save_locked(); r = xstrdup("Deleted."); break; }
    mem_unlock();
    return r ? r : xstrdup("Error: no memory with that id");
}

/* NULL = not a memory tool */
static char *mem_call(const char *tool, J *a) {
    if (!strcmp(tool, "memory_save")) return mem_t_save(a);
    if (!strcmp(tool, "memory_search")) return mem_t_search(a);
    if (!strcmp(tool, "memory_list")) return mem_t_list(a);
    if (!strcmp(tool, "memory_delete")) return mem_t_delete(a);
    return NULL;
}

/* call after fs_register(), which clears every built-in (kind 0) tool first */
static void mem_register(void) {
    mem_lock(); mem_load_locked(); mem_unlock();
    if (!G.tools_on || !G.mem_on) return;
    tool_add("memory_save",
        "Save a durable fact about the user to long-term memory. Call this IMMEDIATELY and without asking when you learn a preference, identity or profile fact, environment detail, decision, correction, or recurring constraint, or when the user says to remember something. Do not save small talk or facts already in memory. Saving the same text again updates it.",
        "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\",\"description\":\"The fact as a standalone sentence, e.g. 'Prefers PowerShell over Bash for Windows scripts'\"},\"category\":{\"type\":\"string\",\"description\":\"profile, preference, project, decision, environment or constraint\"},\"pinned\":{\"type\":\"boolean\",\"description\":\"true for durable identity facts that must always be shown first\"},\"id\":{\"type\":\"string\",\"description\":\"Existing memory id to replace (optional)\"}},\"required\":[\"text\"]}",
        0, 0, "memory_save", 1, 0);
    tool_add("memory_search", "Search saved memories by keywords. Use before answering questions about the user's past context that is not already in the memory section of the system prompt.",
        "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\",\"description\":\"Default 10, max 50\"}},\"required\":[\"query\"]}",
        0, 0, "memory_search", 1, 0);
    tool_add("memory_list", "List saved memories (newest first), optionally only one category.",
        "{\"type\":\"object\",\"properties\":{\"category\":{\"type\":\"string\"}}}", 0, 0, "memory_list", 1, 0);
    tool_add("memory_delete", "Delete a saved memory by its id (ids come from memory_search or memory_list).",
        "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},\"required\":[\"id\"]}", 0, 0, "memory_delete", 0, 0);
}

#endif /* LLAMAC_MEMORY_H */