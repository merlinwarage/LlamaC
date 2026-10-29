/* chat_tree.h - Conversation tree: nodes, attachments, paths (portable) */
#ifndef LLAMAC_CHAT_TREE_H
#define LLAMAC_CHAT_TREE_H

/* ======================= chat tree ======================= */
enum { ROLE_USER, ROLE_ASSIST, ROLE_SYS, ROLE_ROOT };
enum { ATT_IMAGE, ATT_TEXT, ATT_AUDIO };
typedef struct { int kind; char *name, *data, *mime; } Att;
typedef struct {
    char id[40]; int parent, role;
    char *content, *reasoning;
    int *kids, nk, capk;
    Att *att; int na;
    long long ts;
    int think_open;
} Node;
typedef struct { char id[40]; char *name; long long mod; Node *n; int cnt, cap, root, leaf; } Conv;

static unsigned g_ctr;
static void mkid(char *out) { snprintf(out, 40, "%llx-%x", (unsigned long long)time(NULL), ++g_ctr); }
static void kid_add(Conv *c, int parent, int i) {
    Node *p = &c->n[parent];
    if (p->nk == p->capk) { p->capk = p->capk ? p->capk * 2 : 2; p->kids = (int *)realloc(p->kids, (size_t)p->capk * sizeof(int)); }
    p->kids[p->nk++] = i;
}
static int node_add(Conv *c, int parent, int role) {
    if (c->cnt == c->cap) { c->cap = c->cap ? c->cap * 2 : 16; c->n = (Node *)realloc(c->n, (size_t)c->cap * sizeof(Node)); }
    int i = c->cnt++; Node *n = &c->n[i];
    memset(n, 0, sizeof *n); mkid(n->id); n->parent = parent; n->role = role;
    n->content = xstrdup(""); n->reasoning = xstrdup(""); n->ts = (long long)time(NULL) * 1000;
    if (parent >= 0) kid_add(c, parent, i);
    return i;
}
static void att_add(Node *n, int kind, const char *name, const char *data, const char *mime) {
    n->att = (Att *)realloc(n->att, (size_t)(n->na + 1) * sizeof(Att));
    Att *a = &n->att[n->na++];
    a->kind = kind; a->name = xstrdup(name ? name : "file"); a->data = xstrdup(data ? data : ""); a->mime = xstrdup(mime ? mime : "");
}
static Conv *conv_new(void) {
    Conv *c = (Conv *)calloc(1, sizeof(Conv));
    mkid(c->id); c->name = xstrdup("New chat"); c->mod = (long long)time(NULL) * 1000;
    c->root = node_add(c, -1, ROLE_ROOT); c->leaf = c->root;
    return c;
}
static void conv_free(Conv *c) {
    for (int i = 0; i < c->cnt; i++) {
        Node *n = &c->n[i];
        free(n->content); free(n->reasoning); free(n->kids);
        for (int k = 0; k < n->na; k++) { free(n->att[k].name); free(n->att[k].data); free(n->att[k].mime); }
        free(n->att);
    }
    free(c->n); free(c->name); free(c);
}
static int deepest(Conv *c, int i) { while (c->n[i].nk) i = c->n[i].kids[c->n[i].nk - 1]; return i; }
static int conv_path(Conv *c, int **out) {
    int n = 0, i, g;
    for (i = c->leaf, g = 0; i >= 0 && c->n[i].role != ROLE_ROOT && g <= c->cnt; i = c->n[i].parent, g++) n++;
    int *a = (int *)malloc((size_t)(n + 1) * sizeof(int)); int k = n;
    for (i = c->leaf, g = 0; i >= 0 && k > 0 && c->n[i].role != ROLE_ROOT; i = c->n[i].parent, g++) a[--k] = i;
    *out = a; return n;
}

#endif /* LLAMAC_CHAT_TREE_H */
