/* skills_tests.c - portable tests for the SKILL.md front matter parser (tools_core.h).
   Build: cl /nologo /W3 /utf-8 tests\skills_tests.c */
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
#include "../src/tools_core.h"

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

static char nm[72], ds[1024];
static size_t fm(const char *s) { return skill_frontmatter(s, nm, sizeof nm, ds, sizeof ds); }

int main(void)
{
    const char *s;
    size_t b;

    s = "---\nname: tdd\ndescription: Test-driven development. Use when \"red-green\".\n---\n\n# TDD\nbody";
    b = fm(s);
    CHECK(!strcmp(nm, "tdd"));
    CHECK(!strcmp(ds, "Test-driven development. Use when \"red-green\"."));
    CHECK(!strcmp(s + b, "\n# TDD\nbody"));

    /* CRLF, BOM, trailing spaces on the fences, other keys */
    s = "\xEF\xBB\xBF---  \r\nname:  web-fetch \r\nlicense: MIT\r\ndescription: Fetch pages.\r\nallowed-tools: Bash\r\n--- \r\nBody\r\n";
    b = fm(s);
    CHECK(!strcmp(nm, "web-fetch"));
    CHECK(!strcmp(ds, "Fetch pages."));
    CHECK(!strcmp(s + b, "Body\r\n"));

    /* quoted values */
    fm("---\nname: 'it''s'\ndescription: \"Say \\\"hi\\\" now\" # comment\n---\n");
    CHECK(!strcmp(nm, "it's"));
    CHECK(!strcmp(ds, "Say \"hi\" now"));

    /* folded and literal blocks join with spaces */
    fm("---\nname: x\ndescription: >\n  First line\n  second line.\n\n  Third.\nmetadata:\n  a: b\n---\n");
    CHECK(!strcmp(ds, "First line second line. Third."));
    fm("---\ndescription: |-\n    one\n    two\nname: y\n---\n");
    CHECK(!strcmp(ds, "one two"));
    CHECK(!strcmp(nm, "y"));

    /* plain value continued on indented lines; quoted value over two lines */
    fm("---\nname: z\ndescription: Use this skill\n  when the user asks.\n---\n");
    CHECK(!strcmp(ds, "Use this skill when the user asks."));
    fm("---\nname: z\ndescription: \"Starts here\n  and ends here\"\n---\n");
    CHECK(!strcmp(ds, "Starts here and ends here"));

    /* nested keys with the same name do not count */
    fm("---\nname: top\nmetadata:\n  name: inner\n  description: inner desc\n---\n");
    CHECK(!strcmp(nm, "top"));
    CHECK(ds[0] == 0);

    /* no front matter / no closing fence */
    CHECK(fm("# Just markdown\n") == 0 && !nm[0] && !ds[0]);
    CHECK(fm("---\nname: open\n") == 0 && !nm[0]);
    CHECK(fm("") == 0);

    /* cap keeps UTF-8 whole: 'é' is 2 bytes */
    {
        char small[5];
        skill_frontmatter("---\nname: abc\xC3\xA9\n---\n", small, sizeof small, ds, sizeof ds);
        CHECK(!strcmp(small, "abc"));
    }

    printf("%d checks, %d failed\n", runs, fails);
    return fails != 0;
}
