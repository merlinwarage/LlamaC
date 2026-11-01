/* chat_io.h - WebUI-compatible chat export/import (portable) */
#ifndef LLAMAC_CHAT_IO_H
#define LLAMAC_CHAT_IO_H

/* ======================= (de)serialisation, WebUI-compatible export ======================= */
static void att_json(SB *s, Att *a) {
    if (a->kind == ATT_IMAGE) { sb_puts(s, "{\"type\":\"imageFile\",\"name\":"); sb_json(s, a->name); sb_puts(s, ",\"base64Url\":"); sb_json(s, a->data); }
    else if (a->kind == ATT_TEXT) { sb_puts(s, "{\"type\":\"textFile\",\"name\":"); sb_json(s, a->name); sb_puts(s, ",\"content\":"); sb_json(s, a->data); }
    else { sb_puts(s, "{\"type\":\"audioFile\",\"name\":"); sb_json(s, a->name); sb_puts(s, ",\"base64Data\":"); sb_json(s, a->data); sb_puts(s, ",\"mimeType\":"); sb_json(s, a->mime); }
    sb_putc(s, '}');
}
static void conv_json(SB *s, Conv *c) {
    char *mark = (char *)calloc((size_t)c->cnt, 1); int *st = (int *)malloc((size_t)c->cnt * sizeof(int)); int sp = 0, firstm = 1;
    st[sp++] = c->root; mark[c->root] = 1;
    while (sp) { int i = st[--sp]; for (int k = 0; k < c->n[i].nk; k++) { int j = c->n[i].kids[k]; if (!mark[j]) { mark[j] = 1; st[sp++] = j; } } }
    sb_puts(s, "{\"conv\":{\"id\":"); sb_json(s, c->id);
    sb_puts(s, ",\"name\":"); sb_json(s, c->name);
    sb_printf(s, ",\"lastModified\":%lld,\"currNode\":", c->mod); sb_json(s, c->n[c->leaf].id);
    sb_puts(s, "},\"messages\":[");
    for (int i = 0; i < c->cnt; i++) {
        Node *m = &c->n[i]; if (!mark[i]) continue;
        if (!firstm) sb_putc(s, ',');
        firstm = 0;
        sb_puts(s, "{\"id\":"); sb_json(s, m->id);
        sb_puts(s, ",\"convId\":"); sb_json(s, c->id);
        sb_printf(s, ",\"type\":\"%s\",\"timestamp\":%lld,\"role\":\"%s\",\"content\":", m->role == ROLE_ROOT ? "root" : "text", m->ts, role_name(m->role));
        sb_json(s, m->content);
        sb_puts(s, ",\"parent\":"); if (m->parent >= 0) sb_json(s, c->n[m->parent].id); else sb_puts(s, "null");
        sb_puts(s, ",\"reasoningContent\":"); sb_json(s, m->reasoning);
        sb_puts(s, ",\"children\":[");
        for (int k = 0; k < m->nk; k++) { if (k) sb_putc(s, ','); sb_json(s, c->n[m->kids[k]].id); }
        sb_puts(s, "]");
        if (m->na) { sb_puts(s, ",\"extra\":["); for (int k = 0; k < m->na; k++) { if (k) sb_putc(s, ','); att_json(s, &m->att[k]); } sb_putc(s, ']'); }
        sb_putc(s, '}');
    }
    sb_puts(s, "]}");
    free(mark); free(st);
}
static int role_from(const char *r) { if (!r) return ROLE_USER; if (!strcmp(r, "assistant")) return ROLE_ASSIST; if (!strcmp(r, "system")) return ROLE_SYS; return ROLE_USER; }
static Conv *conv_from_json(J *o, int newid) {
    J *cj = j_get(o, "conv"), *ms = j_get(o, "messages");
    if (!cj || !ms || ms->t != J_ARR) return NULL;
    Conv *c = (Conv *)calloc(1, sizeof(Conv)); int m = 0, i = 0; const char *s;
    for (J *e = ms->child; e; e = e->next) m++;
    const char **pids = (const char **)calloc((size_t)m + 1, sizeof(char *));
    c->root = -1;
    if (!newid && (s = j_str(j_get(cj, "id"))) != NULL) { strncpy(c->id, s, 39); c->id[39] = 0; } else mkid(c->id);
    c->name = xstrdup((s = j_str(j_get(cj, "name"))) ? s : "Imported chat");
    c->mod = (long long)j_num(j_get(cj, "lastModified"), (double)time(NULL) * 1000);
    for (J *e = ms->child; e; e = e->next, i++) {
        const char *type = j_str(j_get(e, "type")); int isroot = type && !strcmp(type, "root");
        int idx = node_add(c, -1, isroot ? ROLE_ROOT : role_from(j_str(j_get(e, "role")))); Node *nd = &c->n[idx];
        if ((s = j_str(j_get(e, "id"))) != NULL) { strncpy(nd->id, s, 39); nd->id[39] = 0; }
        if ((s = j_str(j_get(e, "content"))) != NULL) setstr(&nd->content, s);
        if ((s = j_str(j_get(e, "reasoningContent"))) != NULL) setstr(&nd->reasoning, s);
        nd->ts = (long long)j_num(j_get(e, "timestamp"), (double)nd->ts);
        pids[idx] = j_str(j_get(e, "parent"));
        if (isroot && c->root < 0) c->root = idx;
        for (J *x = (j_get(e, "extra") ? j_get(e, "extra")->child : NULL); x; x = x->next) {
            const char *t = j_str(j_get(x, "type")), *nm = j_str(j_get(x, "name"));
            if (!t) continue;
            if (!strcmp(t, "imageFile")) att_add(nd, ATT_IMAGE, nm, j_str(j_get(x, "base64Url")), "");
            else if (!strcmp(t, "audioFile")) att_add(nd, ATT_AUDIO, nm, j_str(j_get(x, "base64Data")), j_str(j_get(x, "mimeType")));
            else if (j_str(j_get(x, "content"))) att_add(nd, ATT_TEXT, nm, j_str(j_get(x, "content")), "");
        }
    }
    if (c->root < 0) c->root = node_add(c, -1, ROLE_ROOT);
    for (i = 0; i < m; i++) {
        if (i == c->root) continue;
        int p = c->root;
        if (pids[i]) for (int k = 0; k < c->cnt; k++) if (!strcmp(c->n[k].id, pids[i])) { p = k; break; }
        if (p == i) p = c->root;
        c->n[i].parent = p; kid_add(c, p, i);
    }
    c->leaf = -1;
    if ((s = j_str(j_get(cj, "currNode"))) != NULL)
        for (int k = 0; k < c->cnt; k++) if (!strcmp(c->n[k].id, s)) {
            int x = k, g = 0; while (x >= 0 && x != c->root && g++ < c->cnt) x = c->n[x].parent;
            if (x == c->root) c->leaf = k;
            break;
        }
    if (c->leaf < 0) c->leaf = deepest(c, c->root);
    free(pids);
    return c;
}

#endif /* LLAMAC_CHAT_IO_H */
