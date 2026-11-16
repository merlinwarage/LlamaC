/* docs_tests.c - tests for inflate.h and docs.h (PDF / DOCX / XLSX text).
   python tests/make_doc_fixtures.py <dir>   then   cl /nologo /W3 /utf-8 tests\docs_tests.c && docs_tests <dir>
   A browser-printed PDF (<dir>/browser.pdf, e.g. from msedge --headless --print-to-pdf) is checked when present. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <math.h>
#include <ctype.h>
#include "../src/util.h"
#include "../src/json.h"
#include "../src/inflate.h"
#include "../src/docs.h"

static int fails, runs, verbose;
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

static const char *g_dir;
static unsigned char *load(const char *name, size_t *n)
{
    char p[600];
    FILE *f;
    unsigned char *b;
    long L;
    snprintf(p, sizeof p, "%s/%s", g_dir, name);
    f = fopen(p, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    L = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (unsigned char *)malloc((size_t)L + 1);
    *n = fread(b, 1, (size_t)L, f);
    b[*n] = 0;
    fclose(f);
    return b;
}
static void t_zlib(const char *name)
{
    char bn[64], tn[64];
    size_t n1, n2;
    unsigned char *z, *t;
    SB o = {0};
    snprintf(bn, sizeof bn, "zlib_%s.bin", name);
    snprintf(tn, sizeof tn, "zlib_%s.txt", name);
    z = load(bn, &n1);
    t = load(tn, &n2);
    CHECK(z && t);
    if (!z || !t)
        return;
    CHECK(inflate_zlib(z, n1, &o, 1u << 30) == 0);
    CHECK(o.n == n2 && !memcmp(o.p, t, n2));
    free(o.p);
    free(z);
    free(t);
}
static char *doc(const char *name)
{
    size_t n;
    unsigned char *b = load(name, &n);
    char err[300], *t;
    if (!b)
        return NULL;
    t = doc_text(name, b, n, err, sizeof err);
    if (!t)
        printf("  %s: error: %s\n", name, err);
    else if (verbose)
        printf("----- %s -----\n%s\n", name, t);
    free(b);
    return t;
}
#define HAS(t, s) ((t) && strstr((t), (s)) != NULL)

int main(int argc, char **argv)
{
    char *t;
    if (argc < 2)
    {
        printf("usage: docs_tests <fixture dir> [-v]\n");
        return 2;
    }
    g_dir = argv[1];
    verbose = argc > 2;
    t_zlib("text9");
    t_zlib("text1");
    t_zlib("stored");
    t_zlib("noise");
    t_zlib("tiny");

    t = doc("sample.docx");
    CHECK(HAS(t, "# Quarterly report\n"));
    CHECK(HAS(t, "Sales & costs rose by 12%.\n"));
    CHECK(HAS(t, "- First point\n"));
    CHECK(HAS(t, "Árvíztűrő tükörfúrógép\ttab<here>\n"));
    CHECK(HAS(t, "Region | Total\n"));
    CHECK(HAS(t, "North | 1\xC2\xA0" "200\n"));
    CHECK(HAS(t, "End.\n"));
    free(t);

    t = doc("sample.xlsx");
    CHECK(HAS(t, "## Sheet: Budget & plan\n"));
    CHECK(HAS(t, "1: Item | Cost\n"));
    CHECK(HAS(t, "2: Rich text | 1250.5 |  | TRUE\n"));
    CHECK(HAS(t, "4: Inline <x> | [BZ] 7\n"));
    CHECK(HAS(t, "## Sheet: Notes\n"));
    CHECK(HAS(t, "5:  |  | Formula result\n"));
    CHECK(!HAS(t, "3:"));
    free(t);

    t = doc("simple.pdf");
    CHECK(HAS(t, "--- page 1 ---\nHello World\n"));
    CHECK(HAS(t, "Kerning works\n"));
    CHECK(HAS(t, "Café (paren)\n"));
    CHECK(HAS(t, "Hex string"));
    CHECK(HAS(t, "--- page 2 ---\nSecond page\nnext line via T*"));
    free(t);

    t = doc("cmap.pdf");
    CHECK(HAS(t, "--- page 1 ---\nÁrvíx\nű v"));
    free(t);

    {
        size_t n;
        unsigned char *b = load("browser.pdf", &n);
        if (b)
        {
            free(b);
            t = doc("browser.pdf");
            CHECK(HAS(t, "Browser PDF test"));
            CHECK(HAS(t, "The quick brown fox jumps over the lazy dog."));
            CHECK(HAS(t, "Árvíztűrő tükörfúrógép"));
            CHECK(HAS(t, "--- page 2 ---"));
            CHECK(HAS(t, "Second page text"));
            free(t);
        }
        else
            printf("(browser.pdf not present: skipped)\n");
    }
    {
        char err[200];
        CHECK(doc_text("x.pdf", (const unsigned char *)"hello", 5, err, sizeof err) == NULL && HAS(err, "not a PDF"));
        CHECK(doc_text("x.docx", (const unsigned char *)"\xD0\xCF\x11\xE0", 4, err, sizeof err) == NULL && HAS(err, "old .doc"));
    }
    printf("%d checks, %d failed\n", runs, fails);
    return fails != 0;
}
