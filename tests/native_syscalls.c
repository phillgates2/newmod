/* NewMod, Arena.ai, 2026-11-21: selected native VM-call/ABI regressions.
 * Exercises the built modules with a fake ET: Legacy-style vararg dispatcher. */
#include <dlfcn.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/game/q_shared.h"
#include "src/game/g_public.h"
#include "src/cgame/cg_public.h"
#include "src/ui/ui_public.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
    exit(1); } } while (0)
#define VM_END ((intptr_t)-1337)

static intptr_t last_op, captured[16], result;
static int captured_count;

static intptr_t fake_syscall(intptr_t op, ...) {
    va_list ap;
    intptr_t a;
    last_op = op; captured_count = 0;
    va_start(ap, op);
    for (;;) {
        a = va_arg(ap, intptr_t);
        if (a == VM_END) break;
        CHECK(captured_count < 16);
        captured[captured_count++] = a;
    }
    va_end(ap);
    return result;
}

static void *must(void *module, const char *name) {
    void *p = dlsym(module, name);
    if (!p) { fprintf(stderr, "missing %s: %s\n", name, dlerror()); exit(1); }
    return p;
}

static void *load(const char *path) {
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    void (*entry)(intptr_t (*)(intptr_t, ...));
    if (!h) { fprintf(stderr, "dlopen %s: %s\n", path, dlerror()); exit(1); }
    entry = must(h, "dllEntry");
    entry(fake_syscall);
    return h;
}

static uint32_t floatbits(float f) {
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    return b;
}

static void game(const char *path) {
    void *h = load(path);
    void (*print)(const char *) = must(h, "trap_Printf");
    int (*argcnt)(void) = must(h, "trap_Argc");
    void (*trace)(void *, const float *, const float *, const float *, const float *, int, int) = must(h, "trap_Trace");
    intptr_t (*vm)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t) = must(h, "vmMain");
    const char *message = "pointer-width game print";
    int result_buffer = 0;
    float start[3] = {1,2,3}, mins[3] = {-1,-1,-1}, maxs[3] = {1,1,1}, end[3] = {4,5,6};
    print(message);
    CHECK(last_op == G_PRINT && captured_count == 1 && captured[0] == (intptr_t)message);
    result = 17; CHECK(argcnt() == 17 && last_op == G_ARGC && captured_count == 0);
    trace(&result_buffer, start, mins, maxs, end, -1, 42);
    CHECK(last_op == G_TRACE && captured_count == 7);
    CHECK(captured[0] == (intptr_t)&result_buffer && captured[1] == (intptr_t)start);
    CHECK(captured[2] == (intptr_t)mins && captured[3] == (intptr_t)maxs);
    CHECK(captured[4] == (intptr_t)end && captured[5] == -1 && captured[6] == 42);
    CHECK(vm(98765, 0,0,0,0,0,0,0) == -1);
    dlclose(h);
}

static void cgame(const char *path) {
    void *h = load(path);
    void (*print)(const char *) = must(h, "trap_Print");
    void (*light)(const float *, float, float, float, float, float, int, int) = must(h, "trap_R_AddLightToScene");
    intptr_t (*vm)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t) = must(h, "vmMain");
    const char *message = "pointer-width cgame print";
    float origin[3] = {1,2,3};
    unsigned char unknown_packet = 255;
    print(message);
    CHECK(last_op == CG_PRINT && captured_count == 1 && captured[0] == (intptr_t)message);
    light(origin, 12.5f, -3.25f, .5f, 1.f, 0.f, 42, 8);
    CHECK(last_op == CG_R_ADDLIGHTTOSCENE && captured_count == 8);
    CHECK(captured[0] == (intptr_t)origin && (uint32_t)captured[1] == floatbits(12.5f));
    CHECK((uint32_t)captured[2] == floatbits(-3.25f) && (uint32_t)captured[3] == floatbits(.5f));
    CHECK(captured[6] == 42 && captured[7] == 8);
    /* Unknown packet is safely rejected; the byte must be read via a full-width pointer. */
    CHECK(vm(CG_MESSAGERECEIVED, (intptr_t)&unknown_packet, 1,0,0,0,0,0,0,0,0,0,0) == -1);
    dlclose(h);
}

static void ui(const char *path) {
    void *h = load(path);
    void (*print)(const char *) = must(h, "trap_Print");
    float (*getvalue)(const char *) = must(h, "trap_Cvar_VariableValue");
    void (*setvalue)(const char *, float) = must(h, "trap_Cvar_SetValue");
    intptr_t (*vm)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t) = must(h, "vmMain");
    const char *message = "pointer-width ui print", *name = "volume";
    print(message);
    CHECK(last_op == UI_PRINT && captured_count == 1 && captured[0] == (intptr_t)message);
    result = (intptr_t)floatbits(3.125f);
    CHECK(fabsf(getvalue(name) - 3.125f) < .0001f);
    CHECK(last_op == UI_CVAR_VARIABLEVALUE && captured_count == 1 && captured[0] == (intptr_t)name);
    setvalue(name, -2.25f);
    CHECK(last_op == UI_CVAR_SETVALUE && captured_count == 2 && captured[0] == (intptr_t)name);
    CHECK((uint32_t)captured[1] == floatbits(-2.25f));
    CHECK(vm(UI_GETAPIVERSION, 0,0,0,0,0,0,0,0,0,0,0,0) == UI_API_VERSION);
    CHECK(vm(98765, 0,0,0,0,0,0,0,0,0,0,0,0) == -1);
    dlclose(h);
}

int main(int argc, char **argv) {
    CHECK(argc == 4);
    game(argv[1]); cgame(argv[2]); ui(argv[3]);
    puts("selected game/cgame/ui syscall and VM entry tests passed");
    return 0;
}
