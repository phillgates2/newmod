/* NewMod regression harness, Arena.ai, 2026-11-21.
 * Loads the actual qagame module and supplies a small fake engine. This is
 * not a substitute for a real client/server integration or gameplay test. */
#include <dlfcn.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "src/game/g_local.h"
#ifdef ET_LUA
#include "src/game/g_lua.h"
#endif

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
    exit(1); } } while (0)
#define VM_END ((intptr_t)-1337)

static void *module;
static level_locals_t *lev;
static gentity_t *ents;
static gclient_t *clients;
static team_info *teams;
static g_shrubbot_level_t **levels;
static g_shrubbot_admin_t **admins;
static g_shrubbot_ban_t **bans;
static vmCvar_t *shrub, *tyranny, *reportfile, *viewlevel, *adminlog, *game_state, *warmup_damage;
static qboolean (*check_command)(gentity_t *);
static void (*say_command)(gentity_t *, int, qboolean);
static void (*client_command)(int);
static int (*find_clients)(char *, int *);
static void (*bit_set)(int *, int);
static void (*bit_clear)(int *, int);
static qboolean (*bit_check)(const int *, int);

static const char *argvs[12];
static int argcount;
static int war_mask, gravity_value, gravity_sets, sound_exists, sound_opens, links;
static char gravity_string[MAX_CVAR_VALUE_STRING], expected_sound[MAX_QPATH];
static int open_fail, short_write, opened, writes, closes;
static int errors, expect_error, abort_init_on_print, dropped, last_drop, binary_sends, binary_length;
static int queued_count;
static char queued_console[MAX_STRING_CHARS * 2];
static jmp_buf error_jump;
static unsigned char binary_packet[PACKET_OFFSET + MAX_CLIENTS * 19];
static char fake_userinfo[MAX_INFO_STRING], fake_session[MAX_STRING_CHARS];
static char campaign_gametype[32];
static char configstrings[MAX_CONFIGSTRINGS][MAX_QPATH * 2];
static char stored[4096], console_output[8192];
static size_t stored_len;
/* Optional in-memory shrubbot file for real read/write/reload paths. */
static int cfg_fixture, cfg_missing, cfg_shortread, cfg_oversize, cfg_write_fail, cfg_mapvote;
static const unsigned char *cfg_data;
static size_t cfg_len, cfg_written_len, cfg_written_cap;
static char *cfg_written;
#ifdef ET_LUA
static unsigned char lua_chunks[2][4096];
static size_t lua_chunk_len[2];
static const unsigned char *lua_read_data;
static size_t lua_read_len;
static int lua_fixture_active, lua_module_opens, lua_invalid_opens;
#endif
static struct { int dest; char text[2048]; } sent[128];
static int sent_count;

static void *sym(const char *name) {
    void *p = dlsym(module, name);
    if (!p) { fprintf(stderr, "missing %s: %s\n", name, dlerror()); exit(1); }
    return p;
}

static void setargs(int count, ...) {
    va_list ap;
    int i;
    CHECK(count <= (int)(sizeof(argvs) / sizeof(argvs[0])));
    argcount = count;
    va_start(ap, count);
    for (i = 0; i < count; ++i) argvs[i] = va_arg(ap, const char *);
    va_end(ap);
}

static void reset_messages(void) {
    sent_count = opened = writes = closes = 0;
    stored_len = 0;
    stored[0] = console_output[0] = '\0';
    queued_count = 0; queued_console[0] = '\0';
    open_fail = short_write = 0;
}

/* Consume pointer-sized arguments up to the ET: Legacy sentinel. */
static intptr_t fake_syscall(intptr_t op, ...) {
    intptr_t args[17];
    int n = 0;
    va_list ap;
    va_start(ap, op);
    for (;;) {
        intptr_t a = va_arg(ap, intptr_t);
        if (a == VM_END) break;
        CHECK(n < 16);
        args[n++] = a;
    }
    va_end(ap);
    switch (op) {
    case G_ARGC:
        CHECK(n == 0);
        return argcount;
    case G_ARGV: {
        int i = (int)args[0], cap = (int)args[2];
        char *buf = (char *)args[1];
        CHECK(n == 3 && cap > 0);
        snprintf(buf, (size_t)cap, "%s", i >= 0 && i < argcount ? argvs[i] : "");
        return 0;
    }
    case G_CVAR_VARIABLE_INTEGER_VALUE:
        CHECK(n == 1);
        if (!strcmp((char *)args[0], "nq_War")) return war_mask;
        if (!strcmp((char *)args[0], "g_gravity")) return gravity_value;
        fprintf(stderr, "unexpected cvar read %s\n", (char *)args[0]); exit(1);
    case G_CVAR_SET:
        CHECK(n == 2);
        if (!strcmp((char *)args[0], "nq_War")) war_mask = atoi((char *)args[1]);
        else if (!strcmp((char *)args[0], "g_gravity")) {
            snprintf(gravity_string, sizeof(gravity_string), "%s", (char *)args[1]);
            gravity_value = atoi(gravity_string); ++gravity_sets;
        } else if (!strcmp((char *)args[0], "g_gametype")) {
            snprintf(campaign_gametype, sizeof(campaign_gametype), "%s", (char *)args[1]);
        } else if (!strcmp((char *)args[0], "session") || !strncmp((char *)args[0], "fireteam", 8)) {
            /* G_ShutdownGame writes world and fireteam session cvars. */
        } else { fprintf(stderr, "unexpected cvar set %s\n", (char *)args[0]); exit(1); }
        return 0;
    case G_GET_CONFIGSTRING:
        CHECK(n == 3 && args[0] >= 0 && args[0] < MAX_CONFIGSTRINGS && args[2] > 0);
        snprintf((char *)args[1], (size_t)args[2], "%s", configstrings[args[0]]);
        return 0;
    case G_SET_CONFIGSTRING:
        CHECK(n == 2 && args[0] >= 0 && args[0] < MAX_CONFIGSTRINGS);
        snprintf(configstrings[args[0]], sizeof(configstrings[0]), "%s", (char *)args[1]);
        return 0;
    case G_LOCATE_GAME_DATA:
        CHECK(n == 5 && (gentity_t *)args[0] == ents && args[2] == sizeof(gentity_t));
        return 0;
    case G_LINKENTITY:
        CHECK(n == 1);
        ((gentity_t *)args[0])->r.linked = qtrue;
        ++links;
        return 0;
    case G_UNLINKENTITY:
        CHECK(n == 1);
        ((gentity_t *)args[0])->r.linked = qfalse;
        return 0;
    case G_CVAR_VARIABLE_STRING_BUFFER:
        CHECK(n == 3 && (int)args[2] > 0);
        snprintf((char *)args[1], (size_t)args[2], "%s",
                 !strcmp((char *)args[0], "fs_game") ? "newmod" :
                 !strcmp((char *)args[0], "g_gravity") ? gravity_string :
                 !strcmp((char *)args[0], "session0") ? fake_session : ".");
        return 0;
    case G_GET_USERINFO:
        CHECK(n == 3 && args[0] >= 0 && args[0] < MAX_CLIENTS && args[2] > 0);
        snprintf((char *)args[1], (size_t)args[2], "%s", fake_userinfo);
        return 0;
    case G_GET_SERVERINFO:
        CHECK(n == 2 && args[1] > 0);
        snprintf((char *)args[0], (size_t)args[1], "\\mapname\\testmap");
        return 0;
    case G_DROP_CLIENT:
        CHECK(n == 3);
        ++dropped; last_drop = (int)args[0];
        return 0;
    case G_SENDMESSAGE:
        CHECK(n == 3 && args[0] >= 0 && args[0] < MAX_CLIENTS &&
              args[2] >= 0 && args[2] <= (intptr_t)sizeof(binary_packet));
        binary_length = (int)args[2];
        memcpy(binary_packet, (void *)args[1], (size_t)binary_length);
        ++binary_sends;
        return 0;
    case G_ERROR:
        CHECK(n == 1);
        ++errors;
        if (expect_error) longjmp(error_jump, 1);
        fprintf(stderr, "unexpected game error: %s\n", (char *)args[0]);
        exit(1);
    case G_SEND_CONSOLE_COMMAND:
        CHECK(n == 2 && args[0] == EXEC_APPEND);
        CHECK(strlen((char *)args[1]) < sizeof(queued_console));
        snprintf(queued_console, sizeof(queued_console), "%s", (char *)args[1]);
        ++queued_count;
        return 0;
    case G_SEND_SERVER_COMMAND:
        CHECK(n == 2 && sent_count < (int)(sizeof(sent) / sizeof(sent[0])));
        sent[sent_count].dest = (int)args[0];
        snprintf(sent[sent_count].text, sizeof(sent[sent_count].text), "%s", (char *)args[1]);
        ++sent_count;
        return 0;
    case G_PRINT:
        CHECK(n == 1);
        if (abort_init_on_print && strstr((char *)args[0], "Game Initialization")) {
            abort_init_on_print = 0;
            longjmp(error_jump, 1);
        }
        strncat(console_output, (char *)args[0], sizeof(console_output) - strlen(console_output) - 1);
        if (strstr((char *)args[0], "Lua API: error") || strstr((char *)args[0], "Lua API: runtime"))
            fprintf(stderr, "%s", (char *)args[0]);
        return 0;
    case G_REAL_TIME: {
        qtime_t *t = (qtime_t *)args[0];
        CHECK(n == 1);
        memset(t, 0, sizeof(*t));
        t->tm_year = 126; t->tm_mon = 10; t->tm_mday = 21;
        t->tm_hour = 12; t->tm_min = 34; t->tm_sec = 56;
        return 0;
    }
    case G_FS_GETFILELIST:
        CHECK(n == 4 && !strcmp((char *)args[0], "scripts") &&
              !strcmp((char *)args[1], ".campaign") && args[3] > 0);
        ((char *)args[2])[0] = '\0';
        return 0;
    case G_FS_FOPEN_FILE:
        CHECK(n == 3);
        if (cfg_fixture && !strcmp((char *)args[0], cfg_mapvote ? "mapvoteinfo.txt" : "shrubbot.cfg")) {
            if ((int)args[2] == FS_READ) {
                *(fileHandle_t *)args[1] = cfg_missing ? 0 : 4;
                return cfg_missing ? -1 : cfg_oversize ? 32 * 1024 * 1024 + 1 : (intptr_t)cfg_len;
            }
            CHECK((int)args[2] == FS_WRITE);
            *(fileHandle_t *)args[1] = cfg_write_fail ? 0 : 4;
            cfg_written_len = 0;
            return cfg_write_fail ? -1 : 0;
        }
        if ((int)args[2] == FS_READ) {
#ifdef ET_LUA
            if (lua_fixture_active) {
                const char *name = (const char *)args[0];
                int which = name && !strcmp(name, "first.lua") ? 0 :
                            name && !strcmp(name, "second.lua") ? 1 : -1;
                if (which < 0) {
                    ++lua_invalid_opens;
                    *(fileHandle_t *)args[1] = 0;
                    return -1;
                }
                ++lua_module_opens;
                lua_read_data = lua_chunks[which];
                lua_read_len = lua_chunk_len[which];
                *(fileHandle_t *)args[1] = 3;
                return (intptr_t)lua_read_len;
            }
#endif
            CHECK(!strcmp((char *)args[0], expected_sound));
            ++sound_opens;
            *(fileHandle_t *)args[1] = sound_exists ? 2 : 0;
            return sound_exists ? 100 : -1;
        }
        CHECK((int)args[2] == FS_APPEND);
        /* Capture report and admin writes. Report-privacy tests assert that
         * a configured admin log was not opened for a report. */
        CHECK(!strcmp((char *)args[0], "reports.log") || !strcmp((char *)args[0], "admin.log"));
        ++opened;
        *(fileHandle_t *)args[1] = open_fail ? 0 : 1;
        return open_fail ? -1 : 0;
    case G_FS_READ:
        if (cfg_fixture && n == 3 && args[2] == 4) {
            size_t amount = (size_t)args[1];
            CHECK(amount == cfg_len);
            if (cfg_shortread && amount) --amount;
            if (amount) memcpy((void *)args[0], cfg_data, amount);
            return 0;
        }
#ifdef ET_LUA
        if (lua_fixture_active && n == 3 && args[2] == 3) {
            CHECK(args[1] >= 0 && (size_t)args[1] == lua_read_len);
            memcpy((void *)args[0], lua_read_data, lua_read_len);
            return 0;
        }
#endif
        /* A deliberately short fake read verifies zero-filling of the tail. */
        CHECK(n == 3 && args[2] == 1 && args[1] == 4);
        memcpy((void *)args[0], "A\0B", 3);
        return 0;
    case G_FS_WRITE: {
        int len = (int)args[1];
        if (cfg_fixture && n == 3 && args[2] == 4) {
            size_t need = cfg_written_len + (size_t)len + 1;
            CHECK(len >= 0 && need >= cfg_written_len);
            if (need > cfg_written_cap) {
                size_t cap = cfg_written_cap ? cfg_written_cap : 4096;
                while (cap < need) cap *= 2;
                cfg_written = realloc(cfg_written, cap);
                CHECK(cfg_written); cfg_written_cap = cap;
            }
            memcpy(cfg_written + cfg_written_len, (void *)args[0], (size_t)len);
            cfg_written_len += (size_t)len;
            cfg_written[cfg_written_len] = '\0';
            return len;
        }
        CHECK(n == 3 && args[2] == 1 && len >= 0);
        ++writes;
        if (short_write) return len ? len - 1 : 0;
        CHECK(stored_len + (size_t)len < sizeof(stored));
        memcpy(stored + stored_len, (char *)args[0], (size_t)len);
        stored_len += (size_t)len;
        stored[stored_len] = '\0';
        return len;
    }
    case G_FS_FCLOSE_FILE:
        CHECK(n == 1 && (args[0] == 1 || args[0] == 2 || (cfg_fixture && args[0] == 4)
#ifdef ET_LUA
                         || (lua_fixture_active && args[0] == 3)
#endif
                        ));
        ++closes;
        return 0;
    default:
        fprintf(stderr, "unexpected game syscall %ld (%d args)\n", (long)op, n);
        exit(1);
    }
}

static g_shrubbot_level_t level0, level3, level5;
static g_shrubbot_admin_t admin0, admin2, immutable3;

static void setup_clients(void) {
    int i;
    memset(lev, 0, sizeof(*lev));
    memset(clients, 0, sizeof(*clients) * MAX_CLIENTS);
    memset(ents, 0, sizeof(*ents) * MAX_GENTITIES);
    memset(teams, 0, sizeof(*teams) * TEAM_NUM_TEAMS);
    memset(configstrings, 0, sizeof(configstrings));
    memset(&level0, 0, sizeof(level0));
    memset(&level3, 0, sizeof(level3));
    memset(&level5, 0, sizeof(level5));
    memset(&admin0, 0, sizeof(admin0));
    memset(&admin2, 0, sizeof(admin2));
    memset(&immutable3, 0, sizeof(immutable3));
    memset(levels, 0, sizeof(*levels) * (MAX_SHRUBBOT_LEVELS + 1));
    memset(admins, 0, sizeof(*admins) * (MAX_SHRUBBOT_ADMINS + 1));
    memset(bans, 0, sizeof(*bans) * (MAX_SHRUBBOT_BANS + 1));
    lev->clients = clients;
    lev->gentities = ents;
    lev->maxclients = MAX_CLIENTS;
    lev->num_entities = MAX_CLIENTS;
    lev->numConnectedClients = 4;
    for (i = 0; i < MAX_CLIENTS; ++i) {
        ents[i].client = &clients[i];
        ents[i].s.number = ents[i].s.clientNum = i;
        clients[i].ps.clientNum = i;
        snprintf(clients[i].pers.netname, sizeof(clients[i].pers.netname), "Player%d", i);
        snprintf(clients[i].pers.cl_guid, sizeof(clients[i].pers.cl_guid), "guid%d", i);
        if (i < 4) {
            lev->sortedClients[i] = i;
            clients[i].pers.connected = CON_CONNECTED;
            clients[i].sess.sessionTeam = TEAM_AXIS;
            ents[i].inuse = qtrue;
            ents[i].health = 100;
        }
    }
    level0.level = 0; snprintf(level0.flags, sizeof(level0.flags), "h");
    level3.level = 3; snprintf(level3.flags, sizeof(level3.flags), "hiqQLpxgUAWe");
    level5.level = 5; snprintf(level5.flags, sizeof(level5.flags), "hiqQLpxgUAWe");
    levels[0] = &level0; levels[1] = &level3; levels[2] = &level5;
    snprintf(admin0.guid, sizeof(admin0.guid), "guid0"); admin0.level = 3;
    snprintf(admin2.guid, sizeof(admin2.guid), "guid2"); admin2.level = 5;
    snprintf(immutable3.guid, sizeof(immutable3.guid), "guid3");
    snprintf(immutable3.flags, sizeof(immutable3.flags), "!"); immutable3.level = 0;
    admins[0] = &admin0; admins[1] = &admin2; admins[2] = &immutable3;
    snprintf(shrub->string, sizeof(shrub->string), "shrubbot.cfg");
    snprintf(reportfile->string, sizeof(reportfile->string), "reports.log");
    adminlog->string[0] = '\0';
    tyranny->integer = 0;
    viewlevel->integer = 5;
    game_state->integer = GS_PLAYING;
    warmup_damage->integer = 0;
    war_mask = 0; gravity_value = 800; gravity_sets = 0;
    snprintf(gravity_string, sizeof(gravity_string), "800");
    snprintf(expected_sound, sizeof(expected_sound), "sound/vo/oasis/news_oasis.wav");
    sound_exists = sound_opens = links = 0;
    errors = expect_error = dropped = binary_sends = binary_length = 0;
    last_drop = -1;
    fake_userinfo[0] = fake_session[0] = campaign_gametype[0] = '\0';
    ((vmCvar_t *)sym("g_dedicated"))->integer = 0;
    ((vmCvar_t *)sym("nq_security"))->integer = 0;
    ((vmCvar_t *)sym("g_logOptions"))->integer = 0;
    ((vmCvar_t *)sym("g_XPSave"))->integer = 0;
    ((void (*)(void))sym("G_InitFreeEntitiesChain"))();
    ((void (*)(int))sym("G_nm_clear_reports"))(-1);
    ((void (*)(int))sym("G_nm_clear_warnings"))(-1);
    ((void (*)(void))sym("G_nm_seen_reset"))();
    reset_messages();
}

static int message_contains(int dest, const char *fragment) {
    int i;
    for (i = 0; i < sent_count; ++i)
        if (sent[i].dest == dest && strstr(sent[i].text, fragment)) return 1;
    return 0;
}

/* Exercise the actual engine-facing config read, swap, failure and write
 * paths, including full dense lists rather than merely unit-testing a helper. */
static void test_shrubbot_config_reload(void) {
    qboolean (*read_config)(gentity_t *, int) = sym("G_shrubbot_readconfig");
    void (*cleanup)(void) = sym("G_shrubbot_cleanup");
    void (*write_config)(void) = sym("_shrubbot_writeconfig");
    qboolean (*permission)(gentity_t *, char) = sym("G_shrubbot_permission");
    qboolean (*mute_check)(const char *, const char *) = sym("G_shrubbot_mute_check");
    qboolean (*restore_mute)(gentity_t *) = sym("G_shrubbot_restore_mute");
    qboolean (*setlevel_lua)(int, int) = sym("G_shrubbot_setlevel_lua");
    g_shrubbot_ban_t **mutes = sym("g_shrubbot_mutes");
    g_shrubbot_command_t **commands = sym("g_shrubbot_commands");
    vmCvar_t *noq = sym("nq_noq");
    g_shrubbot_level_t *saved_level;
    g_shrubbot_ban_t *saved_ban;
    g_shrubbot_command_t *saved_command;
    char valid[4096], line[2048], *full, *p;
    time_t now = time(NULL);
    size_t full_length;
    int i, k;
    const char *bad[] = {
        "[command]\ncommand = fixture\nlevels = -1\n",
        "[command]\ncommand = fixture\nlevels = 3garbage\n",
        "[command]\ncommand = fixture\nlevels = 2147483648\n",
        "[level]\nlevel = 999999999999999999999999\n",
        "[admin]\nlevel = 3oops\n",
        "[admin]\nlevel =\n",
        "[level]\nname = \"unterminated",
        "[level]\nname = \"outer \\\"unfinished",
        "[level]\nname = valid\n/* unfinished comment",
        "[level]\nunknown_property = ignored-before-this-fix\n",
        "[level]\nflags = h\n\"\"\n[ban]\nguid = lost\n",
        "[unsupported]\nname = example\n",
        "[command]\ncommand = fixture\nlevels = +\n",
    };
    const char *sections[] = {"[level]\n", "[admin]\n", "[ban]\n", "[mute]\n", "[command]\n"};
    const int limits[] = {MAX_SHRUBBOT_LEVELS, MAX_SHRUBBOT_ADMINS, MAX_SHRUBBOT_BANS,
                          MAX_SHRUBBOT_BANS, MAX_SHRUBBOT_COMMANDS};

    setup_clients();
    /* Existing tests install stack/static fixtures. This test uses only
     * heap-owned records so the real cleanup is permitted to free them. */
    memset(levels, 0, sizeof(*levels) * (MAX_SHRUBBOT_LEVELS + 1));
    memset(admins, 0, sizeof(*admins) * (MAX_SHRUBBOT_ADMINS + 1));
    memset(bans, 0, sizeof(*bans) * (MAX_SHRUBBOT_BANS + 1));
    memset(mutes, 0, sizeof(*mutes) * (MAX_SHRUBBOT_BANS + 1));
    memset(commands, 0, sizeof(*commands) * (MAX_SHRUBBOT_COMMANDS + 1));
    noq->integer = 0; lev->time = 10000;
    cfg_fixture = 1; cfg_missing = 1; cfg_shortread = cfg_oversize = cfg_write_fail = 0;
    CHECK(!read_config(NULL, 0));
    CHECK(levels[0] && levels[5] && !levels[6] && !strcmp(levels[0]->flags, "iahC"));

    /* Two simultaneous clients with different names share no name-based
     * lookup: permanent, timed and expired GUID records are distinguished. */
    CHECK(now != (time_t)-1 && now - SHRUBBOT_BAN_EXPIRE_OFFSET + 200 < INT_MAX);
    snprintf(valid, sizeof(valid),
        "[level]\nlevel = 3\nname = Admin\nflags = hq\n"
        "[admin]\nname = Player0\nguid = guid0\nlevel = 3\nflags =\n"
        "[ban]\nname = Banned\nguid = bad-guid\nexpires = 0\n"
        "[mute]\nname = Old Name\nguid = guid1\nexpires = 0\n"
        "[mute]\nname = Expired\nguid = guid2\nexpires = %lld\n"
        "[mute]\nname = Timed\nguid = guid3\nexpires = %lld\n"
        "[command]\ncommand = fixture\nexec = \"echo \\\"two words\\\" [1]\"\nlevels = 0  3\t42 2147483647\ndesc = test\n",
        (long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET - 30,
        (long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET + 200);
    cfg_missing = 0; cfg_data = (const unsigned char *)valid; cfg_len = strlen(valid);
    CHECK(read_config(NULL, 0));
    CHECK(levels[0] && levels[0]->level == 3 && !levels[1]);
    CHECK(admins[0] && admins[0]->level == 3 && bans[0] && mutes[2]);
    CHECK(commands[0] && commands[0]->levels[0] == 0 && commands[0]->levels[1] == 3 &&
          commands[0]->levels[2] == 42 && commands[0]->levels[3] == INT_MAX && commands[0]->levels[4] == -1);
    CHECK(permission(&ents[0], 'q') && !permission(&ents[1], 'q'));
    CHECK(clients[1].sess.muted && clients[1].sess.auto_mute_time == -1 &&
          clients[1].sess.muted_by == MUTED_BY_SHRUB);
    CHECK(!clients[2].sess.muted && clients[3].sess.muted &&
          clients[3].sess.auto_mute_time >= lev->time + 198000 &&
          clients[3].sess.auto_mute_time <= lev->time + 200000);
    CHECK(mute_check("", "guid1") && !mute_check("", "guid2") && !mute_check("", ""));
    snprintf(clients[1].pers.netname, sizeof(clients[1].pers.netname), "Renamed Player");
    clients[1].sess.muted = qfalse;
    CHECK(restore_mute(&ents[1]) && clients[1].sess.muted && clients[1].sess.auto_mute_time == -1);
    clients[2].sess.muted = qtrue; clients[2].sess.muted_by = MUTED_BY_REF; clients[2].sess.auto_mute_time = -1;
    clients[3].sess.muted_by = MUTED_BY_REF; clients[3].sess.auto_mute_time = -1;
    CHECK(restore_mute(&ents[3]) && clients[3].sess.muted_by == (MUTED_BY_REF | MUTED_BY_SHRUB) &&
          clients[3].sess.auto_mute_time == -1);
    reset_messages(); setargs(2, "say", "!fixture hello");
    CHECK(check_command(&ents[0]) && !strcmp(queued_console, "echo \"two words\" hello\n"));

    /* A normal config write can be read back. Failure to open for writing
     * must not issue writes on an invalid engine file handle. */
    cfg_write_fail = 1; cfg_written_len = 0; write_config(); CHECK(cfg_written_len == 0);
    cfg_write_fail = 0;
    snprintf(admins[0]->name, sizeof(admins[0]->name), "X\"\\\n[admin]\nguid=evil\nlevel=3");
    snprintf(bans[0]->reason, sizeof(bans[0]->reason), "bad\n[admin]\nguid = injected");
    write_config();
    CHECK(cfg_written_len && strstr(cfg_written, "[command]") && strstr(cfg_written, "2147483647"));
    cfg_data = (const unsigned char *)cfg_written; cfg_len = cfg_written_len;
    CHECK(read_config(NULL, 0));
    CHECK(commands[0]->levels[3] == INT_MAX && commands[0]->levels[4] == -1);
    CHECK(!strcmp(commands[0]->exec, "echo \"two words\" [1]"));
    CHECK(admins[0] && !admins[1] && !strchr(admins[0]->name, '\n') &&
          !strchr(admins[0]->name, '"') && !strchr(admins[0]->name, '\\'));
    CHECK(bans[0] && !bans[1] && !strchr(bans[0]->reason, '\n'));
    reset_messages(); setargs(2, "say", "!fixture hello");
    CHECK(check_command(&ents[0]) && !strcmp(queued_console, "echo \"two words\" hello\n"));
    /* Preserve backslashes before ordinary bytes and before paired quotes;
     * compare exact bytes after the actual file writer and config reload. */
    {
        static const char *templates[] = {
            "echo C:\\folder\\file",
            "echo \"C:\\some path\\file\" [1]",
            "echo before\\\"inner words\\\"after",
            "echo \\\\\"paired\\\\\" finish"
        };
        for (i = 0; i < (int)(sizeof(templates) / sizeof(templates[0])); ++i) {
            snprintf(commands[0]->exec, sizeof(commands[0]->exec), "%s", templates[i]);
            write_config(); CHECK(cfg_written_len);
            cfg_data = (const unsigned char *)cfg_written; cfg_len = cfg_written_len;
            CHECK(read_config(NULL, 0));
            CHECK(commands[0] && !strcmp(commands[0]->exec, templates[i]));
        }
    }
    /* An unrepresentable template must fail before truncating the old file. */
    snprintf(commands[0]->exec, sizeof(commands[0]->exec), "echo \"unbalanced");
    cfg_written_len = 0; write_config(); CHECK(!cfg_written_len);
    snprintf(commands[0]->exec, sizeof(commands[0]->exec), "echo trailing\\");
    cfg_written_len = 0; write_config(); CHECK(!cfg_written_len);
    snprintf(commands[0]->exec, sizeof(commands[0]->exec), "echo \"two words\" [1]");

    saved_level = levels[0]; saved_ban = bans[0]; saved_command = commands[0];
    cfg_missing = 1; CHECK(!read_config(NULL, 0)); cfg_missing = 0;
    CHECK(levels[0] == saved_level && bans[0] == saved_ban && commands[0] == saved_command);
    cfg_oversize = 1; CHECK(!read_config(NULL, 0)); cfg_oversize = 0;
    CHECK(levels[0] == saved_level && bans[0] == saved_ban && commands[0] == saved_command);
    cfg_data = (const unsigned char *)valid; cfg_len = strlen(valid); cfg_shortread = 1;
    CHECK(!read_config(NULL, 0)); cfg_shortread = 0;
    CHECK(levels[0] == saved_level && bans[0] == saved_ban && commands[0] == saved_command);
    { static const char embedded[] = "[level]\nlevel = 0\0[ban]\n";
      cfg_data = (const unsigned char *)embedded; cfg_len = sizeof(embedded) - 1;
      CHECK(!read_config(NULL, 0)); CHECK(levels[0] == saved_level && bans[0] == saved_ban); }
    for (i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); ++i) {
        cfg_data = (const unsigned char *)bad[i]; cfg_len = strlen(bad[i]);
        CHECK(!read_config(NULL, 0));
        CHECK(levels[0] == saved_level && bans[0] == saved_ban && commands[0] == saved_command);
    }
    /* Too long for a name even though the token itself fits COM_ParseExt. */
    memset(line, 'x', sizeof(line)); memcpy(line, "[level]\nname = ", 15);
    line[15 + MAX_NAME_LENGTH] = '\n'; line[16 + MAX_NAME_LENGTH] = '\0';
    cfg_data = (const unsigned char *)line; cfg_len = strlen(line);
    CHECK(!read_config(NULL, 0)); CHECK(levels[0] == saved_level);
    /* A maximum-length quoted token used to write a terminator out of bounds. */
    memcpy(line, "[level]\nname = \"", 16);
    memset(line + 16, 'x', MAX_TOKEN_CHARS);
    line[16 + MAX_TOKEN_CHARS] = '"'; line[17 + MAX_TOKEN_CHARS] = '\n'; line[18 + MAX_TOKEN_CHARS] = '\0';
    cfg_len = strlen(line); CHECK(!read_config(NULL, 0)); CHECK(levels[0] == saved_level);

    /* Exactly 32 command levels are representable; the next must reject the
     * entire reload. Repeated levels lines replace, rather than leak, old ACLs. */
    p = line; p += sprintf(p, "[command]\ncommand = fixture\nexec = echo ok\nlevels = ");
    for (i = 0; i < MAX_SHRUBBOT_LEVELS; ++i) p += sprintf(p, "%d ", i);
    *p++ = '\n'; *p = '\0'; cfg_data = (const unsigned char *)line; cfg_len = strlen(line);
    CHECK(read_config(NULL, 0));
    CHECK(!clients[1].sess.muted && clients[1].sess.muted_by == UNMUTED);
    CHECK(clients[2].sess.muted && clients[2].sess.muted_by == MUTED_BY_REF);
    CHECK(clients[3].sess.muted && clients[3].sess.muted_by == (MUTED_BY_REF | MUTED_BY_SHRUB));
    CHECK(commands[0]->levels[MAX_SHRUBBOT_LEVELS - 1] == MAX_SHRUBBOT_LEVELS - 1 &&
          commands[0]->levels[MAX_SHRUBBOT_LEVELS] == -1);
    saved_command = commands[0];
    p[-1] = ' '; p += sprintf(p, "%d\n", MAX_SHRUBBOT_LEVELS); cfg_len = strlen(line);
    CHECK(!read_config(NULL, 0) && commands[0] == saved_command);
    cfg_data = (const unsigned char *)"[command]\ncommand = fixture\nlevels = 1 2 3\nlevels = 42\n";
    cfg_len = strlen((const char *)cfg_data); CHECK(read_config(NULL, 0));
    CHECK(commands[0]->levels[0] == 42 && commands[0]->levels[1] == -1);

    /* Disabled [admin]/[ban]/[mute] blocks may not feed their fields into the
     * previous level, nor interpret a value as an embedded section header. */
    noq->integer = NOQ_DONT_READ_ADMINS | NOQ_DONT_READ_BANS | NOQ_DONT_READ_MUTES;
    cfg_data = (const unsigned char *)
        "[level]\nlevel = 3\nname = Keep\nflags = h\n"
        "[admin]\nname = [command]\nflags = hijacked\n"
        "[ban]\nname = Fake\nflags = hijacked\n"
        "[mute]\nname = Fake\nflags = hijacked\n"
        "[command]\ncommand = fixture\nlevels = 3\n";
    cfg_len = strlen((const char *)cfg_data); CHECK(read_config(NULL, 0));
    CHECK(!strcmp(levels[0]->name, "Keep") && !strcmp(levels[0]->flags, "h"));
    CHECK(!admins[0] && !bans[0] && !mutes[0] && commands[0] && !commands[1]);
    noq->integer = 0;

    /* Fill all five arrays to capacity and exercise sentinel walks, writes,
     * cleanup and all five capacity+one failure paths under sanitizers. */
    full = malloc(2 * 1024 * 1024); CHECK(full); p = full;
    for (k = 0; k < 5; ++k)
        for (i = 0; i < limits[k]; ++i) { size_t n = strlen(sections[k]); memcpy(p, sections[k], n); p += n; }
    *p = '\0'; full_length = (size_t)(p - full); CHECK(full_length < 2 * 1024 * 1024 - 32);
    cfg_data = (const unsigned char *)full; cfg_len = full_length;
    CHECK(read_config(NULL, 0));
    CHECK(levels[MAX_SHRUBBOT_LEVELS - 1] && !levels[MAX_SHRUBBOT_LEVELS]);
    CHECK(admins[MAX_SHRUBBOT_ADMINS - 1] && !admins[MAX_SHRUBBOT_ADMINS]);
    CHECK(bans[MAX_SHRUBBOT_BANS - 1] && !bans[MAX_SHRUBBOT_BANS]);
    CHECK(mutes[MAX_SHRUBBOT_BANS - 1] && !mutes[MAX_SHRUBBOT_BANS]);
    CHECK(commands[MAX_SHRUBBOT_COMMANDS - 1] && !commands[MAX_SHRUBBOT_COMMANDS]);
    CHECK(!permission(&ents[0], 'q') && !mute_check("", "not-found"));
    levels[0]->level = 3; CHECK(!setlevel_lua(0, 3));
    write_config(); CHECK(cfg_written_len > 10000);
    saved_level = levels[0]; saved_ban = bans[0]; saved_command = commands[0];
    for (k = 0; k < 5; ++k) {
        strcpy(full + full_length, sections[k]); cfg_len = strlen(full);
        CHECK(!read_config(NULL, 0));
        CHECK(levels[0] == saved_level && bans[0] == saved_ban && commands[0] == saved_command);
    }
    cleanup();
    CHECK(!levels[0] && !admins[0] && !bans[0] && !mutes[0] && !commands[0]);
    free(full); free(cfg_written); cfg_written = NULL; cfg_written_len = cfg_written_cap = 0;
    cfg_fixture = 0; cfg_data = NULL; cfg_len = 0;
}

/* The generic quoted field writer is also used by NQ map-vote statistics. */
static void test_mapvote_config_roundtrip(void) {
    void (*write_info)(void) = sym("G_mapvoteinfo_write");
    void (*read_info)(void) = sym("G_mapvoteinfo_read");
    static const char old_format[] =
        "[mapvoteinfo]\nname = testmap\ntimes_played = 14\nlast_played = -1\n"
        "total_votes = 31\nvote_eligible = 1\n";
    setup_clients(); cfg_fixture = cfg_mapvote = 1;
    cfg_missing = cfg_shortread = cfg_oversize = cfg_write_fail = 0;
    lev->mapVoteNumMaps = 2;
    snprintf(lev->mapvoteinfo[0].bspName, sizeof(lev->mapvoteinfo[0].bspName), "testmap");
    snprintf(lev->mapvoteinfo[1].bspName, sizeof(lev->mapvoteinfo[1].bspName), "second_map");
    lev->mapvoteinfo[0].timesPlayed = 12; lev->mapvoteinfo[0].lastPlayed = -1;
    lev->mapvoteinfo[0].totalVotes = 88; lev->mapvoteinfo[0].voteEligible = 1;
    lev->mapvoteinfo[1].timesPlayed = 3; lev->mapvoteinfo[1].lastPlayed = 2;
    lev->mapvoteinfo[1].totalVotes = 17; lev->mapvoteinfo[1].voteEligible = 0;
    write_info(); CHECK(cfg_written_len && strstr(cfg_written, "\"testmap\"") && strstr(cfg_written, "\"12\""));
    cfg_data = (const unsigned char *)cfg_written; cfg_len = cfg_written_len;
    lev->mapvoteinfo[0].timesPlayed = lev->mapvoteinfo[0].lastPlayed = lev->mapvoteinfo[0].totalVotes = 0;
    lev->mapvoteinfo[0].voteEligible = 0;
    lev->mapvoteinfo[1].timesPlayed = lev->mapvoteinfo[1].lastPlayed = lev->mapvoteinfo[1].totalVotes = 0;
    read_info();
    CHECK(lev->mapvoteinfo[0].timesPlayed == 12 && lev->mapvoteinfo[0].lastPlayed == -1 &&
          lev->mapvoteinfo[0].totalVotes == 88 && lev->mapvoteinfo[0].voteEligible == 1);
    CHECK(lev->mapvoteinfo[1].timesPlayed == 3 && lev->mapvoteinfo[1].lastPlayed == 2 &&
          lev->mapvoteinfo[1].totalVotes == 17);
    cfg_data = (const unsigned char *)old_format; cfg_len = strlen(old_format); read_info();
    CHECK(lev->mapvoteinfo[0].timesPlayed == 14 && lev->mapvoteinfo[0].totalVotes == 31);
    cfg_shortread = 1; lev->mapvoteinfo[0].timesPlayed = 42; read_info();
    CHECK(lev->mapvoteinfo[0].timesPlayed == 42); cfg_shortread = 0;
    cfg_oversize = 1; read_info(); CHECK(lev->mapvoteinfo[0].timesPlayed == 42); cfg_oversize = 0;
    cfg_missing = 1; reset_messages(); read_info(); CHECK(closes == 0); cfg_missing = 0;
    cfg_write_fail = 1; reset_messages(); write_info(); CHECK(writes == 0 && closes == 0); cfg_write_fail = 0;
    free(cfg_written); cfg_written = NULL; cfg_written_len = cfg_written_cap = 0;
    cfg_data = NULL; cfg_len = 0; cfg_fixture = cfg_mapvote = 0;
}

/* Actual !showbans handler: paging through expired gaps, per-viewer state,
 * filtering without mutating records, hostile text, and hostile page input. */
static void test_showbans_paging(void) {
    qboolean (*show)(gentity_t *, int) = sym("G_shrubbot_showbans");
    void (*clear_pages)(int) = sym("G_shrubbot_clear_ban_pages");
    static g_shrubbot_ban_t records[70];
    time_t now = time(NULL);
    int i;
    CHECK(now != (time_t)-1 && (long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET < INT_MAX);
    setup_clients(); clear_pages(-1);
    memset(records, 0, sizeof(records));
    for (i = 0; i < 70; ++i) {
        snprintf(records[i].name, sizeof(records[i].name), "Player%02d", i);
        snprintf(records[i].banner, sizeof(records[i].banner), "ByAdmin");
        snprintf(records[i].reason, sizeof(records[i].reason), "MixedCase");
        snprintf(records[i].made, sizeof(records[i].made), "2026-11-21 12:34");
        bans[i] = &records[i];
    }
    records[3].expires = records[4].expires = (int)((long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET - 1);
    snprintf(records[0].name, sizeof(records[0].name), "First\"\\\nName");
    snprintf(records[0].reason, sizeof(records[0].reason), "MixedCase\"\\\nReason");

    reset_messages(); setargs(2, "say", "!showbans"); CHECK(show(&ents[0], 1));
    CHECK(message_contains(0, "First   Name") && message_contains(0, "MixedCase   Reason"));
    CHECK(message_contains(0, "Player31") && !message_contains(0, "Player32"));
    CHECK(message_contains(0, "68"));
    CHECK(!strcmp(records[0].name, "First\"\\\nName"));
    CHECK(!strcmp(records[0].reason, "MixedCase\"\\\nReason"));
    reset_messages(); setargs(2, "say", "!showbans next"); CHECK(show(&ents[0], 1));
    CHECK(message_contains(0, "Player32") && !message_contains(0, "Player31"));
    /* Other players and the console have independent cursors. */
    reset_messages(); CHECK(show(&ents[1], 1));
    CHECK(message_contains(1, "First   Name") && !message_contains(1, "Player32"));
    reset_messages(); setargs(2, "say", "!showbans prev"); CHECK(show(&ents[0], 1));
    CHECK(message_contains(0, "First   Name") && !message_contains(0, "Player32"));
    reset_messages(); setargs(1, "!showbans"); CHECK(show(NULL, 0));
    CHECK(strstr(console_output, "Player31") && !strstr(console_output, "Player32"));
    reset_messages(); setargs(2, "!showbans", "next"); CHECK(show(NULL, 0));
    CHECK(strstr(console_output, "Player32"));

    reset_messages(); setargs(2, "say", "!showbans 1"); CHECK(show(&ents[0], 1));
    CHECK(message_contains(0, "First   Name"));
    reset_messages(); setargs(2, "say", "!showbans 3"); CHECK(show(&ents[0], 1));
    CHECK(message_contains(0, "Player02") && !message_contains(0, "First   Name"));
    reset_messages(); setargs(2, "say", "!showbans reason mIxEdCaSe"); CHECK(show(&ents[0], 1));
    CHECK(message_contains(0, "First   Name") && message_contains(0, "68 active"));
    CHECK(!strcmp(records[0].reason, "MixedCase\"\\\nReason"));
    reset_messages(); setargs(2, "say", "!showbans reason MIXEDCASE next"); CHECK(show(&ents[0], 1));
    CHECK(message_contains(0, "Player32") && !message_contains(0, "Player31"));
    reset_messages(); setargs(2, "say", "!showbans find player -1"); CHECK(show(&ents[0], 1));
    CHECK(message_contains(0, "Player69") && !message_contains(0, "Player68"));
    reset_messages(); setargs(2, "say", "!showbans banner byadmin -2147483648"); CHECK(show(&ents[0], 1));
    CHECK(message_contains(0, "First   Name"));
    reset_messages(); setargs(2, "say", "!showbans reason mixedcase -999999999999999999999");
    CHECK(!show(&ents[0], 1) && message_contains(0, "usage"));
    reset_messages(); setargs(2, "say", "!showbans reason mixedcase 999999999");
    CHECK(!show(&ents[0], 1) && message_contains(0, "no matching bans"));
    reset_messages(); setargs(2, "say", "!showbans next"); CHECK(show(&ents[0], 1));
    CHECK(message_contains(0, "First   Name")); /* changed query resets the cursor */
    reset_messages(); CHECK(show(&ents[0], 1)); CHECK(message_contains(0, "Player32"));
    clear_pages(0);
    reset_messages(); CHECK(show(&ents[0], 1)); CHECK(message_contains(0, "First   Name"));
    CHECK(!show(&ents[MAX_CLIENTS], 1));
}

/* Test the actual ban-creation handlers for duration arithmetic, offline
 * record contents, GUID/IP validation and allocation failures. */
static void cleanup_bans(void) {
    int i;
    for (i = 0; i < MAX_SHRUBBOT_BANS && bans[i]; ++i) free(bans[i]);
    memset(bans, 0, (MAX_SHRUBBOT_BANS + 1) * sizeof(*bans));
}

static void test_ban_creation(void) {
    qboolean (*ban)(gentity_t *, int) = sym("G_shrubbot_ban");
    qboolean (*banguid)(gentity_t *, int) = sym("G_shrubbot_banguid");
    qboolean (*banip)(gentity_t *, int) = sym("G_shrubbot_banIP");
    qboolean (*tempban)(int, char *, int) = sym("G_shrubbot_tempban");
    vmCvar_t *noq = sym("nq_noq");
    time_t now = time(NULL);
    long long rel = (long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET;

    CHECK(now != (time_t)-1 && rel > 0 && rel + 700 < INT_MAX);
    setup_clients(); cleanup_bans(); noq->integer = NOQ_DONT_WRITE;
    lev->time = 10000;
    snprintf(clients[1].pers.client_ip, sizeof(clients[1].pers.client_ip), "203.0.113.1:27960");

    setargs(5, "say", "!ban", "1", "60", "reason");
    CHECK(ban(&ents[0], 1));
    CHECK(bans[0] && !bans[1] && !strcmp(bans[0]->guid, "guid1"));
    CHECK(!strcmp(bans[0]->ip, "203.0.113.1") && !strcmp(bans[0]->reason, "reason"));
    CHECK(bans[0]->expires >= rel + 60 && bans[0]->expires <= rel + 61);
    cleanup_bans();

    /* week modifier times a large value must saturate rather than overflow */
    setargs(5, "say", "!ban", "1", "99999w", "reason");
    CHECK(ban(&ents[0], 1));
    CHECK(bans[0] && bans[0]->expires == INT_MAX);
    cleanup_bans();

    /* offline GUID records require exactly PB_GUID_LENGTH characters */
    setargs(5, "say", "!banguid", "offline", "60", "reason");
    CHECK(!banguid(&ents[0], 1));
    setargs(5, "say", "!banguid",
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "60", "reason");
    CHECK(banguid(&ents[0], 1));
    CHECK(bans[0] && !strcmp(bans[0]->guid, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") &&
          !strcmp(bans[0]->ip, "offline") && !strcmp(bans[0]->name, "unknown"));
    CHECK(bans[0]->expires >= rel + 60 && bans[0]->expires <= rel + 61);
    cleanup_bans();

    /* offline IP bans validate well-formed IPv4 addresses */
    setargs(5, "say", "!banIP", "1.2.3", "60", "reason");
    CHECK(!banip(&ents[0], 1));
    setargs(5, "say", "!banIP", "256.1.1.1", "60", "reason");
    CHECK(!banip(&ents[0], 1));
    setargs(5, "say", "!banIP", "01.1.1.1", "60", "reason");
    CHECK(!banip(&ents[0], 1));
    setargs(5, "say", "!banIP", "203.0.113.5", "60", "reason");
    CHECK(banip(&ents[0], 1));
    CHECK(bans[0] && !strcmp(bans[0]->ip, "203.0.113.5") &&
          !strcmp(bans[0]->guid, "offline"));
    cleanup_bans();

    /* temporary bans reject non-positive durations */
    CHECK(!tempban(1, "reason", 0));
    CHECK(!tempban(1, "reason", -1));
    CHECK(!bans[0]);
    CHECK(tempban(1, "reason", 60));
    CHECK(bans[0] && !strcmp(bans[0]->reason, "reason") &&
          !strcmp(bans[0]->banner, "Temp Ban System"));
    CHECK(bans[0]->expires >= rel + 60 && bans[0]->expires <= rel + 61);
    cleanup_bans();

    /* bots and the local host cannot be banned by name */
    clients[1].pers.localClient = qtrue; ents[1].r.svFlags = SVF_BOT;
    reset_messages(); setargs(5, "say", "!ban", "1", "60", "reason");
    CHECK(!ban(&ents[0], 1) && message_contains(0, "Cannot ban a bot"));
    ents[1].r.svFlags = 0;
    setargs(5, "say", "!ban", "1", "60", "reason");
    CHECK(!ban(&ents[0], 1) && message_contains(0, "Cannot ban server host"));
    clients[1].pers.localClient = qfalse;
    CHECK(!bans[0]);

    /* a higher-level target cannot be banned */
    snprintf(clients[2].pers.cl_guid, sizeof(clients[2].pers.cl_guid), "guid2");
    reset_messages(); setargs(5, "say", "!ban", "2", "60", "reason");
    CHECK(!ban(&ents[0], 1) && message_contains(0, "higher admin"));
    CHECK(!bans[0]);
    noq->integer = 0;
}

/* Test native mute/unmute against live clients and offline persisted records.
 * The latter are deliberately beyond client slot 63. */
static void test_mute_commands(void) {
    qboolean (*mute)(gentity_t *, int) = sym("G_shrubbot_mute");
    qboolean (*unmute)(gentity_t *, int) = sym("G_shrubbot_unmute");
    qboolean (*restore)(gentity_t *) = sym("G_shrubbot_restore_mute");
    qboolean (*check)(const char *, const char *) = sym("G_shrubbot_mute_check");
    qboolean (*permission)(gentity_t *, char) = sym("G_shrubbot_permission");
    qboolean (*setlevel_lua)(int, int) = sym("G_shrubbot_setlevel_lua");
    qboolean (*setlevel)(gentity_t *, int) = sym("G_shrubbot_setlevel");
    qboolean (*ban_check)(const char *, const char *, const char *) = sym("G_shrubbot_ban_check");
    int (*getlevel)(gentity_t *) = sym("G_shrubbot_level");
    g_shrubbot_ban_t **mutes = sym("g_shrubbot_mutes");
    vmCvar_t *noq = sym("nq_noq");
    static g_shrubbot_admin_t invalidAdmin;
    static g_shrubbot_ban_t offline[103];
    const char *invalid[] = {"", "unknown", "No_GuId", "offline"};
    time_t now = time(NULL);
    long long rel = (long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET;
    int i;
    CHECK(now != (time_t)-1 && rel > 0 && rel + 500 < INT_MAX);
    setup_clients(); memset(mutes, 0, (MAX_SHRUBBOT_BANS + 1) * sizeof(*mutes));
    noq->integer = NOQ_DONT_WRITE;
    lev->time = 10000;
    snprintf(clients[1].pers.client_ip, sizeof(clients[1].pers.client_ip), "203.0.113.1:27960");
    setargs(2, "say", "!mute 1 2m spamming"); CHECK(mute(&ents[0], 1));
    CHECK(mutes[0] && !mutes[1] && !strcmp(mutes[0]->guid, "guid1"));
    CHECK(!strcmp(mutes[0]->ip, "203.0.113.1") && !strcmp(mutes[0]->reason, "spamming"));
    CHECK(clients[1].sess.muted && clients[1].sess.muted_by == MUTED_BY_SHRUB &&
          clients[1].sess.auto_mute_time == 130000 && mutes[0]->expires >= rel + 120 &&
          mutes[0]->expires <= rel + 122);
    snprintf(clients[1].pers.netname, sizeof(clients[1].pers.netname), "Renamed Player");
    setargs(2, "say", "!unmute 1"); CHECK(unmute(&ents[0], 1));
    CHECK(!clients[1].sess.muted && mutes[0]->expires == INT_MIN && !restore(&ents[1]));
    free(mutes[0]); memset(mutes, 0, (MAX_SHRUBBOT_BANS + 1) * sizeof(*mutes));

    setargs(2, "say", "!mute 1 999999w overflow"); CHECK(!mute(&ents[0], 1));
    setargs(2, "say", "!mute 1 99999999999999999999999w overflow"); CHECK(!mute(&ents[0], 1));
    setargs(2, "say", "!mute 1 2z overflow"); CHECK(!mute(&ents[0], 1));
    setargs(2, "say", "!mute 1 25d overflow"); CHECK(!mute(&ents[0], 1));
    CHECK(!mutes[0] && !clients[1].sess.muted);
    setargs(2, "say", "!mute 2 60 reason"); CHECK(!mute(&ents[0], 1)); /* higher */
    setargs(2, "say", "!mute 3 60 reason"); CHECK(!mute(&ents[0], 1)); /* immutable */

    /* Blank and common placeholder GUIDs never match privileged admin or
     * mute records and cannot be assigned a level via either entrypoint. */
    memset(&invalidAdmin, 0, sizeof(invalidAdmin)); invalidAdmin.level = 5;
    snprintf(invalidAdmin.flags, sizeof(invalidAdmin.flags), "*"); admins[3] = &invalidAdmin;
    for (i = 0; i < (int)(sizeof(invalid) / sizeof(invalid[0])); ++i) {
        snprintf(clients[1].pers.cl_guid, sizeof(clients[1].pers.cl_guid), "%s", invalid[i]);
        snprintf(invalidAdmin.guid, sizeof(invalidAdmin.guid), "%s", invalid[i]);
        CHECK(!permission(&ents[1], 's') && getlevel(&ents[1]) == 0 && !setlevel_lua(1, 3));
        setargs(2, "say", "!setlevel 1 3"); CHECK(!setlevel(&ents[0], 1));
        setargs(2, "say", "!mute 1 60 reason"); CHECK(!mute(&ents[0], 1));
        CHECK(!check("", invalid[i]) && !restore(&ents[1]));
    }
    admins[3] = NULL;
    snprintf(clients[1].pers.cl_guid, sizeof(clients[1].pers.cl_guid), "guid1");
    {
        char reason[MAX_STRING_CHARS];
        memset(&offline[0], 0, sizeof(offline[0])); bans[0] = &offline[0];
        for (i = 0; i < (int)(sizeof(invalid) / sizeof(invalid[0])); ++i) {
            snprintf(offline[0].guid, sizeof(offline[0].guid), "%s", invalid[i]);
            CHECK(!ban_check("", invalid[i], reason));
        }
        snprintf(offline[0].guid, sizeof(offline[0].guid), "guid1");
        snprintf(offline[0].reason, sizeof(offline[0].reason), "testing");
        CHECK(ban_check("", "GUID1", reason) && strstr(reason, "permanent"));
        snprintf(offline[0].ip, sizeof(offline[0].ip), "203.0.113.");
        CHECK(ban_check("203.0.113.1:27960", "unknown", reason));
        offline[0].expires = (int)rel - 1;
        CHECK(!ban_check("203.0.113.1:27960", "guid1", reason));
        bans[0] = NULL;
    }

    /* Duplicate active keys use permanent or longest expiry irrespective of
     * record order; restoring an excessively distant deadline saturates. */
    memset(offline, 0, sizeof(offline));
    for (i = 0; i < 3; ++i) {
        snprintf(offline[i].guid, sizeof(offline[i].guid), "guid1");
        snprintf(offline[i].name, sizeof(offline[i].name), "OldName");
        mutes[i] = &offline[i];
    }
    offline[0].expires = (int)rel + 100; offline[1].expires = 0; offline[2].expires = (int)rel + 500;
    CHECK(restore(&ents[1]) && clients[1].sess.auto_mute_time == -1);
    offline[1].expires = INT_MIN; clients[1].sess.muted = qfalse;
    CHECK(restore(&ents[1]) && clients[1].sess.auto_mute_time >= lev->time + 498000 &&
          clients[1].sess.auto_mute_time <= lev->time + 500000);
    offline[2].expires = INT_MAX; clients[1].sess.muted = qfalse;
    CHECK(restore(&ents[1]) && clients[1].sess.auto_mute_time == INT_MAX);
    /* A second connected client with the same key retains other mute sources. */
    snprintf(clients[2].pers.cl_guid, sizeof(clients[2].pers.cl_guid), "guid1");
    clients[2].sess.muted = qtrue; clients[2].sess.muted_by = MUTED_BY_REF | MUTED_BY_SHRUB;
    clients[2].sess.auto_mute_time = -1;
    setargs(2, "say", "!unmute 1"); CHECK(unmute(&ents[0], 1));
    CHECK(!clients[1].sess.muted && offline[0].expires == INT_MIN && offline[2].expires == INT_MIN);
    CHECK(clients[2].sess.muted && clients[2].sess.muted_by == (MUTED_BY_REF | MUTED_BY_SHRUB));

    /* Offline record indexes are not entity indexes. Ambiguous record names
     * require an explicit selection and only matching identities expire. */
    memset(mutes, 0, (MAX_SHRUBBOT_BANS + 1) * sizeof(*mutes));
    memset(offline, 0, sizeof(offline));
    for (i = 0; i < 103; ++i) { mutes[i] = &offline[i]; offline[i].expires = INT_MIN; }
    snprintf(offline[100].name, sizeof(offline[100].name), "GoneOne");
    snprintf(offline[100].guid, sizeof(offline[100].guid), "offline-a"); offline[100].expires = 0;
    snprintf(offline[101].name, sizeof(offline[101].name), "GoneTwo");
    snprintf(offline[101].guid, sizeof(offline[101].guid), "offline-b"); offline[101].expires = 0;
    snprintf(offline[102].name, sizeof(offline[102].name), "OtherOldName");
    snprintf(offline[102].guid, sizeof(offline[102].guid), "offline-b"); offline[102].expires = (int)rel + 100;
    reset_messages(); setargs(2, "say", "!unmute nobody"); CHECK(!unmute(&ents[0], 1));
    reset_messages(); setargs(2, "say", "!unmute gone"); CHECK(!unmute(&ents[0], 1));
    CHECK(message_contains(0, "multiple active records") && message_contains(0, "GoneTwo"));
    setargs(2, "say", "!unmute gone 9999999999999999999"); CHECK(!unmute(&ents[0], 1));
    setargs(2, "say", "!unmute gone 2"); CHECK(unmute(&ents[0], 1));
    CHECK(offline[100].expires == 0 && offline[101].expires == INT_MIN && offline[102].expires == INT_MIN);
    setargs(2, "say", "!unmute gone"); CHECK(unmute(&ents[0], 1));
    CHECK(offline[100].expires == INT_MIN);
    memset(mutes, 0, (MAX_SHRUBBOT_BANS + 1) * sizeof(*mutes));
    noq->integer = 0;
}

/* Runs in a separate fake-engine process with tests/fault_preload.c loaded. */
static void test_config_fault_injection(void) {
    void (*fail_clock)(int) = dlsym(RTLD_DEFAULT, "NM_test_fail_clock");
    void (*fail_malloc)(int) = dlsym(RTLD_DEFAULT, "NM_test_fail_malloc_after");
    void (*fail_calloc)(int) = dlsym(RTLD_DEFAULT, "NM_test_fail_calloc_after");
    qboolean (*read_config)(gentity_t *, int) = sym("G_shrubbot_readconfig");
    qboolean (*setlevel_lua)(int, int) = sym("G_shrubbot_setlevel_lua");
    qboolean (*setlevel)(gentity_t *, int) = sym("G_shrubbot_setlevel");
    qboolean (*mute)(gentity_t *, int) = sym("G_shrubbot_mute");
    qboolean (*unmute)(gentity_t *, int) = sym("G_shrubbot_unmute");
    void (*write_config)(void) = sym("_shrubbot_writeconfig");
    void (*cleanup)(void) = sym("G_shrubbot_cleanup");
    g_shrubbot_ban_t **mutes = sym("g_shrubbot_mutes");
    g_shrubbot_command_t **commands = sym("g_shrubbot_commands");
    vmCvar_t *noq = sym("nq_noq");
    g_shrubbot_level_t *saved_level;
    g_shrubbot_admin_t *saved_admin;
    g_shrubbot_ban_t *saved_ban, *saved_mute;
    g_shrubbot_command_t *saved_command;
    size_t saved_write;
    int i;
    time_t warm = time(NULL);
    static const char initial[] =
        "[level]\nlevel = 3\nname = Admin\nflags = m\n"
        "[admin]\nguid = guid0\nlevel = 3\n"
        "[ban]\nguid = bad\nexpires = 0\n"
        "[mute]\nguid = guid1\nname = Player1\nexpires = 0\n"
        "[command]\ncommand = fixture\nexec = echo ok\nlevels = 3\n";
    static const char without_mute[] =
        "[level]\nlevel = 3\nname = Admin\nflags = m\n"
        "[admin]\nguid = guid0\nlevel = 3\n";
    CHECK(fail_clock && fail_malloc && fail_calloc);
    CHECK(localtime(&warm)); /* initialize libc timezone before allocation injection */
    setup_clients();
    memset(levels, 0, (MAX_SHRUBBOT_LEVELS + 1) * sizeof(*levels));
    memset(admins, 0, (MAX_SHRUBBOT_ADMINS + 1) * sizeof(*admins));
    memset(bans, 0, (MAX_SHRUBBOT_BANS + 1) * sizeof(*bans));
    memset(mutes, 0, (MAX_SHRUBBOT_BANS + 1) * sizeof(*mutes));
    memset(commands, 0, (MAX_SHRUBBOT_COMMANDS + 1) * sizeof(*commands));
    noq->integer = 0; lev->time = 10000;
    snprintf(clients[1].pers.client_ip, sizeof(clients[1].pers.client_ip), "203.0.113.1:27960");
    cfg_fixture = 1; cfg_mapvote = cfg_missing = cfg_shortread = cfg_oversize = cfg_write_fail = 0;
    cfg_data = (const unsigned char *)initial; cfg_len = strlen(initial);
    CHECK(read_config(NULL, 0));
    CHECK(clients[1].sess.muted && clients[1].sess.muted_by == MUTED_BY_SHRUB);
    saved_level = levels[0]; saved_admin = admins[0]; saved_ban = bans[0];
    saved_mute = mutes[0]; saved_command = commands[0];
    /* Buffer, staging object, then each of the five section allocations. */
    for (i = 0; i < 7; ++i) {
        fail_calloc(i);
        CHECK(!read_config(NULL, 0));
        CHECK(levels[0] == saved_level && admins[0] == saved_admin && bans[0] == saved_ban &&
              mutes[0] == saved_mute && commands[0] == saved_command && clients[1].sess.muted);
    }
    write_config(); CHECK(cfg_written_len); saved_write = cfg_written_len;
    fail_clock(1);
    write_config(); CHECK(cfg_written_len == saved_write); /* file was not opened */
    setargs(2, "say", "!unmute 1"); CHECK(!unmute(&ents[0], 1));
    CHECK(clients[1].sess.muted && mutes[0] == saved_mute);
    cfg_data = (const unsigned char *)without_mute; cfg_len = strlen(without_mute);
    CHECK(read_config(NULL, 0));
    CHECK(!mutes[0] && clients[1].sess.muted); /* clock failure retains state */
    fail_clock(0);
    CHECK(read_config(NULL, 0)); CHECK(!clients[1].sess.muted);
    fail_malloc(0); CHECK(!setlevel_lua(1, 3) && !admins[1]);
    setargs(2, "say", "!setlevel 1 3"); fail_malloc(0);
    CHECK(!setlevel(&ents[0], 1) && !admins[1]);
    setargs(2, "say", "!mute 1 60 reason"); fail_calloc(0);
    CHECK(!mute(&ents[0], 1) && !mutes[0] && !clients[1].sess.muted);
    cleanup();
    free(cfg_written); cfg_written = NULL; cfg_written_len = cfg_written_cap = 0;
    cfg_data = NULL; cfg_len = 0; cfg_fixture = 0;
    puts("shrub config allocation/clock fault tests passed");
}

static void test_vectors(void) {
    float (*inverse_sqrt)(float) = sym("Q_rsqrt");
    float (*normalize)(float *) = sym("VectorNormalize");
    float (*normalize2)(const float *, float *) = sym("VectorNormalize2");
    void (*normalize_fast)(float *) = sym("VectorNormalizeFast");
    float up[3] = {0, 0, 10}, zero[3] = {0, 0, 0}, out[3] = {9, 9, 9};
    int i;
    /* Shake the stack between calls: the original LP64 implementation read
     * past the float and produced nondeterministically negative vectors. */
    for (i = 0; i < 100; ++i) CHECK(fabsf(inverse_sqrt(100.0f) - .1f) < .001f);
    CHECK(fabsf(normalize(up) - 10.0f) < .05f && up[2] > .99f && up[2] < 1.01f);
    CHECK(normalize(zero) == 0 && zero[0] == 0 && zero[1] == 0 && zero[2] == 0);
    CHECK(normalize2(zero, out) == 0 && out[0] == 0 && out[1] == 0 && out[2] == 0);
    normalize_fast(zero);
    CHECK(zero[0] == 0 && zero[1] == 0 && zero[2] == 0);
}

static void test_shrub_chat_tokenization(void) {
    int (*argc_say)(void) = sym("Q_SayArgc");
    qboolean (*argv_say)(int, char *, int) = sym("Q_SayArgv");
    char *(*concat_say)(int) = sym("Q_SayConcatArgs");
    char out[128], short_buffer[3], near_limit[1019];
    int n;

    setargs(2, "say", "!warn    1  two   words   ");
    CHECK(argc_say() == 5);
    CHECK(argv_say(0, out, sizeof(out)) && !strcmp(out, "say"));
    CHECK(argv_say(1, out, sizeof(out)) && !strcmp(out, "!warn"));
    CHECK(argv_say(2, out, sizeof(out)) && !strcmp(out, "1"));
    CHECK(argv_say(3, out, sizeof(out)) && !strcmp(out, "two"));
    CHECK(argv_say(4, out, sizeof(out)) && !strcmp(out, "words"));
    CHECK(!strcmp(concat_say(3), "two   words   "));
    CHECK(argv_say(4, short_buffer, sizeof(short_buffer)) && !strcmp(short_buffer, "wo"));
    CHECK(!argv_say(5, out, sizeof(out)) && !out[0] && !*concat_say(5));
    CHECK(!argv_say(-1, out, sizeof(out)) && !out[0] && !*concat_say(-1));

    /* Split console args and a quoted message share identical tokenization. */
    setargs(5, "say", "!warn", "1", "two", "words");
    CHECK(argc_say() == 5 && !strcmp(concat_say(3), "two words"));
    /* Prime ConcatArgs' static tail, then use a shorter trailing-space
     * command: old bytes after the new NUL must not become a reason. */
    setargs(2, "say", "!warn 1 stale-in-static-tail"); CHECK(argc_say() == 4);
    setargs(2, "say", "!warn 1       ");
    CHECK(argc_say() == 3 && !*concat_say(3));
    CHECK(!argv_say(3, out, sizeof(out)) && !out[0]);
    setargs(2, "say", "          "); CHECK(argc_say() == 1 && !*concat_say(1));
    setargs(1, ""); CHECK(argc_say() == 0 && !argv_say(0, out, sizeof(out)));
    CHECK(!argv_say(0, NULL, 1) && !argv_say(0, out, 0));
    setargs(2, "say", "\t!cmd\tfoo  bar");
    CHECK(argc_say() == 3 && argv_say(1, out, sizeof(out)) && !strcmp(out, "\t!cmd\tfoo"));
    CHECK(!*concat_say(INT_MAX)); /* NQ's general parser splits literal spaces, not tabs */

    memset(near_limit, 'x', sizeof(near_limit) - 1);
    for (n = 1; n <= 20; ++n) near_limit[sizeof(near_limit) - 1 - n] = ' ';
    near_limit[sizeof(near_limit) - 1] = '\0';
    setargs(2, "say", near_limit);
    CHECK(argc_say() == 2 && !*concat_say(2));
    CHECK(!argv_say(2, out, sizeof(out)) && !out[0]);
}

static void test_string_replacement(void) {
    char *(*replace)(char *, char *, char *) = sym("Q_StrReplace");
    char *(*shortcut_sanitize)(char *) = sym("G_ShortcutSanitize");
    char input[800], replacement[100], *result;
    int i;
    CHECK(!strcmp(replace("alpha beta ALPHA", "alpha", "X"), "X beta X"));
    result = replace(replace("a b a", "a", "hello"), "B", "world");
    CHECK(!strcmp(result, "hello world hello")); /* static return used as input */
    CHECK(!strcmp(replace("source", "", "unused"), "source"));
    CHECK(!strcmp(replace("delete me", "me", NULL), "delete "));
    CHECK(!strcmp(replace("", "x", "y"), "")); /* no stale previous return */
    CHECK(!strcmp(shortcut_sanitize("previous name"), "previous name"));
    CHECK(!strcmp(shortcut_sanitize(""), ""));
    for (i = 0; i < (int)sizeof(input) - 2; i += 2) { input[i] = 'x'; input[i + 1] = '!'; }
    input[i] = '\0';
    memset(replacement, 'r', sizeof(replacement) - 1); replacement[sizeof(replacement) - 1] = '\0';
    result = replace(input, "x", replacement);
    CHECK(strlen(result) == MAX_STRING_CHARS - 1);
    CHECK(!strncmp(result, replacement, strlen(replacement)));
}

/* The fake engine captures the exact console command. Player-controlled
 * substitutions must not create another command, comment, or recursive
 * placeholder; trusted configuration still controls the commands themselves. */
static void test_custom_command_dispatch(void) {
    g_shrubbot_command_t **commands = sym("g_shrubbot_commands");
    vmCvar_t *shortcuts = sym("g_shortcuts");
    g_shrubbot_command_t custom;
    char hugeToken[MAX_NAME_LENGTH], *p;
    int i;
    setup_clients(); shortcuts->integer = 0;
    memset(&custom, 0, sizeof(custom));
    snprintf(custom.command, sizeof(custom.command), "panzerwar");
    snprintf(custom.exec, sizeof(custom.exec), "echo [1]|[2]|[9]");
    custom.levels[0] = 3; custom.levels[1] = -1;
    commands[0] = &custom; commands[1] = NULL;

    /* Custom entries override the native command and use the same whitespace
     * rules for quoted chat, split arguments, and absent substitutions. */
    setargs(2, "say", "    !panzerwar   first   second   ");
    CHECK(check_command(&ents[0]) && queued_count == 1);
    CHECK(!strcmp(queued_console, "echo first|second|\n") && war_mask == 0);
    reset_messages(); setargs(4, "say", "!panzerwar", "first", "second");
    CHECK(check_command(&ents[0]) && !strcmp(queued_console, "echo first|second|\n"));
    reset_messages(); setargs(2, "say", "!panzerwar only     ");
    CHECK(check_command(&ents[0]) && !strcmp(queued_console, "echo only||\n"));
    reset_messages(); setargs(2, "say", "!panzerwar [2] other");
    CHECK(check_command(&ents[0]) && !strcmp(queued_console, "echo [2]|other|\n"));
    reset_messages(); setargs(3, "panzerwar", "from", "console");
    CHECK(check_command(NULL) && !strcmp(queued_console, "echo from|console|\n"));

    /* Team/buddy chat still requires NQ flag 9; a custom level denial never
     * falls through to the same-named native command. */
    reset_messages(); setargs(2, "say_team", "!panzerwar team");
    CHECK(!check_command(&ents[0]) && queued_count == 0);
    snprintf(level3.flags, sizeof(level3.flags), "h9q");
    CHECK(check_command(&ents[0]) && !strcmp(queued_console, "echo team||\n"));
    reset_messages(); setargs(2, "say_buddy", "!panzerwar buddy");
    CHECK(check_command(&ents[0]) && !strcmp(queued_console, "echo buddy||\n"));
    reset_messages(); setargs(2, "say", "!panzerwar on");
    CHECK(!check_command(&ents[1]) && !queued_count && war_mask == 0 && message_contains(1, "permission denied"));

    snprintf(custom.exec, sizeof(custom.exec), "echo [1]; echo configured");
    reset_messages(); setargs(2, "say", "!panzerwar safe");
    CHECK(check_command(&ents[0]) && !strcmp(queued_console, "echo safe; echo configured\n"));
    { const char *bad[] = { "x;quit", "x\";quit", "x\nquit", "x\rquit", "x\\y", "x//y", "x/*y", "x*/y" };
      for (i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); ++i) {
          reset_messages(); setargs(3, "say", "!panzerwar", bad[i]);
          CHECK(!check_command(&ents[0]) && !queued_count && message_contains(0, "unsafe or oversized"));
      }
    }
    snprintf(custom.exec, sizeof(custom.exec), "echo /[1]");
    reset_messages(); setargs(3, "say", "!panzerwar", "/boundary");
    CHECK(!check_command(&ents[0]) && !queued_count); /* // straddles template and argument */
    snprintf(custom.exec, sizeof(custom.exec), "echo [1]/");
    reset_messages(); setargs(3, "say", "!panzerwar", "*");
    CHECK(!check_command(&ents[0]) && !queued_count); /* comment terminator spans the boundary */
    snprintf(custom.exec, sizeof(custom.exec), "echo /[1]*");
    reset_messages(); setargs(2, "say", "!panzerwar");
    CHECK(!check_command(&ents[0]) && !queued_count); /* empty argument joins a comment opener */
    snprintf(custom.exec, sizeof(custom.exec), "echo [1]");
    reset_messages(); setargs(3, "say", "!panzerwar", "maps/oasis");
    CHECK(check_command(&ents[0]) && !strcmp(queued_console, "echo maps/oasis\n"));

    /* ET: Legacy decodes UTF-8 inside an unquoted token. Overlong encodings
     * of ASCII whitespace, quotes, or delimiters must not masquerade as safe
     * bytes; valid shortest-form UTF-8 remains usable. */
    { const char *bad_utf8[] = { "x\xe0\x80\xa0y", "x\xe0\x80\xa2y", "x\xe0\x80\xbb y",
                                 "x\xc0\xa0y", "x\xed\xa0\x80y", "x\xf4\x90\x80\x80y",
                                 "x\xe2\x82", "x\x80y" };
      for (i = 0; i < (int)(sizeof(bad_utf8) / sizeof(bad_utf8[0])); ++i) {
          reset_messages(); setargs(3, "say", "!panzerwar", bad_utf8[i]);
          CHECK(!check_command(&ents[0]) && !queued_count);
      }
    }
    reset_messages(); setargs(3, "say", "!panzerwar", "caf\xc3\xa9\xf0\x9f\x99\x82");
    CHECK(check_command(&ents[0]) && !strcmp(queued_console, "echo caf\xc3\xa9\xf0\x9f\x99\x82\n"));

    /* Reject ambiguous trusted templates rather than treating comments as
     * quotation contexts. Configured separators and quoted URLs still work. */
    { const char *bad_templates[] = { "echo \"[1]", "echo [1]\nquit", "echo /* comment */ [1]",
                                     "echo // \" [1] \"", "echo [1] */" };
      for (i = 0; i < (int)(sizeof(bad_templates) / sizeof(bad_templates[0])); ++i) {
          snprintf(custom.exec, sizeof(custom.exec), "%s", bad_templates[i]);
          reset_messages(); setargs(3, "say", "!panzerwar", "safe");
          CHECK(!check_command(&ents[0]) && !queued_count);
      }
    }
    snprintf(custom.exec, sizeof(custom.exec), "echo \"http://configured\" [1]");
    reset_messages(); setargs(3, "say", "!panzerwar", "safe");
    CHECK(check_command(&ents[0]) && !strcmp(queued_console, "echo \"http://configured\" safe\n"));

    /* Expanded shortcuts are also untrusted. Spaces are usable only inside a
     * quoted template; operator-supplied command separators remain allowed. */
    shortcuts->integer = 1;
    clients[0].pers.lastammo_client = clients[0].pers.lastkiller_client = -1;
    clients[0].pers.lasthealth_client = clients[0].pers.lastkilled_client = -1;
    clients[0].pers.lastrevive_client = clients[0].pers.lastteambleed_client = -1;
    lev->lastconnected_client = lev->lastbye_client = lev->lasthi_client = -1;
    lev->last_axisCallingMedic = lev->last_alliesCallingMedic = -1;
    lev->last_axisCallingAmmo = lev->last_alliesCallingAmmo = -1;
    snprintf(clients[0].pers.netname, sizeof(clients[0].pers.netname), "Player Two");
    snprintf(custom.exec, sizeof(custom.exec), "echo \"[n]\"");
    reset_messages(); setargs(2, "say", "!panzerwar");
    CHECK(check_command(&ents[0]) && !strcmp(queued_console, "echo \"Player Two\"\n"));
    snprintf(custom.exec, sizeof(custom.exec), "echo [n]");
    reset_messages(); CHECK(!check_command(&ents[0]) && !queued_count);
    snprintf(clients[0].pers.netname, sizeof(clients[0].pers.netname), "Mallory;quit");
    snprintf(custom.exec, sizeof(custom.exec), "echo \"[n]\"");
    reset_messages(); CHECK(!check_command(&ents[0]) && !queued_count);

    shortcuts->integer = 0;
    memset(hugeToken, 'x', sizeof(hugeToken) - 1); hugeToken[sizeof(hugeToken) - 1] = '\0';
    p = custom.exec;
    for (i = 0; i < 40; ++i) { memcpy(p, "[1]", 3); p += 3; } *p = '\0';
    reset_messages(); setargs(3, "say", "!panzerwar", hugeToken);
    CHECK(!check_command(&ents[0]) && !queued_count && message_contains(0, "unsafe or oversized"));
    memset(custom.exec, 'x', sizeof(custom.exec) - 2);
    custom.exec[sizeof(custom.exec) - 2] = '\0';
    reset_messages(); setargs(2, "say", "!panzerwar");
    CHECK(check_command(&ents[0]) && queued_count == 1 && strlen(queued_console) == MAX_STRING_CHARS - 1);
    CHECK(queued_console[MAX_STRING_CHARS - 2] == '\n');
    custom.exec[sizeof(custom.exec) - 2] = 'x';
    custom.exec[sizeof(custom.exec) - 1] = '\0';
    reset_messages(); CHECK(!check_command(&ents[0]) && !queued_count); /* no room for newline */
    commands[0] = NULL;
}

static void test_campaign_fallback_separator(void) {
    void (*parse_campaigns)(void) = sym("G_ParseCampaigns");
    vmCvar_t *gametype = sym("g_gametype"), *campaign_file = sym("g_campaignFile");
    int previous_type = gametype->integer;
    char previous_file[sizeof(campaign_file->string)];
    snprintf(previous_file, sizeof(previous_file), "%s", campaign_file->string);
    setup_clients(); gametype->integer = GT_WOLF_CAMPAIGN; campaign_file->string[0] = '\0';
    snprintf(lev->rawmapname, sizeof(lev->rawmapname), "testmap");
    setargs(2, "map", "testmap"); parse_campaigns();
    CHECK(!strcmp(campaign_gametype, "2") && queued_count == 1);
    CHECK(!strcmp(queued_console, "map testmap\n"));
    reset_messages(); setargs(2, "devmap", "testmap"); parse_campaigns();
    CHECK(queued_count == 1 && !strcmp(queued_console, "devmap testmap\n"));
    gametype->integer = previous_type;
    snprintf(campaign_file->string, sizeof(campaign_file->string), "%s", previous_file);
}

static void test_bits(void) {
    struct { int before, words[2], after; } b = { 0x12345678, {0,0}, 0x12345678 };
    int invalid[] = { -1, 64, 102, INT_MIN, INT_MAX }, i;
    const int positions[] = { 0, 31, 32, 63 };
    for (i = 0; i < 4; ++i) { bit_set(b.words, positions[i]); CHECK(bit_check(b.words, positions[i])); }
    CHECK((unsigned)b.words[0] == 0x80000001u && (unsigned)b.words[1] == 0x80000001u);
    for (i = 0; i < 5; ++i) {
        CHECK(!bit_check(b.words, invalid[i]));
        bit_set(b.words, invalid[i]); bit_clear(b.words, invalid[i]);
    }
    CHECK(b.before == 0x12345678 && b.after == 0x12345678);
    CHECK((unsigned)b.words[0] == 0x80000001u && (unsigned)b.words[1] == 0x80000001u);
    for (i = 0; i < 4; ++i) { bit_clear(b.words, positions[i]); CHECK(!bit_check(b.words, positions[i])); }
    CHECK(!b.words[0] && !b.words[1]);
}

static void test_matches(void) {
    struct { int ids[MAX_CLIENT_MATCHES]; int guard; } result;
    int i;
    setup_clients();
    memset(&result, 0, sizeof(result)); result.guard = 0x12345678;
    CHECK(find_clients("", result.ids) == 0 && result.ids[0] == -1);
    CHECK(find_clients("999999999999999999999999999999", result.ids) == 0);
    CHECK(find_clients("0", result.ids) == 1 && result.ids[0] == 0 && result.ids[1] == -1);
    for (i = 0; i < MAX_CLIENTS; ++i) {
        clients[i].pers.connected = CON_CONNECTED;
        snprintf(clients[i].pers.netname, sizeof(clients[i].pers.netname), "shared%d", i);
    }
    CHECK(find_clients("shared", result.ids) == MAX_CLIENTS);
    CHECK(result.ids[MAX_CLIENTS - 1] == MAX_CLIENTS - 1);
    CHECK(result.ids[MAX_CLIENTS] == -1 && result.guard == 0x12345678);
}

static void test_wars(void) {
    setup_clients();
    setargs(2, "say", "!panzerwar on");
    CHECK(check_command(&ents[0]));
    CHECK(war_mask == (WARMODE_ENABLE | WARMODE_PANZER));
    setargs(2, "say", "!sniperwar on");
    CHECK(check_command(&ents[0]));
    CHECK(war_mask == (WARMODE_ENABLE | WARMODE_PANZER | WARMODE_SNIPER));
    setargs(2, "say", "!panzerwar off");
    CHECK(check_command(&ents[0]));
    CHECK(war_mask == (WARMODE_ENABLE | WARMODE_SNIPER));
    setargs(2, "say", "!sniperwar off");
    CHECK(check_command(&ents[0]));
    CHECK(war_mask == 0);
    war_mask = WARMODE_RESERVED | WARMODE_KNIFE;
    setargs(2, "say", "!riflewar on");
    CHECK(check_command(&ents[0]));
    CHECK(war_mask == (WARMODE_RESERVED | WARMODE_KNIFE | WARMODE_RIFLE | WARMODE_ENABLE));
    setargs(2, "say", "!riflewar off");
    CHECK(check_command(&ents[0]));
    CHECK(war_mask == (WARMODE_RESERVED | WARMODE_KNIFE | WARMODE_ENABLE));
    setargs(2, "say", "!panzerwar bogus");
    CHECK(check_command(&ents[0]) && !(war_mask & WARMODE_PANZER));
    setargs(2, "say", "!panzerwar on");
    CHECK(!check_command(&ents[1]) && !(war_mask & WARMODE_PANZER));
    CHECK(message_contains(1, "permission denied"));
}

static void test_mass_and_spec(void) {
    setup_clients();
    clients[0].sess.sessionTeam = TEAM_SPECTATOR;
    setargs(2, "say", "!launcha");
    CHECK(!check_command(&ents[0]));
    CHECK(clients[1].ps.velocity[2] == 0);
    CHECK(message_contains(0, "g_tyranny is not enabled"));
    tyranny->integer = 1;
    reset_messages();
    CHECK(check_command(&ents[0]));
    CHECK(clients[1].ps.velocity[2] > 1000);
    CHECK(clients[2].ps.velocity[2] == 0); /* higher admin */
    CHECK(clients[3].ps.velocity[2] == 0); /* immutable */
    CHECK(message_contains(-1, "1 players"));
    reset_messages();
    setargs(2, "say", "!giba");
    CHECK(check_command(&ents[0])); /* fake clients have takedamage=false */
    CHECK(message_contains(-1, "0 players"));
    setargs(2, "say", "!putteam");
    CHECK(check_command(&ents[0])); /* alias recognized; missing args */
    CHECK(message_contains(0, "usage"));
    tyranny->integer = 0;
    setargs(2, "say", "!lol");
    CHECK(!check_command(&ents[0]) && message_contains(0, "g_tyranny"));

    reset_messages();
    setargs(2, "say", "!spec 1");
    teams[TEAM_AXIS].spec_lock = qtrue;
    CHECK(check_command(&ents[0]));
    CHECK(clients[0].sess.spectatorState != SPECTATOR_FOLLOW);
    CHECK(message_contains(0, "locked"));
    clients[0].sess.spec_invite = TEAM_AXIS;
    reset_messages();
    CHECK(check_command(&ents[0]));
    CHECK(clients[0].sess.spectatorState == SPECTATOR_FOLLOW);
    CHECK(clients[0].sess.spectatorClient == 1);
    CHECK(message_contains(0, "player #1"));
    clients[0].sess.spectatorState = SPECTATOR_FREE;
    setargs(2, "say", "!spec 2");
    CHECK(check_command(&ents[0]));
    CHECK(clients[0].sess.spectatorState == SPECTATOR_FREE); /* higher admin */
    setargs(2, "say", "!spec 1");
    clients[1].ps.pm_flags |= PMF_LIMBO;
    CHECK(check_command(&ents[0]));
    CHECK(clients[0].sess.spectatorState == SPECTATOR_FREE);
    clients[1].ps.pm_flags &= ~PMF_LIMBO;
    clients[1].pers.connected = CON_CONNECTING;
    CHECK(check_command(&ents[0]));
    CHECK(clients[0].sess.spectatorState == SPECTATOR_FREE);
}

/* Successful reinforcement reaches ClientSpawn, which needs a real engine;
 * the optional ET: Legacy bot fixture exercises it. These fake-engine checks
 * ensure rejected requests never reach that path. */
static void test_spawn_rejections(void) {
    vmCvar_t *gametype = sym("g_gametype");
    int oldtype = gametype->integer;
    setup_clients(); gametype->integer = GT_WOLF;
    setargs(2, "say", "!spawn 1");
    CHECK(!check_command(&ents[0]) && message_contains(0, "g_tyranny"));
    tyranny->integer = 1;
    reset_messages(); CHECK(!check_command(&ents[0]) && message_contains(0, "permission denied"));
    snprintf(level3.flags, sizeof(level3.flags), "h%%");
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "no eligible")); /* alive */

    clients[1].ps.pm_flags |= PMF_LIMBO;
    clients[1].sess.sessionTeam = TEAM_SPECTATOR;
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "no eligible"));
    clients[1].sess.sessionTeam = TEAM_AXIS;
    clients[1].pers.connected = CON_CONNECTING;
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "no eligible"));
    clients[1].pers.connected = CON_CONNECTED;
    clients[1].ps.pm_flags &= ~PMF_LIMBO;
    clients[2].ps.pm_flags |= PMF_LIMBO; /* higher admin */
    clients[3].ps.pm_flags |= PMF_LIMBO; /* immutable */
    setargs(2, "say", "!spawn 2");
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "no eligible"));
    setargs(2, "say", "!spawn 3");
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "no eligible"));
    clients[0].sess.sessionTeam = TEAM_SPECTATOR;
    setargs(2, "say", "!spawn all");
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "no eligible"));
    setargs(2, "say", "!spawn -1");
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "no eligible"));
    setargs(2, "say", "!spawn");
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "no eligible"));
    setargs(2, "say", "!spawn Player");
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "ambiguous"));
    setargs(2, "say", "!spawn 1 extra");
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "usage"));
    setargs(2, "say", "!spawn 1");
    clients[1].ps.pm_flags |= PMF_LIMBO; /* would enter ClientSpawn without the phase gates */
    lev->intermissionQueued = 1;
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "intermission"));
    lev->intermissionQueued = 0; lev->intermissiontime = 1;
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "intermission"));
    lev->intermissiontime = 0; game_state->integer = GS_INTERMISSION;
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "intermission"));
    game_state->integer = GS_PLAYING; gametype->integer = GT_WOLF_LMS;
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "LMS"));
    gametype->integer = GT_WOLF;
    lev->match_pause = 100;
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "paused"));
    lev->match_pause = PAUSE_UNPAUSING;
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "unpausing"));
    lev->match_pause = PAUSE_NONE; lev->restarted = qtrue;
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "restart"));
    lev->restarted = qfalse; game_state->integer = GS_INITIALIZE;
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "transition"));
    game_state->integer = GS_RESET;
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "transition"));
    game_state->integer = 100; /* unknown states fail closed */
    reset_messages(); CHECK(check_command(&ents[0]) && message_contains(0, "transition"));
    CHECK(clients[1].ps.pm_flags & PMF_LIMBO);
    gametype->integer = oldtype;
}

static void test_console_print_safety(void) {
    void (*print_chat)(gentity_t *, char *) = sym("G_shrubbot_print_chat");
    void (*print)(gentity_t *, char *) = sym("G_shrubbot_print");
    void (*buffer_begin)(void) = sym("G_shrubbot_buffer_begin");
    void (*buffer_print)(gentity_t *, char *) = sym("G_shrubbot_buffer_print");
    void (*buffer_end)(gentity_t *) = sym("G_shrubbot_buffer_end");
    char long_message[4 * MAX_STRING_CHARS];
    setup_clients();
    print_chat(NULL, "^1unsafe %s %n %p");
    CHECK(!strcmp(console_output, "unsafe %s %n %p\n"));
    reset_messages(); print(NULL, "^2literal %s %n");
    CHECK(!strcmp(console_output, "literal %s %n"));
    memset(long_message, 'x', sizeof(long_message) - 1);
    long_message[sizeof(long_message) - 1] = '\0';
    reset_messages(); print_chat(NULL, long_message);
    CHECK(strlen(console_output) == MAX_STRING_CHARS - 1 &&
          console_output[MAX_STRING_CHARS - 2] == '\n');
    reset_messages(); buffer_begin(); buffer_print(NULL, long_message); buffer_end(NULL);
    CHECK(strlen(console_output) == MAX_STRING_CHARS - 1);
}

static void test_safe_formatting_and_client_input(void) {
    char *(*format)(const char *, ...) = sym("va");
    int (*find_one)(gentity_t *, char *) = sym("ClientNumberFromString");
    void (*log_print)(const char *, ...) = sym("G_LogPrintf");
    char *(*connect)(int, qboolean, qboolean) = sym("ClientConnect");
    char *(*check_local)(gentity_t *, char *) = sym("CheckLocalhost");
    char huge[4096], va_text[32001], *reason;
    vmCvar_t *dedicated = sym("g_dedicated"), *security = sym("nq_security");
    setup_clients();

    memset(va_text, 'v', sizeof(va_text) - 1); va_text[sizeof(va_text) - 1] = '\0';
    va_text[31999] = '\0';
    CHECK(strlen(format("%s", va_text)) == 31999);
    va_text[31999] = 'v';
    expect_error = 1;
    if (!setjmp(error_jump)) {
        format("%s", va_text);
        CHECK(!"oversized va must raise a game error");
    }
    expect_error = 0;
    CHECK(errors == 1);

    reset_messages();
    CHECK(find_one(NULL, "name%p%n") == -1);
    CHECK(strstr(console_output, "name%p%n"));
    reset_messages();
    setargs(4, "bindsearch_r", "%s%n", "literal", "tail");
    client_command(1);
    CHECK(strstr(console_output, "%s%n literal tail"));
    memset(huge, 'z', sizeof(huge) - 1); huge[sizeof(huge) - 1] = '\0';
    reset_messages();
    setargs(5, "bindsearch_r", huge, huge, huge, huge);
    client_command(1);
    CHECK(strlen(console_output) == MAX_STRING_CHARS - 1);

    /* Chat logs must end in a newline and a NUL even at the buffer limit. */
    reset_messages(); lev->chatLog = 1;
    log_print("say: %s", "normal");
    CHECK(strstr(stored, "say: normal\n") && stored_len == strlen(stored));
    reset_messages(); log_print("say: %s", huge);
    CHECK(stored_len == MAX_STRING_CHARS - 1 && stored[stored_len - 1] == '\n');
    lev->chatLog = 0;

    /* Even invalid connect userinfo may carry a password: it is never printed. */
    setup_clients();
    snprintf(fake_userinfo, sizeof(fake_userinfo), "bad-userinfo password=TopSecretValue");
    reason = connect(1, qtrue, qfalse);
    CHECK(reason && !strcmp(reason, "Bad userinfo."));
    CHECK(strstr(console_output, "ClientConnect: 1") && !strstr(console_output, "TopSecretValue"));

    /* A localhost spoof can be checked before client data exists. */
    setup_clients(); security->integer = NQS_ENABLE_LOCALHOST_HACK_CHECK;
    dedicated->integer = 1; ents[1].client = NULL;
    reason = check_local(&ents[1], "\\ip\\localhost\\name\\Spoof");
    CHECK(reason && dropped == 1 && last_drop == 1);
    reset_messages(); dropped = 0; ents[1].client = &clients[1];
    snprintf(clients[1].pers.netname, sizeof(clients[1].pers.netname), "X%%n%%p");
    reason = check_local(&ents[1], "\\ip\\localhost\\name\\Spoof");
    CHECK(reason && dropped == 1 && strstr(console_output, "X%n%p"));
}

static void test_session_data(void) {
    void (*read_session)(gclient_t *) = sym("G_ReadSessionData");
    int i;
    setup_clients();
    for (i = 0; i < 21; ++i) strcat(fake_session, "0 ");
    strcat(fake_session, "123 ");
    for (i = 0; i < 400; ++i) strcat(fake_session, "A");
    read_session(&clients[0]);
    CHECK(clients[0].sess.uci == 123);
#ifdef AUTO_GUID
    CHECK(strlen(clients[0].sess.nqKeyInfo.nquid) == PB_GUID_LENGTH);
#endif
}

static void test_team_packets(void) {
    void (*send_team)(team_t) = sym("TeamplayInfoMessage");
    int i, count, x;
    short health;
    setup_clients();
    lev->numConnectedClients = MAX_CLIENTS;
    for (i = 0; i < MAX_CLIENTS; ++i) {
        lev->sortedClients[i] = i;
        ents[i].inuse = qtrue;
        clients[i].pers.connected = CON_CONNECTED;
        clients[i].sess.sessionTeam = TEAM_AXIS;
        clients[i].pers.teamState.location[0] = 1000 + i;
        clients[i].ps.stats[STAT_HEALTH] = 100 - i;
        ents[i].s.powerups = i * 17;
    }
    clients[MAX_CLIENTS - 1].ps.pm_flags |= PMF_LIMBO;
    send_team(TEAM_AXIS);
    CHECK(binary_sends == MAX_CLIENTS && binary_length == (int)sizeof(binary_packet));
    CHECK(binary_packet[0] == PACKET_S_TEAMPLAYINFOMESSAGE);
    memcpy(&count, binary_packet + 1, sizeof(count)); CHECK(count == MAX_CLIENTS);
    i = PACKET_OFFSET + (MAX_CLIENTS - 1) * 19;
    CHECK(binary_packet[i] == MAX_CLIENTS - 1);
    memcpy(&x, binary_packet + i + 1, sizeof(x)); CHECK(x == 1000 + MAX_CLIENTS - 1);
    memcpy(&health, binary_packet + i + 13, sizeof(health)); CHECK(health == -1);
    send_team(TEAM_AXIS); CHECK(binary_sends == MAX_CLIENTS); /* unchanged cache */
    clients[0].pers.teamState.location[0]++;
    send_team(TEAM_AXIS); CHECK(binary_sends == 2 * MAX_CLIENTS);
}

static void test_game_packet_envelope(void) {
    intptr_t (*vm)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t) = sym("vmMain");
    unsigned char packet = PACKET_C_SEND_NQKEY;
    setup_clients();
    CHECK(vm(GAME_MESSAGERECEIVED, -1, (intptr_t)&packet, 1,0,0,0,0) == 0);
    CHECK(vm(GAME_MESSAGERECEIVED, MAX_CLIENTS, (intptr_t)&packet, 1,0,0,0,0) == 0);
    CHECK(vm(GAME_MESSAGERECEIVED, 1, 0, 0,0,0,0,0) == 0);
    CHECK(vm(GAME_MESSAGERECEIVED, 1, (intptr_t)&packet, 0,0,0,0,0) == 0);
    CHECK(vm(GAME_MESSAGERECEIVED, 1, (intptr_t)&packet, MAX_BINARY_MESSAGE + 1,0,0,0,0) == 0);
    CHECK(dropped == 0);
#ifdef AUTO_GUID
    {
        unsigned char key_packet[PACKET_OFFSET + PB_GUID_LENGTH] = {0};
        const char *key = "0123456789ABCDEF0123456789ABCDEF";
        key_packet[0] = PACKET_C_SEND_NQKEY;
        memcpy(key_packet + PACKET_OFFSET, key, PB_GUID_LENGTH);
        CHECK(vm(GAME_MESSAGERECEIVED, 1, (intptr_t)key_packet, sizeof(key_packet),0,0,0,0) == 0);
        CHECK(!strcmp(clients[1].sess.nqKeyInfo.nquid, key) && dropped == 0);
        CHECK(vm(GAME_MESSAGERECEIVED, 1, (intptr_t)key_packet, sizeof(key_packet) - 1,0,0,0,0) == 0);
        CHECK(dropped == 1 && last_drop == 1);
        key_packet[PACKET_OFFSET] = '/';
        CHECK(vm(GAME_MESSAGERECEIVED, 1, (intptr_t)key_packet, sizeof(key_packet),0,0,0,0) == 0);
        CHECK(dropped == 2 && !strcmp(clients[1].sess.nqKeyInfo.nquid, key));
    }
#endif
}

static void test_xpsave_paths(void) {
    qboolean (*save)(gentity_t *) = sym("G_xpsave_add");
    qboolean (*load)(gentity_t *) = sym("G_xpsave_load");
    vmCvar_t *xp = sym("g_XPSave"), *dir = sym("g_XPSaveDirectory");
    vmCvar_t *maxage = sym("g_XPSaveMaxAge"), *gametype = sym("g_gametype");
    const char *bad[] = {"../escape", "..", "foo/bar", "foo\\bar", "x.y"};
    const char *good = "ABCDEF0123456789ABCDEF0123456789";
    char root[] = "/tmp/newmod-xp-XXXXXX", path[MAX_OSPATH], longdir[MAX_OSPATH];
    size_t i, n;
    setup_clients(); CHECK(mkdtemp(root));
    snprintf(path, sizeof(path), "%s/xp", root); CHECK(mkdir(path, 0700) == 0);
    snprintf(dir->string, sizeof(dir->string), "%s", path);
    xp->integer = XPSF_ENABLE; maxage->integer = 3600; gametype->integer = GT_WOLF;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        snprintf(clients[1].pers.cl_guid, sizeof(clients[1].pers.cl_guid), "%s", bad[i]);
        CHECK(!save(&ents[1]) && !load(&ents[1]));
    }
    snprintf(path, sizeof(path), "%s/escape.xp", root); CHECK(access(path, F_OK) != 0);
    snprintf(clients[1].pers.cl_guid, sizeof(clients[1].pers.cl_guid), "%s", good);
    clients[1].sess.skillpoints[0] = 12.5f;
    CHECK(save(&ents[1]));
    snprintf(path, sizeof(path), "%s/xp/%s.xp", root, good); CHECK(access(path, F_OK) == 0);
    clients[1].sess.skillpoints[0] = 0;
    CHECK(load(&ents[1]) && fabsf(clients[1].sess.skillpoints[0] - 12.5f) < .001f);
    CHECK(unlink(path) == 0);
#ifdef AUTO_GUID
    snprintf(path, sizeof(path), "%s/xp_nq", root); CHECK(mkdir(path, 0700) == 0);
    snprintf(clients[1].pers.cl_guid, sizeof(clients[1].pers.cl_guid), "NO_GUID");
    snprintf(clients[1].sess.nqKeyInfo.nquid, sizeof(clients[1].sess.nqKeyInfo.nquid), "../escape");
    CHECK(!save(&ents[1]) && !load(&ents[1]));
    snprintf(clients[1].sess.nqKeyInfo.nquid, sizeof(clients[1].sess.nqKeyInfo.nquid), "%s", good);
    clients[1].sess.skillpoints[0] = 20.5f;
    CHECK(save(&ents[1]));
    snprintf(path, sizeof(path), "%s/xp_nq/%s.xp", root, good); CHECK(access(path, F_OK) == 0);
    clients[1].sess.skillpoints[0] = 0; clients[1].sess.startxptotal = 0;
    CHECK(load(&ents[1]) && fabsf(clients[1].sess.skillpoints[0] - 20.5f) < .001f);
    CHECK(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/xp_nq", root); CHECK(rmdir(path) == 0);
    snprintf(clients[1].pers.cl_guid, sizeof(clients[1].pers.cl_guid), "%s", good);
#endif
    snprintf(path, sizeof(path), "%s/xp", root); CHECK(rmdir(path) == 0);

    /* An existing, very long directory plus GUID cannot be truncated. */
    snprintf(longdir, sizeof(longdir), "%s/", root); n = strlen(longdir);
    memset(longdir + n, 'd', 210); longdir[n + 210] = '\0';
    CHECK(mkdir(longdir, 0700) == 0);
    snprintf(dir->string, sizeof(dir->string), "%s", longdir);
    CHECK(!save(&ents[1]));
    CHECK(rmdir(longdir) == 0 && rmdir(root) == 0);
}

static void test_shutdown_gravity(void) {
    void (*frame)(void) = sym("G_nm_gravity_frame");
    void (*shutdown)(int) = sym("G_ShutdownGame");
    setup_clients(); lev->time = 1000;
    snprintf(gravity_string, sizeof(gravity_string), "925.50"); gravity_value = 925;
    setargs(2, "say", "!crazygravity on"); CHECK(check_command(&ents[0]));
    lev->time = 31000; frame(); CHECK(gravity_sets == 1);
    /* The fixture's admin pointers are static, not allocations to free. */
    lev->numConnectedClients = 0; levels[0] = NULL; admins[0] = NULL;
    shutdown(0);
    CHECK(!lev->nmCrazyGravity && gravity_sets == 2 && !strcmp(gravity_string, "925.50"));
}

static void test_crazy_gravity(void) {
    void (*frame)(void) = sym("G_nm_gravity_frame");
    void (*restore)(void) = sym("G_nm_gravity_restore");
    setup_clients();
    lev->time = 1000; gravity_value = 925;
    snprintf(gravity_string, sizeof(gravity_string), "925.50");
    setargs(2, "say", "!crazygravity on");
    CHECK(!check_command(&ents[1])); /* missing q permission */
    CHECK(!lev->nmCrazyGravity && gravity_value == 925);
    CHECK(check_command(&ents[0]));
    CHECK(lev->nmCrazyGravity && !strcmp(lev->nmSavedGravity, "925.50"));
    CHECK(lev->nmGravityStart == 1000 && gravity_sets == 0);
    reset_messages();
    lev->time = 25999; frame(); CHECK(sent_count == 0);
    lev->time = 26001; frame(); CHECK(message_contains(-1, "5 seconds"));
    reset_messages(); frame(); CHECK(sent_count == 0); /* warning only once */
    lev->time = 31001; frame();
    CHECK(gravity_value >= 0 && gravity_value < 1200 && gravity_sets == 1);
    CHECK(message_contains(-1, "gravity changed"));
    CHECK(lev->nmGravityStart == 31001 && !lev->nmGravityWarned);
    /* Repeated enable must not overwrite the original value. */
    setargs(2, "say", "!crazygravity on"); CHECK(check_command(&ents[0]));
    CHECK(!strcmp(lev->nmSavedGravity, "925.50"));
    setargs(2, "say", "!crazygravity off"); CHECK(check_command(&ents[0]));
    CHECK(!lev->nmCrazyGravity && gravity_value == 925 && gravity_sets == 2);
    CHECK(!strcmp(gravity_string, "925.50"));
    restore(); CHECK(gravity_sets == 2);
    setargs(2, "say", "!crazygravity on"); CHECK(check_command(&ents[0]));
    lev->time = 10; frame(); /* map-clock rollback resets the schedule */
    CHECK(lev->nmGravityStart == 10 && gravity_sets == 2);
    restore(); CHECK(gravity_value == 925 && gravity_sets == 3);
}

static void test_shake(void) {
    void (*frame)(gentity_t *) = sym("G_nm_shake_frame");
    int i;
    setup_clients();
    lev->time = 1000; clients[0].sess.sessionTeam = TEAM_SPECTATOR;
    setargs(2, "say", "!shake 1 120");
    CHECK(!check_command(&ents[0]) && clients[1].pers.nmShakeUntil == 0);
    tyranny->integer = 1; reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(clients[1].pers.nmShakeUntil == 61000); /* capped at 60 seconds */
    CHECK(clients[2].pers.nmShakeUntil == 0 && clients[3].pers.nmShakeUntil == 0);
    frame(&ents[1]);
    CHECK(lev->num_entities == MAX_CLIENTS + 1 && links == 1);
    CHECK(ents[MAX_CLIENTS].s.eType == ET_EVENTS + EV_SHAKE);
    CHECK((ents[MAX_CLIENTS].r.svFlags & SVF_SINGLECLIENT) &&
          (ents[MAX_CLIENTS].r.svFlags & SVF_BROADCAST) &&
          ents[MAX_CLIENTS].r.singleClient == 1 && ents[MAX_CLIENTS].s.onFireStart == 1000000);
    frame(&ents[1]); CHECK(links == 1);
    lev->time = 1749; frame(&ents[1]); CHECK(links == 1);
    lev->time = 1750; frame(&ents[1]); CHECK(links == 2);
    /* Reserve must prevent G_Spawn from reaching its fatal exhaustion path. */
    lev->num_entities = ENTITYNUM_MAX_NORMAL;
    for (i = MAX_CLIENTS; i < ENTITYNUM_MAX_NORMAL - (MIN_SPARE_GENTITIES - 2); ++i)
        ents[i].inuse = qtrue;
    lev->time = 2500; frame(&ents[1]); CHECK(links == 2);
    ents[MAX_CLIENTS + 2].inuse = qfalse; /* one above the reserve threshold */
    frame(&ents[1]); CHECK(links == 3);
    lev->time = 61000; frame(&ents[1]); CHECK(links == 3);
    setargs(2, "say", "!shake 2 10"); CHECK(check_command(&ents[0]));
    CHECK(clients[2].pers.nmShakeUntil == 0); /* higher admin */
    setargs(2, "say", "!shake 3 10"); CHECK(check_command(&ents[0]));
    CHECK(clients[3].pers.nmShakeUntil == 0); /* immutable */
    setargs(2, "say", "!shake 1 99999999999999999999999999");
    CHECK(check_command(&ents[0]) && message_contains(0, "seconds must be a number"));
    CHECK(clients[1].pers.nmShakeUntil == 61000);
    setargs(2, "say", "!shake"); CHECK(check_command(&ents[0]));
    CHECK(clients[1].pers.nmShakeUntil == 62000 && clients[2].pers.nmShakeUntil == 0);
}

static int deaths;
static void fake_die(gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod) {
    (void)inflictor; (void)attacker; (void)damage; (void)mod;
    ++deaths; self->inuse = qfalse;
}

static void test_splat_and_smite(void) {
    void (*damage)(gentity_t *, gentity_t *, gentity_t *, float *, float *, int, int, int) = sym("G_Damage");
    setup_clients();
    lev->time = 1000; lev->framenum = 1;
    clients[0].sess.sessionTeam = TEAM_SPECTATOR;
    ents[1].takedamage = ents[2].takedamage = ents[3].takedamage = qtrue;
    ents[1].die = ents[2].die = ents[3].die = fake_die;
    clients[1].ps.powerups[PW_INVULNERABLE] = 10000;
    deaths = 0;
    setargs(2, "say", "!splat 1"); CHECK(!check_command(&ents[0]));
    CHECK(ents[1].health == 100 && deaths == 0); /* tyranny disabled */
    tyranny->integer = 1;
    /* Existing NQ DAMAGE_NO_PROTECTION callers and giba still respect
     * spawn protection; splat alone deliberately bypasses it. */
    damage(&ents[1], NULL, NULL, NULL, NULL, 10000, DAMAGE_NO_PROTECTION, MOD_UNKNOWN);
    CHECK(ents[1].health == 100 && deaths == 0);
    setargs(2, "say", "!giba"); reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(deaths == 0 && message_contains(-1, "0 players gibbed"));
    setargs(2, "say", "!splat 1");
    ents[1].flags |= FL_GODMODE; reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(deaths == 0 && ents[1].health == 100 && message_contains(0, "no eligible"));
    ents[1].flags &= ~FL_GODMODE; clients[1].noclip = qtrue;
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(deaths == 0 && ents[1].health == 100 && message_contains(0, "no eligible"));
    clients[1].noclip = qfalse; clients[1].ps.pm_flags |= PMF_LIMBO;
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(deaths == 0 && message_contains(0, "no eligible"));
    clients[1].ps.pm_flags &= ~PMF_LIMBO;
    game_state->integer = GS_WARMUP;
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(deaths == 0 && message_contains(0, "damage is disabled"));
    game_state->integer = GS_PLAYING;
    lev->intermissionQueued = 1; reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(deaths == 0 && message_contains(0, "damage is disabled"));
    lev->intermissionQueued = 0; reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(deaths == 1 && ents[1].health <= GIB_HEALTH);
    CHECK(ents[2].inuse && ents[3].inuse); /* hierarchy and immutable */
    setargs(2, "say", "!splata"); CHECK(check_command(&ents[0]));
    CHECK(deaths == 1);

    setup_clients(); lev->time = 2000; lev->framenum = 3; tyranny->integer = 1;
    clients[0].sess.sessionTeam = TEAM_SPECTATOR;
    ents[1].lastBurnedFrameNumber = 3; /* test effects without damage side effects */
    setargs(2, "say", "!smite"); CHECK(check_command(&ents[0]));
    CHECK(ents[1].flameQuota == 5 && ents[1].s.onFireEnd > lev->time);
    CHECK(clients[1].ps.onFireStart == lev->time && message_contains(1, "Burn baby"));
    CHECK(ents[2].flameQuota == 0 && ents[3].flameQuota == 0);
    lev->intermissiontime = 1;
    setargs(2, "say", "!smite 1"); CHECK(check_command(&ents[0]));
    CHECK(ents[1].flameQuota == 5);
}

static void test_firegod_toggle(void) {
    qboolean (*firegod)(gentity_t *, int) = sym("G_nm_firegod");
    setup_clients();
    lev->time = 5000; lev->framenum = 7; tyranny->integer = 1;
    clients[0].sess.sessionTeam = TEAM_SPECTATOR;
    ents[1].s.onFireEnd = lev->time - 1;
    ents[1].takedamage = qtrue; ents[1].health = 100;
    reset_messages();
    CHECK(firegod(&ents[0], 1));
    CHECK(ents[1].flags & FL_GODMODE && !ents[1].takedamage);
    CHECK(ents[1].s.onFireStart == lev->time && ents[1].s.onFireEnd == lev->time + 2000);
    CHECK(ents[1].flameBurnEnt == 1 && ents[1].lastBurnedFrameNumber == lev->framenum);
    CHECK(message_contains(1, "set you invincible flaming god"));
    setargs(2, "say", "!firegod 1");
    CHECK(firegod(&ents[0], 1));
    CHECK(!(ents[1].flags & FL_GODMODE) && ents[1].takedamage);
    CHECK(ents[1].s.onFireEnd == lev->time);
    CHECK(message_contains(1, "set you mortal player"));
    setargs(3, "say", "!firegod 1", "noclip");
    CHECK(firegod(&ents[0], 1));
    CHECK(clients[1].noclip);
    setargs(2, "say", "!firegod 1");
    CHECK(firegod(&ents[0], 1));
    CHECK(!clients[1].noclip);
    setargs(4, "say", "!firegod 1", "extra", "extra");
    CHECK(!firegod(&ents[0], 1));
    CHECK(message_contains(0, "usage"));
    setargs(2, "say", "!firegod nobody");
    CHECK(!firegod(&ents[0], 1));
    CHECK(message_contains(0, "ambiguous") || message_contains(0, "not found"));
    ents[1].health = 0; setargs(2, "say", "!firegod 1");
    CHECK(!firegod(&ents[0], 1));
    CHECK(message_contains(0, "no eligible"));
}

static void test_real_splat_death(void) {
    void (*real_die)(gentity_t *, gentity_t *, gentity_t *, int, int) = sym("player_die");
    setup_clients();
    lev->time = lev->timeCurrent = 3000; lev->framenum = 3;
    clients[0].sess.sessionTeam = TEAM_SPECTATOR;
    clients[1].ps.stats[STAT_HEALTH] = 100;
    ents[1].s.eType = ET_PLAYER; ents[1].takedamage = qtrue;
    ents[1].die = real_die; tyranny->integer = 1;
    setargs(2, "say", "!splat 1"); CHECK(check_command(&ents[0]));
    CHECK(ents[1].health <= GIB_HEALTH && !ents[1].takedamage);
    CHECK(ents[1].s.eType == ET_INVISIBLE && clients[1].ps.pm_type == PM_DEAD);
    CHECK(message_contains(-1, "1 player splattered"));
}

static void test_real_giba_death(void) {
    void (*real_die)(gentity_t *, gentity_t *, gentity_t *, int, int) = sym("player_die");
    setup_clients();
    lev->time = lev->timeCurrent = 3000; lev->framenum = 3;
    clients[0].sess.sessionTeam = TEAM_SPECTATOR;
    clients[1].ps.stats[STAT_HEALTH] = 100;
    ents[1].s.eType = ET_PLAYER; ents[1].takedamage = qtrue;
    ents[1].die = real_die; tyranny->integer = 1;
    setargs(2, "say", "!giba"); CHECK(check_command(&ents[0]));
    CHECK(ents[1].health <= GIB_HEALTH && !ents[1].takedamage);
    CHECK(ents[1].s.eType == ET_INVISIBLE && clients[1].ps.pm_type == PM_DEAD);
    CHECK(ents[2].health == 100 && ents[3].health == 100);
    CHECK(message_contains(-1, "1 players gibbed"));
}

/* The record comes from NQ's actual enemy-kill path, not a command-time
 * approximation based on connected players' current playerstates. */
static void test_map_spree_record(void) {
    void (*real_die)(gentity_t *, gentity_t *, gentity_t *, int, int) = sym("player_die");
    void (*disconnect)(int) = sym("ClientDisconnect");
    int (*set_spree)(playerState_t *, int) = sym("BG_SetKillSpree");
    setup_clients();
    lev->time = lev->timeCurrent = 3000; lev->framenum = 3;
    snprintf(level3.flags, sizeof(level3.flags), "hE");
    setargs(2, "say", "!spreerecord");
    CHECK(!check_command(&ents[1]) && message_contains(1, "permission denied"));
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no record this map") && !message_contains(-1, "record"));
    reset_messages(); setargs(2, "say", "!spreerecord extra"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "usage"));

    /* The actual death handler scores an enemy kill; retain the name as of
     * the new record even if the player disconnects or changes name. */
    clients[1].sess.sessionTeam = TEAM_ALLIES;
    snprintf(clients[0].pers.netname, sizeof(clients[0].pers.netname), "Winner\"\\\n%%n");
    clients[1].ps.stats[STAT_HEALTH] = 0;
    ents[1].s.eType = ET_PLAYER; ents[1].health = GIB_HEALTH - 1;
    real_die(&ents[1], &ents[0], &ents[0], 200, MOD_UNKNOWN);
    CHECK(lev->maxspree_count == 1 && clients[0].sess.killingSpree == 1);
    snprintf(clients[0].pers.netname, sizeof(clients[0].pers.netname), "Renamed");
    reset_messages(); setargs(2, "say", "!spreerecord"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "Winner   %n") && message_contains(0, "1 kill (not persistent)"));
    CHECK(!message_contains(0, "Winner\"") && !message_contains(-1, "Winner"));
    reset_messages(); setargs(1, "!spreerecord"); CHECK(check_command(NULL));
    CHECK(strstr(console_output, "Winner   %n") && strstr(console_output, "1 kill"));
    /* Disconnect through the real lifecycle, rather than merely changing
     * the connected bit, and verify the detached snapshot remains available. */
    ((vmCvar_t *)sym("g_maxclients"))->integer = MAX_CLIENTS;
    ((vmCvar_t *)sym("g_serverInfo"))->integer = 0;
    reset_messages(); disconnect(0);
    CHECK(!ents[0].inuse && clients[0].pers.connected == CON_DISCONNECTED);
    reset_messages(); setargs(1, "!spreerecord"); CHECK(check_command(NULL));
    CHECK(strstr(console_output, "Winner   %n") && strstr(console_output, "1 kill"));

    /* Equal streaks leave the first record in place; a later larger streak
     * replaces it. Warmup and limbo updates cannot create a record. */
    clients[2].sess.sessionTeam = TEAM_AXIS;
    clients[1].ps.pm_type = PM_NORMAL; ents[1].s.eType = ET_PLAYER;
    ents[1].health = GIB_HEALTH - 1;
    real_die(&ents[1], &ents[2], &ents[2], 200, MOD_UNKNOWN);
    CHECK(lev->maxspree_count == 1 && strstr(lev->maxspree_player, "Winner"));
    set_spree(&clients[2].ps, 1);
    clients[1].ps.pm_type = PM_NORMAL; ents[1].s.eType = ET_PLAYER;
    ents[1].health = GIB_HEALTH - 1;
    real_die(&ents[1], &ents[2], &ents[2], 200, MOD_UNKNOWN);
    CHECK(lev->maxspree_count == 2 && !strcmp(lev->maxspree_player, "Player2"));
    set_spree(&clients[2].ps, 10); game_state->integer = GS_WARMUP;
    clients[1].ps.pm_type = PM_NORMAL; ents[1].s.eType = ET_PLAYER;
    ents[1].health = GIB_HEALTH - 1;
    real_die(&ents[1], &ents[2], &ents[2], 200, MOD_UNKNOWN);
    CHECK(lev->maxspree_count == 2);
    game_state->integer = GS_PLAYING; clients[2].ps.pm_flags |= PMF_LIMBO;
    clients[1].ps.pm_type = PM_NORMAL; ents[1].s.eType = ET_PLAYER;
    ents[1].health = GIB_HEALTH - 1;
    real_die(&ents[1], &ents[2], &ents[2], 200, MOD_UNKNOWN);
    CHECK(lev->maxspree_count == 2);
    clients[2].ps.pm_flags &= ~PMF_LIMBO;
    /* A teammate kill never increases the killer's NQ spree. */
    clients[1].sess.sessionTeam = TEAM_AXIS;
    clients[1].ps.pm_type = PM_NORMAL; ents[1].s.eType = ET_PLAYER;
    ents[1].health = GIB_HEALTH - 1;
    real_die(&ents[1], &ents[2], &ents[2], 200, MOD_UNKNOWN);
    CHECK(lev->maxspree_count == 2);
}

static void test_information_and_news(void) {
    int i, mode;
    char (*sounds)[MAX_QPATH] = sym("gs_sounds");
    vmCvar_t *method = sym("csMethod");
    setup_clients(); method->integer = 0; lev->time = 1000;
    snprintf(lev->rawmapname, sizeof(lev->rawmapname), "oasis");
    setargs(2, "say", "!news"); CHECK(!check_command(&ents[1]));
    CHECK(sound_opens == 0 && message_contains(1, "permission denied"));
    reset_messages(); setargs(2, "say", "!news ../other"); CHECK(check_command(&ents[0]));
    CHECK(sound_opens == 0 && message_contains(0, "invalid map"));
    reset_messages();
    setargs(2, "say", "!news"); CHECK(check_command(&ents[0]));
    CHECK(sound_opens == 1 && message_contains(0, "no news reel"));
    sound_exists = 1; reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(sound_opens == 2 && lev->num_entities == MAX_CLIENTS + 1);
    CHECK(ents[MAX_CLIENTS].s.eType == ET_EVENTS + EV_GLOBAL_SOUND);
    CHECK(ents[MAX_CLIENTS].r.svFlags & SVF_BROADCAST);
    CHECK(!strcmp(configstrings[CS_SOUNDS + 1], "sound/vo/oasis/news_oasis.wav"));
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(sound_opens == 2 && message_contains(0, "please wait"));
    /* Both configstring modes must refuse a new sound when full, but may
     * reuse an existing entry even when there is no free slot. */
    for (mode = 0; mode <= 1; ++mode) {
        setup_clients(); lev->time = 1000; method->integer = mode; sound_exists = 1;
        snprintf(lev->rawmapname, sizeof(lev->rawmapname), "oasis");
        memset(sounds, 0, MAX_SOUNDS * MAX_QPATH);
        for (i = 1; i < MAX_SOUNDS; ++i) {
            if (mode) snprintf(sounds[i], MAX_QPATH, "filled%d", i);
            else snprintf(configstrings[CS_SOUNDS + i], sizeof(configstrings[0]), "filled%d", i);
        }
        setargs(2, "say", "!news"); CHECK(check_command(&ents[0]));
        CHECK(message_contains(0, "sound index is full") && lev->num_entities == MAX_CLIENTS);
        if (mode) snprintf(sounds[MAX_SOUNDS - 1], MAX_QPATH, "%s", expected_sound);
        else snprintf(configstrings[CS_SOUNDS + MAX_SOUNDS - 1], sizeof(configstrings[0]), "%s", expected_sound);
        reset_messages(); CHECK(check_command(&ents[0]));
        CHECK(lev->num_entities == MAX_CLIENTS + 1);
        CHECK(ents[MAX_CLIENTS].s.eventParm == GAMESOUND_MAX + MAX_SOUNDS - 1);
    }
    /* csMethod=1 creates and transmits its own dynamic sound entry. */
    setup_clients(); method->integer = 1; lev->time = 1000; sound_exists = 1;
    memset(sounds, 0, MAX_SOUNDS * MAX_QPATH);
    snprintf(lev->rawmapname, sizeof(lev->rawmapname), "oasis");
    setargs(2, "say", "!news"); CHECK(check_command(&ents[0]));
    CHECK(!strcmp(sounds[1], expected_sound) && message_contains(1, "gs "));
    /* A long occupied entry with the same 63-byte prefix is not a match. */
    setup_clients(); method->integer = 0; lev->time = 1000; sound_exists = 1;
    snprintf(lev->rawmapname, sizeof(lev->rawmapname), "abcdefghijklmnopqrstuv");
    snprintf(expected_sound, sizeof(expected_sound), "sound/vo/%s/news_%s.wav", lev->rawmapname, lev->rawmapname);
    CHECK(strlen(expected_sound) == MAX_QPATH - 1);
    for (i = 1; i < MAX_SOUNDS; ++i)
        snprintf(configstrings[CS_SOUNDS + i], sizeof(configstrings[0]), "filled%d", i);
    snprintf(configstrings[CS_SOUNDS + MAX_SOUNDS - 1], sizeof(configstrings[0]), "%sX", expected_sound);
    setargs(2, "say", "!news"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "sound index is full") && lev->num_entities == MAX_CLIENTS);
    /* Entity reserve is checked before registering a sound. */
    setup_clients(); method->integer = 0; lev->time = 1000; sound_exists = 1;
    snprintf(lev->rawmapname, sizeof(lev->rawmapname), "oasis");
    lev->num_entities = ENTITYNUM_MAX_NORMAL;
    for (i = MAX_CLIENTS; i < ENTITYNUM_MAX_NORMAL - (MIN_SPARE_GENTITIES - 2); ++i) ents[i].inuse = qtrue;
    setargs(2, "say", "!news"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "not enough free entities") && !configstrings[CS_SOUNDS + 1][0]);
    memset(sounds, 0, MAX_SOUNDS * MAX_QPATH); method->integer = 0;

    setup_clients();
    snprintf(configstrings[CS_SERVERINFO], sizeof(configstrings[0]), "server");
    setargs(2, "say", "!about"); CHECK(check_command(&ents[1]));
    CHECK(message_contains(1, "parity is incomplete"));
    reset_messages(); setargs(2, "say", "!status -all"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "players 4/64") && message_contains(0, "database statistics are not available"));
    snprintf(clients[1].pers.netname, sizeof(clients[1].pers.netname), "Bad\"\\\nName");
    clients[1].sess.killingSpree = 3; clients[2].sess.killingSpree = 8;
    setargs(2, "say", "!spree"); reset_messages(); CHECK(check_command(&ents[1]));
    CHECK(message_contains(1, "killing spree is 3"));
    setargs(2, "say", "!tspree 2"); reset_messages(); CHECK(check_command(&ents[1]));
    CHECK(message_contains(1, "Player2") && message_contains(1, "Bad   Name"));
    setargs(2, "say", "!lsplayers"); reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "Bad   Name") && !message_contains(0, "Bad\""));
}

/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): exercise actual dispatcher,
 * permissions, NQ records, overflow-safe stat sums and optional sound paths. */
static void test_imported_information_commands(void) {
    static g_shrubbot_ban_t permanent, expired, temporary;
    time_t now = time(NULL);
    vmCvar_t *method = sym("csMethod");
    setup_clients();
    method->integer = 0;
    snprintf(level3.flags, sizeof(level3.flags), "hiqQLpxgUAWtCBs");
    snprintf(level3.name, sizeof(level3.name), "L3\"\\\n%%n");
    snprintf(clients[1].pers.netname, sizeof(clients[1].pers.netname), "Bad\"\\\n%%n");
    clients[1].sess.aWeaponStats[WS_MP40].atts = 30;
    clients[1].sess.aWeaponStats[WS_THOMPSON].atts = 10;
    clients[1].sess.aWeaponStats[WS_MP40].hits = 8;
    clients[1].sess.aWeaponStats[WS_THOMPSON].hits = 2;
    clients[1].sess.aWeaponStats[WS_MP40].headshots = 2;
    clients[1].sess.aWeaponStats[WS_THOMPSON].headshots = 1;
    clients[2].sess.aWeaponStats[WS_MP40].atts = UINT_MAX;
    clients[2].sess.aWeaponStats[WS_THOMPSON].atts = UINT_MAX;
    setargs(2, "say", "!stats");
    CHECK(!check_command(&ents[1]) && message_contains(1, "permission denied"));
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "40") && message_contains(0, "25.0") && message_contains(0, "30.0"));
    CHECK(message_contains(0, "8589934590") && message_contains(0, "Bad   %n"));
    CHECK(!message_contains(-1, "Bad"));
    reset_messages(); setargs(2, "say", "!ftime"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "server local time"));
    reset_messages(); setargs(2, "say", "!levlist"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "L3   %n"));
    reset_messages(); setargs(2, "say", "!levinfo 3"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "NQ level 3") && message_contains(0, "hiqQLpxgUAWtCBs"));
    reset_messages(); setargs(2, "say", "!levinfo 30"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "level not found"));

    /* Oz-style connected-user view is private and checks hierarchy, immunity
     * and the separate NQ finger permission before revealing identifiers. */
    strcat(level3.flags, "e");
    snprintf(clients[1].pers.client_ip, sizeof(clients[1].pers.client_ip), "203.0.113.8");
    reset_messages(); setargs(2, "say", "!userinfo 1");
    CHECK(!check_command(&ents[1]) && message_contains(1, "permission denied"));
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "Bad   %n") && message_contains(0, "slot 1"));
    CHECK(message_contains(0, "GUID and IP hidden") && !message_contains(0, "203.0.113.8"));
    CHECK(!message_contains(-1, "Bad") && !message_contains(-1, "203.0.113.8"));
    reset_messages(); setargs(2, "say", "!userinfo 2"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "protected") && !message_contains(0, "guid2"));
    reset_messages(); setargs(2, "say", "!userinfo 3"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "protected"));
    reset_messages(); setargs(2, "say", "!userinfo 999999999999999999"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "not found"));
    clients[1].sess.sessionTeam = TEAM_SPECTATOR;
    strcat(level3.flags, "f");
    reset_messages(); setargs(2, "say", "!userinfo 1"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "spectator") && message_contains(0, "203.0.113.8") && message_contains(0, "guid1"));
    CHECK(!strcmp(clients[1].pers.client_ip, "203.0.113.8"));
    clients[1].sess.sessionTeam = TEAM_AXIS;
    level3.flags[strlen(level3.flags) - 1] = '\0'; /* remove f for ban privacy test */

    memset(&permanent, 0, sizeof(permanent));
    memset(&expired, 0, sizeof(expired));
    memset(&temporary, 0, sizeof(temporary));
    snprintf(permanent.name, sizeof(permanent.name), "Banned\"\\\n%%n");
    snprintf(permanent.guid, sizeof(permanent.guid), "0123456789abcdef0123456789abcdef");
    snprintf(permanent.ip, sizeof(permanent.ip), "203.0.113.8");
    snprintf(permanent.reason, sizeof(permanent.reason), "test\"\\\nreason");
    snprintf(permanent.banner, sizeof(permanent.banner), "Admin");
    expired.expires = (int)((long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET - 1);
    snprintf(expired.name, sizeof(expired.name), "ExpiredPlayer");
    temporary.expires = (int)((long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET + 600);
    snprintf(temporary.name, sizeof(temporary.name), "TemporaryPlayer");
    bans[0] = &permanent; bans[1] = &expired; bans[2] = &temporary;
    reset_messages(); setargs(2, "say", "!banlist");
    CHECK(!check_command(&ents[1]) && message_contains(1, "permission denied"));
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "#1 Banned   %n") && message_contains(0, "#3 TemporaryPlayer"));
    CHECK(!message_contains(0, "ExpiredPlayer") && message_contains(0, "2 active"));
    reset_messages(); setargs(2, "say", "!baninfo 1"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "Banned   %n") && message_contains(0, "test   reason"));
    CHECK(message_contains(0, "GUID and IP hidden"));
    CHECK(!message_contains(0, "203.0.113.8") && !message_contains(-1, "203.0.113.8"));
    strcat(level3.flags, "f");
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "203.0.113.8") && message_contains(0, permanent.guid));
    reset_messages(); setargs(2, "say", "!baninfo 2"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "expired"));
    reset_messages(); setargs(2, "say", "!baninfo 4"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "not found"));
    reset_messages(); setargs(2, "say", "!baninfo 9999999999999999999999"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "usage"));
    reset_messages(); setargs(2, "say", "!banlist 3"); CHECK(check_command(&ents[0]));
    CHECK(!message_contains(0, "#1 Banned") && message_contains(0, "#3 TemporaryPlayer"));
    memset(bans, 0, sizeof(*bans) * MAX_SHRUBBOT_BANS);

    setup_clients(); method->integer = 0; lev->time = 1000;
    snprintf(expected_sound, sizeof(expected_sound), "sound/jaymod/chicken.wav");
    setargs(2, "say", "!chicken 1");
    CHECK(!check_command(&ents[1]) && sound_opens == 0);
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "not installed") && lev->num_entities == MAX_CLIENTS);
    sound_exists = 1; reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(lev->num_entities == MAX_CLIENTS + 1 && message_contains(-1, "headless chicken"));
    CHECK(!strcmp(configstrings[CS_SOUNDS + 1], expected_sound));
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "cooldown") && lev->num_entities == MAX_CLIENTS + 1);
    lev->time = 12000;
    reset_messages(); setargs(2, "say", "!chicken 2"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "protected") && lev->num_entities == MAX_CLIENTS + 1);
    reset_messages(); setargs(2, "say", "!chicken 3"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "protected") && lev->num_entities == MAX_CLIENTS + 1);
    snprintf(expected_sound, sizeof(expected_sound), "sound/jaymod/crybaby.wav");
    reset_messages(); setargs(2, "say", "!crybaby 1"); CHECK(check_command(&ents[0]));
    CHECK(lev->num_entities == MAX_CLIENTS + 2 && message_contains(-1, "little baby"));
}

/* NewMod, 2026-11-21: exercise the actual dispatcher, live pages, record
 * visibility, authorization changes, console isolation and slot lifecycle. */
static void test_paginated_reports(void) {
    static g_shrubbot_admin_t records[45];
    static g_shrubbot_level_t level1, hiddenLevel, manyLevels[MAX_SHRUBBOT_LEVELS];
    static g_shrubbot_ban_t manyBans[45];
    void (*clear_reports)(int) = sym("G_nm_clear_reports");
    void (*disconnect)(int) = sym("ClientDisconnect");
    char *(*connect)(int, qboolean, qboolean) = sym("ClientConnect");
    int i;

    setup_clients();
    snprintf(level3.flags, sizeof(level3.flags), "hsB");
    snprintf(level5.flags, sizeof(level5.flags), "hsB");
    memset(&level1, 0, sizeof(level1)); level1.level = 1;
    snprintf(level1.flags, sizeof(level1.flags), "hsB"); levels[3] = &level1;
    snprintf(admin0.name, sizeof(admin0.name), "CurrentAdmin");
    snprintf(admin2.name, sizeof(admin2.name), "HigherSecret");
    snprintf(immutable3.name, sizeof(immutable3.name), "ImmutableSecret");
    memset(records, 0, sizeof(records));
    for (i = 0; i < 45; ++i) {
        snprintf(records[i].guid, sizeof(records[i].guid), "staff-guid-%02d", i);
        snprintf(records[i].name, sizeof(records[i].name), "Staff%02d", i);
        records[i].level = i < 30 ? 1 : 3;
        admins[i + 3] = &records[i];
    }
    setargs(2, "say", "!userlist");
    CHECK(!check_command(&ents[1]) && message_contains(1, "permission denied"));
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "CurrentAdmin") && message_contains(0, "Staff00"));
    CHECK(message_contains(0, "page 1 of 3") && message_contains(0, "46 matching"));
    CHECK(!message_contains(0, "Staff19") && !message_contains(0, "HigherSecret"));
    CHECK(!message_contains(0, "ImmutableSecret") && !message_contains(0, "staff-guid"));
    CHECK(!message_contains(-1, "Staff00"));
    for (i = 0; i < sent_count; ++i) CHECK(strlen(sent[i].text) < MAX_STRING_CHARS);

    reset_messages(); setargs(2, "say", "!page"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "3 pages available"));
    reset_messages(); CHECK(check_command(&ents[2]));
    CHECK(message_contains(2, "no saved long report"));
    reset_messages(); setargs(2, "say", "!page 2"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "Staff19") && message_contains(0, "Staff38") && !message_contains(0, "Staff18"));
    CHECK(message_contains(0, "page 2 of 3"));
    reset_messages(); setargs(2, "say", "!page 999"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "Staff44") && message_contains(0, "page 3 of 3"));
    reset_messages(); setargs(2, "say", "!page -2"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "Staff00") && message_contains(0, "page 1 of 3"));
    reset_messages(); setargs(2, "say", "!page 9999999999999999999999"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "usage"));

    /* Short filtered reports do not replace the last long one. */
    reset_messages(); setargs(2, "say", "!userlist -name sTaFf02 -level 1"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "Staff02") && message_contains(0, "1 matching") && !message_contains(0, "Staff03"));
    reset_messages(); setargs(2, "say", "!page 2"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "Staff19"));
    reset_messages(); setargs(2, "say", "!userlist -ip 203.0.113.1"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no IP or last-seen filter") && !message_contains(0, "Staff"));
    reset_messages(); setargs(2, "say", "!userlist -level -1"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "usage"));

    /* Queries are recomputed: hierarchy, special flags, and current data
     * take effect even after a report was first generated. */
    snprintf(records[19].name, sizeof(records[19].name), "NewStaffName");
    reset_messages(); setargs(2, "say", "!page 2"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "NewStaffName") && !message_contains(0, "Staff19"));
    snprintf(records[0].flags, sizeof(records[0].flags), "@");
    snprintf(records[1].flags, sizeof(records[1].flags), "-!"); /* conservative */
    records[2].level = 5;
    memset(&hiddenLevel, 0, sizeof(hiddenLevel)); hiddenLevel.level = 2;
    snprintf(hiddenLevel.flags, sizeof(hiddenLevel.flags), "@"); levels[4] = &hiddenLevel;
    records[3].level = 2;
    for (i = 0; i < 4; ++i) {
        char command[64];
        snprintf(command, sizeof(command), "!userlist -name staff%02d", i);
        reset_messages(); setargs(2, "say", command); CHECK(check_command(&ents[0]));
        CHECK(message_contains(0, "0 matching") && !message_contains(0, records[i].name));
    }
    reset_messages(); setargs(2, "say", "!page 3"); CHECK(check_command(&ents[0]));
    CHECK(!message_contains(0, "Staff00") && !message_contains(0, "Staff01") &&
          !message_contains(0, "Staff02") && !message_contains(0, "Staff03"));
    admin0.level = 1;
    reset_messages(); setargs(2, "say", "!page 99"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "page 2 of 2") && !message_contains(0, "Staff44"));
    snprintf(level1.flags, sizeof(level1.flags), "hB"); /* no source permission */
    reset_messages(); setargs(2, "say", "!page 1"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no longer authorized") && !message_contains(0, "Staff"));
    snprintf(level1.flags, sizeof(level1.flags), "hsB");
    reset_messages(); setargs(2, "say", "!page"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no saved long report"));

    /* The console has a separate, unrestricted query, not a client slot's. */
    admin0.level = 3;
    reset_messages(); setargs(1, "!userlist"); CHECK(check_command(NULL));
    CHECK(strstr(console_output, "HigherSecret") && strstr(console_output, "ImmutableSecret"));
    reset_messages(); setargs(1, "!page 3"); CHECK(check_command(NULL));
    CHECK(strstr(console_output, "Staff44"));
    reset_messages(); setargs(2, "say", "!page"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no saved long report") && !message_contains(0, "Staff44"));

    /* Bans use live expiry and their own permission; indices remain NQ ban
     * numbers. A filtered short report does not displace the larger query. */
    setup_clients(); snprintf(level3.flags, sizeof(level3.flags), "hBs");
    memset(manyBans, 0, sizeof(manyBans));
    for (i = 0; i < 45; ++i) {
        snprintf(manyBans[i].name, sizeof(manyBans[i].name), "Banned%02d", i + 1);
        bans[i] = &manyBans[i];
    }
    setargs(2, "say", "!banlist 5"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "#5 Banned05") && message_contains(0, "41 active") && message_contains(0, "page 1 of 3"));
    reset_messages(); setargs(2, "say", "!page 2"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "#25 Banned25") && !message_contains(0, "#24 Banned24"));
    reset_messages(); setargs(2, "say", "!banlist 45"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "#45 Banned45") && message_contains(0, "1 active"));
    reset_messages(); setargs(2, "say", "!page 2"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "#25 Banned25"));
    snprintf(level3.flags, sizeof(level3.flags), "hs");
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no longer authorized") && !message_contains(0, "Banned"));

    /* Level listing also has live pagination and a case-insensitive filter. */
    setup_clients(); memset(manyLevels, 0, sizeof(manyLevels));
    for (i = 0; i < MAX_SHRUBBOT_LEVELS; ++i) {
        manyLevels[i].level = i; snprintf(manyLevels[i].name, sizeof(manyLevels[i].name), "Level%02d", i);
        levels[i] = &manyLevels[i];
    }
    snprintf(manyLevels[3].flags, sizeof(manyLevels[3].flags), "hsB");
    setargs(2, "say", "!levlist"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "Level00") && message_contains(0, "page 1 of 2"));
    reset_messages(); setargs(2, "say", "!page 2"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "Level20") && message_contains(0, "Level31"));
    reset_messages(); setargs(2, "say", "!levlist -name lEvEl02"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "Level02") && message_contains(0, "1 matching"));

    /* A rejected connect and an early disconnect clear the slot before it
     * can be reused. Map initialization uses the all-slots reset. */
    snprintf(fake_userinfo, sizeof(fake_userinfo), "bad-userinfo");
    CHECK(connect(0, qtrue, qfalse));
    reset_messages(); setargs(2, "say", "!page"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no saved long report"));
    reset_messages(); setargs(2, "say", "!levlist"); CHECK(check_command(&ents[0]));
    ents[0].client = NULL; disconnect(0); ents[0].client = &clients[0];
    reset_messages(); setargs(2, "say", "!page"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no saved long report"));
    reset_messages(); setargs(2, "say", "!levlist"); CHECK(check_command(&ents[0]));
    clear_reports(-1);
    reset_messages(); setargs(2, "say", "!page"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no saved long report"));

    /* More than 99 pages is capped without retaining rendered report data or
     * sending an oversized engine command. */
    { g_shrubbot_admin_t *bulk = calloc(2001, sizeof(*bulk));
      CHECK(bulk); setup_clients(); snprintf(level3.flags, sizeof(level3.flags), "hs");
      snprintf(admin0.name, sizeof(admin0.name), "Operator");
      for (i = 0; i < 2001; ++i) {
          snprintf(bulk[i].name, sizeof(bulk[i].name), "Bulk%04d", i);
          snprintf(bulk[i].guid, sizeof(bulk[i].guid), "bulk-guid-%04d", i);
          bulk[i].level = 1; admins[i + 3] = &bulk[i];
      }
      setargs(2, "say", "!userlist"); CHECK(check_command(&ents[0]));
      CHECK(message_contains(0, "page 1 of 99") && message_contains(0, "first 1980"));
      reset_messages(); setargs(2, "say", "!page 999"); CHECK(check_command(&ents[0]));
      CHECK(message_contains(0, "page 99 of 99") && message_contains(0, "Bulk1959") && message_contains(0, "Bulk1978"));
      CHECK(!message_contains(0, "Bulk1979") && !message_contains(0, "bulk-guid"));
      for (i = 0; i < sent_count; ++i) CHECK(strlen(sent[i].text) < MAX_STRING_CHARS);
      clear_reports(-1); memset(admins, 0, sizeof(*admins) * MAX_SHRUBBOT_ADMINS); free(bulk); }
}

/* Exercise the real shrub/client dispatchers and lifecycle, not just the
 * storage helper. The warning cache is a deliberately connection-local
 * subset of Oz, not a persistent GUID/IP moderation database. */
static void test_connection_warnings(void) {
    void (*clear_warnings)(int) = sym("G_nm_clear_warnings");
    void (*disconnect)(int) = sym("ClientDisconnect");
    char *(*connect)(int, qboolean, qboolean) = sym("ClientConnect");
    char command[512]; int i;
    setup_clients();
    snprintf(level3.flags, sizeof(level3.flags), "hR");
    snprintf(level5.flags, sizeof(level5.flags), "hR");
    setargs(2, "say", "!warn 1 test");
    CHECK(!check_command(&ents[1]) && message_contains(1, "permission denied"));
    reset_messages(); setargs(2, "say", "!dewarn 1");
    CHECK(!check_command(&ents[1]) && message_contains(1, "permission denied"));
    reset_messages(); setargs(2, "say", "!warn 2 protected");
    CHECK(check_command(&ents[0]) && message_contains(0, "protected") && !message_contains(2, "protected"));
    reset_messages(); setargs(2, "say", "!warn 3 protected");
    CHECK(check_command(&ents[0]) && message_contains(0, "protected") && !message_contains(3, "protected"));
    reset_messages(); setargs(2, "say", "!warn 1");
    CHECK(check_command(&ents[0]) && message_contains(0, "usage"));
    reset_messages(); setargs(2, "say", "!warn 1      ");
    CHECK(check_command(&ents[0]) && message_contains(0, "usage"));
    reset_messages(); setargs(2, "say", "!warn 9999999999999999999 invalid");
    CHECK(check_command(&ents[0]) && message_contains(0, "no connected player"));
    reset_messages(); setargs(2, "say", "!warn Player ambiguous");
    CHECK(check_command(&ents[0]) && message_contains(0, "multiple matches"));
    reset_messages(); setargs(2, "say", "!dewarn 1 1 extra");
    CHECK(check_command(&ents[0]) && message_contains(0, "usage"));

    /* Target and reason are sanitized before they enter engine command
     * strings or the cache. Incognito warner names are not exposed. */
    snprintf(admin0.flags, sizeof(admin0.flags), "@");
    snprintf(clients[1].pers.netname, sizeof(clients[1].pers.netname), "Target\"\\\n%%n");
    clients[1].sess.sessionTeam = TEAM_SPECTATOR;
    reset_messages(); setargs(2, "say", "!warn 1 bad\"\\\n%n"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(-1, "Target   %n") && message_contains(1, "bad   %n"));
    CHECK(message_contains(1, "an admin") && !message_contains(1, "Player0"));
    CHECK(!message_contains(-1, "bad   %n"));
    CHECK(clients[1].ps.events[0] == EV_GENERAL_SOUND && clients[1].ps.eventSequence == 1);
    reset_messages(); setargs(1, "warnings"); client_command(1);
    CHECK(message_contains(1, "1 saved for this connection") && message_contains(1, "bad   %n"));
    CHECK(!message_contains(0, "bad   %n") && !message_contains(-1, "bad   %n"));
    reset_messages(); setargs(2, "warnings", "2"); client_command(1);
    CHECK(message_contains(1, "usage") && !message_contains(1, "bad"));
    reset_messages(); setargs(1, "warnings"); client_command(2);
    CHECK(message_contains(2, "0 saved") && !message_contains(2, "bad"));

    /* Listing/removal is limited to R admins and respects the target's
     * current level and immutable flag, including after permissions change. */
    admin0.flags[0] = '\0';
    reset_messages(); setargs(2, "say", "!warn 1 second reason"); CHECK(check_command(&ents[0]));
    reset_messages(); setargs(2, "say", "!dewarn 1"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "2 saved") && message_contains(0, "second reason"));
    CHECK(!message_contains(-1, "second reason") && !message_contains(1, "second reason"));
    for (i = 0; i < 4; ++i) {
        const char *bad[] = {"0", "-1", "abc", "999999999999999999999999"};
        snprintf(command, sizeof(command), "!dewarn 1 %s", bad[i]);
        reset_messages(); setargs(2, "say", command); CHECK(check_command(&ents[0]));
        CHECK(message_contains(0, "usage"));
    }
    reset_messages(); setargs(2, "say", "!dewarn 1 1"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "warning 1 removed"));
    reset_messages(); setargs(1, "warnings"); client_command(1);
    CHECK(message_contains(1, "1 saved") && message_contains(1, "1. ^7second reason") && !message_contains(1, "bad"));
    reset_messages(); setargs(2, "say", "!dewarn 1 2"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no warning with that number"));
    snprintf(level3.flags, sizeof(level3.flags), "h");
    reset_messages(); setargs(2, "say", "!dewarn 1"); CHECK(!check_command(&ents[0]));
    CHECK(message_contains(0, "permission denied") && !message_contains(0, "second reason"));
    snprintf(level3.flags, sizeof(level3.flags), "hR");
    reset_messages(); setargs(1, "!warn 2 higher reason"); CHECK(check_command(NULL));
    reset_messages(); setargs(2, "say", "!dewarn 2"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "protected") && !message_contains(0, "higher reason"));
    reset_messages(); setargs(1, "!dewarn 2"); CHECK(check_command(NULL));
    CHECK(strstr(console_output, "higher reason"));
    reset_messages(); setargs(1, "warnings"); client_command(2); CHECK(message_contains(2, "higher reason"));
    reset_messages(); setargs(2, "say", "!dewarn 3"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "protected"));

    /* Do not silently evict or kick when the bounded cache fills. The ninth
     * notification still arrives, but the issuer is told it was not saved. */
    clear_warnings(1);
    for (i = 0; i < 9; ++i) {
        snprintf(command, sizeof(command), "!warn 1 reason%02d", i);
        reset_messages(); setargs(2, "say", command); CHECK(check_command(&ents[0]));
        CHECK(message_contains(1, i == 8 ? "reason08" : "reason"));
        if (i == 8) CHECK(message_contains(0, "delivered but not saved"));
    }
    CHECK(dropped == 0);
    reset_messages(); setargs(1, "warnings"); client_command(1);
    CHECK(message_contains(1, "8 saved") && message_contains(1, "reason00") &&
          message_contains(1, "reason07") && !message_contains(1, "reason08"));
    /* A playerstate/respawn reset cannot clear the separate connection cache. */
    memset(&clients[1].ps, 0, sizeof(clients[1].ps)); clients[1].ps.clientNum = 1;
    reset_messages(); setargs(1, "warnings"); client_command(1); CHECK(message_contains(1, "8 saved"));

    /* Both a rejected connect and the actual disconnect clear the slot before
     * reuse. ClientDisconnect itself is exercised, not just the helper. */
    snprintf(fake_userinfo, sizeof(fake_userinfo), "bad-userinfo");
    CHECK(connect(1, qtrue, qfalse));
    reset_messages(); setargs(1, "warnings"); client_command(1); CHECK(message_contains(1, "0 saved"));
    reset_messages(); setargs(2, "say", "!warn 1 temporary"); CHECK(check_command(&ents[0]));
    ((vmCvar_t *)sym("g_maxclients"))->integer = MAX_CLIENTS;
    ((vmCvar_t *)sym("g_serverInfo"))->integer = 0;
    reset_messages(); disconnect(1);
    CHECK(clients[1].pers.connected == CON_DISCONNECTED && !ents[1].inuse);
    ents[1].client = &clients[1]; ents[1].inuse = qtrue; clients[1].pers.connected = CON_CONNECTED;
    reset_messages(); setargs(1, "warnings"); client_command(1);
    CHECK(message_contains(1, "0 saved") && !message_contains(1, "temporary"));
    clear_warnings(-1);

    /* The warning notification changes from center print to chat during
     * intermission, and /warnings remains available to the target. */
    setup_clients(); snprintf(level3.flags, sizeof(level3.flags), "hR");
    game_state->integer = GS_INTERMISSION; lev->intermissiontime = 1;
    reset_messages(); setargs(2, "say", "!warn 1 intermission reason");
    CHECK(check_command(&ents[0]) && message_contains(1, "chat \"") && message_contains(1, "intermission reason"));
    reset_messages(); setargs(1, "warnings"); client_command(1);
    CHECK(message_contains(1, "intermission reason"));
    game_state->integer = GS_PLAYING; lev->intermissiontime = 0;

    /* Unlike private !report, ordinary NQ shrub commands sent via say are
     * broadcast as chat, and g_logAdmin can retain their raw arguments. */
    setup_clients(); snprintf(level3.flags, sizeof(level3.flags), "hR");
    snprintf(adminlog->string, sizeof(adminlog->string), "admin.log");
    setargs(2, "say", "!warn 1 chat-visible");
    say_command(&ents[0], SAY_ALL, qfalse);
    CHECK(message_contains(2, "!warn 1 chat-visible"));
    CHECK(opened == 1 && writes == 1 && closes == 1 && strstr(stored, "chat-visible"));

    /* Names and args cannot forge a second admin-log line, even when the
     * entry fills the entire fixed-size formatter. */
    snprintf(clients[0].pers.netname, sizeof(clients[0].pers.netname), "actor\nforged");
    snprintf(clients[1].pers.netname, sizeof(clients[1].pers.netname), "target\rforged");
    reset_messages(); setargs(2, "say", "!warn 1 reason\nforged"); CHECK(check_command(&ents[0]));
    CHECK(strstr(stored, "actor forged") && strstr(stored, "target forged") && strstr(stored, "reason forged"));
    CHECK(strchr(stored, '\n') == stored + strlen(stored) - 1);
    { char long_reason[960], prefix[160], too_long[161];
      memset(long_reason, 'x', sizeof(long_reason) - 1); long_reason[sizeof(long_reason) - 1] = '\0';
      memset(prefix, 'x', sizeof(prefix) - 1); prefix[sizeof(prefix) - 1] = '\0';
      memset(too_long, 'x', sizeof(too_long) - 1); too_long[sizeof(too_long) - 1] = '\0';
      reset_messages(); setargs(4, "say", "!warn", "1", long_reason); CHECK(check_command(&ents[0]));
      CHECK(strlen(stored) > 900 && strchr(stored, '\n') == stored + strlen(stored) - 1);
      reset_messages(); setargs(1, "warnings"); client_command(1);
      CHECK(message_contains(1, prefix) && !message_contains(1, too_long));
    }
}

static void test_seen_command(void) {
    static g_shrubbot_admin_t recent, bulk[130];
    void (*record_disconnect)(gentity_t *) = sym("G_nm_seen_disconnect");
    void (*disconnect)(int) = sym("ClientDisconnect");
    void (*reset_seen)(void) = sym("G_nm_seen_reset");
    int i;
    setup_clients();
    snprintf(level3.flags, sizeof(level3.flags), "hi");
    ((vmCvar_t *)sym("g_maxclients"))->integer = MAX_CLIENTS;
    ((vmCvar_t *)sym("g_serverInfo"))->integer = 0;
    memset(&recent, 0, sizeof(recent));
    snprintf(recent.guid, sizeof(recent.guid), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    snprintf(recent.name, sizeof(recent.name), "Medic"); recent.level = 1; admins[3] = &recent;
    snprintf(clients[1].pers.cl_guid, sizeof(clients[1].pers.cl_guid), "%s", recent.guid);
    snprintf(clients[1].pers.netname, sizeof(clients[1].pers.netname), "Medic\"\\\n%%n");
    clients[1].sess.sessionTeam = TEAM_SPECTATOR;
    setargs(2, "say", "!seen Medic");
    CHECK(!check_command(&ents[1]) && message_contains(1, "permission denied"));
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "Medic   %n") && message_contains(0, "currently online"));
    CHECK(!message_contains(0, recent.guid) && !message_contains(-1, "Medic"));

    /* Call the actual disconnect path: it records before clearing the
     * client and never writes a persistent database. */
    reset_messages(); disconnect(1);
    CHECK(clients[1].pers.connected == CON_DISCONNECTED && !ents[1].inuse);
    reset_messages(); setargs(2, "say", "!seen medic"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "Medic   %n") && message_contains(0, "disconnected") && message_contains(0, "seconds ago"));
    CHECK(message_contains(0, "map-local") && !message_contains(0, recent.guid));

    /* Reconnection wins over a stale disconnect record; a later disconnect
     * updates the one cache entry instead of inventing multiple identities. */
    ents[1].inuse = qtrue; clients[1].pers.connected = CON_CONNECTED;
    snprintf(clients[1].pers.netname, sizeof(clients[1].pers.netname), "NewMedic");
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "NewMedic") && message_contains(0, "currently online") && !message_contains(0, "seconds ago"));
    record_disconnect(&ents[1]); clients[1].pers.connected = CON_DISCONNECTED; ents[1].inuse = qfalse;
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "NewMedic") && message_contains(0, "seconds ago") && !message_contains(0, "Medic   %n"));
    recent.level = 5;
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no visible matching") && !message_contains(0, "NewMedic"));
    reset_messages(); setargs(2, "!seen", "medic"); CHECK(check_command(NULL));
    CHECK(strstr(console_output, "NewMedic") && strstr(console_output, "seconds ago"));
    recent.level = 1; snprintf(recent.flags, sizeof(recent.flags), "@");
    reset_messages(); setargs(2, "say", "!seen medic"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no visible matching") && !message_contains(0, "NewMedic"));
    recent.flags[0] = '\0'; admins[3] = NULL;
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no visible matching"));
    admins[3] = &recent; reset_seen();
    reset_messages(); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no visible matching"));

    /* Unknown/invalid GUIDs cannot create a misleading stored history. */
    ents[1].inuse = qtrue; clients[1].pers.connected = CON_CONNECTED;
    snprintf(clients[1].pers.cl_guid, sizeof(clients[1].pers.cl_guid), "NO_GUID");
    record_disconnect(&ents[1]); clients[1].pers.connected = CON_DISCONNECTED; ents[1].inuse = qfalse;
    reset_messages(); CHECK(check_command(&ents[0])); CHECK(message_contains(0, "no visible matching"));

    /* The bounded ring evicts its oldest entry; searches print at most four
     * authorized results. No address or GUID is included in output. */
    setup_clients(); snprintf(level3.flags, sizeof(level3.flags), "hi");
    memset(bulk, 0, sizeof(bulk));
    ents[1].inuse = qtrue; clients[1].pers.connected = CON_CONNECTED;
    for (i = 0; i < 130; ++i) {
        snprintf(bulk[i].guid, sizeof(bulk[i].guid), "%032x", i + 1000);
        snprintf(bulk[i].name, sizeof(bulk[i].name), "Seen%03d", i); bulk[i].level = 1;
        admins[i + 3] = &bulk[i];
        snprintf(clients[1].pers.cl_guid, sizeof(clients[1].pers.cl_guid), "%s", bulk[i].guid);
        snprintf(clients[1].pers.netname, sizeof(clients[1].pers.netname), "%s", bulk[i].name);
        record_disconnect(&ents[1]);
    }
    clients[1].pers.connected = CON_DISCONNECTED; ents[1].inuse = qfalse;
    setargs(2, "say", "!seen Seen000"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no visible matching") && !message_contains(0, "Seen000"));
    reset_messages(); setargs(2, "say", "!seen Seen129"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "Seen129") && message_contains(0, "seconds ago"));
    reset_messages(); setargs(2, "say", "!seen Seen"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "more matches") && !message_contains(0, "000000000000000000000000000003e8"));
    { int shown = 0; for (i = 0; i < sent_count; ++i) if (strstr(sent[i].text, "seconds ago")) ++shown; CHECK(shown == 4); }
    reset_seen();
}

/* Enter the actual G_InitGame with both caches populated. The fake engine
 * deliberately interrupts at its first print, so this tests the entry reset,
 * not a completed map load; etlegacy_smoke.py covers a real map restart. */
static void test_map_init_cache_entry(void) {
    static g_shrubbot_admin_t recent;
    void (*disconnect)(int) = sym("ClientDisconnect");
    void (*init_game)(int, int, int) = sym("G_InitGame");
    setup_clients(); snprintf(level3.flags, sizeof(level3.flags), "hiR");
    ((vmCvar_t *)sym("g_maxclients"))->integer = MAX_CLIENTS;
    ((vmCvar_t *)sym("g_serverInfo"))->integer = 0;
    memset(&recent, 0, sizeof(recent));
    snprintf(recent.guid, sizeof(recent.guid), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    snprintf(recent.name, sizeof(recent.name), "MapCacheMedic"); recent.level = 1; admins[3] = &recent;
    snprintf(clients[1].pers.cl_guid, sizeof(clients[1].pers.cl_guid), "%s", recent.guid);
    snprintf(clients[1].pers.netname, sizeof(clients[1].pers.netname), "%s", recent.name);
    disconnect(1);
    reset_messages(); setargs(2, "say", "!seen MapCacheMedic"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "MapCacheMedic") && message_contains(0, "seconds ago"));
    reset_messages(); setargs(1, "!warn 2 map-cache-reason"); CHECK(check_command(NULL));
    reset_messages(); setargs(1, "warnings"); client_command(2); CHECK(message_contains(2, "map-cache-reason"));
    abort_init_on_print = 1;
    if (!setjmp(error_jump)) {
        init_game(1000, 123, 1);
        CHECK(!"expected fake-engine init interruption");
    }
    CHECK(!abort_init_on_print);
    reset_messages(); setargs(2, "say", "!seen MapCacheMedic"); CHECK(check_command(&ents[0]));
    CHECK(message_contains(0, "no visible matching") && !message_contains(0, "seconds ago"));
    reset_messages(); setargs(1, "warnings"); client_command(2);
    CHECK(message_contains(2, "0 saved") && !message_contains(2, "map-cache-reason"));
}

static void assert_no_public_report(void) {
    int i;
    for (i = 0; i < sent_count; ++i) {
        CHECK(sent[i].dest != -1);
        CHECK(sent[i].dest != 0 && sent[i].dest != 3);
    }
}

static void test_private_reports(void) {
    int i;
    setup_clients();
    /* A configured general log must not receive copies of private reports. */
    snprintf(adminlog->string, sizeof(adminlog->string), "admin.log");
    snprintf(clients[0].pers.netname, sizeof(clients[0].pers.netname), "Victim\"\\\nName");
    setargs(2, "say", "!report 0 bad\"\\\nbehavior");
    say_command(&ents[1], SAY_ALL, qfalse);
    CHECK(opened == 1 && writes == 1 && closes == 1);
    CHECK(clients[1].pers.reportsSent == 1);
    CHECK(strstr(stored, "[2026-11-21 12:34:56]"));
    CHECK(strstr(stored, "reporter_guid=\"guid1\""));
    CHECK(strstr(stored, "target_guid=\"guid0\""));
    CHECK(strstr(stored, "reason=\"bad   behavior\""));
    CHECK(strchr(stored, '\n') == stored + strlen(stored) - 1);
    CHECK(message_contains(1, "saved"));
    CHECK(message_contains(2, "reported"));
    CHECK(!message_contains(2, "bad\""));
    assert_no_public_report();

    /* Silent denial and disabled features must never fall through to G_Say. */
    reset_messages();
    level0.flags[0] = '\0';
    setargs(2, "say", "!report 0 private denied reason");
    say_command(&ents[1], SAY_ALL, qfalse);
    CHECK(opened == 0 && message_contains(1, "permission denied"));
    assert_no_public_report();
    level0.flags[0] = 'h'; level0.flags[1] = '\0';
    shrub->string[0] = '\0';
    reset_messages();
    say_command(&ents[1], SAY_ALL, qfalse);
    CHECK(opened == 0 && message_contains(1, "disabled"));
    assert_no_public_report();
    snprintf(shrub->string, sizeof(shrub->string), "shrubbot.cfg");

    reset_messages();
    reportfile->string[0] = '\0';
    say_command(&ents[1], SAY_ALL, qfalse);
    CHECK(opened == 0 && message_contains(1, "disabled"));
    assert_no_public_report();
    snprintf(reportfile->string, sizeof(reportfile->string), "reports.log");

    reset_messages();
    open_fail = 1;
    say_command(&ents[1], SAY_ALL, qfalse);
    CHECK(opened == 1 && writes == 0 && clients[1].pers.reportsSent == 1);
    CHECK(message_contains(1, "could not save"));
    assert_no_public_report();
    reset_messages(); short_write = 1;
    say_command(&ents[1], SAY_ALL, qfalse);
    CHECK(writes == 1 && closes == 1 && clients[1].pers.reportsSent == 1);
    CHECK(message_contains(1, "could not save"));
    assert_no_public_report();

    /* Direct client-console dispatch needs no stealth-admin flag. */
    reset_messages();
    setargs(3, "!report", "0", "console report");
    client_command(1);
    CHECK(opened == 1 && clients[1].pers.reportsSent == 2);
    assert_no_public_report();
    reset_messages();
    setargs(4, "say_team", "!report", "0", "team report");
    say_command(&ents[1], SAY_TEAM, qfalse);
    CHECK(opened == 1 && clients[1].pers.reportsSent == 3);
    assert_no_public_report();
    reset_messages();
    setargs(2, "say", "!report 0 limit report");
    say_command(&ents[1], SAY_ALL, qfalse);
    CHECK(opened == 0 && message_contains(1, "limit reached"));
    for (i = 0; i < sent_count; ++i) CHECK(!strstr(sent[i].text, "limit report"));
    assert_no_public_report();

    /* Leading spaces and tabs are private and parsed as delimiters. */
    setup_clients();
    setargs(2, "say", "  \t!REPORT\t0\twhitespace reason");
    client_command(1);
    CHECK(opened == 1 && strstr(stored, "reason=\"whitespace reason\""));
    assert_no_public_report();
    reset_messages();
    setargs(5, "say_teamnl", " ", "!report", "0", "teamnl report");
    client_command(1);
    CHECK(opened == 1 && strstr(stored, "teamnl report"));
    assert_no_public_report();

    /* A custom shrub command with the same name must not intercept the
     * built-in private-report routes or receive their arguments. */
    { g_shrubbot_command_t custom, **commands = sym("g_shrubbot_commands");
      setup_clients(); memset(&custom, 0, sizeof(custom));
      snprintf(custom.command, sizeof(custom.command), "report");
      snprintf(custom.exec, sizeof(custom.exec), "echo OVERRIDE [1] [2]");
      custom.levels[0] = 0; custom.levels[1] = -1; commands[0] = &custom; commands[1] = NULL;
      setargs(2, "say", "  !report 0 custom override secret"); client_command(1);
      CHECK(opened == 1 && strstr(stored, "custom override secret") && queued_count == 0);
      assert_no_public_report();
      reset_messages(); setargs(3, "!report", "0", "direct override secret"); client_command(1);
      CHECK(opened == 1 && strstr(stored, "direct override secret") && queued_count == 0);
      assert_no_public_report();
      commands[0] = NULL;
    }

    /* Direct console input can contain a quoted, multiword target name. */
    setup_clients();
    snprintf(clients[0].pers.netname, sizeof(clients[0].pers.netname), "Player Zero");
    setargs(3, "!report", "Player Zero", "quoted target");
    client_command(1);
    CHECK(opened == 1 && strstr(stored, "target=\"Player Zero\""));
    assert_no_public_report();

    /* A zero or negative notification threshold must not expose reports
     * to level-zero players. Muted users may report privately. */
    setup_clients();
    clients[1].sess.muted = qtrue;
    viewlevel->integer = 0;
    setargs(2, "say_buddy", "!report 0 muted reason");
    client_command(1);
    CHECK(opened == 1 && message_contains(1, "saved"));
    CHECK(message_contains(0, "reported") && message_contains(2, "reported"));
    CHECK(!message_contains(3, "reported") && !message_contains(-1, "reported"));
}

#ifdef ET_LUA
/* Verify that even a Lua module that consumes and rebroadcasts client
 * commands cannot intercept the built-in private report entry points. */
static void test_lua_report_dispatch(void) {
    qboolean (*start_vm)(lua_vm_t *) = sym("G_LuaStartVM");
    void (*stop_vm)(lua_vm_t *) = sym("G_LuaStopVM");
    lua_vm_t **vms = sym("lVM"), *vm;
    const char *script =
        "function et_ClientCommand(n, cmd) "
        "et.trap_SendServerCommand(-1, 'print \\\"HOOK SAW COMMAND\\\"'); return 1 end\n";
    setup_clients();
    vm = calloc(1, sizeof(*vm)); CHECK(vm);
    vm->id = 0;
    snprintf(vm->file_name, sizeof(vm->file_name), "report-hook.lua");
    vm->code = strdup(script); CHECK(vm->code);
    vm->code_size = (int)strlen(vm->code);
    CHECK(start_vm(vm)); vms[0] = vm;

    setargs(2, "say", "  !report 0 secret from lua");
    client_command(1);
    CHECK(opened == 1 && strstr(stored, "secret from lua"));
    CHECK(!message_contains(-1, "HOOK"));
    assert_no_public_report();
    reset_messages();
    setargs(3, "!report", "0", "console secret");
    client_command(1);
    CHECK(opened == 1 && !message_contains(-1, "HOOK"));
    assert_no_public_report();
    reset_messages();
    setargs(2, "say", "!reporting is ordinary chat");
    client_command(1);
    CHECK(opened == 0 && message_contains(-1, "HOOK"));
    /* Unlike private reports, ordinary shrub chat passes through Lua first:
     * consuming it prevents both native dispatch and the chat broadcast. */
    reset_messages(); snprintf(level3.flags, sizeof(level3.flags), "hR9");
    setargs(2, "say", "!warn 1 swallowed by lua"); client_command(0);
    CHECK(message_contains(-1, "HOOK") && !message_contains(1, "swallowed by lua"));
    reset_messages(); setargs(2, "say_team", "!warn 1 swallowed team"); client_command(0);
    CHECK(message_contains(-1, "HOOK") && !message_contains(1, "swallowed team"));
    stop_vm(vm);
    reset_messages(); setargs(1, "warnings"); client_command(1);
    CHECK(message_contains(1, "0 saved") && !message_contains(1, "swallowed"));
}

static void test_lua_fields(void) {
    qboolean (*start_vm)(lua_vm_t *) = sym("G_LuaStartVM");
    void (*stop_vm)(lua_vm_t *) = sym("G_LuaStopVM");
    static gitem_t item;
    lua_vm_t *vm;
    const char *script =
        "for _, name in ipairs({'gentity_get','gentity_set','FindMod','IPCSend',\n"
        " 'MutePlayer','UnmutePlayer','G_Say','ClientUserinfoChanged','G_Sound',\n"
        " 'G_ClientSound','G_Damage','G_AddSkillPoints','G_LoseSkillPoints','G_AddEvent',\n"
        " 'G_shrubbot_permission','G_shrubbot_level','G_shrubbot_setlevel_lua',\n"
        " 'G_XP_Set','G_ResetXP','G_FreeEntity','trap_LinkEntity','trap_UnlinkEntity',\n"
        " 'trap_FS_Read','trap_FS_Write'}) do assert(type(et[name]) == 'function', name) end\n"
        "assert(et.gentity_get(0, 'enemy') == 2)\n"
        "assert(et.gentity_get(0, 'speed') == 2.5)\n"
        "assert(et.gentity_get(0, 'item') == 'weapon_test')\n"
        "assert(et.gentity_get(0, 'ps.stats', 0) == 123)\n"
        "assert(not pcall(et.gentity_get, -1, 'health'))\n"
        "assert(not pcall(et.gentity_get, 9999999, 'health'))\n"
        "assert(not pcall(et.gentity_get, 0, 'ps.stats', -1))\n"
        "assert(not pcall(et.gentity_get, 0, 'ps.stats', 16))\n"
        "et.gentity_set(0, 'ps.stats', 15, 77)\n"
        "assert(not pcall(et.gentity_set, 0, 'ps.stats', 16, 88))\n"
        "assert(not pcall(et.gentity_set, -1, 'health', 10))\n"
        "local n, s = et.FindMod(-1); assert(n == nil and s == nil)\n"
        "n, s = et.FindMod(9999); assert(n == nil and s == nil)\n"
        "assert(et.IPCSend(-1, 'x') == 0 and et.IPCSend(9999, 'x') == 0)\n"
        "assert(not pcall(et.MutePlayer, -1, 1))\n"
        "assert(not pcall(et.UnmutePlayer, 64))\n"
        "assert(not pcall(et.G_Say, -1, et.SAY_ALL, 'x'))\n"
        "assert(not pcall(et.ClientUserinfoChanged, 64))\n"
        "assert(not pcall(et.G_Sound, 1024, 1))\n"
        "assert(not pcall(et.G_ClientSound, -1, 1))\n"
        "assert(not pcall(et.G_Damage, -1, 0, 0, 10, 0, 0))\n"
        "assert(not pcall(et.G_Damage, 0, 0, 0, 10, 0, 9999))\n"
        "assert(not pcall(et.G_AddSkillPoints, 0, 99, 1))\n"
        "assert(not pcall(et.G_LoseSkillPoints, -1, 0, 1))\n"
        "assert(not pcall(et.G_AddEvent, 1024, 1, 1))\n"
        "assert(not pcall(et.G_shrubbot_permission, 64, 'h'))\n"
        "assert(not pcall(et.G_shrubbot_level, 64))\n"
        "assert(not pcall(et.G_shrubbot_setlevel_lua, -1, 1))\n"
        "assert(not pcall(et.G_XP_Set, -1, 1, 0, false))\n"
        "assert(not pcall(et.G_ResetXP, 64))\n"
        "assert(not pcall(et.G_FreeEntity, -1))\n"
        "assert(not pcall(et.G_FreeEntity, 0))\n"
        "assert(not pcall(et.trap_LinkEntity, 1024))\n"
        "assert(not pcall(et.trap_UnlinkEntity, -1))\n"
        "assert(not pcall(et.trap_FS_Read, 1, -1))\n"
        "assert(not pcall(et.trap_FS_Read, 1, 16777217))\n"
        "assert(not pcall(et.trap_FS_Write, 'abc', -1, 1))\n"
        "assert(not pcall(et.trap_FS_Write, 'abc', 4, 1))\n"
        "assert(et.trap_FS_Read(1, 4) == string.char(65, 0, 66, 0))\n"
        "assert(et.trap_FS_Write(string.char(65, 0, 66), 3, 1) == 3)\n"
        "et.G_LogPrint('Lua literal %s %n %p')\n"
        "et.gentity_set(0, 'pers.netname', 'MuchLongerPlayerName')\n"
        "assert(et.gentity_get(0, 'pers.netname') == 'MuchLongerPlayerName')\n"
        "et.gentity_set(0, 'classname', 'first_class')\n"
        "assert(et.gentity_get(0, 'classname') == 'first_class')\n"
        "et.G_SetSpawnVar(0, 'classname', 'second_class')\n"
        "assert(et.G_GetSpawnVar(0, 'classname') == 'second_class')\n";
    setup_clients();
    ents[0].enemy = &ents[2];
    ents[0].speed = 2.5f;
    item.classname = "weapon_test"; ents[0].item = &item;
    ents[0].classname = "static_original";
    clients[0].ps.stats[0] = 123;
    vm = calloc(1, sizeof(*vm)); CHECK(vm);
    vm->id = -1;
    snprintf(vm->file_name, sizeof(vm->file_name), "regression.lua");
    vm->code = strdup(script); CHECK(vm->code);
    vm->code_size = (int)strlen(vm->code);
    CHECK(start_vm(vm));
    CHECK(strstr(console_output, "Lua literal %s %n %p"));
    CHECK(stored_len == 3 && !memcmp(stored, "A\0B", 3));
    CHECK(clients[0].ps.stats[15] == 77);
    CHECK(!strcmp(clients[0].pers.netname, "MuchLongerPlayerName"));
    CHECK(!strcmp(ents[0].classname, "second_class"));
    stop_vm(vm);
}

/* The hook result must be consumed on both early-return and fall-through
 * paths, as well as on Lua exceptions. Repeated calls used to leak slots. */
static void test_lua_hook_stack(void) {
    qboolean (*start_vm)(lua_vm_t *) = sym("G_LuaStartVM");
    void (*stop_vm)(lua_vm_t *) = sym("G_LuaStopVM");
    int (*gettop)(lua_State *) = sym("lua_gettop");
    qboolean (*connect_hook)(int, qboolean, qboolean, char *) = sym("G_LuaHook_ClientConnect");
    qboolean (*client_hook)(int, char *) = sym("G_LuaHook_ClientCommand");
    qboolean (*console_hook)(char *) = sym("G_LuaHook_ConsoleCommand");
    qboolean (*upgrade_hook)(int, skillType_t) = sym("G_LuaHook_UpgradeSkill");
    qboolean (*setskill_hook)(int, skillType_t) = sym("G_LuaHook_SetPlayerSkill");
    qboolean (*obit_hook)(int, int, int) = sym("G_LuaHook_Obituary");
    lua_vm_t **vms = sym("lVM"), *vm;
    char reason[MAX_STRING_CHARS];
    int i, top;
    const char *script =
        "local function number(n, yes) if n == 1 then return yes "
        "elseif n == 2 then error('hook failure') else return nil end end\n"
        "function et_ClientConnect(n) "
        "if n == 1 then return 'denied' elseif n == 2 then error('hook failure') end end\n"
        "function et_ClientCommand(n) return number(n, 1) end\n"
        "function et_ConsoleCommand(c) "
        "if c == 'yes' then return 1 elseif c == 'fail' then error('hook failure') end end\n"
        "function et_UpgradeSkill(n) return number(n, -1) end\n"
        "function et_SetPlayerSkill(n) return number(n, -1) end\n"
        "function et_Obituary(n) "
        "if n == 1 then return 'handled' elseif n == 2 then error('hook failure') end end\n";
    setup_clients();
    vm = calloc(1, sizeof(*vm)); CHECK(vm);
    vm->id = 0;
    snprintf(vm->file_name, sizeof(vm->file_name), "hooks.lua");
    vm->code = strdup(script); CHECK(vm->code);
    vm->code_size = (int)strlen(vm->code);
    CHECK(start_vm(vm));
    vms[0] = vm;
    top = gettop(vm->L);
#define CHECK_HOOK(expr, expected) do { \
    CHECK((expr) == (expected)); CHECK(gettop(vm->L) == top); \
} while (0)
    for (i = 0; i < 30; ++i) {
        CHECK_HOOK(connect_hook(0, qtrue, qfalse, reason), qfalse);
        CHECK_HOOK(connect_hook(1, qtrue, qfalse, reason), qtrue);
        CHECK(!strcmp(reason, "denied"));
        CHECK_HOOK(connect_hook(2, qtrue, qfalse, reason), qfalse);
        CHECK_HOOK(client_hook(0, "test"), qfalse);
        CHECK_HOOK(client_hook(1, "test"), qtrue);
        CHECK_HOOK(client_hook(2, "test"), qfalse);
        CHECK_HOOK(console_hook("no"), qfalse);
        CHECK_HOOK(console_hook("yes"), qtrue);
        CHECK_HOOK(console_hook("fail"), qfalse);
        CHECK_HOOK(upgrade_hook(0, 0), qfalse);
        CHECK_HOOK(upgrade_hook(1, 0), qtrue);
        CHECK_HOOK(upgrade_hook(2, 0), qfalse);
        CHECK_HOOK(setskill_hook(0, 0), qfalse);
        CHECK_HOOK(setskill_hook(1, 0), qtrue);
        CHECK_HOOK(setskill_hook(2, 0), qfalse);
        CHECK_HOOK(obit_hook(0, 0, 0), qfalse);
        CHECK_HOOK(obit_hook(1, 0, 0), qtrue);
        CHECK_HOOK(obit_hook(2, 0, 0), qfalse);
    }
#undef CHECK_HOOK
    stop_vm(vm);
    CHECK(vms[0] == NULL);
}

struct lua_dump_buffer { unsigned char *bytes; size_t used, capacity; };
static int save_lua_chunk(lua_State *L, const void *data, size_t len, void *opaque) {
    struct lua_dump_buffer *out = opaque;
    (void)L;
    if (len > out->capacity - out->used) return 1;
    memcpy(out->bytes + out->used, data, len);
    out->used += len;
    return 0;
}

/* Generate real Lua 5.1 bytecode chunks which share a NUL-containing
 * header, then check that only the exact allowlisted digest loads. */
static void test_lua_module_loading(void) {
    lua_State *(*newstate)(void) = sym("luaL_newstate");
    void (*close_lua)(lua_State *) = sym("lua_close");
    int (*loadbuffer)(lua_State *, const char *, size_t, const char *) = sym("luaL_loadbuffer");
    int (*dump)(lua_State *, lua_Writer, void *) = sym("lua_dump");
    void (*settop)(lua_State *, int) = sym("lua_settop");
    char *(*sha1_buffer)(const void *, unsigned int) = sym("G_SHA1Buffer");
    char *(*sha1_string)(char *) = sym("G_SHA1");
    qboolean (*init_lua)(void) = sym("G_LuaInit");
    void (*shutdown_lua)(void) = sym("G_LuaShutdown");
    vmCvar_t *modules = sym("lua_modules"), *allowed = sym("lua_allowedModules");
    lua_vm_t **vms = sym("lVM");
    lua_State *L;
    const char *programs[] = {"fixture = 1", "fixture = 2"};
    char first_hash[41], second_hash[41];
    const unsigned char *nul0, *nul1;
    int i;
    setup_clients();
    CHECK(!strcmp(sha1_buffer("abc\0def", 7), "487B1975D97215516D7267DFF3557C0676956056"));
    CHECK(!strcmp(sha1_string("abc"), "A9993E364706816ABA3E25717850C26C9CD0D89D"));
    L = newstate(); CHECK(L);
    for (i = 0; i < 2; ++i) {
        struct lua_dump_buffer out = {lua_chunks[i], 0, sizeof(lua_chunks[i])};
        CHECK(loadbuffer(L, programs[i], strlen(programs[i]), "fixture") == 0);
        CHECK(dump(L, save_lua_chunk, &out) == 0);
        CHECK(out.used > 0 && out.used < sizeof(lua_chunks[i]));
        lua_chunk_len[i] = out.used;
        settop(L, 0);
    }
    close_lua(L);
    nul0 = memchr(lua_chunks[0], 0, lua_chunk_len[0]);
    nul1 = memchr(lua_chunks[1], 0, lua_chunk_len[1]);
    CHECK(nul0 && nul1 && nul0 - lua_chunks[0] == nul1 - lua_chunks[1]);
    CHECK(!memcmp(lua_chunks[0], lua_chunks[1], (size_t)(nul0 - lua_chunks[0])));
    snprintf(first_hash, sizeof(first_hash), "%s", sha1_buffer(lua_chunks[0], (unsigned)lua_chunk_len[0]));
    snprintf(second_hash, sizeof(second_hash), "%s", sha1_buffer(lua_chunks[1], (unsigned)lua_chunk_len[1]));
    CHECK(strcmp(first_hash, second_hash));

    snprintf(modules->string, sizeof(modules->string), ",  \tfirst.lua;,, second.lua;; \t");
    snprintf(allowed->string, sizeof(allowed->string), "%s", first_hash);
    lua_fixture_active = 1; lua_module_opens = lua_invalid_opens = 0;
    CHECK(init_lua());
    CHECK(lua_module_opens == 2 && lua_invalid_opens == 0);
    CHECK(vms[0] && !vms[1]);
    CHECK(!strcmp(vms[0]->file_name, "first.lua"));
    CHECK(!strcmp(vms[0]->mod_signature, first_hash));
    shutdown_lua();
    CHECK(vms[0] == NULL);

    snprintf(modules->string, sizeof(modules->string), "  ,;\t ");
    lua_module_opens = lua_invalid_opens = 0;
    CHECK(init_lua());
    CHECK(lua_module_opens == 0 && lua_invalid_opens == 0 && vms[0] == NULL);
    shutdown_lua();
    lua_fixture_active = 0;
    modules->string[0] = allowed->string[0] = '\0';
}
#endif

int main(int argc, char **argv) {
    void (*entry)(intptr_t (*)(intptr_t, ...));
    CHECK(argc == 2);
    module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!module) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    entry = (void (*)(intptr_t (*)(intptr_t, ...)))sym("dllEntry");
    entry(fake_syscall);
    lev = sym("level"); ents = sym("g_entities"); clients = sym("g_clients");
    teams = sym("teamInfo"); levels = sym("g_shrubbot_levels"); admins = sym("g_shrubbot_admins");
    bans = sym("g_shrubbot_bans");
    shrub = sym("g_shrubbot"); tyranny = sym("g_tyranny");
    reportfile = sym("g_ReportFile"); viewlevel = sym("g_viewreport_level"); adminlog = sym("g_logAdmin");
    game_state = sym("g_gamestate"); warmup_damage = sym("match_warmupDamage");
    check_command = sym("G_shrubbot_cmd_check"); say_command = sym("Cmd_Say_f");
    client_command = sym("ClientCommand"); find_clients = sym("ClientNumbersFromString");
    bit_set = sym("COM_BitSet"); bit_clear = sym("COM_BitClear"); bit_check = sym("COM_BitCheck");
    if (getenv("NEWMOD_FAULT_ONLY")) { test_config_fault_injection(); dlclose(module); return 0; }
    test_shrubbot_config_reload(); test_mapvote_config_roundtrip(); test_showbans_paging(); test_mute_commands();
    test_ban_creation();
    test_vectors(); test_shrub_chat_tokenization(); test_string_replacement(); test_custom_command_dispatch();
    test_campaign_fallback_separator();
    test_bits(); test_matches(); test_wars(); test_mass_and_spec(); test_spawn_rejections();
    test_console_print_safety(); test_safe_formatting_and_client_input();
    test_session_data(); test_team_packets(); test_game_packet_envelope(); test_xpsave_paths();
    test_crazy_gravity(); test_shake(); test_splat_and_smite();
    test_firegod_toggle();
    test_real_splat_death(); test_real_giba_death(); test_map_spree_record(); test_information_and_news();
    test_imported_information_commands(); test_paginated_reports(); test_private_reports();
    test_connection_warnings(); test_seen_command(); test_map_init_cache_entry();
#ifdef ET_LUA
    test_lua_report_dispatch();
    test_lua_fields();
    test_lua_hook_stack();
    test_lua_module_loading();
#endif
    test_shutdown_gravity();
    puts("game command/permission/report/packet/XP/Lua tests passed");
    dlclose(module);
    return 0;
}
