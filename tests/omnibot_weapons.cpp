/* NewMod, Arena.ai, 2026-11-21: run the actual Omni-Bot event adapter with
 * a fake event sink. Tests the distinction between bot IDs and game bits. */
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
#include "src/game/g_local.h"
}
#include "Omnibot/Common/BotExports.h"
#include "Omnibot/Common/Base_Messages.h"

#define CHECK(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
    std::exit(1); } } while (0)

static int count, ids[4], weapons[4];
static void send_event(int dest, const MessageHelper &message) {
    CHECK(dest == 0 && count < 4);
    ids[count] = message.GetMessageId();
    if (ids[count] == MESSAGE_ADDWEAPON)
        weapons[count] = message.Get<Event_AddWeapon>()->m_WeaponId;
    else if (ids[count] == MESSAGE_REMOVEWEAPON)
        weapons[count] = message.Get<Event_RemoveWeapon>()->m_WeaponId;
    else CHECK(false);
    ++count;
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    gentity_t *ents = static_cast<gentity_t *>(dlsym(h, "g_entities"));
    gclient_t *clients = static_cast<gclient_t *>(dlsym(h, "g_clients"));
    bool *loaded = static_cast<bool *>(dlsym(h, "g_IsOmnibotLoaded"));
    Bot_EngineFuncs_t *callbacks = static_cast<Bot_EngineFuncs_t *>(dlsym(h, "g_BotFunctions"));
    typedef void (*EventFn)(int, int);
    typedef int (*TranslateFn)(int);
    typedef void (*BitFn)(int *, int);
    EventFn add = reinterpret_cast<EventFn>(dlsym(h, "Bot_Event_AddWeapon"));
    TranslateFn translate = reinterpret_cast<TranslateFn>(dlsym(h, "Bot_WeaponGameToBot"));
    BitFn set = reinterpret_cast<BitFn>(dlsym(h, "COM_BitSet"));
    CHECK(ents && clients && loaded && callbacks && add && translate && set);
    std::memset(&ents[0], 0, sizeof(ents[0]));
    std::memset(&clients[0], 0, sizeof(clients[0]));
    ents[0].client = &clients[0]; ents[0].r.svFlags = SVF_BOT;
    *loaded = true; callbacks->pfnSendEvent = send_event;
    CHECK(translate(WP_JOHNSON) == 102 && translate(WP_JOHNSON_SCOPE) == 101);
    count = 0; add(0, 102);
    CHECK(count == 1 && ids[0] == MESSAGE_ADDWEAPON && weapons[0] == 102);
    set(clients[0].ps.weapons, WP_JOHNSON_SCOPE);
    count = 0; add(0, 102);
    CHECK(count == 2 && ids[0] == MESSAGE_REMOVEWEAPON && weapons[0] == 102);
    CHECK(ids[1] == MESSAGE_ADDWEAPON && weapons[1] == 101);
    count = 0; add(0, 101);
    CHECK(count == 2 && ids[0] == MESSAGE_REMOVEWEAPON && weapons[0] == 102);
    CHECK(ids[1] == MESSAGE_ADDWEAPON && weapons[1] == 101);
    *loaded = false;
    dlclose(h);
    std::puts("Omni-Bot Johnson weapon mapping tests passed");
    return 0;
}
