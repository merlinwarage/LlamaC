/* textops_tests.c - portable tests for the text helpers (whitespace-tolerant edit, log folding, diff, BM25)
   Build: cl /nologo /W3 /utf-8 tests\textops_tests.c   (or: cc -std=c99 tests/textops_tests.c -lm) */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <math.h>
#include <ctype.h>
#include "../src/util.h"
#include "../src/regex.h"
#include "../src/textops.h"

static int fails, runs;
#define CHECK(c)                                                    \
    do                                                              \
    {                                                               \
        runs++;                                                     \
        if (!(c))                                                   \
        {                                                           \
            fails++;                                                \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);     \
        }                                                           \
    } while (0)

static void t_ws_replace(void)
{
    int cnt, line;
    char *r;
    /* indentation differs: the file uses 8 spaces, old_text 4; new_text gets the file's indentation */
    r = tx_ws_replace("int f(void)\n{\n        int a = 1;\n        return a;\n}\n",
                      "    int a = 1;\n    return a;", "    int a = 2;\n    if (a)\n        a++;\n    return a;", 0, &cnt, &line);
    CHECK(r && cnt == 1 && line == 3);
    CHECK(r && !strcmp(r, "int f(void)\n{\n        int a = 2;\n        if (a)\n            a++;\n        return a;\n}\n"));
    free(r);
    /* trailing spaces and inner whitespace width */
    r = tx_ws_replace("x  =   1;   \ny = 2;\n", "x = 1;", "x = 3;", 0, &cnt, &line);
    CHECK(r && !strcmp(r, "x = 3;\ny = 2;\n"));
    free(r);
    /* ambiguous without replace_all, both replaced with it */
    r = tx_ws_replace("a();\nb();\n  a();\n", "a();", "c();", 0, &cnt, &line);
    CHECK(!r && cnt == 2);
    r = tx_ws_replace("a();\nb();\n  a();\n", "a();", "c();", 1, &cnt, &line);
    CHECK(r && cnt == 2 && !strcmp(r, "c();\nb();\n  c();\n"));
    free(r);
    /* CRLF file keeps CRLF */
    r = tx_ws_replace("one\r\n  two\r\nthree\r\n", "two\nthree", "TWO\nTHREE", 0, &cnt, &line);
    CHECK(r && !strcmp(r, "one\r\n  TWO\r\n  THREE\r\n"));
    free(r);
    /* blank edge lines of old_text are ignored, and the same number dropped from new_text */
    r = tx_ws_replace("p\nq\nr\n", "\nq\n", "\nQ\n", 0, &cnt, &line);
    CHECK(r && !strcmp(r, "p\nQ\nr\n"));
    free(r);
    /* no match */
    r = tx_ws_replace("abc\n", "abd", "x", 0, &cnt, &line);
    CHECK(!r && cnt == 0);
    /* whitespace inside a token must still differ */
    r = tx_ws_replace("ab\n", "a b", "x", 0, &cnt, &line);
    CHECK(!r && cnt == 0);
}

static void t_fold(void)
{
    char *r = tx_fold("\x1b[31mred\x1b[0m\nx\nx\nx\nprogress 10%\rprogress 100%\nwarning C4996: 'strcpy' may be unsafe in file\nok\nwarning C4996: 'strcpy' may be unsafe in file\n");
    CHECK(strstr(r, "red\n") == r);
    CHECK(strstr(r, "[previous line repeated 2 more times]") != NULL);
    CHECK(strstr(r, "progress 10%") == NULL && strstr(r, "progress 100%") != NULL);
    CHECK(strstr(r, "[1 repeated line omitted]") != NULL);
    free(r);
}

static void t_diff(void)
{
    char *d = tx_diff("a\nb\nc\nd\ne\nf\ng\n", "a\nb\nc\nD\ne\nf\ng\n", 4000);
    CHECK(strstr(d, "1 line(s) removed, 1 added") != NULL);
    CHECK(strstr(d, "- d\n+ D\n") != NULL);
    CHECK(strstr(d, "  b\n  c\n") != NULL); /* context */
    free(d);
    d = tx_diff("same\n", "same\n", 4000);
    CHECK(strstr(d, "0 line(s) removed, 0 added") != NULL);
    free(d);
    d = tx_diff("", "new\nfile\n", 4000);
    CHECK(strstr(d, "+ new\n+ file\n") != NULL);
    free(d);
}

static void t_bm25(void)
{
    const char *docs[] = {"Prefers PowerShell over Bash for Windows scripts", "Works on the LlamaC native Win32 client in C",
                          "Likes hiking in the mountains", "Uses llama-server with a Qwen3 model"};
    double s[4];
    tx_bm25("how do I write a powershell script", docs, 4, s);
    CHECK(s[0] > 0 && s[0] > s[1] && s[0] > s[2] && s[0] > s[3]);
    tx_bm25("fix the LlamaC client", docs, 4, s);
    CHECK(s[1] > s[0] && s[1] > s[2]);
    tx_bm25("zzz", docs, 4, s);
    CHECK(s[0] == 0 && s[1] == 0);
}

int main(void)
{
    t_ws_replace();
    t_fold();
    t_diff();
    t_bm25();
    printf("%d checks, %d failed\n", runs, fails);
    return fails != 0;
}
