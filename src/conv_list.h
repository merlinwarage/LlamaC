/* conv_list.h - Sidebar conversation list */
#ifndef LLAMAC_CONV_LIST_H
#define LLAMAC_CONV_LIST_H

/* ---------- conversation list ---------- */
static int cmp_mod(const void *a, const void *b) {
    long long x = (*(Conv *const *)a)->mod, y = (*(Conv *const *)b)->mod; return x < y ? 1 : x > y ? -1 : 0;
}
static int conv_match(Conv *c, const char *q) {
    if (StrStrIA(c->name, q)) return 1;
    for (int i = 0; i < c->cnt; i++) if (StrStrIA(c->n[i].content, q)) return 1;
    return 0;
}
static void refresh_list(void) {
    qsort(convs, (size_t)nconv, sizeof(Conv *), cmp_mod);
    char *q = get_text(hSearch); int sel = -1;
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    free(lmap); lmap = (int *)malloc((size_t)(nconv + 1) * sizeof(int)); nmap = 0;
    for (int i = 0; i < nconv; i++) {
        if (q[0] && !conv_match(convs[i], q)) continue;
        int idx = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"");
        lmap[nmap++] = i; if (convs[i] == cur) sel = idx;
    }
    free(q);
    if (sel >= 0) SendMessageW(hList, LB_SETCURSEL, (WPARAM)sel, 0);
}
static void select_conv(Conv *c) { cur = c; refresh_list(); render_all(); }
static void new_chat(void) {
    if (busy) return;
    Conv *c = conv_new(); convs_add(c); select_conv(c); SetFocus(hInput);
}

#endif /* LLAMAC_CONV_LIST_H */
