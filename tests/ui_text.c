/* NewMod, Arena.ai, 2026-11-21: exercise the built UI edit-field painter
 * with a fake display context, including stale offsets and narrow fields. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/ui/ui_local.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
    exit(1); } } while (0)

static const char *value;
static char last_text[2048];
static int draw_calls, cursor_calls, last_cursor;

static int width(const char *text, float scale, int limit) {
    (void)scale; (void)limit;
    return (int)strlen(text) * 10;
}
static int height(const char *text, float scale, int limit) {
    (void)text; (void)scale; (void)limit;
    return 10;
}
static void get_cvar(const char *name, char *buffer, int size) {
    (void)name;
    snprintf(buffer, (size_t)size, "%s", value);
}
static void draw(float x, float y, float scale, vec4_t color, const char *text,
                 float adjust, int limit, int style) {
    (void)x; (void)y; (void)scale; (void)color; (void)adjust; (void)limit; (void)style;
    snprintf(last_text, sizeof(last_text), "%s", text);
    ++draw_calls;
}
static void draw_cursor(float x, float y, float scale, vec4_t color, const char *text,
                        int cursor_pos, char cursor, int limit, int style) {
    (void)x; (void)y; (void)scale; (void)color; (void)cursor; (void)limit; (void)style;
    snprintf(last_text, sizeof(last_text), "%s", text);
    ++cursor_calls; last_cursor = cursor_pos;
}
static qboolean overstrike(void) { return qfalse; }

static void run(const char *path) {
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    void (*init)(displayContextDef_t *);
    void (*paint)(itemDef_t *);
    qboolean *editing;
    displayContextDef_t dc;
    itemDef_t item;
    editFieldDef_t edit;
    menuDef_t menu;
    char long_value[1024];
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); exit(1); }
    init = dlsym(h, "Init_Display"); paint = dlsym(h, "Item_TextField_Paint");
    editing = dlsym(h, "g_editingField");
    CHECK(init && paint && editing);
    memset(&dc, 0, sizeof(dc));
    dc.textWidth = width; dc.textHeight = height; dc.getCVarString = get_cvar;
    dc.drawText = draw; dc.drawTextWithCursor = draw_cursor; dc.getOverstrikeMode = overstrike;
    init(&dc);
    memset(&item, 0, sizeof(item)); memset(&edit, 0, sizeof(edit)); memset(&menu, 0, sizeof(menu));
    item.type = ITEM_TYPE_EDITFIELD; item.typeData = &edit; item.parent = &menu;
    item.text = "label"; item.cvar = "test"; item.textscale = 1;
    item.textRect.w = 1; item.textRect.h = 1; item.window.rect.w = 30;
    edit.maxPaintChars = 1024;

    value = "abcdef"; draw_calls = 0;
    paint(&item);
    CHECK(draw_calls == 2 && !strcmp(last_text, "ef"));
    edit.paintOffset = 999; paint(&item);
    CHECK(edit.paintOffset == 6 && !strcmp(last_text, ""));
    edit.paintOffset = -20; value = "abc"; item.window.rect.w = 0;
    paint(&item);
    CHECK(edit.paintOffset == 0 && !strcmp(last_text, ""));
    memset(long_value, 'x', sizeof(long_value) - 1); long_value[sizeof(long_value) - 1] = 0;
    value = long_value; edit.paintOffset = 5000; item.window.rect.w = 30;
    paint(&item);
    CHECK(edit.paintOffset == 1023 && !strcmp(last_text, ""));
    value = "abc"; edit.paintOffset = 0; item.window.rect.w = 100;
    item.window.flags = WINDOW_HASFOCUS; item.cursorPos = 2; *editing = qtrue;
    cursor_calls = 0; paint(&item);
    CHECK(cursor_calls == 1 && !strcmp(last_text, "abc") && last_cursor == 2);
    *editing = qfalse;
    dlclose(h);
}

int main(int argc, char **argv) {
    CHECK(argc == 3);
    run(argv[1]); run(argv[2]);
    puts("UI and cgame shared edit-field tests passed");
    return 0;
}
