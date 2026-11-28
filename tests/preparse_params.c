/* Overzealous, Arena.ai, 2026-11-28: functional regression for the
 * PreParse_* script preparsers in qagame.
 *
 * Every preparser allocated its first string into data_char[01] -- an
 * octal literal, i.e. index 1, not 0 -- then Q_strncpyz() copied into
 * the still-NULL data_char[0], segfaulting in libc strncpy at map
 * start (reported on oasis via gotomarker). These calls exercise the
 * real functions in the built module with mapscript-style parameters
 * and check that each string lands in the slot the runtime actions
 * read (data_char[0] / data_char[1]).
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

static void *must(void *handle, const char *name) {
    void *result = dlsym(handle, name);
    if (!result) { fprintf(stderr, "%s: %s\n", name, dlerror()); exit(1); }
    return result;
}
static intptr_t fake_syscall(intptr_t op, ...) { (void)op; return 0; }

/* Layout must match g_script_preparsed_params_t in src/game/g_local.h. */
typedef struct {
    int data_int[8];
    float data_float[8];
    char *data_char[8];
} preparsed_params_t;

typedef int (*preparse_fn)(void *ent, char *params, preparsed_params_t *preparsed_params);

static void check_str(const char *what, const char *got, const char *want) {
    if (!got || strcmp(got, want)) {
        fprintf(stderr, "%s: got '%s', want '%s'\n", what, got ? got : "(null)", want);
        exit(1);
    }
}

int main(int argc, char **argv) {
    void *h;
    void (*entry)(intptr_t (*)(intptr_t, ...));
    void (*begin)(const char *);
    preparse_fn goto_marker, trigger, set_state, shader_remap, follow_spline, accum, global_accum;
    preparsed_params_t pp;

    CHECK(argc == 2);
    h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    CHECK(h);
    entry = must(h, "dllEntry"); entry(fake_syscall);
    begin = must(h, "COM_BeginParseSession");

    /* Mangled names: the game code is compiled as C++. */
    goto_marker = must(h, "_Z32PreParse_ScriptAction_GotoMarkerP9gentity_sPcP27g_script_preparsed_params_t");
    trigger     = must(h, "_Z29PreParse_ScriptAction_TriggerP9gentity_sPcP27g_script_preparsed_params_t");
    set_state   = must(h, "_Z30PreParse_ScriptAction_SetStateP9gentity_sPcP27g_script_preparsed_params_t");
    shader_remap= must(h, "_Z33PreParse_ScriptAction_ShaderRemapP9gentity_sPcP27g_script_preparsed_params_t");
    follow_spline = must(h, "_Z34PreParse_ScriptAction_FollowSplineP9gentity_sPcP27g_script_preparsed_params_t");
    accum       = must(h, "_Z27PreParse_ScriptAction_AccumP9gentity_sPcP27g_script_preparsed_params_t");
    global_accum= must(h, "_Z33PreParse_ScriptAction_GlobalAccumP9gentity_sPcP27g_script_preparsed_params_t");

    /* gotomarker <t> <s> [wait] -- the oasis crash case. */
    memset(&pp, 0, sizeof(pp));
    begin("gotomarker");
    CHECK(goto_marker(NULL, "gate_lever2_downpos 16 wait", &pp));
    check_str("gotomarker target", pp.data_char[0], "gate_lever2_downpos");
    CHECK(pp.data_float[0] == 16.0f);
    CHECK(pp.data_int[1] == 1); /* wait */
    CHECK(pp.data_char[1] == NULL);

    /* gotomarker with a relative target: both string slots used. */
    memset(&pp, 0, sizeof(pp));
    begin("gotomarker relative");
    CHECK(goto_marker(NULL, "door1_opened 30 wait relative door1_closed", &pp));
    check_str("gotomarker target", pp.data_char[0], "door1_opened");
    check_str("gotomarker relative", pp.data_char[1], "door1_closed");
    CHECK(pp.data_int[3] == 1); /* relative */

    /* trigger <scriptname> <trigger> */
    memset(&pp, 0, sizeof(pp));
    begin("trigger");
    CHECK(trigger(NULL, "my_script my_trigger", &pp));
    check_str("trigger scriptname", pp.data_char[0], "my_script");
    check_str("trigger trigger", pp.data_char[1], "my_trigger");

    /* setstate <t> <e> */
    memset(&pp, 0, sizeof(pp));
    begin("setstate");
    CHECK(set_state(NULL, "my_target invisible", &pp));
    check_str("setstate target", pp.data_char[0], "my_target");
    CHECK(pp.data_int[1] == 1); /* STATE_INVISIBLE */

    /* remapshader <original> <new> */
    memset(&pp, 0, sizeof(pp));
    begin("remapshader");
    CHECK(shader_remap(NULL, "shader_a shader_b", &pp));
    check_str("remapshader original", pp.data_char[0], "shader_a");
    check_str("remapshader new", pp.data_char[1], "shader_b");

    /* followspline <d> <t> <s> [w] */
    memset(&pp, 0, sizeof(pp));
    begin("followspline");
    CHECK(follow_spline(NULL, "1 spline_target 100 wait", &pp));
    check_str("followspline target", pp.data_char[0], "spline_target");
    CHECK(pp.data_float[0] == 100.0f);
    CHECK(pp.data_int[2] == 1); /* wait */

    /* accum 0 trigger_if_equal 5 <scriptname> <trigger> */
    memset(&pp, 0, sizeof(pp));
    begin("accum");
    CHECK(accum(NULL, "0 trigger_if_equal 5 my_script my_trigger", &pp));
    check_str("accum scriptname", pp.data_char[0], "my_script");
    check_str("accum trigger", pp.data_char[1], "my_trigger");
    CHECK(pp.data_int[2] == 5);

    /* globalaccum 0 trigger_if_equal 5 <scriptname> <trigger> */
    memset(&pp, 0, sizeof(pp));
    begin("globalaccum");
    CHECK(global_accum(NULL, "0 trigger_if_equal 5 my_script my_trigger", &pp));
    check_str("globalaccum scriptname", pp.data_char[0], "my_script");
    check_str("globalaccum trigger", pp.data_char[1], "my_trigger");

    /* accum 0 set_to_dynamitecount <t> */
    memset(&pp, 0, sizeof(pp));
    begin("accum dynamitecount");
    CHECK(accum(NULL, "0 set_to_dynamitecount my_target", &pp));
    check_str("accum dynamitecount target", pp.data_char[0], "my_target");

    printf("preparse_params: all preparser string slots correct\n");
    return 0;
}
