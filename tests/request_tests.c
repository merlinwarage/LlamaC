/* request_tests.c - portable tests for the request builder: tool-result stubs, the reminder, context fitting.
   Build: cl /nologo /W3 /utf-8 tests\request_tests.c   Writes the built bodies to the folder given as argv[1]
   (optional) so they can be sent to a real llama-server. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#include "../src/util.h"
#include "../src/json.h"
#include "../src/config.h"
#include "../src/chat_tree.h"
#include "../src/regex.h"
#include "../src/textops.h"
#include "../src/tools_core.h"
#include "../src/request.h"

static const char *g_tools_json = NULL;
static char *tools_schema_build(void) { return g_tools_json ? xstrdup(g_tools_json) : NULL; }
static char *mem_digest_for(const char *q)
{
    (void)q;
    return NULL;
}

static int fails, runs;
#define CHECK(c)                                                \
    do                                                          \
    {                                                           \
        runs++;                                                 \
        if (!(c))                                               \
        {                                                       \
            fails++;                                            \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                       \
    } while (0)

static int add(Conv *c, int parent, int role, const char *text)
{
    int i = node_add(c, parent, role);
    setstr(&c->n[i].content, text);
    c->leaf = i;
    return i;
}
static char *big(char ch, size_t n)
{
    char *s = (char *)malloc(n + 1);
    memset(s, ch, n);
    s[n] = 0;
    return s;
}
/* user asks, assistant calls read_file, tool returns `size` chars, assistant answers; repeated `turns` times */
static Conv *agent_chat(int turns, size_t size)
{
    Conv *c = conv_new();
    int at = c->root;
    for (int t = 0; t < turns; t++)
    {
        char id[32], calls[200], q[64];
        char *r = big((char)('a' + t % 26), size);
        int tn;
        snprintf(q, sizeof q, "Question %d", t);
        at = add(c, at, ROLE_USER, q);
        snprintf(id, sizeof id, "call_%d", t);
        snprintf(calls, sizeof calls, "[{\"id\":\"%s\",\"type\":\"function\",\"function\":{\"name\":\"read_file\",\"arguments\":\"{\\\"path\\\":\\\"f%d.c\\\"}\"}}]", id, t);
        at = add(c, at, ROLE_ASSIST, "");
        setstr(&c->n[at].tcalls, calls);
        tn = add(c, at, ROLE_TOOL, r);
        setstr(&c->n[tn].tcid, id);
        setstr(&c->n[tn].tname, "read_file");
        at = add(c, tn, ROLE_ASSIST, "Answer.");
        free(r);
    }
    return c;
}
static const char *g_out;
static void dump(const char *name, const char *body)
{
    char p[512];
    FILE *f;
    if (!g_out)
        return;
    snprintf(p, sizeof p, "%s/%s.json", g_out, name);
    f = fopen(p, "wb");
    if (f)
    {
        fwrite(body, 1, strlen(body), f);
        fclose(f);
    }
}
static int valid_json(const char *s)
{
    J *j = json_parse_strict(s);
    int ok = j != NULL;
    j_free(j);
    return ok;
}

static void t_plain(Cfg *g)
{
    Conv *c = agent_chat(3, 2000);
    char *b;
    g_req_lim = 0;
    b = build_request(c, g);
    CHECK(valid_json(b));
    CHECK(strstr(b, "\"return_progress\":true") != NULL);
    CHECK(strstr(b, "Earlier result of") == NULL);
    dump("plain", b);
    free(b);
    conv_free(c);
}
static void t_elided(Cfg *g)
{
    Conv *c = agent_chat(3, 2000);
    char *b;
    for (int i = 0; i < c->cnt; i++)
        if (c->n[i].role == ROLE_TOOL && !strcmp(c->n[i].tcid, "call_0"))
            c->n[i].elided = 1;
    g_req_lim = 0;
    b = build_request(c, g);
    CHECK(valid_json(b));
    CHECK(strstr(b, "Earlier result of read_file({\\\"path\\\":\\\"f0.c\\\"})") != NULL);
    CHECK(strstr(b, "2000 characters") != NULL);
    CHECK(strstr(b, "aaaaaaaaaa") == NULL); /* call_0's content is gone */
    CHECK(strstr(b, "bbbbbbbbbb") != NULL);
    dump("elided", b);
    free(b);
    conv_free(c);
}
static void t_reminder(Cfg *g)
{
    Conv *c = agent_chat(2, 100);
    char *b, *first, *last;
    strcpy(g->remind, "Answer briefly.");
    g_req_lim = 0;
    b = build_request(c, g);
    CHECK(valid_json(b));
    first = strstr(b, "<reminder>");
    last = strstr(b, "Question 1");
    CHECK(first && last && first > last);                    /* on the latest user message */
    CHECK(first && strstr(first + 1, "<reminder>") == NULL); /* only once */
    dump("reminder", b);
    free(b);
    g->remind[0] = 0;
    conv_free(c);
}
static void t_fit(Cfg *g)
{
    /* 10 turns with 9000-char tool results = 90k chars; a 16k-token context at 3 chars/token cannot hold it */
    Conv *c = agent_chat(10, 9000);
    char *b;
    g->budget = 1;
    g_cpt = 3.0;
    g_req_lim = 16384;
    b = build_request(c, g);
    CHECK(valid_json(b));
    CHECK(g_req_fit_elided > 0);
    CHECK(strlen(b) < (size_t)((16384 - reply_reserve(g, 16384)) * 3.0) + 2000);
    CHECK(strstr(b, "jjjjjjjjjj") != NULL); /* the newest result is kept */
    CHECK(strstr(b, "Question 0") != NULL); /* stubs were enough: no turn left out */
    dump("fit", b);
    free(b);
    /* one huge result in the current turn: it gets clipped */
    {
        Conv *d = agent_chat(1, 200000);
        g_req_lim = 16384;
        b = build_request(d, g);
        CHECK(valid_json(b));
        CHECK(g_req_fit_clipped == 1);
        CHECK(strstr(b, "characters left out to fit the context") != NULL);
        CHECK(strlen(b) < 50000);
        dump("fit_clip", b);
        free(b);
        conv_free(d);
    }
    /* tiny context: early turns are left out, starting at a user message */
    g_req_lim = 2048;
    b = build_request(c, g);
    CHECK(valid_json(b));
    CHECK(g_req_fit_dropped > 0);
    CHECK(strstr(b, "were left out of this request") != NULL);
    dump("fit_drop", b);
    free(b);
    g_req_lim = 0;
    conv_free(c);
}
static void t_logprobs(Cfg *g)
{
    Conv *c = conv_new();
    char *b;
    add(c, c->root, ROLE_USER, "Hi");
    g->logprobs = 1;
    g_tools_json = NULL;
    b = build_request(c, g);
    CHECK(strstr(b, "\"logprobs\":true") != NULL);
    free(b);
    g_tools_json = "[{\"type\":\"function\",\"function\":{\"name\":\"x\",\"description\":\"d\",\"parameters\":{\"type\":\"object\",\"properties\":{}}}}]";
    b = build_request(c, g);
    CHECK(strstr(b, "\"logprobs\"") == NULL); /* not allowed together with tools */
    CHECK(valid_json(b));
    free(b);
    g_tools_json = NULL;
    g->logprobs = 0;
    conv_free(c);
}
static void t_prefill(Cfg *g)
{
    Conv *c = conv_new();
    char *b;
    int u = add(c, c->root, ROLE_USER, "Count to ten.");
    add(c, u, ROLE_ASSIST, "1, 2, 3, 4,");
    b = build_request(c, g);
    CHECK(valid_json(b));
    CHECK(strstr(b, "{\"role\":\"assistant\",\"content\":\"1, 2, 3, 4,\"}]") != NULL); /* last message: continued */
    dump("prefill", b);
    free(b);
    conv_free(c);
}

/* a screenshot attached to a tool result goes to the model as a user message right after the tool results */
static void t_tool_image(Cfg *g)
{
    Conv *c = conv_new();
    char *b, *tool, *img;
    int u = add(c, c->root, ROLE_USER, "Look at my screen"), a, t1, t2;
    a = add(c, u, ROLE_ASSIST, "");
    setstr(&c->n[a].tcalls, "[{\"id\":\"c1\",\"type\":\"function\",\"function\":{\"name\":\"screenshot\",\"arguments\":\"{}\"}},"
                            "{\"id\":\"c2\",\"type\":\"function\",\"function\":{\"name\":\"system_info\",\"arguments\":\"{}\"}}]");
    t1 = add(c, a, ROLE_TOOL, "Screenshot of the primary screen, 10 x 10 pixels; the image follows.");
    setstr(&c->n[t1].tcid, "c1");
    setstr(&c->n[t1].tname, "screenshot");
    att_add(&c->n[t1], ATT_IMAGE, "screenshot.png", "data:image/png;base64,AAAA", "image/png");
    t2 = add(c, t1, ROLE_TOOL, "OS: Windows");
    setstr(&c->n[t2].tcid, "c2");
    setstr(&c->n[t2].tname, "system_info");
    g_req_lim = 0;
    b = build_request(c, g);
    CHECK(valid_json(b));
    tool = strstr(b, "\"tool_call_id\":\"c2\"");
    img = strstr(b, "\"image_url\":{\"url\":\"data:image/png;base64,AAAA\"}");
    CHECK(tool && img && img > tool); /* after the last tool result of the round, not between them */
    CHECK(strstr(b, "{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"Image returned by the tool call above:\"}") != NULL);
    dump("tool_image", b);
    free(b);
    c->n[t1].elided = 1; /* an old, shortened result does not send its image again */
    b = build_request(c, g);
    CHECK(strstr(b, "image_url") == NULL);
    free(b);
    conv_free(c);
}

int main(int argc, char **argv)
{
    Cfg g;
    cfg_defaults(&g);
    g_out = argc > 1 ? argv[1] : NULL;
    t_plain(&g);
    t_elided(&g);
    t_reminder(&g);
    t_fit(&g);
    t_logprobs(&g);
    t_prefill(&g);
    t_tool_image(&g);
    printf("%d checks, %d failed\n", runs, fails);
    return fails != 0;
}
