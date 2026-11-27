/* NewMod, Arena.ai, 2026-11-21: shared parser boundary regression for all
 * three native modules. Not an exhaustive compatibility test for game assets. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "src/game/q_shared.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

static void *must(void *handle, const char *name) {
    void *result = dlsym(handle, name);
    if (!result) { fprintf(stderr, "%s: %s\n", name, dlerror()); exit(1); }
    return result;
}
static intptr_t fake_syscall(intptr_t op, ...) { (void)op; return 0; }

static void test_module(const char *path) {
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    void (*entry)(intptr_t (*)(intptr_t, ...));
    void (*begin)(const char *), (*restore)(char **);
    qboolean (*had_error)(void);
    char *(*parse)(char **, qboolean);
    char input[2 * MAX_TOKEN_CHARS + 32], *p, *token;
    const char *escaped = "\"one \\\"two words\\\" more\" tail";
    long page_size = sysconf(_SC_PAGESIZE);
    char *pages, *at_end;
    int size;
    CHECK(h && page_size >= 4096);
    entry = must(h, "dllEntry"); entry(fake_syscall);
    begin = must(h, "COM_BeginParseSession");
    restore = must(h, "COM_RestoreParseSession");
    had_error = must(h, "COM_ParseHadError");
    parse = must(h, "COM_ParseExt");

    begin("boundary"); p = (char *)escaped;
    CHECK(!strcmp(parse(&p, qtrue), "one \"two words\" more"));
    CHECK(!strcmp(parse(&p, qtrue), "tail") && !had_error());
    p = "one\n  two"; begin("line break");
    CHECK(!strcmp(parse(&p, qfalse), "one"));
    CHECK(!*parse(&p, qfalse) && p && !had_error());
    CHECK(!strcmp(parse(&p, qfalse), "two"));
    p = "caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac"; begin("UTF-8");
    CHECK(!strcmp(parse(&p, qtrue), "caf\xc3\xa9"));
    CHECK(!strcmp(parse(&p, qtrue), "\xe6\x97\xa5\xe6\x9c\xac") && !had_error());

    /* Exactly 1023 bytes plus a terminator fit; 1024 and larger do not.
     * Both quoted and regular oversized tokens are consumed and discarded. */
    for (size = MAX_TOKEN_CHARS - 1; size <= MAX_TOKEN_CHARS + 1; ++size) {
        memset(input, 'x', sizeof(input)); input[0] = '"';
        input[size + 1] = '"'; strcpy(input + size + 2, " after");
        begin("quoted size"); p = input; token = parse(&p, qtrue);
        CHECK(size == MAX_TOKEN_CHARS - 1 ? (strlen(token) == (size_t)size && !had_error())
                                        : (!*token && had_error()));
        CHECK(!strcmp(parse(&p, qtrue), "after"));
        memset(input, 'x', sizeof(input)); strcpy(input + size, " after");
        begin("word size"); p = input; token = parse(&p, qtrue);
        CHECK(size == MAX_TOKEN_CHARS - 1 ? (strlen(token) == (size_t)size && !had_error())
                                        : (!*token && had_error()));
        CHECK(!strcmp(parse(&p, qtrue), "after"));
    }

    p = "/* never closed"; begin("comment");
    CHECK(!*parse(&p, qtrue) && p == NULL && had_error());
    p = "\"outer \\\"inner without end"; begin("nested quote");
    CHECK(!*parse(&p, qtrue) && p == NULL && had_error());
    restore(&p);
    CHECK(p && !had_error());
    CHECK(!*parse(&p, qtrue) && p == NULL && had_error());
    CHECK(!*parse(&p, qtrue) && p == NULL);

    /* Catch an EOF pointer advanced past NUL, even without ASan. */
    pages = mmap(NULL, (size_t)page_size * 2, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(pages != MAP_FAILED);
    CHECK(mprotect(pages + page_size, (size_t)page_size, PROT_NONE) == 0);
    at_end = pages + page_size - 3;
    /* Opening quote, character and terminating NUL touch the guard page. */
    at_end[0] = '"'; at_end[1] = 'x'; at_end[2] = '\0'; p = at_end;
    begin("guard page");
    CHECK(!*parse(&p, qtrue) && p == NULL && had_error());
    CHECK(!*parse(&p, qtrue) && p == NULL);
    CHECK(munmap(pages, (size_t)page_size * 2) == 0);
    dlclose(h);
}

int main(int argc, char **argv) {
    int i;
    CHECK(argc == 4);
    for (i = 1; i < argc; ++i) test_module(argv[i]);
    puts("game/cgame/ui parser boundary tests passed");
    return 0;
}
