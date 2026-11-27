/* NewMod, Arena.ai, 2026-11-21: exercise the experimental share needle in
 * actual native modules with fake engine callbacks. Compile separately for
 * GAMEDLL and CGAMEDLL: their pmoveExt_t layouts are intentionally different.
 * Synthetic traces/scripts are not a real-client or media integration test. */
#include <dlfcn.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef GAMEDLL
#include "src/game/g_local.h"
#else
#include "src/cgame/cg_local.h"
#endif

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)
#define VM_END ((intptr_t)-1337)

static const char *nq_item_names[] = {
#include "nq_item_order.inc"
};
static void *module;
static animScriptData_t *scriptdata;
static animModelInfo_t *model;
static bg_character_t character;
static weapontable_t *weapon_table;
static weaponStatsInfo_t *weapon_info;
static void (*set_bit)(int *, int);
static void (*clear_bit)(int *, int);
static qboolean (*check_bit)(const int *, int);
static int (*move_player)(pmove_t *);
static int pc_pos, pc_loads, pc_frees;
#ifndef GAMEDLL
static int sound_calls, last_sound, last_sound_entity, last_sound_channel, last_sound_volume;
#endif
static char pc_file[MAX_QPATH];
static const char *pc_tokens[] = {
    "weaponDef", "{", "both", "{", "name", "Revival Needle", "}", "client", "{", "}", "}"
};
#ifdef GAMEDLL
static level_locals_t *lev;
static gentity_t *ents;
static gclient_t *clients;
static vmCvar_t *medics, *adrenaline, *war, *insanity, *weapons;
static void (*events)(gentity_t *, int);
static void (*to_entity)(playerState_t *, entityState_t *, int, qboolean);
static trace_t injection_traces[2];
static int trace_count, point_contents;
static int command_argc, command_messages, follow_fixture;
static char command_reply[512];
static const char *command_name = "adrenother", *command_argument = "extra";
static float trace_lengths[2];
static vec3_t trace_starts[2], trace_ends[2];
#endif

static void *sym(const char *name) {
    void *p = dlsym(module, name);
    if (!p) { fprintf(stderr, "missing %s: %s\n", name, dlerror()); exit(1); }
    return p;
}

static intptr_t fake_syscall(intptr_t op, ...) {
    intptr_t a[17];
    int n = 0;
    va_list ap;
    va_start(ap, op);
    for (;;) {
        intptr_t v = va_arg(ap, intptr_t);
        if (v == VM_END) break;
        CHECK(n < 16); a[n++] = v;
    }
    va_end(ap);
#ifdef GAMEDLL
#define PC_LOAD BOTLIB_PC_LOAD_SOURCE
#define PC_READ BOTLIB_PC_READ_TOKEN
#define PC_FREE BOTLIB_PC_FREE_SOURCE
#define SNAP G_SNAPVECTOR
#define PRINT G_PRINT
#define ERROR G_ERROR
#else
#define PC_LOAD CG_PC_LOAD_SOURCE
#define PC_READ CG_PC_READ_TOKEN
#define PC_FREE CG_PC_FREE_SOURCE
#define SNAP CG_SNAPVECTOR
#define PRINT CG_PRINT
#define ERROR CG_ERROR
#endif
    switch (op) {
    case PC_LOAD:
        CHECK(n == 1);
        snprintf(pc_file, sizeof(pc_file), "%s", (const char *)a[0]);
        pc_pos = 0; ++pc_loads; return 1;
    case PC_READ: {
        pc_token_t *token = (pc_token_t *)a[1];
        CHECK(n == 2 && a[0] == 1);
        if (pc_pos == (int)(sizeof(pc_tokens) / sizeof(pc_tokens[0]))) return 0;
        memset(token, 0, sizeof(*token));
        snprintf(token->string, sizeof(token->string), "%s", pc_tokens[pc_pos++]);
        return 1;
    }
    case PC_FREE: CHECK(n == 1 && a[0] == 1); ++pc_frees; return 0;
    case SNAP: {
        float *v = (float *)a[0]; int i;
        CHECK(n == 1);
        for (i = 0; i < 3; ++i) v[i] = roundf(v[i]);
        return 0;
    }
    case PRINT: CHECK(n == 1); return 0;
    case ERROR: CHECK(n == 1); fprintf(stderr, "module error: %s\n", (char *)a[0]); exit(1);
#ifdef GAMEDLL
    case G_ARGC: CHECK(n == 0); return command_argc;
    case G_ARGV:
        CHECK(n == 3 && a[2] > 0);
        snprintf((char *)a[1], (size_t)a[2], "%s", a[0] == 0 ? command_name : command_argument);
        return 0;
    case G_SEND_SERVER_COMMAND:
        CHECK(n == 2 && (a[0] == 0 || (follow_fixture && a[0] >= -1 && a[0] < MAX_CLIENTS)));
        ++command_messages;
        snprintf(command_reply, sizeof(command_reply), "%s", (const char *)a[1]);
        return 0;
    case G_GET_CONFIGSTRING:
        CHECK(follow_fixture && n == 3 && a[2] > 0);
        snprintf((char *)a[1], (size_t)a[2], "%s", ""); return 0;
    case G_SET_CONFIGSTRING: CHECK(follow_fixture && n == 2); return 0;
    case G_GET_USERINFO:
        CHECK(follow_fixture && n == 3 && a[2] > 0);
        snprintf((char *)a[1], (size_t)a[2], "\\name\\Viewer\\rate\\25000\\ip\\10.0.0.2:27960");
        return 0;
    case G_SET_USERINFO: CHECK(follow_fixture && n == 2); return 0;
    case G_GET_USERCMD: {
        usercmd_t *cmd = (usercmd_t *)a[1];
        CHECK(follow_fixture && n == 2 && a[0] == 1);
        memset(cmd, 0, sizeof(*cmd)); cmd->serverTime = lev->time - SERVER_FRAMETIME_DELTA;
        return 0;
    }
    case G_UNLINKENTITY:
        CHECK(follow_fixture && n == 1); ((gentity_t *)a[0])->r.linked = qfalse; return 0;
    case G_TRACE: {
        trace_t *tr = (trace_t *)a[0];
        const float *start = (const float *)a[1], *end = (const float *)a[4];
        int idx = trace_count++;
        float dx = end[0] - start[0], dy = end[1] - start[1], dz = end[2] - start[2];
        CHECK(n == 7 && idx < 2 && a[2] == 0 && a[3] == 0 && a[5] == 0 && a[6] == MASK_SHOT);
        *tr = injection_traces[idx];
        trace_lengths[idx] = sqrtf(dx * dx + dy * dy + dz * dz);
        memcpy(trace_starts[idx], start, sizeof(vec3_t));
        memcpy(trace_ends[idx], end, sizeof(vec3_t));
        memcpy(tr->endpos, end, sizeof(vec3_t));
        return 0;
    }
    case G_POINT_CONTENTS: CHECK(n == 2); return point_contents;
#else
    case CG_S_STARTSOUND:
        CHECK(n == 5); ++sound_calls; last_sound = (int)a[3];
        last_sound_entity = (int)a[1]; last_sound_channel = (int)a[2]; last_sound_volume = (int)a[4];
        return 0;
    case CG_S_UPDATEENTITYPOSITION: CHECK(n == 2); return 0;
#endif
    default: fprintf(stderr, "unexpected module syscall %ld (%d args)\n", (long)op, n); exit(1);
    }
}

static void empty_trace(trace_t *tr, const vec3_t start, const vec3_t mins,
                        const vec3_t maxs, const vec3_t end, int pass, int mask) {
    (void)start; (void)mins; (void)maxs; (void)pass; (void)mask;
    memset(tr, 0, sizeof(*tr)); tr->fraction = 1.0f; tr->entityNum = ENTITYNUM_NONE;
    memcpy(tr->endpos, end, sizeof(vec3_t));
}
static int empty_contents(const vec3_t p, int pass) { (void)p; (void)pass; return 0; }
#ifdef GAMEDLL
static void no_voice(int client, const char *chat) { (void)client; (void)chat; }
#endif

static void common_init(void) {
    void (*entry)(intptr_t (*)(intptr_t, ...)) = sym("dllEntry");
    void (*parse)(animModelInfo_t *, animScriptData_t *, const char *, char *) = sym("BG_AnimParseAnimScript");
    char empty[] = "";
    entry(fake_syscall);
    ((void (*)(void))sym("Init_FindClipForWeapon"))();
    ((void (*)(void))sym("Init_FindAmmoForWeapon"))();
    set_bit = sym("COM_BitSet"); clear_bit = sym("COM_BitClear"); check_bit = sym("COM_BitCheck");
    move_player = sym("Pmove");
    weapon_table = sym("WeaponTable"); weapon_info = sym("BG_Weapons");
    scriptdata = calloc(1, sizeof(*scriptdata)); model = calloc(1, sizeof(*model));
    CHECK(scriptdata && model);
    memset(&character, 0, sizeof(character)); character.animModelInfo = model;
    parse(model, scriptdata, "synthetic-empty.anim", empty);
#ifdef GAMEDLL
    lev = sym("level"); ents = sym("g_entities"); clients = sym("g_clients");
    medics = sym("g_medics"); adrenaline = sym("g_adrenaline"); war = sym("nq_War");
    insanity = sym("jp_insanity"); weapons = sym("g_weapons");
    events = sym("ClientEvents"); to_entity = sym("BG_PlayerStateToEntityState");
    ((vmCvar_t *)sym("sv_fps"))->integer = 20;
#else
    ((vmCvar_t *)sym("cg_insanity"))->integer = 0;
#endif
}

static void test_registration(void) {
    gitem_t *items = sym("bg_itemlist"), *share;
    int *num_items = sym("bg_numItems"), *alts = sym("weapAlts"), *teamconv = sym("weapTeamConversion");
    animStringItem_t *strings = sym("weaponStrings");
    gitem_t *(*find_weapon)(weapon_t) = sym("BG_FindItemForWeapon");
    gitem_t *(*find_class)(const char *, long) = sym("BG_FindItemForClassName");
    gitem_t *(*find_ammo_item)(int) = sym("BG_FindItemForAmmo");
    weapon_t (*ammo_for)(weapon_t) = sym("BG_FindAmmoForWeapon");
    weapon_t (*clip_for)(weapon_t) = sym("BG_FindClipForWeapon");
    int (*max_ammo)(weapon_t, int *, int) = sym("BG_MaxAmmoForWeapon");
    int (*extra_ammo)(weapon_t, int *, int) = sym("BG_ExtraAmmoForWeapon");
    long (*hash)(const char *) = sym("BG_StringHashValue");
    int (*find_string)(char *, animStringItem_t *, qboolean) = sym("BG_IndexForString");
    size_t i;
    CHECK(WP_ADRENALINE_SHARE == 63 && WP_NUM_WEAPONS <= MAX_WEAPONS);
    CHECK(IL_TEAM_TEAM_CTF_BLUEFLAG == (int)(sizeof(nq_item_names) / sizeof(nq_item_names[0])));
    CHECK(IL_WEAPON_ADRENALINE_SHARE == IL_TEAM_TEAM_CTF_BLUEFLAG + 1);
    CHECK(*num_items == IL_NUM_ITEMS && !items[*num_items].classname);
    for (i = 0; i < sizeof(nq_item_names) / sizeof(nq_item_names[0]); ++i)
        CHECK(items[i + 1].classname && !strcmp(items[i + 1].classname, nq_item_names[i]));
    share = find_weapon(WP_ADRENALINE_SHARE);
    CHECK(share == &items[IL_WEAPON_ADRENALINE_SHARE]);
    CHECK(share->classnamehash == hash(share->classname));
    CHECK(find_class("weapon_adrenaline_share", 0) == share);
    CHECK(share->giType == IT_WEAPON && share->giTag == WP_ADRENALINE_SHARE);
    { entityState_t item; playerState_t player; int skill[SK_NUM_SKILLS] = {0};
      qboolean (*can_grab)(const entityState_t *, const playerState_t *, int *, int) = sym("BG_CanItemBeGrabbed");
      memset(&item, 0, sizeof(item)); memset(&player, 0, sizeof(player));
      item.modelindex = IL_WEAPON_ADRENALINE_SHARE;
      CHECK(!can_grab(&item, &player, skill, TEAM_AXIS)); }
    CHECK(weapon_table[WP_ADRENALINE_SHARE].clipindex == WP_MEDIC_ADRENALINE);
    CHECK(weapon_table[WP_ADRENALINE_SHARE].ammoindex == WP_MEDIC_ADRENALINE);
    CHECK(ammo_for(WP_ADRENALINE_SHARE) == WP_MEDIC_ADRENALINE);
    CHECK(clip_for(WP_ADRENALINE_SHARE) == WP_MEDIC_ADRENALINE);
    CHECK(find_ammo_item(WP_MP40) == &items[IL_AMMO_MP40]);
    CHECK(find_ammo_item(WP_MEDIC_SYRINGE) == &items[IL_AMMO_SYRINGE]);
    CHECK(find_ammo_item(WP_MEDIC_ADRENALINE) == NULL);
    CHECK(find_ammo_item(WP_ADRENALINE_SHARE) == NULL);
    { int skill[SK_NUM_SKILLS] = {0};
      skill[SK_MEDIC] = 0x1e;
#ifdef GAMEDLL
      adrenaline->integer = 0;
#else
      ((cgs_t *)sym("cgs"))->adrenaline = 0;
#endif
      CHECK(max_ammo(WP_ADRENALINE_SHARE, skill, PC_MEDIC) == 12);
      CHECK(extra_ammo(WP_ADRENALINE_SHARE, skill, PC_MEDIC) == 12);
      skill[SK_MEDIC] |= (1 << 6);
      CHECK(max_ammo(WP_ADRENALINE_SHARE, skill, PC_MEDIC) == 15);
#ifdef GAMEDLL
      adrenaline->integer = ADF_JUSTONE;
#else
      ((cgs_t *)sym("cgs"))->adrenaline = ADF_JUSTONE;
#endif
      CHECK(max_ammo(WP_ADRENALINE_SHARE, skill, PC_MEDIC) == 1);
      CHECK(max_ammo(WP_ADRENALINE_SHARE, skill, PC_MEDIC) == max_ammo(WP_MEDIC_ADRENALINE, skill, PC_MEDIC));
#ifdef GAMEDLL
      adrenaline->integer = 0;
#else
      ((cgs_t *)sym("cgs"))->adrenaline = 0;
#endif
    }
    CHECK(alts[WP_ADRENALINE_SHARE] == WP_MEDIC_ADRENALINE && alts[WP_MEDIC_ADRENALINE] == WP_ADRENALINE_SHARE);
    CHECK(teamconv[WP_ADRENALINE_SHARE] == WP_ADRENALINE_SHARE);
    CHECK(((void *(*)(weapon_t))sym("BG_ValidStatWeapon"))(WP_ADRENALINE_SHARE) == NULL);
    CHECK(((int (*)(weapon_t))sym("BG_WeapStatForWeapon"))(WP_ADRENALINE_SHARE) == WS_MAX);
    ((void (*)(void))sym("BG_InitWeaponStrings"))();
    CHECK(strings[WP_ADRENALINE_SHARE].string && !strings[WP_NUM_WEAPONS].string);
    CHECK(find_string("adrenaline_share", strings, qtrue) == WP_ADRENALINE_SHARE);
    CHECK(find_string("not_a_weapon", strings, qtrue) == -1);
    ((void (*)(int, int, int, qboolean))sym("BG_UpdateConditionValue"))(0, ANIM_COND_WEAPON, WP_ADRENALINE_SHARE, qtrue);
    CHECK(((int (*)(int, int, qboolean))sym("BG_GetConditionValue"))(0, ANIM_COND_WEAPON, qtrue) == WP_MEDIC_SYRINGE);
    ((void (*)(int, qboolean))sym("BG_RegisterWeapon"))(WP_ADRENALINE_SHARE, qtrue);
    CHECK(pc_loads == 1 && pc_frees == 1 && !strcmp(pc_file, "weapons/syringe.weap"));
    CHECK(weapon_info[WP_ADRENALINE_SHARE].registered &&
          !strcmp(weapon_info[WP_ADRENALINE_SHARE].name, "Shared adrenaline"));
#ifndef GAMEDLL
    CHECK(((weaponInfo_t *)sym("cg_weapons"))[WP_ADRENALINE_SHARE].registered);
#endif
    CHECK(((int (*)(int))sym("PM_AltSwitchFromForWeapon"))(WP_ADRENALINE_SHARE) == WEAP_DROP);
    CHECK(((int (*)(int))sym("PM_AltSwitchToForWeapon"))(WP_ADRENALINE_SHARE) == WEAP_RAISE);
    CHECK(((int (*)(int))sym("PM_IdleAnimForWeapon"))(WP_ADRENALINE_SHARE) == WEAP_IDLE1);
    CHECK(((int (*)(int))sym("PM_AttackAnimForWeapon"))(WP_ADRENALINE_SHARE) == WEAP_ATTACK1);
}

static void init_move(pmove_t *pm, playerState_t *ps, pmoveExt_t *ext) {
    memset(ps, 0, sizeof(*ps)); memset(ext, 0, sizeof(*ext)); memset(pm, 0, sizeof(*pm));
    ps->clientNum = 0; ps->pm_type = PM_NORMAL; ps->stats[STAT_HEALTH] = 100;
    ps->stats[STAT_MAX_HEALTH] = 100; ps->stats[STAT_PLAYER_CLASS] = PC_MEDIC;
    ps->persistant[PERS_TEAM] = TEAM_AXIS; ps->gravity = 800; ps->speed = 320;
    ps->groundEntityNum = ENTITYNUM_NONE; ps->origin[2] = 128;
    ps->weapon = WP_ADRENALINE_SHARE; ps->weaponstate = WEAPON_READY;
    ps->commandTime = 9900; ps->classWeaponTime = -999999;
    ps->ammoclip[WP_MEDIC_ADRENALINE] = 12;
    set_bit(ps->weapons, WP_MEDIC_ADRENALINE); set_bit(ps->weapons, WP_ADRENALINE_SHARE);
    pm->ps = ps; pm->pmext = ext; pm->character = &character;
    pm->tracemask = MASK_PLAYERSOLID; pm->trace = empty_trace; pm->pointcontents = empty_contents;
    pm->medicChargeTime = 40000; pm->skillBits[SK_MEDIC] = 0x1e;
    pm->cmd.weapon = WP_ADRENALINE_SHARE; pm->pmove_fixed = qtrue; pm->pmove_msec = 50;
#ifdef GAMEDLL
    pm->voice = no_voice;
#endif
}

static int count_fire(const playerState_t *ps) {
    int i, count = 0;
    for (i = 0; i < ps->eventSequence && i < MAX_EVENTS; ++i) {
        int ev = ps->events[i & (MAX_EVENTS - 1)];
        if (ev == EV_FIRE_WEAPON || ev == EV_FIRE_WEAPON_LASTSHOT) ++count;
    }
    return count;
}

static int fire_parm(const playerState_t *ps) {
    int i;
    for (i = 0; i < ps->eventSequence && i < MAX_EVENTS; ++i) {
        int ev = ps->events[i & (MAX_EVENTS - 1)];
        if (ev == EV_FIRE_WEAPON || ev == EV_FIRE_WEAPON_LASTSHOT)
            return ps->eventParms[i & (MAX_EVENTS - 1)];
    }
    return -1;
}

static void run_move(pmove_t *pm, int time, int buttons, int wbuttons) {
    pm->cmd.serverTime = time; pm->cmd.buttons = buttons; pm->cmd.wbuttons = wbuttons;
    move_player(pm);
}

static void test_prediction(void) {
    pmove_t pm; playerState_t ps; pmoveExt_t ext;
    init_move(&pm, &ps, &ext);
    CHECK(check_bit(ps.weapons, 63) && ((unsigned int)ps.weapons[1] & 0x80000000u));
    run_move(&pm, 9950, BUTTON_ATTACK, 0);
    run_move(&pm, 10000, BUTTON_ATTACK, 0);
    CHECK(count_fire(&ps) == 1 && ps.ammoclip[WP_MEDIC_ADRENALINE] == 11);
    CHECK(fire_parm(&ps) == NM_SHARE_FIRE_CONSUMED);
    CHECK(ps.classWeaponTime == -999999); /* server charges only on a valid hit */

    init_move(&pm, &ps, &ext); ps.ammoclip[WP_MEDIC_ADRENALINE] = 1;
    run_move(&pm, 9950, BUTTON_ATTACK, 0); run_move(&pm, 10000, BUTTON_ATTACK, 0);
    CHECK(count_fire(&ps) == 1 && ps.ammoclip[WP_MEDIC_ADRENALINE] == 0);
    CHECK(fire_parm(&ps) == NM_SHARE_FIRE_CONSUMED);

    init_move(&pm, &ps, &ext); ps.classWeaponTime = 9990;
    run_move(&pm, 9950, BUTTON_ATTACK, 0); run_move(&pm, 10000, BUTTON_ATTACK, 0);
    CHECK(count_fire(&ps) == 0 && ps.ammoclip[WP_MEDIC_ADRENALINE] == 12);

    /* Charge checks are shared prediction code in both modules. Reject
     * unrepresentable and invalid costs instead of accepting NaN or overflow. */
    { qboolean (*check_charge)(pmove_t *) = sym("BG_CheckCharge");
      float *cost = sym("chargeCost_Adrenaline"), old = cost[0];
      init_move(&pm, &ps, &ext); pm.cmd.serverTime = 10000; pm.skillBits[SK_MEDIC] = 0;
      pm.medicChargeTime = -1; CHECK(!check_charge(&pm));
      pm.medicChargeTime = 0; CHECK(check_charge(&pm));
      ps.classWeaponTime = 10001; CHECK(!check_charge(&pm));
      ps.classWeaponTime = -999999; pm.medicChargeTime = INT_MAX; cost[0] = 2.0f;
      CHECK(!check_charge(&pm));
      pm.medicChargeTime = 40000; cost[0] = NAN; CHECK(!check_charge(&pm));
      cost[0] = INFINITY; CHECK(!check_charge(&pm));
      cost[0] = old; }

#ifdef GAMEDLL
    insanity->integer = JP_INSANITY_AMMUNITION | JP_INSANITY_CHARGEBAR;
#else
    ((vmCvar_t *)sym("cg_insanity"))->integer = JP_INSANITY_AMMUNITION | JP_INSANITY_CHARGEBAR;
#endif
    init_move(&pm, &ps, &ext); ps.classWeaponTime = 9999; ps.ammoclip[WP_MEDIC_ADRENALINE] = 1;
    run_move(&pm, 9950, BUTTON_ATTACK, 0); run_move(&pm, 10000, BUTTON_ATTACK, 0);
    CHECK(count_fire(&ps) == 1 && ps.ammoclip[WP_MEDIC_ADRENALINE] == 1);
    CHECK(fire_parm(&ps) == NM_SHARE_FIRE_UNLIMITED);
#ifdef GAMEDLL
    insanity->integer = 0;
#else
    ((vmCvar_t *)sym("cg_insanity"))->integer = 0;
#endif

    /* An ordinary shot also identifies its firing weapon; it must remain
     * distinguishable from either share marker and the binocular marker. */
    init_move(&pm, &ps, &ext); ps.weapon = pm.cmd.weapon = WP_MEDIC_ADRENALINE;
    run_move(&pm, 9950, BUTTON_ATTACK, 0); run_move(&pm, 10000, BUTTON_ATTACK, 0);
    CHECK(count_fire(&ps) == 1 && fire_parm(&ps) == NM_FIRE_WEAPON_PARM(WP_MEDIC_ADRENALINE));
    CHECK(!NM_IS_SHARE_FIRE_PARM(fire_parm(&ps)) && fire_parm(&ps) != WP_BINOCULARS);

    /* Produce the inherited binocular marker through actual Pmove in both
     * modules, rather than assuming the delayed-event encoding. */
    init_move(&pm, &ps, &ext);
    ps.weapon = pm.cmd.weapon = WP_MP40;
    ps.stats[STAT_PLAYER_CLASS] = PC_FIELDOPS;
    ps.stats[STAT_KEYS] |= 1 << INV_BINOCS;
    ps.eFlags |= EF_ZOOMING;
    set_bit(ps.weapons, WP_MP40);
    run_move(&pm, 9950, BUTTON_ATTACK, WBUTTON_ZOOM); run_move(&pm, 10000, BUTTON_ATTACK, WBUTTON_ZOOM);
    CHECK(count_fire(&ps) == 1 && fire_parm(&ps) == WP_BINOCULARS && ext.artyFire);
    CHECK(!NM_IS_FIRE_WEAPON_PARM(fire_parm(&ps)) && !NM_IS_SHARE_FIRE_PARM(fire_parm(&ps)));

    /* Switch via the actual shared alt-weapon path. The game emits EV_WEAPALT
     * for cgame selection; both modules must transition to the same weapon. */
    init_move(&pm, &ps, &ext); ps.weapon = pm.cmd.weapon = WP_MEDIC_ADRENALINE;
    run_move(&pm, 9950, 0, WBUTTON_ATTACK2);
#ifdef GAMEDLL
    { int i, found = 0;
      for (i = 0; i < ps.eventSequence && i < MAX_EVENTS; ++i)
          if (ps.events[i] == EV_WEAPALT && ps.eventParms[i] == WP_ADRENALINE_SHARE) found = 1;
      CHECK(found); }
#endif
    pm.oldcmd.wbuttons = WBUTTON_ATTACK2; pm.cmd.weapon = WP_ADRENALINE_SHARE;
    run_move(&pm, 10000, 0, 0); run_move(&pm, 10050, 0, 0);
    CHECK(ps.weapon == WP_ADRENALINE_SHARE && ps.weaponstate == WEAPON_READY);

    /* Removing the inventory bit prevents the alt selection on both sides. */
    init_move(&pm, &ps, &ext); ps.weapon = pm.cmd.weapon = WP_MEDIC_ADRENALINE;
    clear_bit(ps.weapons, WP_ADRENALINE_SHARE);
    run_move(&pm, 9950, 0, WBUTTON_ATTACK2);
    CHECK(ps.weapon == WP_MEDIC_ADRENALINE && ps.nextWeapon != WP_ADRENALINE_SHARE);
}

#ifdef GAMEDLL
static void setup_server(void) {
    int i;
    memset(lev, 0, sizeof(*lev)); memset(ents, 0, sizeof(*ents) * MAX_GENTITIES);
    memset(clients, 0, sizeof(*clients) * MAX_CLIENTS);
    lev->gentities = ents; lev->clients = clients; lev->maxclients = MAX_CLIENTS;
    lev->num_entities = MAX_CLIENTS; lev->medicChargeTime[0] = lev->medicChargeTime[1] = 40000;
    for (i = 0; i < 2; ++i) {
        ents[i].client = &clients[i]; ents[i].s.number = ents[i].s.clientNum = i;
        ents[i].inuse = qtrue; ents[i].health = 100;
        clients[i].ps.clientNum = i; clients[i].ps.stats[STAT_HEALTH] = 100;
        clients[i].pers.connected = CON_CONNECTED; clients[i].sess.sessionTeam = TEAM_AXIS;
    }
    clients[0].sess.playerType = PC_MEDIC; clients[0].sess.skillBits[SK_MEDIC] = 0x1e;
    medics->integer = MEDIC_SHAREADRENALINE; adrenaline->integer = ADF_MEDIC;
    war->integer = insanity->integer = weapons->integer = 0;
    memset(injection_traces, 0, sizeof(injection_traces));
    injection_traces[0].fraction = injection_traces[1].fraction = 0.5f;
    injection_traces[0].entityNum = injection_traces[1].entityNum = 1;
    trace_count = point_contents = follow_fixture = 0;
}

static void server_begin(pmove_t *pm) {
    init_move(pm, &clients[0].ps, &clients[0].pmext);
    clients[0].sess.skillBits[SK_MEDIC] = 0x1e;
}
static void server_step(pmove_t *pm, int time, int buttons, qboolean dispatch) {
    int old = pm->ps->eventSequence;
    lev->time = time; run_move(pm, time, buttons, 0);
    to_entity(pm->ps, &ents[0].s, time, qtrue);
    if (dispatch) events(&ents[0], old);
}
static void server_shot(pmove_t *pm) {
    server_step(pm, 9950, BUTTON_ATTACK, qtrue);
    server_step(pm, 10000, BUTTON_ATTACK, qtrue);
    CHECK(count_fire(&clients[0].ps) == 1);
}

static void test_server_fire(void) {
    pmove_t pm; int i;
    setup_server(); server_begin(&pm);
    clients[1].ps.torsoAnim = 37; clients[1].ps.legsAnim = 19;
    server_shot(&pm);
    CHECK(trace_count == 1 && fabsf(trace_lengths[0] - CH_REVIVE_DIST) < 0.01f);
    CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 11 && clients[0].ps.classWeaponTime == 10000);
    CHECK(clients[1].ps.powerups[PW_ADRENALINE] == 10000 + (int)ADRENALINE_TIME);
    CHECK(clients[1].ps.events[0] == EV_GENERAL_SOUND && clients[1].ps.eventParms[0] == GAMESOUND_MISC_REVIVE);
    CHECK(clients[1].ps.torsoAnim == 37 && clients[1].ps.legsAnim == 19); /* no recipient animation override */
    CHECK(lev->num_entities == MAX_CLIENTS);

    /* Each rejected trace refunds the unit Pmove consumed, including a full
     * 12-needle clip (the inherited maxclip field is only one). */
    for (i = 0; i < 18; ++i) {
        setup_server(); server_begin(&pm);
        switch (i) {
        case 0: injection_traces[0].fraction = 1.0f; break;
        case 1: injection_traces[0].entityNum = -1; break;
        case 2: injection_traces[0].entityNum = MAX_CLIENTS; break;
        case 3: injection_traces[0].entityNum = 0; break;
        case 4: clients[1].sess.sessionTeam = TEAM_ALLIES; break;
        case 5: ents[1].health = 0; break;
        case 6: clients[1].ps.stats[STAT_HEALTH] = 0; break;
        case 7: clients[1].ps.pm_type = PM_DEAD; break;
        case 8: clients[1].ps.pm_flags = PMF_LIMBO; break;
        case 9: clients[1].pers.connected = CON_DISCONNECTED; break;
        case 10: clients[1].ps.powerups[PW_ADRENALINE] = 10001; break;
        case 11: point_contents = CONTENTS_WATER; break;
        case 12: injection_traces[0].allsolid = qtrue; break;
        case 13: injection_traces[0].startsolid = injection_traces[1].startsolid = qtrue; break;
        case 14: injection_traces[0].startsolid = injection_traces[1].allsolid = qtrue; break;
        case 15: clients[1].ps.powerups[PW_ADRENALINE] = -20000; break;
        case 16: clients[1].ps.eFlags |= EF_PLAYDEAD; break;
        case 17: clients[1].ps.pm_type = PM_SPECTATOR; break;
        }
        server_shot(&pm);
        CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12);
        CHECK(clients[0].ps.classWeaponTime == -999999);
        CHECK(clients[1].ps.powerups[PW_ADRENALINE] == (i == 10 ? 10001 : i == 15 ? -20000 : 0));
        CHECK(clients[1].ps.eventSequence == 0);
        if (i == 13 || i == 14) CHECK(trace_count == 2);
    }
    /* Equal or stale positive expiry still awaits ClientEndFrame's cooldown;
     * a share shot must not overwrite it even for one frame. */
    for (i = 0; i < 2; ++i) {
        setup_server(); server_begin(&pm);
        clients[1].ps.powerups[PW_ADRENALINE] = 10000 - i;
        server_shot(&pm);
        CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12);
        CHECK(clients[1].ps.powerups[PW_ADRENALINE] == 10000 - i);
        CHECK(clients[1].ps.eventSequence == 0);
    }
    setup_server(); server_begin(&pm); point_contents = CONTENTS_WATER;
    weapons->integer = WPF_UNDERWATER_SYRINGE; server_shot(&pm);
    CHECK(clients[1].ps.powerups[PW_ADRENALINE] == 20000);
    setup_server(); server_begin(&pm); injection_traces[0].startsolid = qtrue;
    server_shot(&pm);
    CHECK(trace_count == 2 && fabsf(trace_lengths[0] - 64.0f) < 0.01f && fabsf(trace_lengths[1] - 8.0f) < 0.01f);
    CHECK(clients[1].ps.powerups[PW_ADRENALINE] == 20000);

    /* Compare the real NQ FireWeapon -> Weapon_Syringe trace with share,
     * including poison, lean, crouch and the startsolid retry. The ordinary
     * syringe explicitly replaces the poison-adjusted muzzle direction. */
    for (i = 0; i < 4; ++i) {
        vec3_t starts[2], ends[2]; int j, k, expected = (i & 1) ? 2 : 1;
        void (*share_fire)(gentity_t *, qboolean) = sym("G_NM_ShareAdrenalineFire");
        void (*fire_weapon)(gentity_t *) = sym("FireWeapon");
        setup_server(); server_begin(&pm); lev->time = 12345;
        clients[0].ps.eFlags = (i & 2 ? EF_POISONED : 0) | EF_CROUCHING;
        clients[0].ps.viewangles[PITCH] = 36.0f;
        clients[0].ps.viewangles[YAW] = 53.0f;
        clients[0].ps.viewangles[ROLL] = 9.0f;
        clients[0].ps.leanf = 6.0f; clients[0].ps.viewheight = 48;
        ents[0].s.pos.trBase[0] = 12; ents[0].s.pos.trBase[1] = -19; ents[0].s.pos.trBase[2] = 5;
        injection_traces[0].fraction = injection_traces[1].fraction = 1.0f;
        injection_traces[0].startsolid = (i & 1) ? qtrue : qfalse;
        share_fire(&ents[0], qfalse); CHECK(trace_count == expected);
        memcpy(starts, trace_starts, sizeof(starts)); memcpy(ends, trace_ends, sizeof(ends));
        trace_count = 0; ents[0].s.weapon = clients[0].ps.weapon = WP_MEDIC_SYRINGE;
        fire_weapon(&ents[0]); CHECK(trace_count == expected);
        for (j = 0; j < expected; ++j) for (k = 0; k < 3; ++k) {
            CHECK(fabsf(starts[j][k] - trace_starts[j][k]) < 0.001f);
            CHECK(fabsf(ends[j][k] - trace_ends[j][k]) < 0.001f);
        }
    }

    /* Eligibility/charge may change between prediction and the server event.
     * Rejected attempts must restore the already-consumed unit. */
    for (i = 0; i < 3; ++i) {
        int old;
        setup_server(); server_begin(&pm); server_step(&pm, 9950, BUTTON_ATTACK, qtrue);
        old = clients[0].ps.eventSequence;
        server_step(&pm, 10000, BUTTON_ATTACK, qfalse);
        CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 11);
        if (i == 0) medics->integer = 0;
        if (i == 1) war->integer = WARMODE_ENABLE;
        if (i == 2) clients[0].ps.classWeaponTime = 10000;
        events(&ents[0], old);
        CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12 && !clients[1].ps.powerups[PW_ADRENALINE]);
    }
    setup_server(); server_begin(&pm); clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] = 15;
    injection_traces[0].entityNum = ENTITYNUM_WORLD; adrenaline->integer |= ADF_JUSTONE;
    server_shot(&pm); CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 15);

    for (i = 0; i < 2; ++i) {
        setup_server(); server_begin(&pm); insanity->integer = JP_INSANITY_AMMUNITION | JP_INSANITY_CHARGEBAR;
        clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] = 1; clients[0].ps.classWeaponTime = 9999;
        if (i == 0) injection_traces[0].entityNum = ENTITYNUM_WORLD;
        server_shot(&pm);
        CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 1 && clients[0].ps.classWeaponTime == 9999);
        CHECK(!!clients[1].ps.powerups[PW_ADRENALINE] == i);
    }

    /* Dispatch must use the shot's event tag, not the final selected weapon.
     * A mounted/dead/revoked shooter is rejected, and the marker records the
     * deduction even if unlimited-ammo settings change after prediction. */
    for (i = 0; i < 7; ++i) {
        int old;
        setup_server(); server_begin(&pm);
        if (i == 4) insanity->integer = JP_INSANITY_AMMUNITION;
        server_step(&pm, 9950, BUTTON_ATTACK, qtrue);
        old = clients[0].ps.eventSequence;
        server_step(&pm, 10000, BUTTON_ATTACK, qfalse);
        if (i == 0) { clients[0].ps.weapon = WP_MP40; ents[0].s.weapon = WP_MP40; }
        if (i == 1) clients[0].ps.eFlags |= EF_MOUNTEDTANK;
        if (i == 2) clients[0].ps.persistant[PERS_HWEAPON_USE] = 1;
        if (i == 3) { insanity->integer = JP_INSANITY_AMMUNITION; injection_traces[0].entityNum = ENTITYNUM_WORLD; }
        if (i == 4) { insanity->integer = 0; injection_traces[0].entityNum = ENTITYNUM_WORLD; }
        if (i == 5) clear_bit(clients[0].ps.weapons, WP_ADRENALINE_SHARE);
        if (i == 6) { clients[0].ps.stats[STAT_HEALTH] = 0; ents[0].health = 0; }
        events(&ents[0], old);
        CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == (i == 0 ? 11 : 12));
        CHECK(clients[1].ps.powerups[PW_ADRENALINE] == (i == 0 ? 20000 : 0));
        if (i == 1 || i == 2 || i == 5 || i == 6) CHECK(trace_count == 0);
    }
    setup_server(); server_begin(&pm);
    clients[0].ps.events[0] = EV_FIRE_WEAPON; clients[0].ps.eventParms[0] = 0;
    clients[0].ps.eventSequence = 1; ents[0].s.weapon = WP_ADRENALINE_SHARE;
    events(&ents[0], 0); CHECK(trace_count == 0 && !clients[1].ps.powerups[PW_ADRENALINE]);

    /* A real predicted self-injection followed by a switch to share must
     * still apply to the shooter, not be silently lost or inject a teammate.
     * Its weapon and selection are restored after regular dispatch. */
    setup_server(); server_begin(&pm); clients[0].ps.weapon = pm.cmd.weapon = WP_MEDIC_ADRENALINE;
    server_step(&pm, 9950, BUTTON_ATTACK, qtrue);
    { int old = clients[0].ps.eventSequence;
      server_step(&pm, 10000, BUTTON_ATTACK, qfalse);
      CHECK(fire_parm(&clients[0].ps) == NM_FIRE_WEAPON_PARM(WP_MEDIC_ADRENALINE));
      clients[0].ps.weapon = ents[0].s.weapon = WP_ADRENALINE_SHARE;
      clients[0].ps.eFlags |= EF_ZOOMING; /* changed after the ordinary shot */
      events(&ents[0], old);
      CHECK(clients[0].ps.powerups[PW_ADRENALINE] == 20000);
      CHECK(!clients[1].ps.powerups[PW_ADRENALINE] && trace_count == 0);
      CHECK(clients[0].ps.eFlags & EF_ZOOMING);
      CHECK(clients[0].ps.weapon == WP_ADRENALINE_SHARE && ents[0].s.weapon == WP_ADRENALINE_SHARE); }

    /* A binocular event remains ordinary artillery when the final weapon was
     * changed to share; use warmode to stop before creating artillery entities. */
    setup_server(); server_begin(&pm); lev->time = 10000;
    ((vmCvar_t *)sym("g_noAttacklInvul"))->integer = 1;
    war->integer = WARMODE_ENABLE; clients[0].sess.playerType = PC_FIELDOPS;
    clients[0].ps.stats[STAT_PLAYER_CLASS] = PC_FIELDOPS;
    clients[0].ps.stats[STAT_KEYS] |= 1 << INV_BINOCS;
    clients[0].ps.eFlags |= EF_ZOOMING;
    clients[0].ps.powerups[PW_INVULNERABLE] = 12345;
    clients[0].ps.events[0] = EV_FIRE_WEAPON; clients[0].ps.eventParms[0] = WP_BINOCULARS;
    clients[0].ps.eventSequence = 1; ents[0].s.weapon = WP_ADRENALINE_SHARE;
    events(&ents[0], 0);
    CHECK(!clients[0].ps.powerups[PW_INVULNERABLE] && trace_count == 0);
    CHECK(ents[0].s.weapon == WP_ADRENALINE_SHARE && clients[0].ps.weapon == WP_ADRENALINE_SHARE);
    ((vmCvar_t *)sym("g_noAttacklInvul"))->integer = 0;

    /* Produce a binocular marker with Pmove, then dispatch it after the
     * final playerstate has unzoomed and switched to share. The no-impact
     * trace stops actual artillery after charging, before bombs spawn.
     * Current class, equipment and mount/lean eligibility still win. */
    for (i = 0; i < 5; ++i) {
        setup_server(); server_begin(&pm); lev->time = 50000;
        lev->lieutenantChargeTime[0] = pm.ltChargeTime = 40000;
        clients[0].sess.playerType = PC_FIELDOPS;
        clients[0].ps.stats[STAT_PLAYER_CLASS] = PC_FIELDOPS;
        clients[0].ps.stats[STAT_KEYS] |= 1 << INV_BINOCS;
        clients[0].ps.classWeaponTime = 10000;
        clients[0].ps.commandTime = 49900;
        clients[0].ps.weapon = pm.cmd.weapon = WP_MP40;
        set_bit(clients[0].ps.weapons, WP_MP40);
        run_move(&pm, 49950, BUTTON_ATTACK, WBUTTON_ZOOM);
        run_move(&pm, 50000, BUTTON_ATTACK, WBUTTON_ZOOM);
        CHECK(count_fire(&clients[0].ps) == 1 && fire_parm(&clients[0].ps) == WP_BINOCULARS);
        clients[0].ps.eFlags &= ~EF_ZOOMING;
        clients[0].ps.weapon = ents[0].s.weapon = WP_ADRENALINE_SHARE;
        injection_traces[0].surfaceFlags = SURF_NOIMPACT;
        if (i == 1) clients[0].ps.stats[STAT_KEYS] &= ~(1 << INV_BINOCS);
        if (i == 2) clients[0].ps.leanf = 1;
        if (i == 3) { clients[0].ps.persistant[PERS_HWEAPON_USE] = 1; ents[0].active = qtrue; }
        if (i == 4) clients[0].ps.stats[STAT_PLAYER_CLASS] = PC_MEDIC;
        events(&ents[0], 0);
        CHECK(trace_count == (i == 0 ? 1 : 0));
        CHECK(clients[0].ps.classWeaponTime == (i == 0 ? 50000 : 10000));
        CHECK(!(clients[0].ps.eFlags & EF_ZOOMING));
        CHECK(ents[0].s.weapon == WP_ADRENALINE_SHARE && clients[0].ps.weapon == WP_ADRENALINE_SHARE);
        CHECK(lev->num_entities == MAX_CLIENTS);
    }

    /* Shared shots retain NQ's no-attack-invulnerability and disguise rules. */
    setup_server(); server_begin(&pm);
    ((vmCvar_t *)sym("g_noAttacklInvul"))->integer = 1;
    clients[0].ps.powerups[PW_INVULNERABLE] = 12345;
    clients[0].ps.powerups[PW_OPS_DISGUISED] = 12345;
    server_shot(&pm);
    CHECK(!clients[0].ps.powerups[PW_INVULNERABLE]);
    CHECK(clients[0].ps.powerups[PW_OPS_DISGUISED] == 12345);
    ((vmCvar_t *)sym("g_noAttacklInvul"))->integer = 0;

    /* Invalid or unrepresentable server charge cannot inject and refunds the
     * previously consumed unit. Prediction applies the same cost checks. */
    for (i = 0; i < 3; ++i) {
        void (*share_fire)(gentity_t *, qboolean) = sym("G_NM_ShareAdrenalineFire");
        float *cost = sym("chargeCost_Adrenaline"), saved = cost[4];
        setup_server(); server_begin(&pm); lev->time = 10000;
        clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] = 11;
        clients[0].sess.skillBits[SK_MEDIC] = 1 << 4;
        if (i == 0) lev->medicChargeTime[0] = -1;
        if (i == 1) cost[4] = NAN;
        if (i == 2) { cost[4] = 2.0f; lev->medicChargeTime[0] = INT_MAX; }
        share_fire(&ents[0], qtrue); cost[4] = saved;
        CHECK(!clients[1].ps.powerups[PW_ADRENALINE]);
        CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12);
    }

    /* Do not produce INT_MAX, which is the non-expiring powerup sentinel. */
    for (i = 0; i < 2; ++i) {
        void (*share_fire)(gentity_t *, qboolean) = sym("G_NM_ShareAdrenalineFire");
        setup_server(); server_begin(&pm); clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] = 11;
        lev->time = INT_MAX - (int)ADRENALINE_TIME - (i == 0 ? 1 : 0);
        share_fire(&ents[0], qtrue);
        CHECK(clients[1].ps.powerups[PW_ADRENALINE] == (i == 0 ? INT_MAX - 1 : 0));
        CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == (i == 0 ? 11 : 12));
    }
}

static void test_server_grants(void) {
    qboolean (*can)(const gclient_t *) = sym("G_NM_CanShareAdrenaline");
    qboolean (*sync)(gclient_t *) = sym("G_NM_SyncAdrenalineShare");
    void (*check_weapons)(gentity_t *) = sym("ClientThink_checkWeapons");
    void (*steal_tools)(gclient_t *) = sym("G_ClassSteal_RemoveTools");
    void (*spawn_weapons)(gclient_t *) = sym("SetWolfSpawnWeapons");
    pmove_t pm;
    setup_server(); server_begin(&pm); clear_bit(clients[0].ps.weapons, WP_ADRENALINE_SHARE);
    CHECK(can(&clients[0]) && !sync(&clients[0]));
    CHECK(check_bit(clients[0].ps.weapons, WP_ADRENALINE_SHARE));
    CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12);
    medics->integer = 0; clients[0].ps.weapon = clients[0].ps.nextWeapon = WP_ADRENALINE_SHARE;
    CHECK(sync(&clients[0]));
    CHECK(!check_bit(clients[0].ps.weapons, WP_ADRENALINE_SHARE) && clients[0].ps.weapon == WP_NONE && clients[0].ps.nextWeapon == WP_NONE);
    CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12);

    setup_server(); server_begin(&pm); clients[0].weaponsRemoved = qtrue; war->integer = WARMODE_ENABLE;
    check_weapons(&ents[0]);
    CHECK(!check_bit(clients[0].ps.weapons, WP_ADRENALINE_SHARE) && clients[0].ps.weapon == WP_NONE);
    CHECK(clients[0].ps.events[0] == EV_WEAPONSWITCHED);

    setup_server(); server_begin(&pm); clients[0].sess.playerType = PC_ENGINEER;
    check_weapons(&ents[0]); CHECK(!check_bit(clients[0].ps.weapons, WP_ADRENALINE_SHARE));
    setup_server(); server_begin(&pm); adrenaline->integer = 0; check_weapons(&ents[0]);
    CHECK(!check_bit(clients[0].ps.weapons, WP_MEDIC_ADRENALINE) && !check_bit(clients[0].ps.weapons, WP_ADRENALINE_SHARE));
    setup_server(); server_begin(&pm); clients[0].sess.skillBits[SK_MEDIC] = 0;
    CHECK(!can(&clients[0])); sync(&clients[0]); CHECK(!check_bit(clients[0].ps.weapons, WP_ADRENALINE_SHARE));

    /* Class stealing retains NQ's behavior: no new self-adrenaline bit or
     * needles until a normal spawn, even if the player becomes a medic. */
    setup_server(); server_begin(&pm); clients[0].sess.playerType = PC_MEDIC;
    steal_tools(&clients[0]);
    CHECK(!check_bit(clients[0].ps.weapons, WP_MEDIC_ADRENALINE));
    CHECK(!check_bit(clients[0].ps.weapons, WP_ADRENALINE_SHARE));
    CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12);

    setup_server(); server_begin(&pm);
    clients[0].ps.ammo[WP_FOOTKICK] = 71; clients[0].ps.ammoclip[WP_FOOTKICK] = 72;
    clients[0].ps.ammo[WP_JOHNSON_SCOPE] = clients[0].ps.ammoclip[WP_JOHNSON_SCOPE] = 73;
    clients[0].ps.ammo[WP_ADRENALINE_SHARE] = clients[0].ps.ammoclip[WP_ADRENALINE_SHARE] = 74;
    spawn_weapons(&clients[0]);
    CHECK(clients[0].ps.ammo[WP_FOOTKICK] == 71 && clients[0].ps.ammoclip[WP_FOOTKICK] == 72);
    CHECK(!clients[0].ps.ammo[WP_JOHNSON_SCOPE] && !clients[0].ps.ammoclip[WP_JOHNSON_SCOPE]);
    CHECK(!clients[0].ps.ammo[WP_ADRENALINE_SHARE] && !clients[0].ps.ammoclip[WP_ADRENALINE_SHARE]);
    CHECK(check_bit(clients[0].ps.weapons, WP_MEDIC_ADRENALINE) && check_bit(clients[0].ps.weapons, WP_ADRENALINE_SHARE));
    CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12);
    setup_server(); server_begin(&pm); adrenaline->integer |= ADF_NOSPAWN;
    spawn_weapons(&clients[0]); CHECK(!clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE]);

    /* A follow/limbo playerstate mirrors the followed client; warmode and
     * share eligibility must not remove the viewed player's inventory. */
    setup_server(); server_begin(&pm); clients[0].ps.pm_flags = PMF_FOLLOW | PMF_LIMBO;
    clients[0].sess.sessionTeam = TEAM_SPECTATOR; war->integer = WARMODE_ENABLE;
    check_weapons(&ents[0]);
    CHECK(check_bit(clients[0].ps.weapons, WP_ADRENALINE_SHARE));
    CHECK(clients[0].ps.weapon == WP_ADRENALINE_SHARE && clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12);
}

static void test_server_auxiliary(void) {
    qboolean (*give)(gentity_t *, char *, char *, char *) = sym("ExecGive");
    int (*add_clip)(playerState_t *, int, int, int) = sym("AddToClip");
    void (*identify)(gentity_t *) = sym("G_SetIdentifyClient");
    void (*adrenaline_frame)(gclient_t *) = sym("G_NM_AdrenalineFrame");
    int (*adrenaline_deadline)(int) = sym("G_NM_AdrenalineDeadline");
    void (*self_adrenaline)(gentity_t *) = sym("Weapon_AdrenalineSyringe");
    qboolean (*disguise)(gentity_t *) = sym("jP_CheckDisguise");
    pmove_t pm;

    setup_server(); server_begin(&pm);
    CHECK(!add_clip(&clients[0].ps, WP_ADRENALINE_SHARE, 0, qtrue));
    CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12);
    /* The shared clip guard also affects ordinary guns: verify actual
     * reserve transfers and ensure bad indices/amounts cannot remove ammo. */
    { int maxclip = weapon_table[WP_MP40].maxclip;
      CHECK(maxclip > 1);
      clients[0].ps.ammo[WP_MP40] = maxclip + 5;
      clients[0].ps.ammoclip[WP_MP40] = maxclip - 2;
      CHECK(add_clip(&clients[0].ps, WP_MP40, 0, qtrue));
      CHECK(clients[0].ps.ammoclip[WP_MP40] == maxclip && clients[0].ps.ammo[WP_MP40] == maxclip + 3);
      CHECK(!add_clip(&clients[0].ps, WP_MP40, 1, qtrue));
      clients[0].ps.ammoclip[WP_MP40] = maxclip + 1;
      CHECK(!add_clip(&clients[0].ps, WP_MP40, 0, qtrue));
      CHECK(clients[0].ps.ammoclip[WP_MP40] == maxclip + 1);
      clients[0].ps.ammoclip[WP_MP40] = 1; clients[0].ps.ammo[WP_MP40] = -1;
      CHECK(!add_clip(&clients[0].ps, WP_MP40, 0, qtrue));
      CHECK(!add_clip(&clients[0].ps, WP_MP40, -1, qfalse));
      CHECK(!add_clip(&clients[0].ps, -1, 0, qtrue));
      CHECK(!add_clip(&clients[0].ps, WP_NUM_WEAPONS, 0, qtrue));
      CHECK(!add_clip(NULL, WP_MP40, 0, qtrue));
      CHECK(clients[0].ps.ammoclip[WP_MP40] == 1 && clients[0].ps.ammo[WP_MP40] == -1);
      clients[0].ps.ammo[WP_MP40] = clients[0].ps.ammoclip[WP_MP40] = 0; }
    CHECK(give(&ents[0], "allammo", "3", ""));
    CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12);
    CHECK(clients[0].ps.ammo[WP_MEDIC_ADRENALINE] == 3); /* not added twice */
    CHECK(give(&ents[0], "ammo", "2", ""));
    CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12 && clients[0].ps.ammo[WP_MEDIC_ADRENALINE] == 5);
    CHECK(give(&ents[0], "ammo", "", ""));
    CHECK(clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12);

    setup_server(); server_begin(&pm); clients[0].sess.playerType = PC_SOLDIER;
    CHECK(give(&ents[0], "weapons", "", ""));
    CHECK(!check_bit(clients[0].ps.weapons, WP_ADRENALINE_SHARE));
    setup_server(); server_begin(&pm); clear_bit(clients[0].ps.weapons, WP_ADRENALINE_SHARE);
    CHECK(give(&ents[0], "weapons", "", ""));
    CHECK(check_bit(clients[0].ps.weapons, WP_ADRENALINE_SHARE));
    ents[0].s.weapon = WP_ADRENALINE_SHARE; CHECK(disguise(&ents[0]));

    /* Identify HUD is a 0..255 fraction of the actual skill-dependent
     * maximum, rather than maxclip=1 or an unbounded float conversion. */
    setup_server(); server_begin(&pm); clients[0].ps.identifyClient = 1;
    ents[1].s.weapon = WP_ADRENALINE_SHARE; clients[1].sess.playerType = PC_MEDIC;
    clients[1].sess.skillBits[SK_MEDIC] = 0x1e;
    clients[1].ps.ammoclip[WP_MEDIC_ADRENALINE] = 6;
    identify(&ents[0]); CHECK(clients[0].ps.stats[STAT_IDENTIFYCLIP] == 127);
    clients[1].ps.ammoclip[WP_MEDIC_ADRENALINE] = INT_MAX;
    identify(&ents[0]); CHECK(clients[0].ps.stats[STAT_IDENTIFYCLIP] == 255);
    clients[1].ps.ammoclip[WP_MEDIC_ADRENALINE] = -1;
    identify(&ents[0]); CHECK(clients[0].ps.stats[STAT_IDENTIFYCLIP] == 0);
    adrenaline->integer |= ADF_JUSTONE; clients[1].ps.ammoclip[WP_MEDIC_ADRENALINE] = 1;
    identify(&ents[0]); CHECK(clients[0].ps.stats[STAT_IDENTIFYCLIP] == 255);

    /* Active effect -> NQ's recipient cooldown -> ready. Pause must extend
     * both positive active and negative cooldown deadlines. */
    setup_server(); clients[1].ps.powerups[PW_ADRENALINE] = 20000; lev->time = 20001;
    adrenaline_frame(&clients[1]); CHECK(clients[1].ps.powerups[PW_ADRENALINE] == -50001);
    lev->time = 50001; adrenaline_frame(&clients[1]); CHECK(clients[1].ps.powerups[PW_ADRENALINE] == -50001);
    lev->time = 50002; adrenaline_frame(&clients[1]); CHECK(clients[1].ps.powerups[PW_ADRENALINE] == 0);
    lev->match_pause = PAUSE_UNPAUSING; lev->previousTime = 18900; lev->time = 19000;
    clients[1].ps.powerups[PW_ADRENALINE] = 20000;
    adrenaline_frame(&clients[1]); CHECK(clients[1].ps.powerups[PW_ADRENALINE] == 20100);
    clients[1].ps.powerups[PW_ADRENALINE] = -50000;
    adrenaline_frame(&clients[1]); CHECK(clients[1].ps.powerups[PW_ADRENALINE] == -50100);
    lev->match_pause = PAUSE_NONE; lev->time = 50101;
    adrenaline_frame(&clients[1]); CHECK(clients[1].ps.powerups[PW_ADRENALINE] == 0);

    CHECK(adrenaline_deadline(10000) == 20000);
    CHECK(adrenaline_deadline(INT_MAX - (int)ADRENALINE_TIME) == INT_MAX - 1);
    CHECK(adrenaline_deadline(INT_MAX) == INT_MAX - 1);
    CHECK(adrenaline_deadline(-1) == 0);
    lev->time = INT_MAX - (int)ADRENALINE_TIME; self_adrenaline(&ents[0]);
    CHECK(clients[0].ps.powerups[PW_ADRENALINE] == INT_MAX - 1);
    lev->match_pause = PAUSE_UNPAUSING; lev->time = INT_MAX - 10; lev->previousTime = INT_MAX - 110;
    clients[1].ps.powerups[PW_ADRENALINE] = INT_MAX - 20;
    adrenaline_frame(&clients[1]); CHECK(clients[1].ps.powerups[PW_ADRENALINE] == INT_MAX - 1);
    clients[0].ps.powerups[PW_ADRENALINE] = INT_MAX;
    adrenaline_frame(&clients[0]); CHECK(clients[0].ps.powerups[PW_ADRENALINE] == INT_MAX);
    lev->match_pause = PAUSE_NONE; lev->time = INT_MAX;
    adrenaline_frame(&clients[1]); CHECK(clients[1].ps.powerups[PW_ADRENALINE] == -INT_MAX);
    clients[1].ps.powerups[PW_ADRENALINE] = INT_MIN;
    adrenaline_frame(&clients[1]); CHECK(clients[1].ps.powerups[PW_ADRENALINE] == -INT_MAX);

    /* Short or invalid recharge times leave no post-effect cooldown. */
    { int charges[] = {-1, 0, 5000, 10000, 10001}; int j;
      for (j = 0; j < 5; ++j) {
          setup_server(); lev->time = 20001; lev->medicChargeTime[0] = charges[j];
          clients[1].ps.powerups[PW_ADRENALINE] = 20000;
          adrenaline_frame(&clients[1]);
          CHECK(clients[1].ps.powerups[PW_ADRENALINE] == (j == 4 ? -20002 : 0));
      } }
    clients[1].sess.sessionTeam = TEAM_SPECTATOR; clients[1].ps.powerups[PW_ADRENALINE] = 1;
    adrenaline_frame(&clients[1]); CHECK(clients[1].ps.powerups[PW_ADRENALINE] == 0);
}

static void command_setup(void) {
    setup_server();
    lev->time = 50000;
    medics->integer = MEDIC_ADRENOTHER_CMD;
    clients[0].ps.weapon = WP_MEDIC_SYRINGE;
    set_bit(clients[0].ps.weapons, WP_MEDIC_SYRINGE);
    clients[0].ps.ammoclip[WP_MEDIC_SYRINGE] = 3;
    clients[0].ps.classWeaponTime = 10000;
    command_argc = 1; command_name = "adrenother"; command_argument = "extra";
    command_messages = 0; command_reply[0] = '\0';
}

static void test_knives_command(void) {
    void (*dispatch)(int) = sym("ClientCommand");
    int axis_clip = weapon_table[WP_KNIFE].clipindex;
    int allies_clip = weapon_table[WP_KNIFE_KABAR].clipindex;

    command_setup(); command_name = "knives";
    set_bit(clients[0].ps.weapons, WP_KNIFE);
    clients[0].ps.ammoclip[axis_clip] = 4;
    dispatch(0);
    CHECK(command_messages == 1 && strstr(command_reply, "4 knives left"));

    command_setup(); command_name = "KNIVES";
    clients[0].sess.sessionTeam = TEAM_ALLIES;
    set_bit(clients[0].ps.weapons, WP_KNIFE_KABAR);
    clients[0].ps.ammoclip[allies_clip] = 1;
    dispatch(0);
    CHECK(command_messages == 1 && strstr(command_reply, "1 knife left"));

    command_setup(); command_name = "knives";
    clients[0].ps.ammoclip[axis_clip] = 7; /* stale clip without a weapon */
    dispatch(0); CHECK(strstr(command_reply, "0 knives left"));
    command_setup(); command_name = "knives";
    set_bit(clients[0].ps.weapons, WP_KNIFE); clients[0].ps.ammoclip[axis_clip] = -1;
    dispatch(0); CHECK(strstr(command_reply, "0 knives left"));
    command_setup(); command_name = "knives"; command_argc = 2;
    dispatch(0); CHECK(strstr(command_reply, "usage"));
    command_setup(); command_name = "knives"; clients[0].sess.sessionTeam = TEAM_SPECTATOR;
    dispatch(0); CHECK(strstr(command_reply, "unavailable"));
    command_setup(); command_name = "knives"; clients[0].ps.pm_flags = PMF_LIMBO;
    dispatch(0); CHECK(strstr(command_reply, "unavailable"));
    command_setup(); command_name = "knives"; clients[0].ps.pm_flags = PMF_FOLLOW;
    dispatch(0); CHECK(strstr(command_reply, "unavailable"));
    command_setup(); command_name = "knives";
    weapon_table[WP_KNIFE].clipindex = MAX_WEAPONS;
    dispatch(0); CHECK(strstr(command_reply, "inventory is unavailable"));
    weapon_table[WP_KNIFE].clipindex = axis_clip;
}

static void test_adrenother_command(void) {
    void (*dispatch)(int) = sym("ClientCommand");
    int adrenaline_event = GAMESOUND_MISC_REVIVE;

    /* A successful command dispatch consumes a revival syringe and full
     * charge, applies NQ's finite effect, sends the target sound and awards
     * medic XP. Repeating against an active target must be a no-op. */
    command_setup(); dispatch(0);
    CHECK(!command_messages && trace_count == 1);
    CHECK(clients[0].ps.ammoclip[WP_MEDIC_SYRINGE] == 2);
    CHECK(clients[0].ps.classWeaponTime == 50000);
    CHECK(clients[1].ps.powerups[PW_ADRENALINE] == 50000 + (int)ADRENALINE_TIME);
    CHECK(clients[1].ps.events[0] == EV_GENERAL_SOUND && clients[1].ps.eventParms[0] == adrenaline_event);
    CHECK(clients[0].sess.skillpoints[SK_FIRST_AID] == 2.f && clients[0].sess.team_hits == -2.f);
    dispatch(0);
    CHECK(command_messages == 1 && strstr(command_reply, "unavailable"));
    CHECK(clients[0].ps.ammoclip[WP_MEDIC_SYRINGE] == 2 && clients[1].ps.eventSequence == 1);
    CHECK(clients[0].sess.skillpoints[SK_FIRST_AID] == 2.f);

    /* Case-insensitive registration, no extra arguments, and opt-in flag. */
    command_setup(); command_name = "ADRENOTHER"; command_argc = 2; dispatch(0);
    CHECK(command_messages == 1 && strstr(command_reply, "usage") && !trace_count);
    command_setup(); medics->integer = MEDIC_SHAREADRENALINE; dispatch(0);
    CHECK(command_messages == 1 && !trace_count && clients[0].ps.ammoclip[WP_MEDIC_SYRINGE] == 3);
    command_setup(); command_name = "AdrenOther"; dispatch(0);
    CHECK(!command_messages && clients[1].ps.powerups[PW_ADRENALINE] > lev->time);

    /* Class/skill/weapon/life/team and pause constraints do not spend ammo. */
#define REJECT(change) do { command_setup(); change; dispatch(0); \
    CHECK(command_messages == 1 && !trace_count && \
          clients[0].ps.ammoclip[WP_MEDIC_SYRINGE] == 3 && \
          clients[1].ps.powerups[PW_ADRENALINE] == 0); } while (0)
    REJECT(clients[0].sess.playerType = PC_SOLDIER);
    REJECT(clients[0].sess.skillBits[SK_MEDIC] = 0);
    REJECT(clients[0].sess.sessionTeam = TEAM_SPECTATOR);
    REJECT(clients[0].ps.weapon = WP_MP40);
    REJECT(clear_bit(clients[0].ps.weapons, WP_MEDIC_SYRINGE));
    REJECT(clients[0].ps.pm_type = PM_DEAD);
    REJECT(clients[0].ps.pm_flags = PMF_LIMBO);
    REJECT(clients[0].ps.eFlags = EF_MG42_ACTIVE);
    REJECT(lev->match_pause = PAUSE_UNPAUSING);
    REJECT(lev->intermissiontime = 1);
    REJECT(lev->time = INT_MAX - (int)ADRENALINE_TIME);
    REJECT(clients[0].ps.classWeaponTime++); /* 39999 ms, not full */
    REJECT(lev->medicChargeTime[0] = -1);
#undef REJECT
    command_setup(); clients[0].ps.ammoclip[WP_MEDIC_SYRINGE] = 0; dispatch(0);
    CHECK(command_messages == 1 && !trace_count && !clients[0].ps.ammoclip[WP_MEDIC_SYRINGE]);

    /* Reject enemies, cooldowns, underwater targets and embedded traces
     * without changing charge, ammunition or rewards. */
#define BAD_TARGET(change) do { command_setup(); change; dispatch(0); \
    CHECK(command_messages == 1 && trace_count >= 1 && \
          clients[0].ps.ammoclip[WP_MEDIC_SYRINGE] == 3 && \
          clients[0].ps.classWeaponTime == 10000 && \
          clients[0].sess.skillpoints[SK_FIRST_AID] == 0); } while (0)
    BAD_TARGET(clients[1].sess.sessionTeam = TEAM_ALLIES);
    BAD_TARGET(clients[1].ps.powerups[PW_ADRENALINE] = -60000);
    BAD_TARGET(clients[1].ps.powerups[PW_ADRENALINE] = lev->time);
    BAD_TARGET(clients[1].ps.powerups[PW_ADRENALINE] = lev->time - 1);
    BAD_TARGET(clients[1].ps.pm_flags = PMF_LIMBO);
    BAD_TARGET(point_contents = CONTENTS_WATER);
    BAD_TARGET(injection_traces[0].startsolid = injection_traces[1].startsolid = qtrue);
#undef BAD_TARGET

    command_setup(); insanity->integer = JP_INSANITY_CHARGEBAR | JP_INSANITY_AMMUNITION;
    clients[0].ps.classWeaponTime = lev->time; dispatch(0);
    CHECK(!command_messages && clients[0].ps.classWeaponTime == lev->time);
    CHECK(clients[0].ps.ammoclip[WP_MEDIC_SYRINGE] == 3);
    CHECK(clients[1].ps.powerups[PW_ADRENALINE] == lev->time + (int)ADRENALINE_TIME);
}

static void buy_setup(int now) {
    command_setup();
    lev->time = now;
    command_name = "buy"; command_argument = "adrenaline"; command_argc = 2;
    insanity->integer = JP_INSANITY_ALLOWBUYING;
    adrenaline->integer = ADF_MEDIC;
    ((vmCvar_t *)sym("g_fear"))->integer = 0;
    clients[0].sess.credits = 100;
}

static void test_buy_adrenaline(void) {
    void (*dispatch)(int) = sym("ClientCommand");
    int i;
    int clocks[] = { 0, 10000, INT_MAX - (int)ADRENALINE_TIME - 1,
                     INT_MAX - (int)ADRENALINE_TIME, INT_MAX - 1, INT_MAX, -1 };
    for (i = 0; i < (int)(sizeof(clocks) / sizeof(clocks[0])); ++i) {
        int valid = i < 4;
        buy_setup(clocks[i]); dispatch(0);
        CHECK(command_messages == 1);
        CHECK(clients[0].sess.credits == (valid ? 50 : 100));
        CHECK(!!clients[0].sess.buyTime == valid);
        CHECK(!!(clients[0].ps.eFlags & EF_BUYING) == valid);
        if (valid) {
            int expected = i < 2 ? clocks[i] + (int)ADRENALINE_TIME : INT_MAX - 1;
            CHECK(clients[0].ps.powerups[PW_ADRENALINE] == expected);
            CHECK(expected > clocks[i] && expected != INT_MAX);
            dispatch(0); /* one purchase per life, including time zero */
            CHECK(command_messages == 2 && clients[0].sess.credits == 50);
            CHECK(clients[0].ps.powerups[PW_ADRENALINE] == expected);
        } else {
            CHECK(!clients[0].ps.powerups[PW_ADRENALINE]);
            CHECK(strstr(command_reply, "unavailable"));
        }
    }
    buy_setup(10000); clients[0].sess.credits = 49; dispatch(0);
    CHECK(clients[0].sess.credits == 49 && !clients[0].sess.buyTime && !clients[0].ps.powerups[PW_ADRENALINE]);
    buy_setup(10000); adrenaline->integer = 0; dispatch(0);
    CHECK(clients[0].sess.credits == 100 && !clients[0].sess.buyTime && !clients[0].ps.powerups[PW_ADRENALINE]);
    buy_setup(10000); insanity->integer = 0; dispatch(0);
    CHECK(clients[0].sess.credits == 100 && !clients[0].sess.buyTime && !clients[0].ps.powerups[PW_ADRENALINE]);
}

static void test_spectator_lifecycle(void) {
    void (*spectator_frame)(gentity_t *) = sym("SpectatorClientEndFrame");
    void (*stop_following)(gentity_t *) = sym("StopFollowing");
    void (*check_weapons)(gentity_t *) = sym("ClientThink_checkWeapons");
    void (*client_think)(gentity_t *) = sym("ClientThink_real");
    bg_character_t *(*get_character)(int, int) = sym("BG_GetCharacter");
    entityarray_t *spawns = sym("g_spawns");
    static gentity_t intermission;
    pmove_t pm;

    setup_server(); server_begin(&pm); follow_fixture = 1;
    lev->time = lev->timeCurrent = 50000; lev->previousTime = 49950;
    lev->numConnectedClients = 2; lev->sortedClients[0] = 0; lev->sortedClients[1] = 1;
    ((vmCvar_t *)sym("g_gamestate"))->integer = GS_PLAYING;
    ((vmCvar_t *)sym("g_serverInfo"))->integer = 0;
    ((vmCvar_t *)sym("g_antiwarp"))->integer = 0;
    ((vmCvar_t *)sym("g_redlimbotime"))->integer = 30000;
    ((vmCvar_t *)sym("g_bluelimbotime"))->integer = 30000;
    ((void (*)(void))sym("G_InitFreeEntitiesChain"))();
    memset(&intermission, 0, sizeof(intermission));
    intermission.classnamehash = INFO_PLAYER_INTERMISSION_HASH;
    memset(spawns, 0, sizeof(*spawns)); spawns->ent[0] = &intermission; spawns->count = 1;
    get_character(TEAM_SPECTATOR, PC_SOLDIER)->animModelInfo = model;
    snprintf(clients[1].pers.netname, sizeof(clients[1].pers.netname), "Viewer");
    snprintf(clients[1].pers.client_ip, sizeof(clients[1].pers.client_ip), "10.0.0.2:27960");
    clients[1].pers.maxHealth = 100;

    clients[0].ps.origin[0] = 123; clients[0].ps.viewangles[YAW] = 90;
    clients[0].ps.powerups[PW_ADRENALINE] = 60000;
    clients[0].ps.ammo[VERYBIGEXPLOSION] = 11; clients[0].ps.ammo[WP_DUMMY_MG42] = 22;
    clients[0].ps.ammoclip[VERYBIGEXPLOSION] = 33; clients[0].ps.ammoclip[WP_DUMMY_MG42] = 44;
    clients[0].ps.ammo[WP_ARTY] = 55;
    clients[1].sess.sessionTeam = TEAM_SPECTATOR;
    clients[1].sess.spectatorState = SPECTATOR_FOLLOW;
    clients[1].sess.spectatorClient = 0; clients[1].ps.ping = 42;
    clients[1].ps.eFlags = EF_VOTED;
    spectator_frame(&ents[1]);
    CHECK(clients[1].ps.clientNum == 0 && (clients[1].ps.pm_flags & PMF_FOLLOW));
    CHECK(clients[1].ps.weapon == WP_ADRENALINE_SHARE && check_bit(clients[1].ps.weapons, WP_ADRENALINE_SHARE));
    CHECK(clients[1].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12 && clients[1].ps.powerups[PW_ADRENALINE] == 60000);
    CHECK(clients[1].ps.ping == 42 && (clients[1].ps.eFlags & EF_VOTED));
    medics->integer = 0; war->integer = WARMODE_ENABLE;
    check_weapons(&ents[1]);
    CHECK(clients[1].ps.weapon == WP_ADRENALINE_SHARE && check_bit(clients[1].ps.weapons, WP_ADRENALINE_SHARE));
    /* Run a real viewer command between follow frames, not just the weapon
     * check. Skill and artillery HUD slots must remain the viewed values. */
    clients[1].pers.lastCCPulseTime = lev->time;
    clients[1].pers.cmd.serverTime = lev->time;
    client_think(&ents[1]);
    CHECK(clients[1].ps.ammo[VERYBIGEXPLOSION] == 11 && clients[1].ps.ammo[WP_DUMMY_MG42] == 22);
    CHECK(clients[1].ps.ammoclip[VERYBIGEXPLOSION] == 33 && clients[1].ps.ammoclip[WP_DUMMY_MG42] == 44);
    CHECK(clients[1].ps.ammo[WP_ARTY] == 55 && clients[1].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12);
    CHECK(check_bit(clients[1].ps.weapons, WP_ADRENALINE_SHARE));

    /* Actual StopFollowing -> SetTeam -> ClientBegin resets the viewer's
     * copied inventory while preserving its position and view direction. */
    stop_following(&ents[1]);
    CHECK(clients[1].sess.sessionTeam == TEAM_SPECTATOR && clients[1].sess.spectatorState == SPECTATOR_FREE);
    CHECK(clients[1].ps.clientNum == 1 && !(clients[1].ps.pm_flags & PMF_FOLLOW));
    CHECK(!check_bit(clients[1].ps.weapons, WP_ADRENALINE_SHARE));
    CHECK(!clients[1].ps.ammoclip[WP_MEDIC_ADRENALINE] && !clients[1].ps.powerups[PW_ADRENALINE]);
    CHECK(clients[1].ps.origin[0] == 123 && fabsf(clients[1].ps.viewangles[YAW] - 90) < 0.01f);
    CHECK(check_bit(clients[0].ps.weapons, WP_ADRENALINE_SHARE) && clients[0].ps.ammoclip[WP_MEDIC_ADRENALINE] == 12);

    /* Re-follow and let the viewer's end-frame handle a disconnected target. */
    clients[1].sess.spectatorState = SPECTATOR_FOLLOW; clients[1].sess.spectatorClient = 0;
    spectator_frame(&ents[1]);
    CHECK(check_bit(clients[1].ps.weapons, WP_ADRENALINE_SHARE));
    clients[0].pers.connected = CON_DISCONNECTED;
    spectator_frame(&ents[1]);
    CHECK(clients[1].sess.spectatorState == SPECTATOR_FREE && clients[1].ps.clientNum == 1);
    CHECK(!(clients[1].ps.pm_flags & PMF_FOLLOW) && !check_bit(clients[1].ps.weapons, WP_ADRENALINE_SHARE));
    CHECK(!clients[1].ps.ammoclip[WP_MEDIC_ADRENALINE]);

    /* Stale slots, including an index beyond the physical array, fall back
     * to a clean spectator state instead of indexing arbitrary memory. */
    { int slots[] = { 2, MAX_CLIENTS * 2 }, j;
      lev->maxclients = 2;
      for (j = 0; j < 2; ++j) {
          clients[1].sess.spectatorState = SPECTATOR_FOLLOW;
          clients[1].sess.spectatorClient = slots[j];
          set_bit(clients[1].ps.weapons, WP_ADRENALINE_SHARE);
          spectator_frame(&ents[1]);
          CHECK(clients[1].sess.spectatorState == SPECTATOR_FREE && clients[1].ps.clientNum == 1);
          CHECK(!check_bit(clients[1].ps.weapons, WP_ADRENALINE_SHARE));
      }
      lev->maxclients = MAX_CLIENTS; }

    /* Limbo follow keeps its own score, respawn timer and ping while it views
     * the shared weapon. Reinforcement and an actual client are not simulated. */
    clients[0].pers.connected = CON_CONNECTED;
    clients[1].sess.sessionTeam = TEAM_AXIS; clients[1].sess.spectatorState = SPECTATOR_FOLLOW;
    clients[1].sess.spectatorClient = 0; clients[1].ps.pm_flags = PMF_LIMBO;
    clients[1].ps.pm_time = 100; clients[1].ps.ping = 75; clients[1].ps.persistant[PERS_SCORE] = 23;
    clients[1].respawnTime = lev->timeCurrent + 1000;
    spectator_frame(&ents[1]);
    CHECK((clients[1].ps.pm_flags & (PMF_LIMBO | PMF_FOLLOW)) == (PMF_LIMBO | PMF_FOLLOW));
    CHECK(clients[1].ps.clientNum == 0 && clients[1].ps.pm_time == 100 && clients[1].ps.ping == 75);
    CHECK(clients[1].ps.persistant[PERS_SCORE] == 23 && check_bit(clients[1].ps.weapons, WP_ADRENALINE_SHARE));
    memset(spawns, 0, sizeof(*spawns)); follow_fixture = 0;
}
#else
static void test_cgame_selection(void) {
    cg_t *state = sym("cg"); int bank = -1, cycle = -1;
    qboolean (*selectable)(int) = sym("CG_WeaponSelectable");
    int (*index_for)(int, int *, int *) = sym("CG_WeaponIndex");
    memset(&state->predictedPlayerState, 0, sizeof(state->predictedPlayerState));
    set_bit(state->predictedPlayerState.weapons, WP_ADRENALINE_SHARE);
    CHECK(index_for(WP_ADRENALINE_SHARE, &bank, &cycle) && bank == 7 && cycle == 2);
    CHECK(!selectable(WP_ADRENALINE_SHARE));
    state->predictedPlayerState.ammoclip[WP_MEDIC_ADRENALINE] = 1;
    CHECK(selectable(WP_ADRENALINE_SHARE));
    clear_bit(state->predictedPlayerState.weapons, WP_ADRENALINE_SHARE);
    CHECK(!selectable(WP_ADRENALINE_SHARE));
}

static void test_cgame_events(void) {
    cg_t *state = sym("cg"); cgs_t *shared = sym("cgs");
    weaponInfo_t *weapons_cg = sym("cg_weapons");
    void (*entity_event)(centity_t *, vec3_t) = sym("CG_EntityEvent");
    static snapshot_t snap;
    centity_t cent; int flags, parm;
    memset(&snap, 0, sizeof(snap)); memset(&cent, 0, sizeof(cent));
    snap.ps.clientNum = 0; state->snap = &snap; state->refdef_current = &state->refdef;
    state->time = 12345; state->weaponSelect = WP_MEDIC_ADRENALINE;
    shared->clientinfo[0].team = TEAM_AXIS; shared->clientinfo[0].cls = PC_MEDIC;
    snap.ps.weapon = WP_ADRENALINE_SHARE;
    CHECK(((qboolean (*)(float))sym("CG_CheckDisguise"))(0.0f));
    cent.currentState.number = cent.currentState.clientNum = 0;

    /* Exercise the actual client dispatcher, not only shared pmove. */
    cent.currentState.event = EV_WEAPALT; cent.currentState.eventParm = WP_ADRENALINE_SHARE;
    entity_event(&cent, cent.lerpOrigin);
    CHECK(state->weaponSelect == WP_ADRENALINE_SHARE && state->lastWeapSelInBank[7] == WP_ADRENALINE_SHARE);
    cent.currentState.number = 1; cent.currentState.eventParm = WP_MEDIC_ADRENALINE;
    entity_event(&cent, cent.lerpOrigin); CHECK(state->weaponSelect == WP_ADRENALINE_SHARE);
    cent.currentState.number = 0; entity_event(&cent, cent.lerpOrigin);
    CHECK(state->weaponSelect == WP_MEDIC_ADRENALINE);

    /* A delayed shot after a switch or mount must still use the share sound,
     * and may not mutate the received entity state. */
    weapons_cg[WP_ADRENALINE_SHARE].flashSound[0] = 73;
    weapons_cg[WP_MP40].flashSound[0] = 45;
    cent.currentState.weapon = WP_MP40;
    flags = EF_MOUNTEDTANK | EF_MG42_ACTIVE | EF_ZOOMING;
    cent.currentState.eFlags = flags; snap.ps.eFlags = EF_ZOOMING;
    for (parm = 0; parm < 2; ++parm) {
        sound_calls = last_sound = 0; state->lastFiredWeapon = WP_NONE;
        state->kickAVel[0] = 7; state->kickAVel[1] = 8; state->kickAVel[2] = 9;
        cent.currentState.event = parm ? EV_FIRE_WEAPON_LASTSHOT : EV_FIRE_WEAPON;
        cent.currentState.eventParm = parm ? NM_SHARE_FIRE_UNLIMITED : NM_SHARE_FIRE_CONSUMED;
        entity_event(&cent, cent.lerpOrigin);
        CHECK(sound_calls == 1 && last_sound == 73 && state->lastFiredWeapon == WP_ADRENALINE_SHARE);
        CHECK(state->kickAVel[0] == 7 && state->kickAVel[1] == 8 && state->kickAVel[2] == 9);
        CHECK(cent.currentState.weapon == WP_MP40 && cent.currentState.eFlags == flags);
    }
    /* Conversely, an earlier ordinary shot must not play the share sound or
     * lose recoil just because the current snapshot has switched to share. */
    cent.currentState.weapon = WP_ADRENALINE_SHARE;
    flags = EF_ZOOMING | EF_MG42_ACTIVE | EF_MOUNTEDTANK;
    cent.currentState.eFlags = flags; snap.ps.eFlags = EF_ZOOMING;
    for (parm = 0; parm < 3; ++parm) {
        sound_calls = last_sound = 0; state->lastFiredWeapon = WP_NONE; state->kickAVel[0] = 0;
        cent.currentState.event = parm == 0 ? EV_FIRE_WEAPON : parm == 1 ? EV_FIRE_WEAPON_LASTSHOT : EV_FIRE_WEAPONB;
        cent.currentState.eventParm = NM_FIRE_WEAPON_PARM(WP_MP40);
        entity_event(&cent, cent.lerpOrigin);
        CHECK(sound_calls == 1 && last_sound == 45 && state->lastFiredWeapon == WP_MP40);
        CHECK(state->kickAVel[0] < 0 && cent.currentState.weapon == WP_ADRENALINE_SHARE);
        CHECK(cent.currentState.eFlags == flags);
        if (parm == 2) CHECK(cent.akimboFire);
    }
    /* The existing binocular event parameter retains its radio behavior. */
    cent.currentState.weapon = WP_BINOCULARS; cent.currentState.eFlags = EF_ZOOMING;
    cent.currentState.event = EV_FIRE_WEAPON; cent.currentState.eventParm = WP_BINOCULARS;
    shared->media.radioSound[0] = 88; sound_calls = last_sound = 0;
    entity_event(&cent, cent.lerpOrigin); CHECK(sound_calls == 1 && last_sound == 88);
    cent.currentState.weapon = WP_ADRENALINE_SHARE; cent.currentState.eFlags = EF_MOUNTEDTANK;
    sound_calls = last_sound = 0; entity_event(&cent, cent.lerpOrigin);
    CHECK(sound_calls == 1 && last_sound == 88);
    CHECK(cent.currentState.weapon == WP_ADRENALINE_SHARE && cent.currentState.eFlags == EF_MOUNTEDTANK);

    /* Exercise the recipient's playerstate -> entity event ring -> cgame
     * sound dispatch, including not replaying the same received event. */
    { playerState_t target;
      void (*to_state)(playerState_t *, entityState_t *, int, qboolean) = sym("BG_PlayerStateToEntityState");
      void (*check_events)(centity_t *) = sym("CG_CheckEvents");
      memset(&target, 0, sizeof(target)); memset(&cent, 0, sizeof(cent));
      target.clientNum = 1; target.stats[STAT_HEALTH] = 100; target.pm_type = PM_NORMAL;
      target.events[0] = EV_GENERAL_SOUND; target.eventParms[0] = GAMESOUND_MISC_REVIVE; target.eventSequence = 1;
      shared->cachedSounds[GAMESOUND_MISC_REVIVE] = 99;
      to_state(&target, &cent.currentState, state->time, qfalse);
      CHECK(cent.currentState.events[0] == EV_GENERAL_SOUND && cent.currentState.eventParms[0] == GAMESOUND_MISC_REVIVE);
      sound_calls = 0; check_events(&cent);
      CHECK(sound_calls == 1 && last_sound == 99 && last_sound_entity == 1);
      CHECK(last_sound_channel == CHAN_VOICE && last_sound_volume == 255);
      check_events(&cent); CHECK(sound_calls == 1); }

    /* Same event ID, different weapon tag: play the server-corrected effect,
     * but not when only the share's ammo-consumption bit changed. */
    { playerState_t before, after;
      void (*check_player_events)(playerState_t *, playerState_t *) = sym("CG_CheckPlayerstateEvents");
      centity_t *pred = &state->predictedPlayerEntity;
      memset(&before, 0, sizeof(before)); memset(&after, 0, sizeof(after)); memset(pred, 0, sizeof(*pred));
      before.eventSequence = after.eventSequence = 1;
      before.events[0] = after.events[0] = EV_FIRE_WEAPON;
      before.eventParms[0] = NM_FIRE_WEAPON_PARM(WP_MP40);
      after.eventParms[0] = NM_SHARE_FIRE_CONSUMED;
      pred->currentState.number = pred->currentState.clientNum = 0; pred->currentState.weapon = WP_MP40;
      sound_calls = 0; check_player_events(&after, &before);
      CHECK(sound_calls == 1 && last_sound == 73 && pred->currentState.weapon == WP_MP40);
      before = after; after.eventParms[0] = NM_SHARE_FIRE_UNLIMITED;
      sound_calls = 0; check_player_events(&after, &before); CHECK(sound_calls == 0);
      before = after; after.eventParms[0] = NM_FIRE_WEAPON_PARM(WP_MP40);
      pred->currentState.weapon = WP_ADRENALINE_SHARE;
      pred->currentState.eFlags = EF_ZOOMING | EF_MG42_ACTIVE | EF_MOUNTEDTANK; sound_calls = 0;
      check_player_events(&after, &before);
      CHECK(sound_calls == 1 && last_sound == 45 && pred->currentState.weapon == WP_ADRENALINE_SHARE);
      CHECK(pred->currentState.eFlags == (EF_ZOOMING | EF_MG42_ACTIVE | EF_MOUNTEDTANK));
      before.eventParms[0] = NM_SHARE_FIRE_CONSUMED; after.eventParms[0] = WP_BINOCULARS;
      pred->currentState.weapon = WP_MP40; sound_calls = 0;
      check_player_events(&after, &before);
      CHECK(sound_calls == 1 && last_sound == 88 && pred->currentState.weapon == WP_MP40);
      before = after; sound_calls = 0; check_player_events(&after, &before); CHECK(sound_calls == 0); }
}
#endif

int main(int argc, char **argv) {
    CHECK(argc == 2);
    module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!module) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    common_init(); test_registration(); test_prediction();
#ifdef GAMEDLL
    test_server_fire(); test_server_grants(); test_server_auxiliary(); test_adrenother_command(); test_knives_command(); test_buy_adrenaline();
    test_spectator_lifecycle();
    puts("game share weapon, commands and movement tests passed");
#else
    test_cgame_selection(); test_cgame_events();
    puts("cgame share weapon, selection, event and movement tests passed");
#endif
    free(scriptdata); free(model); dlclose(module); return 0;
}
