/*
 * NewMod command adapters, Arena.ai, 2026-11-21.
 *
 * Implements selected Jaymod 2.0 and Oz command behavior on the No Quarter
 * game state. These are adaptations, not a wholesale import of their command
 * or user databases. See CHANGES and licenses/ for provenance and terms.
 */
#include "g_local.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>

/* Don't embed untrusted player names in quoted engine commands. */
static void NM_SafeText(const char *src, char *dst, size_t size) {
	size_t n = 0;
	if (!size) return;
	while (*src && n + 1 < size) {
		unsigned char c = (unsigned char)*src++;
		dst[n++] = (c < 32 || c == 127 || c == '"' || c == '\\') ? ' ' : (char)c;
	}
	dst[n] = '\0';
}

static qboolean NM_ParseNumber(const char *text, int *value) {
	char *end;
	long number;
	if (!text[0]) return qfalse;
	errno = 0;
	number = strtol(text, &end, 10);
	if (errno || *end || number < INT_MIN || number > INT_MAX) return qfalse;
	*value = (int)number;
	return qtrue;
}

static qboolean NM_EligibleTarget(gentity_t *actor, gentity_t *victim) {
	int team;
	if (!victim || !victim->inuse || !victim->client ||
	    victim->client->pers.connected != CON_CONNECTED) return qfalse;
	team = victim->client->sess.sessionTeam;
	if (team != TEAM_AXIS && team != TEAM_ALLIES) return qfalse;
	if (victim->client->ps.pm_flags & PMF_LIMBO) return qfalse;
	if (!_shrubbot_admin_higher(actor, victim)) return qfalse;
	if (actor && actor != victim && G_shrubbot_permission(victim, SBF_IMMUTABLE)) return qfalse;
	return qtrue;
}

/* The NQ lookup returns all partial matches and writes a final -1 sentinel. */
static int NM_Targets(const char *filter, int *pids) {
	int i, count = 0;
	if (filter && *filter) return ClientNumbersFromString((char *)filter, pids);
	for (i = 0; i < level.numConnectedClients && i < MAX_CLIENTS; ++i) {
		int slot = level.sortedClients[i];
		if (slot >= 0 && slot < MAX_CLIENTS) pids[count++] = slot;
	}
	pids[count] = -1;
	return count;
}

/* Oz persists warnings by IP/GUID, supports decay and can auto-kick. NQ has
 * no such store. This deliberately smaller feature stores only sanitized
 * reasons on the current connection: no identity keys, timestamps, automatic
 * punishment or persistence across reconnects/maps. It is not Oz parity. */
#define NM_WARNING_LIMIT 8
#define NM_WARNING_REASON_SIZE 160

typedef struct {
	int count;
	char reason[NM_WARNING_LIMIT][NM_WARNING_REASON_SIZE];
} nm_warning_state_t;
static nm_warning_state_t nmWarnings[MAX_CLIENTS];

void G_nm_clear_warnings(int slot) {
	if (slot == -1) memset(nmWarnings, 0, sizeof(nmWarnings));
	else if (slot >= 0 && slot < MAX_CLIENTS) memset(&nmWarnings[slot], 0, sizeof(nmWarnings[slot]));
}

/* Warnings can target spectators as well as team players. The dispatcher
 * checks flag R; recheck the target hierarchy and immutability for both
 * listing and deletion. Console can access all, and players see only their
 * own warnings via /warnings. */
static int NM_WarningTarget(gentity_t *ent, const char *command, const char *name) {
	int pids[MAX_CLIENT_MATCHES], count;
	gentity_t *victim;
	if (!name[0] || strlen(name) >= MAX_NAME_LENGTH) {
		SP(va("^d%s: ^9invalid target\n", command));
		return -1;
	}
	count = ClientNumbersFromString((char *)name, pids);
	if (count != 1 || pids[0] < 0 || pids[0] >= MAX_CLIENTS) {
		SP(va("^d%s: ^9%s\n", command,
		      count > 1 ? "multiple matches; use a slot number" : "no connected player matches"));
		return -1;
	}
	victim = &g_entities[pids[0]];
	if (!victim->inuse || !victim->client || victim->client->pers.connected != CON_CONNECTED) {
		SP(va("^d%s: ^9player is no longer connected\n", command));
		return -1;
	}
	if (!_shrubbot_admin_higher(ent, victim) ||
	    (ent && ent != victim && G_shrubbot_permission(victim, SBF_IMMUTABLE))) {
		SP(va("^d%s: ^9target is protected\n", command));
		return -1;
	}
	return pids[0];
}

static void NM_ShowWarnings(gentity_t *ent, int slot) {
	int i;
	nm_warning_state_t *state = &nmWarnings[slot];
	SP(va("^dwarnings: ^9%d saved for this connection (not persistent)\n", state->count));
	for (i = 0; i < state->count && i < NM_WARNING_LIMIT; ++i)
		SP(va("^9%d. ^7%s\n", i + 1, state->reason[i]));
}

qboolean G_nm_warn(gentity_t *ent, int skiparg) {
	char target[MAX_STRING_CHARS], reason[NM_WARNING_REASON_SIZE];
	char victimName[MAX_NAME_LENGTH], actorName[MAX_NAME_LENGTH];
	int slot;
	gentity_t *victim;
	nm_warning_state_t *state;
	if (Q_SayArgc() < 3 + skiparg) goto usage;
	Q_SayArgv(1 + skiparg, target, sizeof(target));
	NM_SafeText(Q_SayConcatArgs(2 + skiparg), reason, sizeof(reason));
	if (!reason[0] || strspn(reason, " ") == strlen(reason)) goto usage;
	slot = NM_WarningTarget(ent, "warn", target);
	if (slot < 0) return qfalse;
	victim = &g_entities[slot];
	state = &nmWarnings[slot];
	if (state->count < NM_WARNING_LIMIT) {
		Q_strncpyz(state->reason[state->count++], reason, NM_WARNING_REASON_SIZE);
	} else {
		SP("^dwarn: ^9connection history is full; warning delivered but not saved\n");
	}
	NM_SafeText(victim->client->pers.netname, victimName, sizeof(victimName));
	if (!ent) Q_strncpyz(actorName, "SERVER CONSOLE", sizeof(actorName));
	else if (G_shrubbot_permission(ent, SBF_INCOGNITO)) Q_strncpyz(actorName, "an admin", sizeof(actorName));
	else NM_SafeText(ent->client->pers.netname, actorName, sizeof(actorName));
	G_AddEvent(victim, EV_GENERAL_SOUND, GAMESOUND_MISC_REFEREE);
	AP(va("chat \"^dwarn: ^7%s ^9was warned^7\"", victimName));
	if (g_gamestate.integer == GS_INTERMISSION) {
		CPx(slot, va("chat \"%s ^dwarned ^9you because: %s\"", actorName, reason));
	} else {
		CPx(slot, va("cp \"%s ^dwarned ^9you because:\n%s\"", actorName, reason));
	}
	return qtrue;
usage:
	SP("^dwarn: ^9usage: !warn [name|slot#] [reason]\n");
	return qfalse;
}

qboolean G_nm_dewarn(gentity_t *ent, int skiparg) {
	char target[MAX_STRING_CHARS], arg[32];
	int slot, number, i, argc = Q_SayArgc() - skiparg;
	nm_warning_state_t *state;
	if (argc < 2 || argc > 3) goto usage;
	Q_SayArgv(1 + skiparg, target, sizeof(target));
	if (argc == 3) {
		Q_SayArgv(2 + skiparg, arg, sizeof(arg));
		if (!NM_ParseNumber(arg, &number) || number < 1 || number > NM_WARNING_LIMIT) goto usage;
	}
	slot = NM_WarningTarget(ent, "dewarn", target);
	if (slot < 0) return qfalse;
	state = &nmWarnings[slot];
	if (argc == 2) {
		NM_ShowWarnings(ent, slot);
		return qtrue;
	}
	if (number > state->count) {
		SP("^ddewarn: ^9no warning with that number\n");
		return qfalse;
	}
	for (i = number - 1; i + 1 < state->count; ++i)
		memcpy(state->reason[i], state->reason[i + 1], NM_WARNING_REASON_SIZE);
	memset(state->reason[--state->count], 0, NM_WARNING_REASON_SIZE);
	SP(va("^ddewarn: ^9warning %d removed for slot %d\n", number, slot));
	return qtrue;
usage:
	SP("^ddewarn: ^9usage: !dewarn [name|slot#] (warning#)\n");
	return qfalse;
}

void G_nm_warnings(gentity_t *ent) {
	int slot;
	if (!ent || !ent->client) return;
	slot = ent->s.number;
	if (slot < 0 || slot >= level.maxclients || slot >= MAX_CLIENTS ||
	    &g_entities[slot] != ent || ent->client->pers.connected != CON_CONNECTED) return;
	if (trap_Argc() != 1) {
		SP("^dwarnings: ^9usage: warnings\n");
		return;
	}
	NM_ShowWarnings(ent, slot);
}

qboolean G_nm_crazygravity(gentity_t *ent, int skiparg) {
	char action[16];
	int argc = Q_SayArgc() - skiparg;
	if (argc == 1) {
		SP(va("^dcrazygravity: ^9%s\n", level.nmCrazyGravity ? "enabled" : "disabled"));
		return qtrue;
	}
	if (argc != 2) {
		SP("^dcrazygravity: ^9usage: !crazygravity [on|off]\n");
		return qfalse;
	}
	Q_SayArgv(1 + skiparg, action, sizeof(action));
	if (!Q_stricmp(action, "on")) {
		if (level.nmCrazyGravity) {
			SP("^dcrazygravity: ^9already enabled\n");
			return qfalse;
		}
		/* Read the engine value, which may be newer than the cached vmCvar,
		 * and preserve its exact string for restoration. */
		trap_Cvar_VariableStringBuffer("g_gravity", level.nmSavedGravity,
		                               sizeof(level.nmSavedGravity));
		level.nmGravityStart = level.time;
		level.nmGravityWarned = qfalse;
		level.nmCrazyGravity = qtrue;
		AP("chat \"^dcrazygravity: ^9enabled\"");
		return qtrue;
	}
	if (!Q_stricmp(action, "off")) {
		if (!level.nmCrazyGravity) {
			SP("^dcrazygravity: ^9already disabled\n");
			return qfalse;
		}
		G_nm_gravity_restore();
		AP("chat \"^dcrazygravity: ^9disabled; previous gravity restored\"");
		return qtrue;
	}
	SP("^dcrazygravity: ^9usage: !crazygravity [on|off]\n");
	return qfalse;
}

void G_nm_gravity_frame(void) {
	long long now = level.time, elapsed;
	int value;
	if (!level.nmCrazyGravity) return;
	if (now < level.nmGravityStart) {
		level.nmGravityStart = now;
		level.nmGravityWarned = qfalse;
	}
	elapsed = now - level.nmGravityStart;
	if (elapsed >= 30000) {
		value = rand() % 1200;
		trap_Cvar_Set("g_gravity", va("%d", value));
		level.nmGravityStart = now;
		level.nmGravityWarned = qfalse;
		AP(va("chat \"^dcrazygravity: ^9gravity changed to %d\"", value));
	} else if (elapsed >= 25000 && !level.nmGravityWarned) {
		level.nmGravityWarned = qtrue;
		AP("chat \"^dcrazygravity: ^9gravity will change in about 5 seconds\"");
	}
}

void G_nm_gravity_restore(void) {
	if (!level.nmCrazyGravity) return;
	trap_Cvar_Set("g_gravity", level.nmSavedGravity);
	level.nmCrazyGravity = qfalse;
	level.nmGravityWarned = qfalse;
}

qboolean G_nm_shake(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES], argc = Q_SayArgc() - skiparg; /* CHANGES: match sentinel */
	int i, count, applied = 0, seconds = 1;
	char filter[MAX_STRING_CHARS], timeText[32];
	filter[0] = '\0';
	if (level.intermissiontime || argc < 1 || argc > 3) {
		SP("^dshake: ^9usage: !shake (name|slot#) (seconds), not in intermission\n");
		return qfalse;
	}
	if (argc >= 2) {
		Q_SayArgv(1 + skiparg, filter, sizeof(filter));
		if (!filter[0] || strlen(filter) >= MAX_NAME_LENGTH) {
			SP("^dshake: ^9invalid target\n");
			return qfalse;
		}
	}
	if (argc == 3) {
		Q_SayArgv(2 + skiparg, timeText, sizeof(timeText));
		if (!NM_ParseNumber(timeText, &seconds)) {
			SP("^dshake: ^9seconds must be a number\n");
			return qfalse;
		}
		if (seconds < 1) seconds = 1;
		if (seconds > 60) seconds = 60;
	}
	count = NM_Targets(filter, pids);
	for (i = 0; i < count; ++i) {
		gentity_t *victim = &g_entities[pids[i]];
		if (!NM_EligibleTarget(ent, victim) || victim->health <= 0) continue;
		victim->client->pers.nmShakeUntil = (long long)level.time + seconds * 1000LL;
		victim->client->pers.nmShakeNext = level.time;
		++applied;
	}
	if (!applied) {
		SP("^dshake: ^9no eligible active players found\n");
		return qfalse;
	}
	AP(va("chat \"^dshake: ^9%d player%s shaken for %d second%s\"",
	      applied, applied == 1 ? "" : "s", seconds, seconds == 1 ? "" : "s"));
	return qtrue;
}

void G_nm_shake_frame(gentity_t *ent) {
	gentity_t *event;
	long long now = level.time;
	if (!ent || !ent->client || !ent->inuse ||
	    ent->client->pers.connected != CON_CONNECTED || ent->health <= 0 ||
	    (ent->client->sess.sessionTeam != TEAM_AXIS && ent->client->sess.sessionTeam != TEAM_ALLIES) ||
	    (ent->client->ps.pm_flags & PMF_LIMBO) || level.intermissiontime ||
	    now >= ent->client->pers.nmShakeUntil || now < ent->client->pers.nmShakeNext) return;
	/* Leave a reserve for ordinary gameplay entities on crowded servers. */
	if (G_GentitiesAvailable() <= MIN_SPARE_GENTITIES) return;
	event = G_TempEntity(ent->r.currentOrigin, EV_SHAKE);
	event->s.onFireStart = 1000000;
	/* SINGLECLIENT filters the snapshot first; BROADCAST then bypasses PVS
	 * so the one target receives the event even across a cluster boundary. */
	event->r.svFlags |= SVF_SINGLECLIENT | SVF_BROADCAST;
	event->r.singleClient = (int)(ent - g_entities);
	ent->client->pers.nmShakeNext = now + 750;
}

/* Oz's !spawn can respawn any team member, including live objective carriers
 * and players using mounted weapons. This deliberately narrower NQ adapter
 * only advances players already in limbo through the normal reinforcement
 * path, which restores the persistent state saved by limbo(). It intentionally
 * bypasses the wave/respawn wait but not an LMS round, pause, intermission or
 * restart. It is still a gameplay cheat. See CHANGES for differences. */
qboolean G_nm_spawn(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES], argc = Q_SayArgc() - skiparg;
	int i, count, applied = 0;
	char target[MAX_STRING_CHARS] = "";
	qboolean all = qtrue;
	if (argc < 1 || argc > 2) {
		SP("^dspawn: ^9usage: !spawn (name|slot#|all)\n");
		return qfalse;
	}
	if (level.intermissiontime || level.intermissionQueued || g_gamestate.integer == GS_INTERMISSION ||
	    (g_gametype.integer == GT_WOLF_LMS && g_gamestate.integer == GS_PLAYING)) {
		SP("^dspawn: ^9unavailable during intermission or an LMS round\n");
		return qfalse;
	}
	if (level.match_pause != PAUSE_NONE) {
		SP("^dspawn: ^9unavailable while the match is paused or unpausing\n");
		return qfalse;
	}
	if (level.restarted || (g_gamestate.integer != GS_PLAYING &&
	    g_gamestate.integer != GS_WARMUP && g_gamestate.integer != GS_WARMUP_COUNTDOWN &&
	    g_gamestate.integer != GS_WAITING_FOR_PLAYERS)) {
		SP("^dspawn: ^9unavailable during a restart or match transition\n");
		return qfalse;
	}
	if (argc == 2) {
		Q_SayArgv(skiparg + 1, target, sizeof(target));
		if (!target[0] || strlen(target) >= MAX_NAME_LENGTH) {
			SP("^dspawn: ^9invalid target\n");
			return qfalse;
		}
		all = !Q_stricmp(target, "all") || !Q_stricmp(target, "-1");
	}
	count = NM_Targets(all ? NULL : target, pids);
	if (!all && count != 1) {
		SP("^dspawn: ^9target not found or name is ambiguous\n");
		return qfalse;
	}
	for (i = 0; i < count; ++i) {
		gentity_t *victim;
		gclient_t *client;
		int team;
		if (pids[i] < 0 || pids[i] >= MAX_CLIENTS) continue;
		victim = &g_entities[pids[i]];
		client = victim->client;
		if (!victim->inuse || !client || client->pers.connected != CON_CONNECTED ||
		    !(client->ps.pm_flags & PMF_LIMBO)) continue;
		team = client->sess.sessionTeam;
		if ((team != TEAM_AXIS && team != TEAM_ALLIES) || !_shrubbot_admin_higher(ent, victim) ||
		    (ent && ent != victim && G_shrubbot_permission(victim, SBF_IMMUTABLE))) continue;
		reinforce(victim);
		/* ClientSpawn can change team through class restrictions or Lua. */
		if (victim->inuse && victim->client && victim->client->pers.connected == CON_CONNECTED &&
		    (victim->client->sess.sessionTeam == TEAM_AXIS || victim->client->sess.sessionTeam == TEAM_ALLIES) &&
		    !(victim->client->ps.pm_flags & PMF_LIMBO) && victim->health > 0) ++applied;
	}
	if (!applied) {
		SP("^dspawn: ^9no eligible players in limbo\n");
		return qfalse;
	}
	AP(va("chat \"^dspawn: ^9%d player%s reinforced\"", applied, applied == 1 ? "" : "s"));
	return qtrue;
}

static qboolean NM_SplatTargets(gentity_t *ent, int skiparg, qboolean all) {
	int pids[MAX_CLIENT_MATCHES], i, count, applied = 0; /* CHANGES: match sentinel */
	char filter[MAX_STRING_CHARS];
	const char *command = all ? "splata" : "splat";
	if (level.intermissiontime || level.intermissionQueued ||
	    (g_gamestate.integer != GS_PLAYING && !match_warmupDamage.integer)) {
		SP(va("^d%s: ^9damage is disabled right now\n", command));
		return qfalse;
	}
	if (Q_SayArgc() != (all ? 1 : 2) + skiparg) {
		SP(va("^d%s: ^9usage: !%s%s\n", command, command, all ? "" : " [name|slot#]"));
		return qfalse;
	}
	filter[0] = '\0';
	if (!all) {
		Q_SayArgv(1 + skiparg, filter, sizeof(filter));
		if (!filter[0] || strlen(filter) >= MAX_NAME_LENGTH) {
			SP("^dsplat: ^9invalid target\n");
			return qfalse;
		}
	}
	count = NM_Targets(all ? NULL : filter, pids);
	if (!all && count != 1) {
		SP("^dsplat: ^9target not found or name is ambiguous\n");
		return qfalse;
	}
	for (i = 0; i < count; ++i) {
		gentity_t *victim = &g_entities[pids[i]];
		int healthBefore;
		if (!NM_EligibleTarget(ent, victim) || !victim->takedamage ||
		    victim->client->noclip || (victim->flags & FL_GODMODE) ||
		    victim->entstate == STATE_INVISIBLE || victim->entstate == STATE_UNDERCONSTRUCTION) continue;
		healthBefore = victim->health;
		G_Damage(victim, NULL, NULL, NULL, NULL, 10000,
		         DAMAGE_NO_PROTECTION | DAMAGE_NM_SPLAT, MOD_UNKNOWN);
		/* G_Damage can still decline damage or a death hook can alter it. */
		if (!victim->inuse || !victim->takedamage || victim->health < healthBefore) ++applied;
	}
	if (!applied) {
		SP(va("^d%s: ^9no eligible active players found\n", command));
		return qfalse;
	}
	AP(va("chat \"^d%s: ^9%d player%s splattered\"", command, applied, applied == 1 ? "" : "s"));
	return qtrue;
}

qboolean G_nm_splat(gentity_t *ent, int skiparg) {
	return NM_SplatTargets(ent, skiparg, qfalse);
}

qboolean G_nm_splata(gentity_t *ent, int skiparg) {
	return NM_SplatTargets(ent, skiparg, qtrue);
}

qboolean G_nm_smite(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES], argc = Q_SayArgc() - skiparg; /* CHANGES: match sentinel */
	int i, count, applied = 0;
	char filter[MAX_STRING_CHARS];
	filter[0] = '\0';
	if (level.intermissiontime || argc < 1 || argc > 2) {
		SP("^dsmite: ^9usage: !smite (name|slot#), not in intermission\n");
		return qfalse;
	}
	if (argc == 2) {
		Q_SayArgv(1 + skiparg, filter, sizeof(filter));
		if (!filter[0] || strlen(filter) >= MAX_NAME_LENGTH) {
			SP("^dsmite: ^9invalid target\n");
			return qfalse;
		}
	}
	count = NM_Targets(filter, pids);
	for (i = 0; i < count; ++i) {
		gentity_t *victim = &g_entities[pids[i]];
		if (!NM_EligibleTarget(ent, victim) || victim->health <= 0) continue;
		G_BurnMeGood(victim, victim, NULL);
		trap_SendServerCommand(pids[i], "cp \"^1Burn baby burn!\"");
		++applied;
	}
	if (!applied) {
		SP("^dsmite: ^9no eligible active players found\n");
		return qfalse;
	}
	AP(va("chat \"^dsmite: ^9%d player%s burned\"", applied, applied == 1 ? "" : "s"));
	return qtrue;
}

/* Adapted from Oz's G_SetPlayerOnFire. Unlike Oz's separate firegod timer
 * model, NQ applies burn damage from ent->s.onFireEnd in ClientThink, so the
 * short timer is unnecessary here. The target is toggled: an active fire
 * becomes mortal and damaged again; a new fire is invulnerable for the
 * duration. Setting lastBurnedFrameNumber prevents G_BurnMeGood from
 * applying flame damage as part of the command itself. */
void G_nm_set_player_on_fire(gentity_t *self, gentity_t *body, gentity_t *chunk, qboolean noclip) {
	gentity_t *burner = chunk != NULL ? chunk : self;

	body->flameQuota = 0;
	body->flameQuotaTime = 0;
	body->lastBurnedFrameNumber = level.framenum;
	if (body->s.onFireEnd > level.time) {
		body->flags &= ~FL_GODMODE;
		body->takedamage = qtrue;
		if (body->client) body->client->noclip = qfalse;
		body->s.onFireEnd = level.time;
		return;
	}
	body->flags |= FL_GODMODE;
	body->takedamage = qfalse;
	if (body->client) body->client->noclip = noclip ? qtrue : qfalse;
	body->s.onFireStart = level.time;
	body->s.onFireEnd = level.time + FIRE_FLASH_TIME;
	body->flameBurnEnt = burner != NULL ? burner->s.number : self->s.number;
	if (body->client) body->client->ps.onFireStart = level.time;
}

qboolean G_nm_firegod(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char filter[MAX_STRING_CHARS], actor[MAX_NAME_LENGTH], message[MAX_STRING_CHARS];
	char optArg[32];
	gentity_t *victim;
	qboolean noclip;
	int duration = 0;
	qboolean hasDuration = qfalse;

	/* NewMod, Arena.ai: optional duration. !firegod [name] [opt]
	 * The optional third argument is a positive duration in seconds (which
	 * limits the fire god mode) or the literal "noclip" (which grants
	 * noclip as in the original behavior). Without a third argument the
	 * fire god mode toggles as before. */
	if (Q_SayArgc() < 2 + skiparg || Q_SayArgc() > 3 + skiparg) {
		SP("^dfiregod: ^9usage: !firegod [name|slot#] [duration|noclip]\n");
		return qfalse;
	}
	Q_SayArgv(1 + skiparg, filter, sizeof(filter));
	if (!filter[0] || strlen(filter) >= MAX_NAME_LENGTH) {
		SP("^dfiregod: ^9invalid target\n");
		return qfalse;
	}
	if (ClientNumbersFromString(filter, pids) != 1) {
		SP("^dfiregod: ^9target not found or name is ambiguous\n");
		return qfalse;
	}
	victim = &g_entities[pids[0]];
	if (!NM_EligibleTarget(ent, victim) || victim->health <= 0) {
		SP("^dfiregod: ^9no eligible active players found\n");
		return qfalse;
	}

	/* Determine optional duration or noclip from the third argument */
	noclip = qfalse;
	hasDuration = qfalse;
	if (Q_SayArgc() == 3 + skiparg) {
		Q_SayArgv(2 + skiparg, optArg, sizeof(optArg));
		if (NM_ParseNumber(optArg, &duration) && duration > 0) {
			/* Duration provided: clamp to a reasonable max */
			if (duration > 3600) duration = 3600;
			hasDuration = qtrue;
		} else {
			/* Third arg is noclip (legacy behavior) */
			noclip = qtrue;
		}
	}

	G_nm_set_player_on_fire(victim, victim, NULL, noclip);
	/* If a duration is specified, set the fire end time to expire */
	if (hasDuration) {
		victim->s.onFireEnd = level.time + duration * 1000;
	}
	NM_SafeText(ent && ent->client ? ent->client->pers.netname : "SERVER CONSOLE",
	            actor, sizeof(actor));
	if (hasDuration) {
		snprintf(message, sizeof(message), "cp \"%s ^7set you invincible flaming god for %d seconds\"",
		          actor, duration);
	} else {
		snprintf(message, sizeof(message), "cp \"%s ^7set you %s\"", actor,
		         victim->takedamage ? "mortal player" : "invincible flaming god");
	}
	trap_SendServerCommand(pids[0], message);
	return qtrue;
}

/* G_SoundIndex otherwise raises a fatal error when the configstring pool is
 * full. The two NQ configstring modes store their dynamic sounds separately. */
static qboolean NM_SoundSlotAvailable(const char *sound) {
	/* Match G_FindConfigstringIndex's full comparison even if a malformed
	 * pre-existing engine configstring is longer than MAX_QPATH. */
	char existing[MAX_STRING_CHARS];
	int i;
	for (i = 1; i < MAX_SOUNDS; ++i) {
		if (csMethod.integer) Q_strncpyz(existing, gs_sounds[i], sizeof(existing));
		else trap_GetConfigstring(CS_SOUNDS + i, existing, sizeof(existing));
		if (!existing[0] || !strcmp(existing, sound)) return qtrue;
	}
	return qfalse;
}

qboolean G_nm_news(gentity_t *ent, int skiparg) {
	char map[MAX_STRING_CHARS], sound[MAX_QPATH];
	const char *p;
	fileHandle_t f = 0;
	int length, argc = Q_SayArgc() - skiparg;
	if (argc < 1 || argc > 2) {
		SP("^dnews: ^9usage: !news (mapname)\n");
		return qfalse;
	}
	if (argc == 2) Q_SayArgv(1 + skiparg, map, sizeof(map));
	else Q_strncpyz(map, level.rawmapname, sizeof(map));
	/* Reject paths/control characters and prevent truncating two map copies. */
	for (p = map; *p; ++p) {
		unsigned char c = (unsigned char)*p;
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		      (c >= '0' && c <= '9') || c == '_' || c == '-')) {
			SP("^dnews: ^9invalid map name\n");
			return qfalse;
		}
	}
	if (!map[0] || 2 * strlen(map) + strlen("sound/vo//news_.wav") >= sizeof(sound)) {
		SP("^dnews: ^9invalid map name\n");
		return qfalse;
	}
	if ((long long)level.time < level.nmNewsNext) {
		SP("^dnews: ^9please wait before playing another news reel\n");
		return qfalse;
	}
	Com_sprintf(sound, sizeof(sound), "sound/vo/%s/news_%s.wav", map, map);
	/* Check first, so arbitrary names cannot exhaust the sound configstrings. */
	length = trap_FS_FOpenFile(sound, &f, FS_READ);
	if (f) trap_FS_FCloseFile(f);
	if (length <= 0 || !f) {
		SP("^dnews: ^9no news reel found for that map\n");
		return qfalse;
	}
	if (G_GentitiesAvailable() <= MIN_SPARE_GENTITIES) {
		SP("^dnews: ^9not enough free entities\n");
		return qfalse;
	}
	if (!NM_SoundSlotAvailable(sound)) {
		SP("^dnews: ^9sound index is full\n");
		return qfalse;
	}
	G_globalSound(sound);
	level.nmNewsNext = (long long)level.time + 10000;
	AP(va("chat \"^dnews: ^9playing the %s news reel\"", map));
	return qtrue;
}

qboolean G_nm_about(gentity_t *ent, int skiparg) {
	if (Q_SayArgc() != 1 + skiparg) {
		SP("^dabout: ^9usage: !about\n");
		return qfalse;
	}
	SP("^d" GAME_VERSION " ^9(experimental No Quarter-based ET: Legacy mod)\n");
	SP("^9Includes selected Jaymod and Oz adaptations; feature parity is incomplete.\n");
	return qtrue;
}

qboolean G_nm_status(gentity_t *ent, int skiparg) {
	char arg[16], value[BIG_INFO_VALUE];
	int i, entities = 0, strings = 1, active = 0;
	int argc = Q_SayArgc() - skiparg;
	if (argc < 1 || argc > 2) {
		SP("^dstatus: ^9usage: !status (-all)\n");
		return qfalse;
	}
	if (argc == 2) {
		Q_SayArgv(1 + skiparg, arg, sizeof(arg));
		if (Q_stricmp(arg, "-all")) {
			SP("^dstatus: ^9usage: !status (-all)\n");
			return qfalse;
		}
	}
	for (i = MAX_CLIENTS; i < level.num_entities && i < ENTITYNUM_MAX_NORMAL; ++i)
		if (g_entities[i].inuse) ++entities;
	for (i = 0; i < MAX_CONFIGSTRINGS; ++i) {
		trap_GetConfigstring(i, value, sizeof(value));
		if (value[0]) { strings += (int)strlen(value) + 1; ++active; }
	}
	SP(va("^dstatus: ^9players %d/%d, entities %d/%d\n", level.numConnectedClients,
	      level.maxclients, entities, ENTITYNUM_MAX_NORMAL - MAX_CLIENTS));
	SP(va("^dstatus: ^9configstrings %d/%d, estimated gamestate %d/%d bytes\n",
	      active, MAX_CONFIGSTRINGS, strings, MAX_GAMESTATE_CHARS));
	if (argc == 2) SP("^dstatus: ^9Jaymod database statistics are not available\n");
	return qtrue;
}

qboolean G_nm_lsplayers(gentity_t *ent, int skiparg) {
	int i, skill;
	if (Q_SayArgc() != 1 + skiparg) {
		SP("^dlsplayers: ^9usage: !lsplayers\n");
		return qfalse;
	}
	SBP_begin();
	SBP("^dslot team mute level     XP ping name\n");
	for (i = 0; i < level.maxclients && i < MAX_CLIENTS; ++i) {
		gclient_t *client = &level.clients[i];
		char name[MAX_NAME_LENGTH];
		const char *team;
		double xp = 0;
		int shownLevel;
		if (client->pers.connected != CON_CONNECTED && client->pers.connected != CON_CONNECTING) continue;
		team = client->pers.connected == CON_CONNECTING ? "conn" :
		       client->sess.sessionTeam == TEAM_AXIS ? "axis" :
		       client->sess.sessionTeam == TEAM_ALLIES ? "ally" : "spec";
		for (skill = 0; skill < SK_NUM_SKILLS; ++skill) {
			float value = client->sess.skillpoints[skill];
			if (isfinite(value) && value > 0) xp += value;
		}
		if (xp > INT_MAX) xp = INT_MAX;
		shownLevel = G_shrubbot_permission(&g_entities[i], SBF_INCOGNITO) ? 0 : G_shrubbot_level(&g_entities[i]);
		NM_SafeText(client->pers.netname, name, sizeof(name));
		SBP(va("^7%4d %-4s %-4s %5d %6d %4d %s\n", i, team,
		       client->sess.muted ? "yes" : "no", shownLevel, (int)xp, client->ps.ping, name));
	}
	SBP_end();
	return qtrue;
}

qboolean G_nm_spree(gentity_t *ent, int skiparg) {
	int spree;
	if (!ent || !ent->client || Q_SayArgc() != 1 + skiparg) {
		SP("^dspree: ^9usage: !spree (player only)\n");
		return qfalse;
	}
	if (ent->client->sess.sessionTeam != TEAM_AXIS && ent->client->sess.sessionTeam != TEAM_ALLIES) {
		SP("^dspree: ^9join a team first\n");
		return qfalse;
	}
	spree = ent->client->sess.killingSpree;
	if (spree > 0) SP(va("^dspree: ^9your current killing spree is %d\n", spree));
	else if (spree < 0) SP(va("^dspree: ^9your current death streak is %lld\n", -(long long)spree));
	else SP("^dspree: ^9you have no active streak\n");
	return qtrue;
}

qboolean G_nm_tspree(gentity_t *ent, int skiparg) {
	int slots[MAX_CLIENTS], count = 0, maximum = 5, i, j;
	int argc = Q_SayArgc() - skiparg;
	char number[32];
	if (argc < 1 || argc > 2) goto usage;
	if (argc == 2) {
		Q_SayArgv(1 + skiparg, number, sizeof(number));
		if (!NM_ParseNumber(number, &maximum) || maximum < 1 || maximum > MAX_CLIENTS) goto usage;
	}
	for (i = 0; i < level.numConnectedClients && i < MAX_CLIENTS; ++i) {
		int slot = level.sortedClients[i];
		gclient_t *client;
		if (slot < 0 || slot >= MAX_CLIENTS) continue;
		client = &level.clients[slot];
		if (client->pers.connected != CON_CONNECTED || client->sess.killingSpree <= 0 ||
		    (client->sess.sessionTeam != TEAM_AXIS && client->sess.sessionTeam != TEAM_ALLIES)) continue;
		j = count;
		while (j > 0 && level.clients[slots[j - 1]].sess.killingSpree < client->sess.killingSpree) {
			slots[j] = slots[j - 1];
			--j;
		}
		slots[j] = slot;
		++count;
	}
	SBP_begin();
	SBP("^dTop current killing sprees:\n");
	if (!count) SBP("^9No active killing sprees.\n");
	for (i = 0; i < count && i < maximum; ++i) {
		char name[MAX_NAME_LENGTH];
		gclient_t *client = &level.clients[slots[i]];
		NM_SafeText(client->pers.netname, name, sizeof(name));
		SBP(va("^7%d. %s^7: %d kills\n", i + 1, name, client->sess.killingSpree));
	}
	SBP_end();
	return qtrue;
usage:
	SP("^dtspree: ^9usage: !tspree (count 1-64)\n");
	return qfalse;
}

/* Oz-inspired but deliberately map-local. The recorded name is a snapshot
 * from the moment the record was set, not a live identity or database key. */
qboolean G_nm_spreerecord(gentity_t *ent, int skiparg) {
	char name[MAX_NAME_LENGTH];
	if (Q_SayArgc() != 1 + skiparg) {
		SP("^dspreerecord: ^9usage: !spreerecord\n");
		return qfalse;
	}
	if (level.maxspree_count <= 0) {
		SP("^dspreerecord: ^9no record this map (not persistent)\n");
		return qtrue;
	}
	NM_SafeText(level.maxspree_player, name, sizeof(name));
	SP(va("^dspreerecord: ^9this map: ^7%s^9, %d kill%s (not persistent)\n",
	      name[0] ? name : "unknown", level.maxspree_count, level.maxspree_count == 1 ? "" : "s"));
	return qtrue;
}

/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): Oz's MP40/Thompson report,
 * using the NQ counters. NQ does not retain Oz's headshot-distance field. */
qboolean G_nm_stats(gentity_t *ent, int skiparg) {
	int i;
	if (Q_SayArgc() != 1 + skiparg) {
		SP("^dstats: ^9usage: !stats\n");
		return qfalse;
	}
	SBP_begin();
	SBP("^dMP40 and Thompson stats (current NQ session):\n");
	SBP("^9slot name                         shots       hits   acc%       head   head%\n");
	for (i = 0; i < level.maxclients && i < MAX_CLIENTS; ++i) {
		gclient_t *cl = &level.clients[i];
		unsigned long long shots, hits, head;
		double accuracy, headRatio;
		char name[MAX_NAME_LENGTH];
		if (cl->pers.connected != CON_CONNECTED) continue;
		/* Cast before addition: the individual counters are unsigned ints. */
		shots = (unsigned long long)cl->sess.aWeaponStats[WS_MP40].atts + cl->sess.aWeaponStats[WS_THOMPSON].atts;
		hits = (unsigned long long)cl->sess.aWeaponStats[WS_MP40].hits + cl->sess.aWeaponStats[WS_THOMPSON].hits;
		head = (unsigned long long)cl->sess.aWeaponStats[WS_MP40].headshots + cl->sess.aWeaponStats[WS_THOMPSON].headshots;
		accuracy = shots ? 100.0 * (double)hits / (double)shots : 0.0;
		headRatio = hits ? 100.0 * (double)head / (double)hits : 0.0;
		if (accuracy > 999.9) accuracy = 999.9;
		if (headRatio > 999.9) headRatio = 999.9;
		NM_SafeText(cl->pers.netname, name, sizeof(name));
		SBP(va("^7%4d %-28s %10llu %10llu %6.1f %10llu %6.1f\n",
		       i, name, shots, hits, accuracy, head, headRatio));
	}
	SBP_end();
	return qtrue;
}

/* Jaymod's fun time is server-local, not the requesting client's timezone. */
qboolean G_nm_ftime(gentity_t *ent, int skiparg) {
	time_t now;
	struct tm local;
	char clock[32];
	if (Q_SayArgc() != 1 + skiparg) {
		SP("^dftime: ^9usage: !ftime\n");
		return qfalse;
	}
	now = time(NULL);
	if (now == (time_t)-1 || !localtime_r(&now, &local) ||
	    !strftime(clock, sizeof(clock), "%I:%M:%S %p", &local)) {
		SP("^dftime: ^9server time is unavailable\n");
		return qfalse;
	}
	SP("^3  o-o    ^dTime for fun!\n");
	SP(va("^3 ( ^7%s ^3)\n", clock));
	SP("^3  /--/    ^9(server local time)\n");
	return qtrue;
}

/* Both optional sounds share the news cooldown and resource checks. The mod
 * does not ship their media; only explicitly supplied existing files play. */
qboolean G_nm_fun_sound(gentity_t *ent, int skiparg) {
	char command[32], filter[MAX_STRING_CHARS], name[MAX_NAME_LENGTH];
	char *sound; /* G_globalSound's inherited interface takes char *. */
	const char *description;
	int pids[MAX_CLIENT_MATCHES], length;
	fileHandle_t f = 0;
	gentity_t *victim;
	Q_SayArgv(skiparg, command, sizeof(command));
	if (command[0] == '!') memmove(command, command + 1, strlen(command));
	if (!Q_stricmp(command, "chicken")) {
		sound = "sound/jaymod/chicken.wav";
		description = "is running around like a headless chicken";
	} else if (!Q_stricmp(command, "crybaby")) {
		sound = "sound/jaymod/crybaby.wav";
		description = "is crying like a little baby";
	} else {
		return qfalse;
	}
	if (Q_SayArgc() != 2 + skiparg) {
		SP(va("^d%s: ^9usage: !%s [name|slot#]\n", command, command));
		return qfalse;
	}
	Q_SayArgv(1 + skiparg, filter, sizeof(filter));
	if (!filter[0] || strlen(filter) >= MAX_NAME_LENGTH || NM_Targets(filter, pids) != 1) {
		SP(va("^d%s: ^9target not found or name is ambiguous\n", command));
		return qfalse;
	}
	victim = &g_entities[pids[0]];
	if (!victim->inuse || !victim->client || victim->client->pers.connected != CON_CONNECTED ||
	    !_shrubbot_admin_higher(ent, victim) ||
	    (ent && ent != victim && G_shrubbot_permission(victim, SBF_IMMUTABLE))) {
		SP(va("^d%s: ^9target is protected or unavailable\n", command));
		return qfalse;
	}
	if (level.intermissiontime || (long long)level.time < level.nmNewsNext) {
		SP(va("^d%s: ^9unavailable during intermission or sound cooldown\n", command));
		return qfalse;
	}
	length = trap_FS_FOpenFile(sound, &f, FS_READ);
	if (f) trap_FS_FCloseFile(f);
	if (length <= 0 || !f) {
		SP(va("^d%s: ^9optional sound is not installed\n", command));
		return qfalse;
	}
	if (G_GentitiesAvailable() <= MIN_SPARE_GENTITIES || !NM_SoundSlotAvailable(sound)) {
		SP(va("^d%s: ^9sound resources unavailable\n", command));
		return qfalse;
	}
	G_globalSound(sound);
	level.nmNewsNext = (long long)level.time + 10000;
	NM_SafeText(victim->client->pers.netname, name, sizeof(name));
	AP(va("chat \"^d%s: ^7%s^9 %s\"", command, name, description));
	return qtrue;
}

/* NQ's records are not Jaymod user/level database records. Restrict these
 * views to the corresponding NQ administrator permissions, bound every walk,
 * and never expose full GUID/IP without the separate finger permission. */
static qboolean NM_BanActive(const g_shrubbot_ban_t *ban, time_t now) {
	return ban && (!ban->expires || (long long)ban->expires > (long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET);
}

/* Jaymod's Page caches formatted database reports. NQ has different records,
 * so NewMod stores a small query per connection instead: pages are rebuilt
 * from current records and current permissions. This avoids retaining private
 * text after a privilege change, disconnect, slot reuse, or map restart.
 * It is not Jaymod's offline user database or its snapshot semantics. */
#define NM_REPORT_PAGE_SIZE 20
#define NM_REPORT_MAX_PAGES 99

typedef enum { NM_REPORT_NONE, NM_REPORT_BANS, NM_REPORT_LEVELS, NM_REPORT_USERS } nm_report_kind_t;
typedef struct {
	nm_report_kind_t kind;
	int startBan;
	int userLevel; /* -1 = any */
	char name[MAX_NAME_LENGTH]; /* normalized filter may still identify a person */
} nm_report_query_t;

/* The last slot belongs to the server console. No buffers of rendered
 * records, GUIDs or IP addresses are retained. */
static nm_report_query_t nmReports[MAX_CLIENTS + 1];

void G_nm_clear_reports(int slot) {
	if (slot == -1) memset(nmReports, 0, sizeof(nmReports));
	else if (slot >= 0 && slot < MAX_CLIENTS) memset(&nmReports[slot], 0, sizeof(nmReports[slot]));
}

static nm_report_query_t *NM_ReportSlot(gentity_t *ent) {
	ptrdiff_t slot;
	if (!ent) return &nmReports[MAX_CLIENTS];
	/* All callers are the normal shrubbot dispatcher or a game client. */
	slot = ent - g_entities;
	if (slot < 0 || slot >= level.maxclients || slot >= MAX_CLIENTS ||
	    !ent->client || ent->client->pers.connected != CON_CONNECTED) return NULL;
	return &nmReports[slot];
}

static void NM_NormalizeReportName(const char *input, char *output, size_t size) {
	size_t used = 0;
	if (!size) return;
	while (*input && used + 1 < size) {
		unsigned char c = (unsigned char)*input++;
		if (c == '^' || c == 27) {
			if (*input) ++input; /* match NQ name color stripping */
			continue;
		}
		if (c < 32 || c == 127) continue;
		if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
		output[used++] = (char)c;
	}
	output[used] = '\0';
}

/* NQ has no separate offline-user ACL lookup. Conservatively hide records
 * marked immutable or incognito directly or at their level, even if a flag
 * string also contains a negation. Only the console sees those records;
 * everyone else is also limited by their current hierarchy. Do not treat
 * matching client-supplied GUIDs as proof of record ownership. */
static qboolean NM_ReportVisibleAdmin(gentity_t *actor, const g_shrubbot_admin_t *admin, int actorLevel) {
	int i;
	if (!actor) return qtrue;
	if (admin->level > actorLevel || strchr(admin->flags, SBF_INCOGNITO) ||
	    strchr(admin->flags, SBF_IMMUTABLE)) return qfalse;
	for (i = 0; i < MAX_SHRUBBOT_LEVELS && g_shrubbot_levels[i]; ++i) {
		if (g_shrubbot_levels[i]->level == admin->level &&
		    (strchr(g_shrubbot_levels[i]->flags, SBF_INCOGNITO) ||
		     strchr(g_shrubbot_levels[i]->flags, SBF_IMMUTABLE))) return qfalse;
	}
	return qtrue;
}

/* A small, map-local seen cache is a useful subset of Jaymod's !seen, not
 * its persistent user database. A GUID is only a lookup key, never proof of
 * identity; records are rechecked against the current NQ permissions. */
#define NM_SEEN_CAPACITY 128
#define NM_SEEN_MAX_RESULTS 4

typedef struct {
	char guid[PB_GUID_LENGTH + 1];
	char name[MAX_NAME_LENGTH];
	time_t disconnected;
} nm_seen_t;
static nm_seen_t nmSeen[NM_SEEN_CAPACITY];
static unsigned int nmSeenNext;

void G_nm_seen_reset(void) {
	memset(nmSeen, 0, sizeof(nmSeen));
	nmSeenNext = 0;
}

static qboolean NM_SeenGuidValid(const char *guid) {
	int i;
	if (!guid || strlen(guid) != PB_GUID_LENGTH) return qfalse;
	for (i = 0; i < PB_GUID_LENGTH; ++i) {
		unsigned char c = (unsigned char)guid[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
		      (c >= 'A' && c <= 'F'))) return qfalse;
	}
	return qtrue;
}

static g_shrubbot_admin_t *NM_SeenAdmin(const char *guid) {
	int i;
	for (i = 0; i < MAX_SHRUBBOT_ADMINS && g_shrubbot_admins[i]; ++i)
		if (!Q_stricmp(guid, g_shrubbot_admins[i]->guid)) return g_shrubbot_admins[i];
	return NULL;
}

void G_nm_seen_disconnect(gentity_t *ent) {
	gclient_t *client;
	nm_seen_t *entry = NULL;
	time_t now;
	int i;
	if (!ent || !ent->inuse || !(client = ent->client) ||
	    client->pers.connected != CON_CONNECTED || !client->pers.netname[0] ||
	    !NM_SeenGuidValid(client->pers.cl_guid) || !NM_SeenAdmin(client->pers.cl_guid) ||
	    (now = time(NULL)) == (time_t)-1) return;
	for (i = 0; i < NM_SEEN_CAPACITY; ++i) {
		if (nmSeen[i].guid[0] && !Q_stricmp(nmSeen[i].guid, client->pers.cl_guid)) {
			entry = &nmSeen[i];
			break;
		}
	}
	if (!entry) {
		entry = &nmSeen[nmSeenNext];
		nmSeenNext = (nmSeenNext + 1) % NM_SEEN_CAPACITY;
	}
	Q_strncpyz(entry->guid, client->pers.cl_guid, sizeof(entry->guid));
	NM_SafeText(client->pers.netname, entry->name, sizeof(entry->name));
	entry->disconnected = now;
}

static qboolean NM_SeenOnline(const char *guid) {
	int i;
	for (i = 0; i < level.maxclients && i < MAX_CLIENTS; ++i) {
		if (g_entities[i].inuse && level.clients[i].pers.connected == CON_CONNECTED &&
		    !Q_stricmp(guid, level.clients[i].pers.cl_guid)) return qtrue;
	}
	return qfalse;
}

qboolean G_nm_seen(gentity_t *ent, int skiparg) {
	char input[MAX_STRING_CHARS], needle[MAX_NAME_LENGTH], normalized[MAX_NAME_LENGTH], safe[MAX_NAME_LENGTH];
	int i, matches = 0, actorLevel = ent ? G_shrubbot_level(ent) : INT_MAX;
	time_t now = time(NULL);
	if (Q_SayArgc() != 2 + skiparg) goto usage;
	Q_SayArgv(1 + skiparg, input, sizeof(input));
	if (!input[0] || strlen(input) >= MAX_NAME_LENGTH) goto usage;
	NM_NormalizeReportName(input, needle, sizeof(needle));
	if (!needle[0]) goto usage;

	/* Query connected admins first. Don't show duplicate GUID records when
	 * duplicate-GUID checks are disabled; they are not distinct identities. */
	for (i = 0; i < level.maxclients && i < MAX_CLIENTS; ++i) {
		gclient_t *client = &level.clients[i];
		g_shrubbot_admin_t *admin;
		int j;
		if (!g_entities[i].inuse || client->pers.connected != CON_CONNECTED ||
		    !NM_SeenGuidValid(client->pers.cl_guid) ||
		    !(admin = NM_SeenAdmin(client->pers.cl_guid)) ||
		    !NM_ReportVisibleAdmin(ent, admin, actorLevel)) continue;
		for (j = 0; j < i; ++j) {
			if (g_entities[j].inuse && level.clients[j].pers.connected == CON_CONNECTED &&
			    !Q_stricmp(client->pers.cl_guid, level.clients[j].pers.cl_guid)) break;
		}
		if (j < i) continue;
		NM_NormalizeReportName(client->pers.netname, normalized, sizeof(normalized));
		if (!strstr(normalized, needle)) continue;
		if (++matches <= NM_SEEN_MAX_RESULTS) {
			NM_SafeText(client->pers.netname, safe, sizeof(safe));
			SP(va("^dseen: ^7%s^9 is currently online\n", safe[0] ? safe : "(unnamed)"));
		}
	}
	for (i = 0; i < NM_SEEN_CAPACITY; ++i) {
		g_shrubbot_admin_t *admin;
		double elapsed;
		if (!nmSeen[i].guid[0] || NM_SeenOnline(nmSeen[i].guid) ||
		    !(admin = NM_SeenAdmin(nmSeen[i].guid)) ||
		    !NM_ReportVisibleAdmin(ent, admin, actorLevel)) continue;
		NM_NormalizeReportName(nmSeen[i].name, normalized, sizeof(normalized));
		if (!strstr(normalized, needle)) continue;
		if (++matches > NM_SEEN_MAX_RESULTS) continue;
		elapsed = now == (time_t)-1 ? -1 : difftime(now, nmSeen[i].disconnected);
		if (!isfinite(elapsed) || elapsed < 0) {
			SP(va("^dseen: ^7%s^9 disconnected recently (clock unavailable)\n", nmSeen[i].name));
		} else {
			int seconds = elapsed >= INT_MAX ? INT_MAX : (int)elapsed;
			SP(va("^dseen: ^7%s^9 disconnected %d seconds ago\n", nmSeen[i].name, seconds));
		}
	}
	if (!matches) SP("^dseen: ^9no visible matching admin found\n");
	else if (matches > NM_SEEN_MAX_RESULTS) SP("^dseen: ^9more matches; refine the name\n");
	SP("^9Disconnect history is map-local and GUIDs are not authentication.\n");
	return qtrue;
usage:
	SP("^dseen: ^9usage: !seen [name]\n");
	return qfalse;
}

static int NM_ReportLimit(const nm_report_query_t *query) {
	if (query->kind == NM_REPORT_BANS) return MAX_SHRUBBOT_BANS;
	if (query->kind == NM_REPORT_LEVELS) return MAX_SHRUBBOT_LEVELS;
	if (query->kind == NM_REPORT_USERS) return MAX_SHRUBBOT_ADMINS;
	return 0;
}

static qboolean NM_ReportPresent(const nm_report_query_t *query, int i) {
	if (query->kind == NM_REPORT_BANS) return g_shrubbot_bans[i] != NULL;
	if (query->kind == NM_REPORT_LEVELS) return g_shrubbot_levels[i] != NULL;
	if (query->kind == NM_REPORT_USERS) return g_shrubbot_admins[i] != NULL;
	return qfalse;
}

static qboolean NM_ReportMatches(const nm_report_query_t *query, gentity_t *actor,
                                int actorLevel, int i, time_t now) {
	const char *name;
	char normalized[MAX_NAME_LENGTH];
	if (query->kind == NM_REPORT_BANS)
		return i + 1 >= query->startBan && NM_BanActive(g_shrubbot_bans[i], now);
	if (query->kind == NM_REPORT_LEVELS) {
		name = g_shrubbot_levels[i]->name;
	} else {
		g_shrubbot_admin_t *admin = g_shrubbot_admins[i];
		if (!NM_ReportVisibleAdmin(actor, admin, actorLevel) ||
		    (query->userLevel >= 0 && admin->level != query->userLevel)) return qfalse;
		name = admin->name;
	}
	if (!query->name[0]) return qtrue;
	NM_NormalizeReportName(name, normalized, sizeof(normalized));
	return strstr(normalized, query->name) != NULL;
}

/* Returns the current page count, or -1 if authorization/time fails.
 * Count first so a requested page can be clamped without buffering records. */
static int NM_RunReport(gentity_t *ent, const nm_report_query_t *query, int page, qboolean print) {
	int i, count = 0, pages, seen = 0, first, last, actorLevel;
	int limit = NM_ReportLimit(query);
	time_t now = 0;
	char flag = query->kind == NM_REPORT_BANS ? 'B' : 's';
	if (!limit || !G_shrubbot_permission(ent, flag)) return -1;
	if (query->kind == NM_REPORT_BANS && (now = time(NULL)) == (time_t)-1) return -1;
	actorLevel = ent ? G_shrubbot_level(ent) : INT_MAX;
	for (i = 0; i < limit && NM_ReportPresent(query, i); ++i)
		if (NM_ReportMatches(query, ent, actorLevel, i, now)) ++count;
	pages = (count + NM_REPORT_PAGE_SIZE - 1) / NM_REPORT_PAGE_SIZE;
	if (pages > NM_REPORT_MAX_PAGES) pages = NM_REPORT_MAX_PAGES;
	if (!print) return pages;
	if (page < 1) page = 1;
	if (pages && page > pages) page = pages;
	first = (page - 1) * NM_REPORT_PAGE_SIZE;
	last = first + NM_REPORT_PAGE_SIZE;
	SBP_begin();
	if (query->kind == NM_REPORT_BANS)
		SBP("^dActive NQ shrubbot bans (use !baninfo <ban#>):\n");
	else if (query->kind == NM_REPORT_LEVELS)
		SBP("^dNQ shrubbot levels:\n");
	else
		SBP("^dVisible NQ admin records (not a Jaymod user database):\n");
	for (i = 0; i < limit && NM_ReportPresent(query, i); ++i) {
		char name[MAX_NAME_LENGTH];
		if (!NM_ReportMatches(query, ent, actorLevel, i, now)) continue;
		if (seen >= last) break;
		if (seen++ < first) continue;
		if (query->kind == NM_REPORT_BANS) {
			g_shrubbot_ban_t *ban = g_shrubbot_bans[i];
			NM_SafeText(ban->name, name, sizeof(name));
			SBP(va("^7#%d %s^7 (%s)\n", i + 1, name[0] ? name : "(unnamed)",
			       ban->expires ? "temporary" : "permanent"));
		} else if (query->kind == NM_REPORT_LEVELS) {
			g_shrubbot_level_t *record = g_shrubbot_levels[i];
			NM_SafeText(record->name, name, sizeof(name));
			SBP(va("^7%d: %s\n", record->level, name[0] ? name : "(unnamed)"));
		} else {
			g_shrubbot_admin_t *record = g_shrubbot_admins[i];
			NM_SafeText(record->name, name, sizeof(name));
			SBP(va("^7%s^7 (level %d)\n", name[0] ? name : "(unnamed)", record->level));
		}
	}
	if (query->kind == NM_REPORT_BANS)
		SBP(va("^9%d active ban(s) from ban #%d.\n", count, query->startBan));
	else
		SBP(va("^9%d matching record(s).\n", count));
	SBP_end();
	if (pages > 1) SP(va("^d--page %d of %d; use !page <number>\n", page, pages));
	if (count > NM_REPORT_PAGE_SIZE * NM_REPORT_MAX_PAGES)
		SP("^dReport limited to the first 1980 matching records; narrow the filter.\n");
	return pages;
}

static qboolean NM_ShowReport(gentity_t *ent, const nm_report_query_t *query) {
	int pages = NM_RunReport(ent, query, 1, qtrue);
	nm_report_query_t *slot = NM_ReportSlot(ent);
	if (pages < 0 || !slot) return qfalse;
	/* Like Jaymod, a short report does not displace the last long report. */
	if (pages > 1) *slot = *query;
	return qtrue;
}

qboolean G_nm_page(gentity_t *ent, int skiparg) {
	nm_report_query_t *slot = NM_ReportSlot(ent);
	int argc = Q_SayArgc() - skiparg, page, pages;
	char arg[32];
	if (argc < 1 || argc > 2 || !slot) goto usage;
	if (slot->kind == NM_REPORT_NONE) {
		SP("^dpage: ^9no saved long report is available\n");
		return qfalse;
	}
	pages = NM_RunReport(ent, slot, 1, qfalse);
	if (pages < 0) {
		memset(slot, 0, sizeof(*slot));
		SP("^dpage: ^9original report is no longer authorized or available\n");
		return qfalse;
	}
	if (argc == 1) {
		SP(va("^dpage: ^9you have %d page%s available\n", pages, pages == 1 ? "" : "s"));
		return qtrue;
	}
	Q_SayArgv(1 + skiparg, arg, sizeof(arg));
	if (!NM_ParseNumber(arg, &page)) goto usage;
	if (!pages) {
		SP("^dpage: ^9the report no longer has matching records\n");
		return qfalse;
	}
	return NM_RunReport(ent, slot, page, qtrue) >= 0;
usage:
	SP("^dpage: ^9usage: !page (number)\n");
	return qfalse;
}

qboolean G_nm_banlist(gentity_t *ent, int skiparg) {
	char arg[32];
	nm_report_query_t query;
	memset(&query, 0, sizeof(query));
	query.kind = NM_REPORT_BANS; query.startBan = 1; query.userLevel = -1;
	if (Q_SayArgc() < 1 + skiparg || Q_SayArgc() > 2 + skiparg) goto usage;
	if (Q_SayArgc() == 2 + skiparg) {
		Q_SayArgv(1 + skiparg, arg, sizeof(arg));
		if (!NM_ParseNumber(arg, &query.startBan) || query.startBan < 1 ||
		    query.startBan > MAX_SHRUBBOT_BANS) goto usage;
	}
	return NM_ShowReport(ent, &query);
usage:
	SP("^dbanlist: ^9usage: !banlist (start ban#)\n");
	return qfalse;
}

qboolean G_nm_baninfo(gentity_t *ent, int skiparg) {
	char arg[32], name[MAX_NAME_LENGTH], banner[MAX_NAME_LENGTH], reason[512];
	char made[50], guid[PB_GUID_LENGTH + 1], ip[18], remaining[64];
	g_shrubbot_ban_t *ban;
	int number, i;
	time_t now = time(NULL);
	long long seconds;
	if (Q_SayArgc() != 2 + skiparg || now == (time_t)-1) goto usage;
	Q_SayArgv(1 + skiparg, arg, sizeof(arg));
	if (!NM_ParseNumber(arg, &number) || number < 1 || number > MAX_SHRUBBOT_BANS) goto usage;
	/* The NQ list is dense. Do not index past its terminating NULL. */
	for (i = 0; i < number && i < MAX_SHRUBBOT_BANS && g_shrubbot_bans[i]; ++i) {}
	if (i != number) {
		SP("^dbaninfo: ^9ban number not found\n");
		return qfalse;
	}
	ban = g_shrubbot_bans[number - 1];
	NM_SafeText(ban->name, name, sizeof(name));
	NM_SafeText(ban->banner, banner, sizeof(banner));
	NM_SafeText(ban->reason, reason, sizeof(reason));
	NM_SafeText(ban->made, made, sizeof(made));
	NM_SafeText(ban->guid, guid, sizeof(guid));
	NM_SafeText(ban->ip, ip, sizeof(ip));
	if (!ban->expires) Q_strncpyz(remaining, "permanent", sizeof(remaining));
	else {
		seconds = (long long)ban->expires - ((long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET);
		if (seconds <= 0) Q_strncpyz(remaining, "expired", sizeof(remaining));
		else G_shrubbot_duration((int)(seconds > INT_MAX ? INT_MAX : seconds), remaining, sizeof(remaining));
	}
	SP(va("^dbaninfo: ^9NQ ban #%d: %s^9\n", number, name[0] ? name : "(unnamed)"));
	SP(va("^9Made: %s  Remaining: %s\n", made[0] ? made : "unknown", remaining));
	SP(va("^9By: %s  Reason: %s\n", banner[0] ? banner : "unknown", reason[0] ? reason : "none"));
	if (G_shrubbot_permission(ent, 'f'))
		SP(va("^9GUID: %s  IP: %s\n", guid[0] ? guid : "none", ip[0] ? ip : "none"));
	else
		SP("^9GUID and IP hidden (requires finger permission).\n");
	return qtrue;
usage:
	SP("^dbaninfo: ^9usage: !baninfo [ban#]\n");
	return qfalse;
}

qboolean G_nm_levlist(gentity_t *ent, int skiparg) {
	nm_report_query_t query;
	char option[32], name[MAX_STRING_CHARS];
	int argc = Q_SayArgc() - skiparg;
	memset(&query, 0, sizeof(query));
	query.kind = NM_REPORT_LEVELS; query.userLevel = -1;
	if (argc == 3) {
		Q_SayArgv(1 + skiparg, option, sizeof(option));
		Q_SayArgv(2 + skiparg, name, sizeof(name));
		if (Q_stricmp(option, "-name") || !name[0] || strlen(name) >= MAX_NAME_LENGTH) goto usage;
		NM_NormalizeReportName(name, query.name, sizeof(query.name));
		if (!query.name[0]) goto usage;
	} else if (argc != 1) goto usage;
	return NM_ShowReport(ent, &query);
usage:
	SP("^dlevlist: ^9usage: !levlist (-name name)\n");
	return qfalse;
}

/* NQ records contain admin assignments, not Jaymod's complete user history.
 * Limit the supported filters to fields NQ actually stores. */
qboolean G_nm_userlist(gentity_t *ent, int skiparg) {
	nm_report_query_t query;
	char option[32], value[MAX_STRING_CHARS];
	int argc = Q_SayArgc() - skiparg, i;
	qboolean haveName = qfalse, haveLevel = qfalse;
	memset(&query, 0, sizeof(query));
	query.kind = NM_REPORT_USERS; query.userLevel = -1;
	if (argc < 1 || argc > 5 || !(argc & 1)) goto usage;
	for (i = 1; i < argc; i += 2) {
		Q_SayArgv(i + skiparg, option, sizeof(option));
		Q_SayArgv(i + 1 + skiparg, value, sizeof(value));
		if (!Q_stricmp(option, "-name") && !haveName) {
			if (!value[0] || strlen(value) >= MAX_NAME_LENGTH) goto usage;
			NM_NormalizeReportName(value, query.name, sizeof(query.name));
			if (!query.name[0]) goto usage;
			haveName = qtrue;
		} else if (!Q_stricmp(option, "-level") && !haveLevel) {
			if (!NM_ParseNumber(value, &query.userLevel) || query.userLevel < 0) goto usage;
			haveLevel = qtrue;
		} else goto usage;
	}
	return NM_ShowReport(ent, &query);
usage:
	SP("^duserlist: ^9usage: !userlist (-name name) (-level level); NQ has no IP or last-seen filter\n");
	return qfalse;
}

qboolean G_nm_levinfo(gentity_t *ent, int skiparg) {
	char arg[32], name[MAX_NAME_LENGTH], flags[MAX_SHRUBBOT_FLAGS];
	int i, number;
	if (Q_SayArgc() != 2 + skiparg) goto usage;
	Q_SayArgv(1 + skiparg, arg, sizeof(arg));
	if (!NM_ParseNumber(arg, &number) || number < 0) goto usage;
	for (i = 0; i < MAX_SHRUBBOT_LEVELS && g_shrubbot_levels[i]; ++i) {
		g_shrubbot_level_t *record = g_shrubbot_levels[i];
		if (record->level != number) continue;
		NM_SafeText(record->name, name, sizeof(name));
		NM_SafeText(record->flags, flags, sizeof(flags));
		SP(va("^dlevinfo: ^9NQ level %d: %s^9\n", number, name[0] ? name : "(unnamed)"));
		SP(va("^9Flags: %s\n", flags[0] ? flags : "none"));
		return qtrue;
	}
	SP("^dlevinfo: ^9level not found\n");
	return qfalse;
usage:
	SP("^dlevinfo: ^9usage: !levinfo [level number]\n");
	return qfalse;
}

/* NewMod, Arena.ai, 2026-11-21: an Oz-style connected-user view, not
 * Jaymod's offline user database. Do not expose identifiers to callers
 * who lack NQ's finger flag, and never mutate the cached IP string. */
qboolean G_nm_userinfo(gentity_t *ent, int skiparg) {
	/* NewMod, Arena.ai: check fine-grained userinfo permission first */
	if (ent && !NM_FineGrainedAllowed(ent, SBF_CAN_VIEW_USERINFO)) {
		SP("^duserinfo: ^9you do not have permission to view user info\n");
		return qfalse;
	}
	int pids[MAX_CLIENT_MATCHES];
	char filter[MAX_STRING_CHARS], name[MAX_NAME_LENGTH];
	char guid[PB_GUID_LENGTH + 1], ip[MAX_IP_LENGTH];
	const char *team;
	gentity_t *victim;
	if (Q_SayArgc() != 2 + skiparg) goto usage;
	Q_SayArgv(1 + skiparg, filter, sizeof(filter));
	if (!filter[0] || strlen(filter) >= MAX_NAME_LENGTH || NM_Targets(filter, pids) != 1) {
		SP("^duserinfo: ^9target not found or name is ambiguous\n");
		return qfalse;
	}
	victim = &g_entities[pids[0]];
	if (!victim->inuse || !victim->client || victim->client->pers.connected != CON_CONNECTED) {
		SP("^duserinfo: ^9player is not connected\n");
		return qfalse;
	}
	if (!_shrubbot_admin_higher(ent, victim) ||
	    (ent && ent != victim && G_shrubbot_permission(victim, SBF_IMMUTABLE))) {
		SP("^duserinfo: ^9target is protected\n");
		return qfalse;
	}
	team = victim->client->sess.sessionTeam == TEAM_AXIS ? "axis" :
	       victim->client->sess.sessionTeam == TEAM_ALLIES ? "allies" : "spectator";
	NM_SafeText(victim->client->pers.netname, name, sizeof(name));
	SP(va("^duserinfo: ^9%s^9 (slot %d)\n", name[0] ? name : "(unnamed)", pids[0]));
	SP(va("^9Team: %s  Client: %s\n", team, (victim->r.svFlags & SVF_BOT) ? "bot" : "player"));
	if (G_shrubbot_permission(ent, 'f')) {
		NM_SafeText(victim->client->pers.cl_guid, guid, sizeof(guid));
		NM_SafeText(victim->client->pers.client_ip, ip, sizeof(ip));
		SP(va("^9GUID: %s  IP: %s\n", guid[0] ? guid : "none", ip[0] ? ip : "none"));
	} else {
		SP("^9GUID and IP hidden (requires finger permission).\n");
	}
	return qtrue;
usage:
	SP("^duserinfo: ^9usage: !userinfo [name|slot#]\n");
	return qfalse;
}
