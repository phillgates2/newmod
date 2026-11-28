/* NewMod, Arena.ai, 2026-11-21: changes in this file; see CHANGES. */
/*
 * name:g_shrubbot.c
 *
 * desc:	This code is the original work of Tony J. White
 *
 * 			The functionality of this code mimics the behaviour of the currently
 * 			inactive project shrubmod (http://www.etstats.com/shrubet/index.php?ver=2)
 * 			by Ryan Mannion.   However, shrubmod was a closed-source project and
 * 			none of its code has been copied, only its functionality.
 *
 * 			You are free to use this implementation of shrubbot in your
 * 			own mod project if you wish.
 *
 * NQQS:	HACK!
 * 			IRATA: Test & add code 'unknown' and 'NO_GUID' GUID case, this was never used w/o PB (R.I.P)
 *          ...fu! ... this is not trivial ... bans/mute/kick are based on that shit
 *          ONE UNKNOWN GUID FITS ALL :/
 *
 */
#include "g_local.h"
#include "../../etmain/ui/menudef.h"
#include <errno.h>

// IlDuca - patheticcockroach's fix for !nade overflow bug
int lol_flood_control = 0;

char *G_Shortcuts(gentity_t *ent, char *text);
static void NM_ShrubDisplay(const char *src, char *dst, size_t size);

extern char bigTextBuffer[100000];

// Note flag 'J' is free now - !ammopack & !medpack IS removed
static const struct g_shrubbot_cmd g_shrubbot_cmds[] =
{
	{"readconfig",	G_shrubbot_readconfig,	'G', 0,				"reloads the shrubbot config file and refreshes user flags", ""},
	{"time",		G_shrubbot_time,		'C', 0,				"displays the local time", "[^3name|slot#^9] (^hreason^9)"},
	{"setlevel",	G_shrubbot_setlevel,	's', 0,				"sets the admin level of a player", "[^3name|slot#^9] [^3level^9)"},
	{"kick",		G_shrubbot_kick,		'k', 0,				"kick a player with an optional reason", "^9(^hcommand^9)"},
	{"ban",			G_shrubbot_ban,			'b', 0,				"ban a player by NAME/slot with an optional expiration time (seconds) and reason", "[^3name|slot#^9] (^htime^9) (^hreason^9)"},
	{"banguid",		G_shrubbot_banguid,		'b', 0,				"ban a (perhaps not ingame) player by GUID with an optional expiration time (seconds) and reason", "[^3GUID^9] (^htime^9) (^hreason^9)"},
	{"banip",		G_shrubbot_banIP,		'b', 0,				"ban a (perhaps not ingame) player by IP with an optional expiration time (seconds) and reason", "[^3IP^9] (^htime^9) (^hreason^9)"},
	{"unban",		G_shrubbot_unban,		'b', 0,				"unbans a player specified ban number as seen in ^2!showbans^9", "[^3ban slot#^9]"},
	{"put",			G_shrubbot_putteam,		'p', 0,				"move a player to a specified team", "[^3name|slot#^9] [^3r|b|s^9]"},
	{"pause",		G_shrubbot_pause,		'Z', 0,				"pauses the game for all players", ""},
	{"unpause",		G_shrubbot_unpause,		'Z', 0,				"unpauses the game if it had been paused by ^2!pause^9", ""},
	{"listplayers",	G_shrubbot_listplayers,	'i', 0,				"display a list of connected players, their slot numbers as well as their admin levels",""},
	{"listteams",	G_shrubbot_listteams,	'I', 0, 			"displays info about the teams", ""},
	{"mute",		G_shrubbot_mute,		'm', 0,				"mutes a player", "[^3name|slot#^9]"},
	{"unmute",		G_shrubbot_unmute,		'm', 0,				"unmutes a muted player", "[^3name|slot#^9]"},
	{"showbans",	G_shrubbot_showbans,	'B', 0,				"display a (partial) list of active bans", "(^hstart at ban#^9) ((^hbanner^9) (^3banner's name^9)) ((^3find^9) (^hbanned player^9)) ((^3reason^9) (^hreason for ban^9))"},
	{"help",		G_shrubbot_help,		'h', 0,				"display commands available to you or help on a specific command", "^9(^hcommand^9)"},
	{"admintest",	G_shrubbot_admintest,	'a', 0,				"display your current admin level", ""},
	{"cancelvote",	G_shrubbot_cancelvote,	'c', 0,				"cancels the vote currently taking place", ""},
	{"passvote",	G_shrubbot_passvote,	'V', 0,				"passes the vote currently taking place", ""},
	{"spec999",		G_shrubbot_spec999,		'P', 0,				"moves 500-999 pingers to the spectator team", ""},
	{"shuffle",		G_shrubbot_shuffle,		'S', 0,				"shuffle the teams to try and even them", ""},
	{"rename",		G_shrubbot_rename,		'N', SCMDF_TYRANNY, "renames a player", "[^3name|slot#^9] [^3new name^9]"},
	{"gib",			G_shrubbot_gib,			'g', SCMDF_TYRANNY, "insantly gibs a player", "(^3name|slot#^9) (^hreason^9)"},
	{"slap",		G_shrubbot_slap,		'A', SCMDF_TYRANNY, "give a player a specified amount of damage for a specified reason", "[^3name|slot#^9] (^hdamage^9) (^hreason^9)"},
	{"burn",		G_shrubbot_burn,		'U', SCMDF_TYRANNY, "burns a player taking some of his health", "[^3name|slot#^9] (^hreason^9)"},
	{"warn",		G_shrubbot_warn,		'R', 0,				"warns a player; retains up to eight reasons for this connection", "[^3name|slot#^9] [^3reason^9]"},
	/* NewMod, Arena.ai, 2026-11-21: connection-local Oz warning subset. */
	{"dewarn",      G_nm_dewarn,            'R', 0, "list or remove a connection-local warning", "[name|slot#] (warning#)"},
	//{"news",		G_shrubbot_news,		'W', 0,				"play the map's news reel or another map's news reel if specified", "^9(^hmapname^9)"},
	{"lock",		G_shrubbot_lock,		'K', 0,				"lock one or all of the teams from players joining", "[^3r|b|s|all^7]"},
	{"unlock",		G_shrubbot_unlock,		'K', 0,				"unlock one or all locked teams", "^9[^3r|b|s|all^9]"},
	{"nade",		G_shrubbot_lol,			'x', SCMDF_TYRANNY, "makes a player drop a bunch of grenades or all players", "^9[^3name|slot#^9]"},
	{"pip",			G_shrubbot_pip,			'z', SCMDF_TYRANNY, "show sparks around a player or all players", "^9[^3name|slot#^9]"},
	{"pop",			G_shrubbot_pop,			'z', SCMDF_TYRANNY, "pops the helmets of a player or all players", "^9[^3name|slot#^9]"},
	{"restart",		G_shrubbot_reset,		'r', 0,				"restarts the current map", ""},
	{"reset",		G_shrubbot_reset,		'r', 0,				"resets the current match", ""},
	{"fling",		G_shrubbot_fling,		'L', SCMDF_TYRANNY, "flings a player in a random direction", "^9[^3name|slot#^9]"},
	{"throw",		G_shrubbot_fling,		'L', SCMDF_TYRANNY, "throws a player forward", "^9[^3name|slot#^9]"},
	{"launch",		G_shrubbot_fling,		'L', SCMDF_TYRANNY, "launch a player vertically", "^9[^3name|slot#^9]"},
	{"disorient",	G_shrubbot_disorient,	'd', SCMDF_TYRANNY, "flips a player's view", "^9[^3name|slot#^9] (^hreason^9)"},
	{"orient",		G_shrubbot_orient,		'd', SCMDF_TYRANNY, "unflips a disoriented player's view", "^9[^3name|slot#^9]"},
	{"resetxp",		G_shrubbot_resetxp,		'X', SCMDF_TYRANNY, "reset a player's XP", "^9[^3name|slot#^9] (^hreason^9)"},
	{"resetmyxp",   G_shrubbot_resetmyxp,   'M', 0,				"resets your own XP", ""},
	{"nextmap",     G_shrubbot_nextmap,     'n', 0,				"loads the next map", ""},
	{"swap",        G_shrubbot_swap,        'w', 0,				"swap teams", ""},

	// jet Pilot - new shrubbot commands
	{"revive",		G_shrubbot_revive,		'v', SCMDF_CHEAT, 	"revives a dead player", "^9[^3name|slot#^9] (^hreason^9)"},
#ifdef __CLIENTCMD__
	{"clientcmd",	G_shrubbot_clientcmd,	'j', SCMDF_TYRANNY, "can force a player to perform some commands", "^9[^3name|slot#^9] [^3command^9]"},
#endif
	{"rocket",		G_shrubbot_rocket,		'j', SCMDF_TYRANNY, "have a rocket shoot from the player who uses this command", ""},
	{"disguise",	G_shrubbot_disguise,	'T', SCMDF_CHEAT, 	"gives a player a disguise of the specified class", "^9[^3name|slot#^9] (^hclass number^9)"},
	{"poison",		G_shrubbot_poison,		'U', SCMDF_TYRANNY, "poisons a player", "^9(^3name|slot#^9) (^hreason^9)"},
	{"pants",		G_shrubbot_pants,		't', SCMDF_TYRANNY, "removes a player's pants!", "^9(^3name|slot#^9)"},
	{"give",		G_shrubbot_give,		'e', SCMDF_CHEAT, 	"gives a player something...", "^9[^3name|slot#^9] item/thing"},

	{"dw",			G_shrubbot_dropweapon,	'D', SCMDF_TYRANNY, "drops a player's primary and secondary weapons!", "^9(^3name|slot#^9)"},
	// Jaybird
	{"finger",		G_shrubbot_finger,		'f', 0,				"gives specific information about a player", "^9[^3name|slot#^9]"},
	{"uptime",		G_shrubbot_uptime,		'u', 0,				"displays server uptime", "" },
	{"glow",		G_shrubbot_glow,		'o', SCMDF_TYRANNY, "makes player(s) glow", "^9(^3name|slot#^9)"},
	{"freeze",		G_shrubbot_freeze,		'E', SCMDF_TYRANNY, "freezes player(s) move", "^9(^3name|slot#^9)"},
	{"unfreeze",	G_shrubbot_unfreeze,	'E', SCMDF_TYRANNY, "makes player(s) moving again", "^9(^3name|slot#^9)"},
	{"weapons",		G_shrubbot_weapons,		'h', 0, 			"displays a list of disabled or restricted weapons"},
	/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): selected Oz/Jaymod command adaptations. */
	{"putteam",     G_shrubbot_putteam,      'p', 0, "move a player to a team (alias for put)", "[name|slot#] [r|b|s]"},
	{"lol",         G_shrubbot_lol,          'x', SCMDF_TYRANNY, "drop grenades (alias for nade)", "(name|slot#)"},
	{"giba",        G_shrubbot_giba,         'Q', SCMDF_TYRANNY, "gib eligible players", ""},
	{"flinga",      G_shrubbot_fling,        'L', SCMDF_TYRANNY, "fling eligible players", ""},
	{"throwa",      G_shrubbot_fling,        'L', SCMDF_TYRANNY, "throw eligible players", ""},
	{"launcha",     G_shrubbot_fling,        'L', SCMDF_TYRANNY, "launch eligible players", ""},
	{"panzerwar",   G_shrubbot_war,          'q', 0, "set or show panzer war mode", "(on|off)"},
	{"sniperwar",   G_shrubbot_war,          'q', 0, "set or show sniper war mode", "(on|off)"},
	{"riflewar",    G_shrubbot_war,          'q', 0, "set or show rifle war mode", "(on|off)"},
	{"spec",        G_shrubbot_spec,         'Q', 0, "spectate a player", "[name|slot#]"},
	{"spawn",       G_nm_spawn,              '%', SCMDF_CHEAT, "reinforce eligible players in limbo", "(name|slot#|all)"},
	{"report",      G_shrubbot_report,       'h', 0, "privately report a player to admins", "[name|slot#] [reason]"},
	{"crazygravity",G_nm_crazygravity,       'q', 0, "change gravity randomly every 30 seconds", "(on|off)"},
	{"shake",       G_nm_shake,              'A', SCMDF_TYRANNY, "shake eligible players' cameras", "(name|slot#) (seconds)"},
	{"splat",       G_nm_splat,              'g', SCMDF_TYRANNY, "splatter a player", "[name|slot#]"},
	{"splata",      G_nm_splata,             'Q', SCMDF_TYRANNY, "splatter eligible players", ""},
	{"smite",       G_nm_smite,              'U', SCMDF_TYRANNY, "set eligible players on fire", "(name|slot#)"},
	{"firegod",     G_nm_firegod,            'e', SCMDF_TYRANNY, "toggle flaming god mode for a player", "(name|slot#) [noclip]"},
	{"news",        G_nm_news,               'W', 0, "play a map's available news reel", "(mapname)"},
	{"about",       G_nm_about,              'h', 0, "show mod information", ""},
	{"status",      G_nm_status,             'i', 0, "show server resource usage", ""},
	{"lsplayers",   G_nm_lsplayers,          'i', 0, "list connected players", ""},
	{"spree",       G_nm_spree,              'h', 0, "show your current spree", ""},
	{"tspree",      G_nm_tspree,             'h', 0, "show the top current sprees", "(count)"},
	{"spreerecord", G_nm_spreerecord,        'E', 0, "show this map's nonpersistent NQ-derived spree record", ""},
	/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): explicit NQ-backed adaptations,
	 * not Jaymod's user database or its unrestricted remote-control commands. */
	{"stats",       G_nm_stats,              't', 0, "show connected players' MP40 and Thompson accuracy", ""},
	{"ftime",       G_nm_ftime,              'C', 0, "show the server time with a little art", ""},
	{"chicken",     G_nm_fun_sound,          'W', 0, "play the chicken sound for an eligible player", "[name|slot#]"},
	{"crybaby",     G_nm_fun_sound,          'W', 0, "play the crybaby sound for an eligible player", "[name|slot#]"},
	{"banlist",     G_nm_banlist,            'B', 0, "list active NQ bans", "(start ban#)"},
	{"baninfo",     G_nm_baninfo,            'B', 0, "show an NQ ban's details", "[ban#]"},
	{"levlist",     G_nm_levlist,            's', 0, "list NQ shrubbot levels", ""},
	{"levinfo",     G_nm_levinfo,            's', 0, "show an NQ level's name and flags", "[level]"},
	{"userinfo",    G_nm_userinfo,           'e', 0, "show information about a connected player", "[name|slot#]"},
	{"userlist",    G_nm_userlist,           's', 0, "list visible NQ admin records", "(-name name) (-level level)"},
	{"page",        G_nm_page,               'h', 0, "view a page from the latest long report", "(number)"},
	{"seen",        G_nm_seen,               'i', 0, "find a visible admin online or recently disconnected", "[name]"},
	{"",			NULL,					'\0', 0}
};

/* Each dense list reserves an extra NULL slot for the inherited sentinel
 * walks, including when the configured maximum is completely full. */
g_shrubbot_level_t		*g_shrubbot_levels[MAX_SHRUBBOT_LEVELS + 1];
g_shrubbot_admin_t		*g_shrubbot_admins[MAX_SHRUBBOT_ADMINS + 1];
g_shrubbot_ban_t		*g_shrubbot_bans[MAX_SHRUBBOT_BANS + 1];
g_shrubbot_ban_t		*g_shrubbot_mutes[MAX_SHRUBBOT_BANS + 1];
g_shrubbot_command_t	*g_shrubbot_commands[MAX_SHRUBBOT_COMMANDS + 1];

/* Placeholders are shared by unrelated clients; never treat them as a
 * persistent identity. This is only a lookup-key check, not authentication. */
qboolean G_shrubbot_usable_guid(const char *guid) {
	return guid && *guid && Q_stricmp(guid, "unknown") &&
	       Q_stricmp(guid, "NO_GUID") && Q_stricmp(guid, "offline");
}

/* Ban durations are parsed as int and multiplied by a week/day/hour/minute
 * modifier. Keep the product and the resulting epoch in range instead of
 * overflowing the signed int and the persisted 32-bit expiry field. */
static int NM_ScaledBanSeconds(int seconds, int modifier) {
	long long product = (long long)seconds * modifier;
	return product > INT_MAX ? INT_MAX : (int)product;
}

static int NM_BanExpiration(time_t now, int seconds) {
	long long base = (long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET;
	long long expiry = base + seconds;
	if (seconds == 0) return 0;
	if (expiry > INT_MAX) return INT_MAX;
	if (expiry < 1) return 1;
	return (int)expiry;
}

/* NQ's offline IP bans use partial substring matching, so accept only a
 * well-formed IPv4 address. This prevents an arbitrary short string from
 * matching every address that contains it. */
static qboolean NM_BanIPValid(const char *ip) {
	int value = 0, parts = 0;
	qboolean octetStart = qtrue;
	const char *p = ip;
	if (!ip || !*ip) return qfalse;
	for (;;) {
		if (*p < '0' || *p > '9') return qfalse;
		if (octetStart && *p == '0' && p[1] >= '0' && p[1] <= '9') return qfalse; /* no leading zero */
		if (octetStart && *p != '0') octetStart = qfalse;
		value = value * 10 + (*p - '0');
		if (value > 255) return qfalse;
		++p;
		if (*p == '.') {
			if (parts == 3) return qfalse;
			++parts;
			value = 0;
			octetStart = qtrue;
			++p;
			continue;
		}
		if (!*p) {
			return parts == 3 && value <= 255;
		}
		/* More digits in the same octet: keep accumulating. */
	}
}

/* Return 1 for allow, 0 for explicit deny, or -1 for no decision. NQ
 * individual flags support -denials+resumed grants; wildcard suffixes are
 * exclusions. Never walk beyond the terminating NUL on a trailing '-'. */
static int NM_ShrubFlagDecision(const char *flags, char flag, qboolean allowDenials) {
	const char *p = flags;
	while (*p) {
		if (*p == flag) return 1;
		if (allowDenials && *p == '-') {
			for (++p; *p && *p != '+'; ++p)
				if (*p == flag) return 0;
			if (*p == '+') ++p;
			continue;
		}
		if (*p == '*') {
			for (++p; *p; ++p) if (*p == flag) return 0;
			return flag != SBF_IMMUTABLE && flag != SBF_INCOGNITO;
		}
		++p;
	}
	return -1;
}


/* NewMod, Arena.ai: fine-grained SBF_CAN_* flags are deny-only. If the
 * flag character appears in the admin's flag string (as an allow or
 * denial), use that decision. If absent, the action is permitted (the
 * level-based hierarchy already gates it). This preserves backward
 * compatibility with existing configs that do not set these flags. */
qboolean NM_FineGrainedAllowed(gentity_t *ent, char flag) {
	int i, l = 0;
	const char *guid;

	if (!ent) return qtrue; /* console always wins */
	guid = level.clients[ent - g_entities].pers.cl_guid;
	if (G_shrubbot_usable_guid(guid)) {
		for (i = 0; g_shrubbot_admins[i]; ++i) {
			if (Q_stricmp(guid, g_shrubbot_admins[i]->guid)) continue;
			if (strchr(g_shrubbot_admins[i]->flags, flag)) {
				return NM_ShrubFlagDecision(g_shrubbot_admins[i]->flags, flag, qtrue) ? qtrue : qfalse;
			}
			l = g_shrubbot_admins[i]->level;
		}
	}
	for (i = 0; g_shrubbot_levels[i]; ++i) {
		if (g_shrubbot_levels[i]->level != l) continue;
		if (strchr(g_shrubbot_levels[i]->flags, flag)) {
			return NM_ShrubFlagDecision(g_shrubbot_levels[i]->flags, flag, qfalse) ? qtrue : qfalse;
		}
	}
	return qtrue; /* not set: allow */
}
qboolean G_shrubbot_permission(gentity_t *ent, char flag) {
	int i, l = 0, decision;
	const char *guid;
	if (!ent) return qtrue; /* console always wins */
	guid = level.clients[ent - g_entities].pers.cl_guid;
	if (G_shrubbot_usable_guid(guid)) {
		for (i = 0; g_shrubbot_admins[i]; ++i) {
			if (Q_stricmp(guid, g_shrubbot_admins[i]->guid)) continue;
			decision = NM_ShrubFlagDecision(g_shrubbot_admins[i]->flags, flag, qtrue);
			if (decision >= 0) return decision ? qtrue : qfalse;
			l = g_shrubbot_admins[i]->level;
		}
	}
	for (i = 0; g_shrubbot_levels[i]; ++i) {
		if (g_shrubbot_levels[i]->level != l) continue;
		decision = NM_ShrubFlagDecision(g_shrubbot_levels[i]->flags, flag, qfalse);
		if (decision >= 0) return decision ? qtrue : qfalse;
	}
	return qfalse;
}

qboolean _shrubbot_admin_higher(gentity_t *admin, gentity_t *victim) {
	int i;
	int alevel = 0;
	char *guid;

	// console always wins
	if(!admin) return qtrue;
	// just in case
	if(!victim) return qtrue;

	guid = level.clients[admin-g_entities].pers.cl_guid;
	for(i=0; g_shrubbot_admins[i]; i++) {
		if(G_shrubbot_usable_guid(guid) && !Q_stricmp(guid, g_shrubbot_admins[i]->guid)) {
			alevel = g_shrubbot_admins[i]->level;
		}
	}
	guid = level.clients[victim-g_entities].pers.cl_guid;
	for(i=0; g_shrubbot_admins[i]; i++) {
		if(G_shrubbot_usable_guid(guid) && !Q_stricmp(guid, g_shrubbot_admins[i]->guid)) {
			if(alevel < g_shrubbot_admins[i]->level)
				return qfalse;
		}
	}
	return qtrue;
}

/* NewMod, Arena.ai, 2026-11-21: serialize untrusted names/reasons as one
 * quoted field, never as config syntax. NQ's paired escaped-quote grammar
 * cannot represent arbitrary unmatched quotes, so replace quotes, backslashes
 * and control bytes in data fields. Trusted command templates are separate. */
void G_shrubbot_writeconfig_string(char *s, fileHandle_t f) {
	char buf[MAX_STRING_CHARS + 3];
	size_t used = 0;
	const unsigned char *p = (const unsigned char *)(s ? s : "");
	buf[used++] = '"';
	while (*p && used < sizeof(buf) - 3) {
		unsigned char c = *p++;
		buf[used++] = (c < 32 || c == 127 || c == '\\') ? ' ' : c == '"' ? '\'' : (char)c;
	}
	buf[used++] = '"';
	buf[used++] = '\n';
	trap_FS_Write(buf, (int)used, f);
}

/* A template is trusted server configuration, but must round-trip without
 * changing which commands are executable. Do not truncate the file if its
 * quotes/controls cannot be represented faithfully by NQ's parser. */
static qboolean NM_ConfigTemplateWritable(const char *s) {
	qboolean quoted = qfalse;
	const unsigned char *p = (const unsigned char *)s;
	if (!p) return qfalse;
	while (*p) {
		if (*p < 32 || *p == 127) return qfalse;
		if (*p == '"') quoted = !quoted;
		++p;
	}
	return !quoted && (p == (const unsigned char *)s || p[-1] != '\\');
}

static void NM_WriteConfigTemplate(const char *s, fileHandle_t f) {
	char buf[2 * MAX_STRING_CHARS + 3];
	size_t used = 0;
	buf[used++] = '"';
	while (*s && used < sizeof(buf) - 4) {
		if (*s == '"') buf[used++] = '\\';
		buf[used++] = *s++;
	}
	buf[used++] = '"';
	buf[used++] = '\n';
	trap_FS_Write(buf, (int)used, f);
}

void G_shrubbot_writeconfig_int(int v, fileHandle_t f) {
	char buf[32];

	Com_sprintf(buf, 32, "%d", v);
	if(buf[0]) trap_FS_Write(buf, strlen(buf), f);
	trap_FS_Write("\n", 1, f);
}

/* IRATA: unused
void G_shrubbot_writeconfig_float(float v, fileHandle_t f)
{
	char buf[32];

	Com_sprintf(buf, 32, "%f", v);
	if(buf[0]) trap_FS_Write(buf, strlen(buf), f);
	trap_FS_Write("\n", 1, f);
}
*/


// Note: since 1.3.0 levels are not permitted if a user has 'NO_GUID'
// TODO: don't write unknown guid entries !
extern "C" void _shrubbot_writeconfig() {
	fileHandle_t f;
	int len, i, j;
	time_t t;
	char levels[MAX_STRING_CHARS] = {""};

	if (nq_noq.integer & NOQ_DONT_WRITE) return;
	if(!g_shrubbot.string[0]) return;
	for (i = 0; g_shrubbot_commands[i]; ++i) {
		if (!NM_ConfigTemplateWritable(g_shrubbot_commands[i]->exec)) {
			G_Printf("_shrubbot_writeconfig: command template cannot be serialized safely; file unchanged\n");
			return;
		}
	}

	if (time(&t) == (time_t)-1) {
		G_Printf("_shrubbot_writeconfig: clock unavailable; file unchanged\n");
		return;
	}
	t = t - SHRUBBOT_BAN_EXPIRE_OFFSET;
	f = 0;
	len = trap_FS_FOpenFile(g_shrubbot.string, &f, FS_WRITE);
	if(len < 0 || !f) {
		G_Printf("_shrubbot_writeconfig: could not open %s\n", g_shrubbot.string);
		return;
	}
	for(i=0; g_shrubbot_levels[i]; i++) {
		trap_FS_Write("[level]\n", 8, f);
		trap_FS_Write("level   = ", 10, f);
		G_shrubbot_writeconfig_int(g_shrubbot_levels[i]->level, f);
		trap_FS_Write("name    = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_levels[i]->name, f);
		trap_FS_Write("flags   = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_levels[i]->flags, f);
		trap_FS_Write("\n", 1, f);
	}
	for(i=0; g_shrubbot_admins[i]; i++) {
		// don't write level 0 users
		if(g_shrubbot_admins[i]->level < 1) continue;
		trap_FS_Write("[admin]\n", 8, f);
		trap_FS_Write("name    = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_admins[i]->name, f);
		trap_FS_Write("guid    = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_admins[i]->guid, f);
		trap_FS_Write("level   = ", 10, f);
		G_shrubbot_writeconfig_int(g_shrubbot_admins[i]->level, f);
		trap_FS_Write("flags   = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_admins[i]->flags, f);
		trap_FS_Write("\n", 1, f);
	}
	for(i=0; g_shrubbot_bans[i]; i++) {
		// don't write expired bans
		// if expires is 0, then it's a perm ban
		if(g_shrubbot_bans[i]->expires != 0 &&
			(g_shrubbot_bans[i]->expires - t) < 1) continue;

		trap_FS_Write("[ban]\n", 6, f);
		trap_FS_Write("name    = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_bans[i]->name, f);
		trap_FS_Write("guid    = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_bans[i]->guid, f);
#ifdef HW_BAN
		trap_FS_Write("hwguid  = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_bans[i]->hwguid, f);
#endif // HW_BAN
		trap_FS_Write("ip      = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_bans[i]->ip, f);
		trap_FS_Write("reason  = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_bans[i]->reason, f);
		trap_FS_Write("made    = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_bans[i]->made, f);
		trap_FS_Write("expires = ", 10, f);
		G_shrubbot_writeconfig_int(g_shrubbot_bans[i]->expires, f);
		trap_FS_Write("banner  = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_bans[i]->banner, f);
		trap_FS_Write("\n", 1, f);
	}
	for(i=0; g_shrubbot_mutes[i]; i++) {
		if(g_shrubbot_mutes[i]->expires != 0 &&
			(g_shrubbot_mutes[i]->expires - t) < 1) continue;

		trap_FS_Write("[mute]\n", 7, f);
		trap_FS_Write("name    = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_mutes[i]->name, f);
		trap_FS_Write("guid    = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_mutes[i]->guid, f);
		trap_FS_Write("ip      = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_mutes[i]->ip, f);
		trap_FS_Write("reason  = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_mutes[i]->reason, f);
		trap_FS_Write("made    = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_mutes[i]->made, f);
		trap_FS_Write("expires = ", 10, f);
		G_shrubbot_writeconfig_int(g_shrubbot_mutes[i]->expires, f);
		trap_FS_Write("muter   = ", 10, f);
		G_shrubbot_writeconfig_string(g_shrubbot_mutes[i]->banner, f);
		trap_FS_Write("\n", 1, f);
	}
	for(i=0; g_shrubbot_commands[i]; i++) {
			levels[0] = '\0';
			trap_FS_Write("[command]\n", 10, f);
			trap_FS_Write("command = ", 10, f);
			G_shrubbot_writeconfig_string(g_shrubbot_commands[i]->command, f);
			trap_FS_Write("exec    = ", 10, f);
			NM_WriteConfigTemplate(g_shrubbot_commands[i]->exec, f);
			trap_FS_Write("desc    = ", 10, f);
			G_shrubbot_writeconfig_string(g_shrubbot_commands[i]->desc, f);
			trap_FS_Write("levels  = ", 10, f);
			for(j=0; g_shrubbot_commands[i]->levels[j] != -1; j++) {
			Q_strcat(levels, sizeof(levels),
			va("%i ", g_shrubbot_commands[i]->levels[j]));
			}
		G_shrubbot_writeconfig_string(levels, f);
		trap_FS_Write("\n", 1, f);
	}
	trap_FS_FCloseFile(f);
}

/* Consume the whole field even on overflow, and report truncation to the
 * transactional shrubbot reader rather than loading a partial command/ACL. */
static qboolean NM_ReadConfigString(char **cnf, char *s, int size) {
	char *t = COM_ParseExt(cnf, qfalse);
	size_t used = 0;
	qboolean ok = qtrue;

	if(!strcmp(t, "=")) {
		t = COM_ParseExt(cnf, qfalse);
	} else {
		G_Printf("^dreadconfig: ^9warning missing = before \"%s\" on line %d\n",
		         t, COM_GetCurrentParseLine());
	}
	s[0] = '\0';
	while(t[0]) {
		size_t tokenLen = strlen(t), separator = used ? 1 : 0;
		if (used + separator + tokenLen >= (size_t)size) {
			ok = qfalse;
		} else if (ok) {
			if (separator) s[used++] = ' ';
			memcpy(s + used, t, tokenLen);
			used += tokenLen;
			s[used] = '\0';
		}
		t = COM_ParseExt(cnf, qfalse);
	}
	return ok && !COM_ParseHadError();
}

void G_shrubbot_readconfig_string(char **cnf, char *s, int size) {
	(void)NM_ReadConfigString(cnf, s, size);
}

void G_shrubbot_readconfig_int(char **cnf, int *v) {
	char *t = COM_ParseExt(cnf, qfalse);

	if(!strcmp(t, "=")) {
		t = COM_ParseExt(cnf, qfalse);
	}
	else {
		G_Printf("^dreadconfig: ^9warning missing = before "
				"\"%s\" on line %d\n",
				t,
				COM_GetCurrentParseLine());
	}
	*v = atoi(t);
}

void G_shrubbot_readconfig_float(char **cnf, float *v) {
	char *t = COM_ParseExt(cnf, qfalse);
	if(!strcmp(t, "=")) {
		t = COM_ParseExt(cnf, qfalse);
	}
	else {
		G_Printf("readconfig: warning missing = before "
				"\"%s\" on line %d\n",
				t,
				COM_GetCurrentParseLine());
	}
	*v = atof(t);
}


/* Populate an empty list with NQ's defaults, without changing a live list
 * if allocation fails during a config reload. */
static qboolean NM_DefaultLevels(g_shrubbot_level_t **out) {
	static const char *flags[] = {"iahC", "iahCp", "iahCpP", "i1ahCpPkm", "i1ahCpPkmBb", "i1ahCpPkmBbs"};
	int i;
	for (i = 0; i < (int)(sizeof(flags) / sizeof(flags[0])); ++i) {
		out[i] = (g_shrubbot_level_t*)calloc(1, sizeof(*out[i]));
		if (!out[i]) {
			while (i > 0) { --i; free(out[i]); out[i] = NULL; }
			return qfalse;
		}
		out[i]->level = i;
		Q_strncpyz(out[i]->flags, flags[i], sizeof(out[i]->flags));
	}
	return qtrue;
}

/*
	return a level for a player entity.
*/
int G_shrubbot_level(gentity_t *ent) {
	int i;
	char *guid;

	if(!ent) {
		// forty - we are on the console, return something high for now.
		return MAX_SHRUBBOT_LEVELS;
	}

	guid = level.clients[ent-g_entities].pers.cl_guid;

	// note: if there is no guid, the level should be 0

	for(i=0; g_shrubbot_admins[i]; i++) {
		if(G_shrubbot_usable_guid(guid) && !Q_stricmp(g_shrubbot_admins[i]->guid, guid)) {
			return g_shrubbot_admins[i]->level;
		}
	}
	// if looped through all admins and none fits:
	return 0;
}

/*
	_shrubbot_command_permission

	Custom [command] entries execute their configured exec text as server
	console commands. As in the inherited implementation, only the configured
	level list is checked here; nested/native command flags and target
	permissions are NOT rechecked. Treat each exec template and each level
	allowed to invoke it as privileged server configuration (see CHANGES).
*/
static qboolean _shrubbot_command_permission(gentity_t *ent, char *command) {
	int i, j;
	int level = G_shrubbot_level(ent);

	if (!ent) {
		return qtrue;
	}
	for (i=0; g_shrubbot_commands[i]; i++) {
		if (!Q_stricmp(command, g_shrubbot_commands[i]->command)) {
			for (j = 0; g_shrubbot_commands[i]->levels[j] != -1; j++) {
				if (g_shrubbot_commands[i]->levels[j] == level) {

					/*
					// core: The level is OK, now check the level of the command that is really executed.. (the one in the "exec = " line)
					// If that command is a native shrub-command, then the permissions of that command must be applied, finally..
					if ( !G_shrubbot_native_cmd_check(ent,command,0) ) {
						return qfalse;
					}
					*/

					return qtrue;
				}
			}
		}
	}
	return qfalse;
}

void _shrubbot_log(gentity_t *admin, char *cmd, int skiparg) {
	fileHandle_t f;
	int len;
	char string[MAX_STRING_CHARS];
	int	min, tens, sec;
	g_shrubbot_admin_t *a;
	g_shrubbot_level_t *l;
	char flags[MAX_SHRUBBOT_FLAGS*2];
	gentity_t *victim = NULL;
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH];

	if(!g_logAdmin.string[0]) return;

	len = trap_FS_FOpenFile(g_logAdmin.string, &f, FS_APPEND);
	if(len < 0) {
		G_Printf("_shrubbot_log: error could not open %s\n",
			g_logAdmin.string);
		return;
	}

	sec = level.time / SECONDS_1;
	min = sec / 60;
	sec -= min * 60;
	tens = sec / 10;
	sec -= tens * 10;

	*flags = '\0';
	if(admin) {
		int i,j;
		for(i=0; g_shrubbot_admins[i]; i++) {
			if(G_shrubbot_usable_guid(admin->client->pers.cl_guid) &&
			   !Q_stricmp(g_shrubbot_admins[i]->guid, admin->client->pers.cl_guid)) {

				a = g_shrubbot_admins[i];
				Q_strncpyz(flags, a->flags, sizeof(flags));
				for(j=0; g_shrubbot_levels[j]; j++) {
					if(g_shrubbot_levels[j]->level == a->level) {
						l = g_shrubbot_levels[j];
						Q_strcat(flags, sizeof(flags), l->flags);
						break;
					}
				}
				break;
			}
		}
	}

	if(Q_SayArgc() > 1+skiparg) {
		Q_SayArgv(1+skiparg, name, sizeof(name));
		if(ClientNumbersFromString(name, pids) == 1) {
			victim = &g_entities[pids[0]];
		}
	}

	if(victim && Q_stricmp(cmd, "attempted")) {
		Com_sprintf(string, sizeof(string),
			"%3i:%i%i: %i: %s: %s: %s: %s: %s: %s: \"%s\"\n",
			min,
			tens,
			sec,
			(admin) ? admin->s.clientNum : -1,
			(admin) ? admin->client->pers.cl_guid
				: "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
			(admin) ? admin->client->pers.netname : "console",
			flags,
			cmd,
			victim->client->pers.cl_guid,
			victim->client->pers.netname,
			Q_SayConcatArgs(2+skiparg));
	}
	else {
		Com_sprintf(string, sizeof(string),
			"%3i:%i%i: %i: %s: %s: %s: %s: \"%s\"\n",
			min,
			tens,
			sec,
			(admin) ? admin->s.clientNum : -1,
			(admin) ? admin->client->pers.cl_guid
				: "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
			(admin) ? admin->client->pers.netname : "console",
			flags,
			cmd,
			Q_SayConcatArgs(1+skiparg));
	}
	/* NewMod, Arena.ai, 2026-11-21: arguments and names can contain
	 * control bytes. Keep each admin command to one log line, including
	 * when the formatted entry was truncated. This does not make the
	 * logged command/reason private or sanitize other engine/chat logs. */
	len = (int)strlen(string);
	for (int j = 0; j < len; ++j) {
		unsigned char c = (unsigned char)string[j];
		if ((c < 32 || c == 127) && !(c == '\n' && j == len - 1)) string[j] = ' ';
	}
	if (len == 0 || string[len - 1] != '\n') {
		if (len >= (int)sizeof(string) - 1) len = (int)sizeof(string) - 2;
		string[len++] = '\n';
		string[len] = '\0';
	}
	trap_FS_Write(string, len, f);
	trap_FS_FCloseFile(f);
}

#define SECONDS_IN_YEAR		31536000
#define SECONDS_IN_MONTH	2592000
#define SECONDS_IN_WEEK		604800
#define SECONDS_IN_DAY		86400
#define SECONDS_IN_HOUR		3600
#define SECONDS_IN_MINUTE	60

void jP_GetDurationString(int secs, char *dur, int len) {
	int		 years, months, weeks, days, hours, minutes;
	qboolean addComma = qfalse;
	char temp[512];

	if (secs < 0) {
		Q_strncpyz(dur, "PERMANENT", len);
		return;
	}

	years   = secs > SECONDS_IN_YEAR   ? (secs / SECONDS_IN_YEAR)   : 0;
	if (years) secs -= SECONDS_IN_YEAR * years;
	months  = secs > SECONDS_IN_MONTH  ? (secs / SECONDS_IN_MONTH)  : 0;
	if (months) secs -= SECONDS_IN_MONTH * months;
	weeks   = secs > SECONDS_IN_WEEK   ? (secs / SECONDS_IN_WEEK)   : 0;
	if (weeks) secs -= SECONDS_IN_WEEK * weeks;
	days    = secs > SECONDS_IN_DAY    ? (secs / SECONDS_IN_DAY)    : 0;
	if (days) secs -= SECONDS_IN_DAY * days;
	hours   = secs > SECONDS_IN_HOUR   ? (secs / SECONDS_IN_HOUR)   : 0;
	if (hours) secs -= SECONDS_IN_HOUR * hours;
	minutes = secs > SECONDS_IN_MINUTE ? (secs / SECONDS_IN_MINUTE) : 0;
	if (minutes) secs -= SECONDS_IN_MINUTE * minutes;

	if (years) {
		Q_strncpyz(temp, va("%i year%s", years, years > 1 ? "s" : ""), 512);
		addComma = qtrue;
	}

	if (months) {
		Q_strcat(temp, 512, va("%s%i month%s", addComma ? (weeks > 0 ? ", " : " and ") : "", months, months > 1 ? "s" : ""));
		addComma = qtrue;
	}

	if (weeks) {
		Q_strcat(temp, 512, va("%s%i week%s", addComma ? (days > 0 ? ", " : " and ") : "", weeks, weeks > 1 ? "s" : ""));
		addComma = qtrue;
	}

	if (days) {
		Q_strcat(temp, 512, va("%s%i day%s", addComma ? (hours > 0 ? ", " : " and ") : "", days, days > 1 ? "s" : ""));
		addComma = qtrue;
	}

	if (hours) {
		Q_strcat(temp, 512, va("%s%i hour%s", addComma ? (minutes > 0 ? ", " : " and ") : "", hours, hours > 1 ? "s" : ""));
		addComma = qtrue;
	}

	if (minutes) {
		Q_strcat(temp, 512, va("%s%i minute%s", addComma ? (secs > 0 ? ", " : " and ") : "", minutes, minutes > 1 ? "s" : ""));
		addComma = qtrue;
	}

	if (secs) {
		Q_strcat(temp, 512, va("%s%i second%s", addComma ? " and " : "", secs, secs > 1 ? "s" : ""));
	}

	Q_strncpyz(dur, temp, len);
}

void G_shrubbot_duration(int secs, char *duration, int dursize) {

	if(secs > (SECONDS_IN_YEAR * 50) || secs < 0) {
		Q_strncpyz(duration, "PERMANENT", dursize);
	}
	else if(secs >= SECONDS_IN_YEAR) {
		Com_sprintf(duration, dursize, "%d years", (secs / SECONDS_IN_YEAR));
	}
	else if(secs >= SECONDS_IN_MONTH) {
		Com_sprintf(duration, dursize, "%i months",	(secs / SECONDS_IN_MONTH));
	}
	else if(secs >= SECONDS_IN_WEEK) {
		Com_sprintf(duration, dursize, "%i weeks", (secs / SECONDS_IN_WEEK));
	}
	else if(secs >= SECONDS_IN_DAY) {
		Com_sprintf(duration, dursize, "%i days", (secs / SECONDS_IN_DAY));
	}
	else if(secs >= SECONDS_IN_HOUR) {
		Com_sprintf(duration, dursize, "%i hours", (secs / SECONDS_IN_HOUR));
	}
	else if(secs >= SECONDS_IN_MINUTE) {
		Com_sprintf(duration, dursize, "%i mins", (secs / SECONDS_IN_MINUTE));
	}
	else {
		Com_sprintf(duration, dursize, "%i secs", secs);
	}
}

qboolean G_shrubbot_ban_check(char const *ip, char const *guid, char const *reason) {
	time_t now;
	long long relativeNow;
	int i;
	if (time(&now) == (time_t)-1) return qfalse;
	relativeNow = (long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET;
	for (i = 0; i < MAX_SHRUBBOT_BANS && g_shrubbot_bans[i]; ++i) {
		const g_shrubbot_ban_t *ban = g_shrubbot_bans[i];
		qboolean guidMatch, ipMatch;
		if (ban->expires && (long long)ban->expires <= relativeNow) continue;
		/* NQ's offline ban-IP records use "offline" as a fake GUID. Empty,
		 * unknown and placeholder keys must never ban every such client. */
		guidMatch = G_shrubbot_usable_guid(guid) && !Q_stricmp(ban->guid, guid);
		/* Retain NQ's partial-IP matching for existing configurations. */
		ipMatch = ip && *ip && ban->ip[0] && Q_stricmp(ban->ip, "localhost") &&
		          Q_stricmp(ban->ip, "offline") && strstr(ip, ban->ip) != NULL;
		if (!guidMatch && !ipMatch) continue;
		if (reason) {
			if (!ban->expires)
				Com_sprintf((char *)reason, MAX_STRING_CHARS, "^9Reason: %s\nExpires: permanent.^7\n", ban->reason);
			else
				Com_sprintf((char *)reason, MAX_STRING_CHARS, "^9Reason: %s\nExpires in: %lld seconds.^7\n",
				            ban->reason, (long long)ban->expires - relativeNow);
		}
		return qtrue;
	}
	return qfalse;
}

#ifdef HW_BAN
qboolean G_shrubbot_hwban_check(char const* hwguid, char const* reason) {
	int 	i;
	time_t 	t;

	if(!time(&t)) return qfalse;
	t = t - SHRUBBOT_BAN_EXPIRE_OFFSET;
	for(i=0; g_shrubbot_bans[i]; i++) {
		// 0 is for perm ban
		if(g_shrubbot_bans[i]->expires != 0 &&
			(g_shrubbot_bans[i]->expires - t) < 1)
			continue;
		if(!Q_stricmp(g_shrubbot_bans[i]->hwguid, hwguid)) {
			if(reason) {
				Com_sprintf(
					(char*)reason,
					MAX_STRING_CHARS,
					"^9Reason: %s\nExpires in: %u seconds.^7\n",
					g_shrubbot_bans[i]->reason,
					g_shrubbot_bans[i]->expires - (int)t
					);
			}
			return qtrue;
		}
	}
	return qfalse;
}
#endif // HW_BAN

/* Use the strongest active record when duplicate GUIDs occur: permanent
 * wins, otherwise the latest expiry wins. Placeholders are not identities. */
static const g_shrubbot_ban_t *NM_FindActiveMute(const char *guid, time_t now) {
	const g_shrubbot_ban_t *best = NULL;
	int i;
	if (!G_shrubbot_usable_guid(guid) || now == (time_t)-1) return NULL;
	for (i = 0; i < MAX_SHRUBBOT_BANS && g_shrubbot_mutes[i]; ++i) {
		const g_shrubbot_ban_t *mute = g_shrubbot_mutes[i];
		if (mute->expires && (long long)mute->expires <= (long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET) continue;
		if (Q_stricmp(mute->guid, guid)) continue;
		if (!mute->expires) return mute;
		if (!best || mute->expires > best->expires) best = mute;
	}
	return best;
}

qboolean G_shrubbot_mute_check(char const* ip, char const* guid) {
	(void)ip; /* NQ currently checks GUID, not IP. */
	return NM_FindActiveMute(guid, time(NULL)) != NULL;
}

static qboolean NM_RestoreMuteAt(gentity_t *ent, time_t now) {
	const g_shrubbot_ban_t *mute;
	long long remaining, deadline;
	int newTime, oldTime, oldBy;
	qboolean otherMute;
	if (!ent || !ent->client || ent->client->pers.connected != CON_CONNECTED) return qfalse;
	mute = NM_FindActiveMute(ent->client->pers.cl_guid, now);
	if (!mute) return qfalse;
	oldTime = ent->client->sess.auto_mute_time;
	oldBy = ent->client->sess.muted_by;
	otherMute = ent->client->sess.muted && oldBy != MUTED_BY_SHRUB;
	if (!mute->expires) {
		newTime = -1;
	} else {
		remaining = (long long)mute->expires - ((long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET);
		deadline = (long long)level.time + remaining * SECONDS_1;
		newTime = (int)(deadline > INT_MAX ? INT_MAX : deadline);
	}
	/* NQ has one timer for all sources. Preserve an existing other-source
	 * mute conservatively; an indefinite source wins over a timed one. */
	if (otherMute) {
		if (oldTime == -1 || newTime == -1) newTime = -1;
		else if (oldTime > newTime) newTime = oldTime;
	}
	ent->client->sess.muted = qtrue;
	ent->client->sess.auto_mute_time = newTime;
	ent->client->sess.muted_by = otherMute ? (oldBy | MUTED_BY_SHRUB) : MUTED_BY_SHRUB;
	ClientConfigStringChanged(ent);
	return qtrue;
}

qboolean G_shrubbot_restore_mute(gentity_t *ent) {
	return NM_RestoreMuteAt(ent, time(NULL));
}

char *G_shrubbot_mute_name(char const *ip, char const *guid) {
	const g_shrubbot_ban_t *mute = NM_FindActiveMute(guid, time(NULL));
	(void)ip;
	return mute ? va("%s", mute->name) : (char*)"";
}

qboolean G_shrubbot_native_cmd_check( gentity_t *ent, char *cmd, int skip ) {
	int i;
	// commands that are native shrub-cmds..
	// Note: These commands are -NOT- the [command] secions in the shrub config file..
	for (i=0; g_shrubbot_cmds[i].keyword[0]; i++) {
		if(Q_stricmp(cmd, g_shrubbot_cmds[i].keyword)) {
			continue;
		}

		if( ((g_shrubbot_cmds[i].cmdFlags & SCMDF_TYRANNY) || (g_shrubbot_cmds[i].cmdFlags & SCMDF_CHEAT)) && !g_tyranny.integer) {
			SP(va("%s: g_tyranny is not enabled\n", g_shrubbot_cmds[i].keyword));
			_shrubbot_log(ent, "attempted", skip-1);
			return qfalse;
		}
		if(G_shrubbot_permission(ent, g_shrubbot_cmds[i].flag)) {
			g_shrubbot_cmds[i].handler(ent, skip);
			/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): reports have a dedicated private
			 * log; do not copy their text into the general admin log. */
			if (Q_stricmp(cmd, "report")) _shrubbot_log(ent, cmd, skip);
			return qtrue;
		}
		// no permission..
		SP(va("%s: permission denied\n", g_shrubbot_cmds[i].keyword));
		if (Q_stricmp(cmd, "report")) _shrubbot_log(ent, "attempted", skip-1);
		return qfalse;
	}
	// it is no shrub command, so let it be executed..
	//return qtrue;
	// cs: #550. this needs to return false. follow it back to ConsoleCommand()
	return qfalse;
}

/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): ET: Legacy's command tokenizer
 * decodes UTF-8 when finding token boundaries, so reject malformed and
 * overlong sequences as well as ASCII syntax characters. Otherwise an
 * overlong encoding of a space could split an unquoted substitution. */
static qboolean NM_CustomValueSafe(const char *value, size_t len, qboolean quoted) {
	size_t i = 0, j, extra;
	while (i < len) {
		unsigned char c = (unsigned char)value[i];
		if (c < 0x80) {
			if (c < 32 || c == 127 || c == ';' || c == '"' || c == '\\' ||
			    (c == ' ' && !quoted)) return qfalse;
			++i;
			continue;
		}
		if (c >= 0xc2 && c <= 0xdf) extra = 1;
		else if (c >= 0xe0 && c <= 0xef) extra = 2;
		else if (c >= 0xf0 && c <= 0xf4) extra = 3;
		else return qfalse;
		if (extra >= len - i) return qfalse;
		for (j = 1; j <= extra; ++j) {
			unsigned char next = (unsigned char)value[i + j];
			if ((next & 0xc0) != 0x80) return qfalse;
		}
		/* Shortest-form UTF-8, excluding surrogate and out-of-range values. */
		if ((c == 0xe0 && (unsigned char)value[i + 1] < 0xa0) ||
		    (c == 0xed && (unsigned char)value[i + 1] >= 0xa0) ||
		    (c == 0xf0 && (unsigned char)value[i + 1] < 0x90) ||
		    (c == 0xf4 && (unsigned char)value[i + 1] >= 0x90)) return qfalse;
		i += extra + 1;
	}
	return qtrue;
}

/* [command] exec templates are trusted server configuration, but arguments
 * and chat shortcuts are player-controlled. Reject unsafe substitutions,
 * malformed UTF-8, newline-bearing or unbalanced templates, and unquoted
 * comment delimiters. These conservative restrictions keep quoted-space
 * checks aligned with ET: Legacy's command buffer/tokenizer. Substitutions
 * are made once and oversized results are rejected. Configured semicolon
 * commands remain allowed: this does not make arbitrary templates safe or
 * enforce nested commands' permissions. */
static qboolean NM_CustomCommandLine(gentity_t *ent, const char *tmpl, int skip,
                                     char *out, size_t capacity) {
	static const char shortcutNames[] = "adhklnrpswtbcxmfgi";
	char args[9][MAX_NAME_LENGTH], shortcuts[sizeof(shortcutNames) - 1][MAX_SAY_TEXT];
	qboolean haveArg[9] = { qfalse }, haveShortcut[sizeof(shortcutNames) - 1] = { qfalse };
	byte fromPlayer[MAX_STRING_CHARS];
	size_t used = 0, i;
	qboolean quoted = qfalse;

	if (!tmpl || !out || capacity < 2 || capacity > sizeof(fromPlayer)) return qfalse;
	while (*tmpl) {
		const char *value = NULL;
		if (tmpl[0] == '[' && tmpl[1] && tmpl[2] == ']') {
			int key = (unsigned char)tmpl[1];
			if (key >= '1' && key <= '9') {
				int index = key - '1';
				if (!haveArg[index]) {
					Q_SayArgv(skip + index + 1, args[index], sizeof(args[index]));
					haveArg[index] = qtrue;
				}
				value = args[index];
			} else if (g_shortcuts.integer) {
				const char *found;
				char marker[4] = { '[', 0, ']', '\0' };
				if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
				found = strchr(shortcutNames, key);
				if (found) {
					int index = (int)(found - shortcutNames);
					if (!haveShortcut[index]) {
						marker[1] = (char)key;
						Q_strncpyz(shortcuts[index], G_Shortcuts(ent, marker), sizeof(shortcuts[index]));
						haveShortcut[index] = qtrue;
					}
					value = shortcuts[index];
				}
			}
		}
		if (value) {
			size_t len = strlen(value);
			if (!NM_CustomValueSafe(value, len, quoted)) return qfalse;
			if (len > capacity - 2 - used) return qfalse;
			/* An omitted argument can also join two trusted bytes into a
			 * comment delimiter. Mark its left boundary as influenced. */
			if (!len && used) fromPlayer[used - 1] = 1;
			memcpy(out + used, value, len);
			memset(fromPlayer + used, 1, len);
			used += len;
			tmpl += 3;
		} else {
			if (*tmpl == '\n' || *tmpl == '\r' || used >= capacity - 2) return qfalse;
			out[used] = *tmpl;
			fromPlayer[used++] = 0;
			if (*tmpl == '"') quoted = !quoted;
			++tmpl;
		}
	}
	if (quoted) return qfalse;
	/* Reject comment syntax influenced by a substitution, even within
	 * quotes. Unquoted configured comments are also rejected: quotes inside
	 * comments make the engine's buffering and tokenization disagree. */
	for (i = 0; i + 1 < used; ++i) {
		if (out[i] == '"') quoted = !quoted;
		if ((out[i] == '/' && (out[i + 1] == '/' || out[i + 1] == '*')) ||
		    (out[i] == '*' && out[i + 1] == '/')) {
			if (!quoted || fromPlayer[i] || fromPlayer[i + 1]) return qfalse;
		}
	}
	/* Cbuf_AddText does not append a separator for EXEC_APPEND. */
	out[used++] = '\n';
	out[used] = '\0';
	return qtrue;
}

qboolean G_shrubbot_cmd_check(gentity_t *ent) {
	int i;
	char command[MAX_SHRUBBOT_CMD_LEN];
	char *cmd;
	int skip = 0;

	if(g_shrubbot.string[0] == '\0') {
		return qfalse;
	}

	command[0] = '\0';
	Q_SayArgv(0, command, sizeof(command));
	if(!Q_stricmp(command, "say") ||
		(G_shrubbot_permission(ent, SBF_TEAMFTSHRUBCMD) &&
			(!Q_stricmp(command, "say_team") ||
			!Q_stricmp(command, "say_buddy")))) {
				skip = 1;
				Q_SayArgv(1, command, sizeof(command));
	}

	if(!command[0]) {
		return qfalse;
	}

	if(command[0] == '!') {
		cmd = &command[1];
	}
	else if(ent == NULL) {
		cmd = &command[0];
	}
	else {
		return qfalse;
	}

	// Custom shrub commands that people can create. These are the [command] sections in the shrub config file.
	for(i=0; g_shrubbot_commands[i]; i++) {
		if(Q_stricmp(cmd, g_shrubbot_commands[i]->command)) {
			continue;
		}

		if(_shrubbot_command_permission(ent, cmd)) {
			char cmdLine[MAX_STRING_CHARS];
			if (!NM_CustomCommandLine(ent, g_shrubbot_commands[i]->exec, skip,
			                          cmdLine, sizeof(cmdLine))) {
				SP("^dcustom command: ^9unsafe or oversized substitution\n");
				_shrubbot_log(ent, "attempted", skip - 1);
				return qfalse;
			}
			trap_SendConsoleCommand(EXEC_APPEND, cmdLine);

			_shrubbot_log(ent, cmd, skip);
			return qtrue;
		}
		else {
			SP(va("%s: permission denied\n", g_shrubbot_commands[i]->command));
			_shrubbot_log(ent, "attempted", skip-1);
			return qfalse;
		}
	}

	// Now check for native shrub commands..
	return G_shrubbot_native_cmd_check( ent, cmd, skip );
}

/* NewMod, Arena.ai, 2026-11-21: load into an isolated snapshot so a failed
 * reload cannot remove existing bans or install partial permission lists. */
#define NM_SHRUBBOT_MAX_CONFIG_BYTES (32 * 1024 * 1024)
typedef struct {
	g_shrubbot_level_t *levels[MAX_SHRUBBOT_LEVELS + 1];
	g_shrubbot_admin_t *admins[MAX_SHRUBBOT_ADMINS + 1];
	g_shrubbot_ban_t *bans[MAX_SHRUBBOT_BANS + 1];
	g_shrubbot_ban_t *mutes[MAX_SHRUBBOT_BANS + 1];
	g_shrubbot_command_t *commands[MAX_SHRUBBOT_COMMANDS + 1];
} nm_shrub_config_t;

static void NM_FreeShrubConfig(nm_shrub_config_t *config) {
	int i;
	if (!config) return;
	for (i = 0; i < MAX_SHRUBBOT_LEVELS && config->levels[i]; ++i) free(config->levels[i]);
	for (i = 0; i < MAX_SHRUBBOT_ADMINS && config->admins[i]; ++i) free(config->admins[i]);
	for (i = 0; i < MAX_SHRUBBOT_BANS && config->bans[i]; ++i) free(config->bans[i]);
	for (i = 0; i < MAX_SHRUBBOT_BANS && config->mutes[i]; ++i) free(config->mutes[i]);
	for (i = 0; i < MAX_SHRUBBOT_COMMANDS && config->commands[i]; ++i) free(config->commands[i]);
	free(config);
}

static qboolean NM_ReadConfigInt(char **cnf, int *value) {
	char *end, *t = COM_ParseExt(cnf, qfalse);
	long number;
	if (!strcmp(t, "=")) t = COM_ParseExt(cnf, qfalse);
	else G_Printf("readconfig: warning missing = before \"%s\" on line %d\n", t, COM_GetCurrentParseLine());
	if (!*t || COM_ParseHadError()) return qfalse;
	errno = 0;
	number = strtol(t, &end, 10);
	if (end == t || *end || errno == ERANGE || number < INT_MIN || number > INT_MAX) return qfalse;
	*value = (int)number;
	return qtrue;
}

static qboolean NM_ReadCommandLevels(char **cnf, g_shrubbot_command_t *command) {
	char text[MAX_STRING_CHARS], *p, *end;
	int count = 0;
	if (!NM_ReadConfigString(cnf, text, sizeof(text))) return qfalse;
	memset(command->levels, -1, sizeof(command->levels));
	p = text;
	while (*p) {
		long number;
		while (*p == ' ') ++p;
		if (!*p) break;
		if (count >= MAX_SHRUBBOT_LEVELS || (*p != '+' && (*p < '0' || *p > '9'))) return qfalse;
		errno = 0;
		number = strtol(p, &end, 10);
		if (end == p || (*end && *end != ' ') || errno == ERANGE || number < 0 || number > INT_MAX) return qfalse;
		command->levels[count++] = (int)number;
		p = end;
	}
	command->levels[count] = -1;
	return qtrue;
}

qboolean G_shrubbot_readconfig(gentity_t *ent, int skiparg) {
	g_shrubbot_level_t		*L = NULL;
	g_shrubbot_admin_t		*a = NULL;
	g_shrubbot_ban_t		*b = NULL;
	g_shrubbot_ban_t		*m = NULL;
	g_shrubbot_command_t	*c = NULL;
	int						lc = 0, ac = 0, bc = 0, cc = 0, mc = 0;
	fileHandle_t f = 0;
	int len, i;
	time_t now;
	char *cnf = NULL, *cnf2 = NULL, *t;
	const char *failure = "out of memory";
	nm_shrub_config_t *config = NULL;
	qboolean level_open, admin_open, ban_open, mute_open, command_open;
	qboolean ignoring_section = qfalse;

	if (!g_shrubbot.string[0]) return qfalse;
	len = trap_FS_FOpenFile(g_shrubbot.string, &f, FS_READ);
	if (len < 0 || !f) {
		SP(va("^dreadconfig: ^9could not open shrubbot config file ^2%s\n", g_shrubbot.string));
		/* Preserve a live snapshot on reload. A new install without a file
		 * retains NQ's initial default levels. */
		if (!g_shrubbot_levels[0] && !g_shrubbot_admins[0] && !g_shrubbot_bans[0] &&
		    !g_shrubbot_mutes[0] && !g_shrubbot_commands[0])
			(void)NM_DefaultLevels(g_shrubbot_levels);
		return qfalse;
	}
	if (len > NM_SHRUBBOT_MAX_CONFIG_BYTES) {
		trap_FS_FCloseFile(f);
		SP("readconfig: config exceeds the 32 MiB limit; previous configuration retained\n");
		return qfalse;
	}
	cnf2 = (char*)calloc((size_t)len + 1, 1);
	config = (nm_shrub_config_t*)calloc(1, sizeof(*config));
	if (!cnf2 || !config) {
		trap_FS_FCloseFile(f);
		goto fail;
	}
	trap_FS_Read(cnf2, len, f);
	trap_FS_FCloseFile(f);
	/* Configs are text. Reject embedded NULs (also catches zero-filled short
	 * reads when the engine cannot report the number of bytes read). */
	if (memchr(cnf2, '\0', (size_t)len)) {
		failure = "embedded NUL or short read";
		goto fail;
	}
	cnf = cnf2;

	level_open = admin_open = mute_open = ban_open = command_open = qfalse;

	// IRATA: init com_lines
	// FIXME: although COM_BeginParseSession is called we don't get the exact line number by COM_GetCurrentParseLine
	// - Issues with SkipWhitespace in COM_ParseExt ?
	COM_BeginParseSession("ShrubbotConfigRead");

	while(qtrue) {
		t = COM_Parse(&cnf);
		if (!*t) {
			if (cnf) { failure = "unexpected empty token"; goto fail; }
			break;
		}

		if(!Q_stricmp(t, "[level]")) {
			ignoring_section = qfalse;
			if (lc >= MAX_SHRUBBOT_LEVELS) { failure = "too many levels"; goto fail; }
			L = (g_shrubbot_level_t*)calloc(1, sizeof(g_shrubbot_level_t));
			if (!L) goto fail;
			L->level	= 0;
			*L->name	= '\0';
			*L->flags	= '\0';
			config->levels[lc++] = L;
			admin_open = mute_open = ban_open = command_open = qfalse;
			level_open = qtrue;
			continue;
		}
		else if(!Q_stricmp(t, "[admin]")) {
			ignoring_section = qfalse;
			if (ac >= MAX_SHRUBBOT_ADMINS) { failure = "too many admins"; goto fail; }

			if (nq_noq.integer & NOQ_DONT_READ_ADMINS) {
				level_open = admin_open = mute_open = ban_open = command_open = qfalse;
				ignoring_section = qtrue;
				continue;
			}

			a = (g_shrubbot_admin_t*)calloc(1, sizeof(g_shrubbot_admin_t));
			if (!a) goto fail;
			*a->name	= '\0';
			*a->guid	= '\0';
			a->level	= 0;
			*a->flags	= '\0';
			config->admins[ac++] = a;
			level_open = mute_open = ban_open = command_open = qfalse;
			admin_open = qtrue;
			continue;
		}
		else if(!Q_stricmp(t, "[ban]")) {
			ignoring_section = qfalse;
			if (bc >= MAX_SHRUBBOT_BANS) { failure = "too many bans"; goto fail; }

			if (nq_noq.integer & NOQ_DONT_READ_BANS) {
				level_open = admin_open = mute_open = ban_open = command_open = qfalse;
				ignoring_section = qtrue;
				continue;
			}

			b = (g_shrubbot_ban_t*)calloc(1, sizeof(g_shrubbot_ban_t));
			if (!b) goto fail;
			*b->name	= '\0';
			*b->guid	= '\0';
#ifdef HW_BAN
			*b->hwguid	= '\0';
#endif // HW_BAN
			*b->ip		= '\0';
			*b->made	= '\0';
			b->expires	= 0;
			*b->reason	= '\0';
			config->bans[bc++] = b;
			level_open = admin_open = mute_open = command_open = qfalse;
			ban_open = qtrue;
			continue;
		}
		else if(!Q_stricmp(t, "[mute]")) {
			ignoring_section = qfalse;
			if (mc >= MAX_SHRUBBOT_BANS) { failure = "too many mutes"; goto fail; }

			if (nq_noq.integer & NOQ_DONT_READ_MUTES) {
				level_open = admin_open = mute_open = ban_open = command_open = qfalse;
				ignoring_section = qtrue;
				continue;
			}

			m = (g_shrubbot_ban_t*)calloc(1, sizeof(g_shrubbot_ban_t));
			if (!m) goto fail;
			*m->name	= '\0';
			*m->guid	= '\0';
			*m->ip		= '\0';
			*m->made	= '\0';
			m->expires	= 0;
			*m->reason	= '\0';
			config->mutes[mc++] = m;
			level_open = admin_open = ban_open = command_open = qfalse;
			mute_open = qtrue;
			continue;
		}
		else if(!Q_stricmp(t, "[command]")) {
			ignoring_section = qfalse;
			if (cc >= MAX_SHRUBBOT_COMMANDS) { failure = "too many commands"; goto fail; }
			c = (g_shrubbot_command_t*)calloc(1, sizeof(g_shrubbot_command_t));
			if (!c) goto fail;
			*c->command = '\0';
			*c->exec = '\0';
			*c->desc = '\0';
			memset(c->levels, -1, sizeof(c->levels));
			config->commands[cc++] = c;
			level_open = admin_open = mute_open = ban_open = qfalse;
			command_open = qtrue;
			continue;
		}

		if ( !(level_open || admin_open || mute_open || ban_open || command_open) ) {
			if (!ignoring_section) { failure = "field outside a recognized section"; goto fail; }
			/* A disabled record is ignored a line at a time: a section-like
			 * value must not be mistaken for a new record. */
			while (COM_ParseExt(&cnf, qfalse)[0]) {}
			continue;
		}

		if(level_open) {
			if(!Q_stricmp(t, "level")) {
				if (!NM_ReadConfigInt(&cnf, &L->level)) { failure = "invalid integer field"; goto fail; }
			}
			else if(!Q_stricmp(t, "name")) {
				if (!NM_ReadConfigString(&cnf, L->name, sizeof(L->name))) { failure = "malformed or oversized field"; goto fail; }
			}
			else if(!Q_stricmp(t, "flags")) {
				if (!NM_ReadConfigString(&cnf, L->flags, sizeof(L->flags))) { failure = "malformed or oversized field"; goto fail; }
			}
			else {
				failure = "unrecognized [level] field"; goto fail;
			}
		}
		else if(admin_open) {
			if(!Q_stricmp(t, "name")) {
				if (!NM_ReadConfigString(&cnf, a->name, sizeof(a->name))) { failure = "malformed or oversized field"; goto fail; }
			}
			else if(!Q_stricmp(t, "guid")) {
				if (!NM_ReadConfigString(&cnf, a->guid, sizeof(a->guid))) { failure = "malformed or oversized field"; goto fail; }
			}
			else if(!Q_stricmp(t, "level")) {
				if (!NM_ReadConfigInt(&cnf, &a->level)) { failure = "invalid integer field"; goto fail; }
			}
			else if(!Q_stricmp(t, "flags")) {
				if (!NM_ReadConfigString(&cnf, a->flags, sizeof(a->flags))) { failure = "malformed or oversized field"; goto fail; }
			}
			else {
				failure = "unrecognized [admin] field"; goto fail;
			}
		}
		else if(ban_open) {
			if(!Q_stricmp(t, "name")) {
				if (!NM_ReadConfigString(&cnf, b->name, sizeof(b->name))) { failure = "malformed or oversized field"; goto fail; }
			}
			else if(!Q_stricmp(t, "guid")) {
				if (!NM_ReadConfigString(&cnf, b->guid, sizeof(b->guid))) { failure = "malformed or oversized field"; goto fail; }
			}
#ifdef HW_BAN
			else if(!Q_stricmp(t, "hwguid")) {
				if (!NM_ReadConfigString(&cnf, b->hwguid, sizeof(b->hwguid))) { failure = "malformed or oversized field"; goto fail; }
			}
#endif // HW_BAN
			else if(!Q_stricmp(t, "ip")) {
				if (!NM_ReadConfigString(&cnf, b->ip, sizeof(b->ip))) { failure = "malformed or oversized field"; goto fail; }
			}
			else if(!Q_stricmp(t, "reason")) {
				if (!NM_ReadConfigString(&cnf, b->reason, sizeof(b->reason))) { failure = "malformed or oversized field"; goto fail; }
			}
			else if(!Q_stricmp(t, "made")) {
				if (!NM_ReadConfigString(&cnf, b->made, sizeof(b->made))) { failure = "malformed or oversized field"; goto fail; }
			}
			else if(!Q_stricmp(t, "expires")) {
				if (!NM_ReadConfigInt(&cnf, &b->expires)) { failure = "invalid integer field"; goto fail; }
			}
			else if(!Q_stricmp(t, "banner")) {
				if (!NM_ReadConfigString(&cnf, b->banner, sizeof(b->banner))) { failure = "malformed or oversized field"; goto fail; }
			}
			else {
				failure = "unrecognized [ban] field"; goto fail;
			}
		}
		else if(mute_open) {
			if(!Q_stricmp(t, "name")) {
				if (!NM_ReadConfigString(&cnf, m->name, sizeof(m->name))) { failure = "malformed or oversized field"; goto fail; }
			}
			else if(!Q_stricmp(t, "guid")) {
				if (!NM_ReadConfigString(&cnf, m->guid, sizeof(m->guid))) { failure = "malformed or oversized field"; goto fail; }
			}
			else if(!Q_stricmp(t, "ip")) {
				if (!NM_ReadConfigString(&cnf, m->ip, sizeof(m->ip))) { failure = "malformed or oversized field"; goto fail; }
			}
			else if(!Q_stricmp(t, "reason")) {
				if (!NM_ReadConfigString(&cnf, m->reason, sizeof(m->reason))) { failure = "malformed or oversized field"; goto fail; }
			}
			else if(!Q_stricmp(t, "made")) {
				if (!NM_ReadConfigString(&cnf, m->made, sizeof(m->made))) { failure = "malformed or oversized field"; goto fail; }
			}
			else if(!Q_stricmp(t, "expires")) {
				if (!NM_ReadConfigInt(&cnf, &m->expires)) { failure = "invalid integer field"; goto fail; }
			}
			else if(!Q_stricmp(t, "muter")) {
				if (!NM_ReadConfigString(&cnf, m->banner, sizeof(m->banner))) { failure = "malformed or oversized field"; goto fail; }
			}
			else {
				failure = "unrecognized [mute] field"; goto fail;
			}
		}
		else if(command_open) {
			if(!Q_stricmp(t, "command")) {
				if (!NM_ReadConfigString(&cnf, c->command, sizeof(c->command))) { failure = "malformed or oversized field"; goto fail; }
			}
			else if(!Q_stricmp(t, "exec")) {
				if (!NM_ReadConfigString(&cnf, c->exec, sizeof(c->exec))) { failure = "malformed or oversized field"; goto fail; }
			}
			else if(!Q_stricmp(t, "desc")) {
				if (!NM_ReadConfigString(&cnf, c->desc, sizeof(c->desc))) { failure = "malformed or oversized field"; goto fail; }
			}
			else if(!Q_stricmp(t, "levels")) {
				if (!NM_ReadCommandLevels(&cnf, c)) { failure = "invalid or excessive command levels"; goto fail; }
			}
			else {
				failure = "unrecognized [command] field"; goto fail;
			}
		}
	}

	if (COM_ParseHadError()) { failure = "malformed or oversized token"; goto fail; }
	if (!lc && !NM_DefaultLevels(config->levels)) goto fail;

	G_shrubbot_cleanup();
	memcpy(g_shrubbot_levels, config->levels, sizeof(g_shrubbot_levels));
	memcpy(g_shrubbot_admins, config->admins, sizeof(g_shrubbot_admins));
	memcpy(g_shrubbot_bans, config->bans, sizeof(g_shrubbot_bans));
	memcpy(g_shrubbot_mutes, config->mutes, sizeof(g_shrubbot_mutes));
	memcpy(g_shrubbot_commands, config->commands, sizeof(g_shrubbot_commands));
	free(config); /* ownership of its records has moved to the live lists */
	free(cnf2);

	/* Restore only active GUID-matched mutes to connected clients. A
	 * successful reload that removed a purely shrub-owned mute clears it;
	 * other or combined mute sources are not cleared by this path. */
	now = time(NULL);
	if (now != (time_t)-1) {
		for (i = 0; i < level.maxclients && i < MAX_CLIENTS; ++i) {
			gentity_t *player = &g_entities[i];
			if (!player->client || player->client->pers.connected != CON_CONNECTED) continue;
			if (!NM_RestoreMuteAt(player, now) && player->client->sess.muted &&
			    player->client->sess.muted_by == MUTED_BY_SHRUB) {
				player->client->sess.muted = qfalse;
				player->client->sess.auto_mute_time = -1;
				player->client->sess.muted_by = UNMUTED;
				ClientConfigStringChanged(player);
			}
		}
	} else {
		G_Printf("readconfig: clock unavailable; client mute states retained\n");
	}
	SP(va("readconfig: loaded %d levels, %d admins, %d bans, %d mutes, %d commands\n", lc, ac, bc, mc, cc));
	return qtrue;

fail:
	NM_FreeShrubConfig(config);
	free(cnf2);
	SP(va("readconfig: %s; previous configuration retained\n", failure));
	return qfalse;
}

qboolean G_shrubbot_time(gentity_t *ent, int skiparg) {
	time_t t;
	struct tm *lt;
	char s[50];

	if(!time(&t)) return qfalse;

	lt = localtime(&t);
	strftime(s, sizeof(s), "%I:%M%p %Z", lt);
	AP(va("chat \"^dtime:^7 %s\" -1", s));
	return qtrue;
}

qboolean G_shrubbot_setlevel(gentity_t *ent, int skiparg) {
	/* NewMod, Arena.ai: check fine-grained setlevel permission first */
	if (ent && !NM_FineGrainedAllowed(ent, SBF_CAN_SETLEVEL)) {
		SP("^dsetlevel: ^9you do not have permission to change levels\n");
		return qfalse;
	}
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	char lstr[11]; // 10 is max strlen() for 32-bit int
	char *guid;
	char n2[MAX_NAME_LENGTH];
	int l, i;
	gentity_t *vic;
	qboolean updated = qfalse;
	g_shrubbot_admin_t *a;
	qboolean found = qfalse;

	if(Q_SayArgc() < 3+skiparg) {
		SP("setlevel usage: !setlevel [name|slot#] [level]\n");
		return qfalse;
	}
	Q_SayArgv(1+skiparg, name, sizeof(name));
	Q_SayArgv(2+skiparg, lstr, sizeof(lstr));
	l = atoi(lstr);

	// forty - don't allow privilege escalation
	if(l > G_shrubbot_level(ent)) {
		SP("setlevel: you may not setlevel higher than your current level\n");
		return qfalse;
	}

	// tjw: if somone sets g_shrubbot on a running server then uses
	//      setlevel in the console, we need to make sure we have
	//      at least the default levels loaded.
	if(!ent)
		G_shrubbot_readconfig(NULL, 0);

	for(i=0; g_shrubbot_levels[i]; i++) {
		if(g_shrubbot_levels[i]->level == l) {
			found = qtrue;
			break;
		}
	}
	if(!found) {
		SP("setlevel: level is not defined\n");
		return qfalse;
	}
	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("setlevel: %s\n", err));
		return qfalse;
	}
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP("setlevel: sorry, but your intended victim has a higher admin level than you do.\n");
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	guid = level.clients[pids[0]].pers.cl_guid;

	// IRATA: don't level if there is no GUID
	// no need for an option to disable ... it doesn't make any sense to level non GUID players
	if (!G_shrubbot_usable_guid(guid)) {
		SP("setlevel: sorry, but your intended victim has no usable GUID!\n");
		return qfalse;
	}

	// tjw: use raw name
	Q_strncpyz(n2, vic->client->pers.netname, sizeof(n2));

	for(i=0;g_shrubbot_admins[i];i++) {
		if(G_shrubbot_usable_guid(guid) && !Q_stricmp(g_shrubbot_admins[i]->guid, guid)) {
			g_shrubbot_admins[i]->level = l;
			Q_strncpyz(g_shrubbot_admins[i]->name, n2,
				sizeof(g_shrubbot_admins[i]->name));
			updated = qtrue;
		}
	}
	if(!updated) {
		if(i == MAX_SHRUBBOT_ADMINS) {
			SP("setlevel: too many admins\n");
			return qfalse;
		}
		a = (g_shrubbot_admin_t*)malloc(sizeof(g_shrubbot_admin_t));
		if (!a) {
			SP("setlevel: out of memory\n");
			return qfalse;
		}
		a->level = l;
		Q_strncpyz(a->name, n2, sizeof(a->name));
		Q_strncpyz(a->guid, guid, sizeof(a->guid));
		*a->flags = '\0';
		g_shrubbot_admins[i] = a;
	}

	AP(va("chat \"^dsetlevel: ^9%s^9 is now a level %d user^7\" -1",
		vic->client->pers.netname,
		l));
	_shrubbot_writeconfig();
	return qtrue;
}

/* Lua is trusted, but assigning a shared placeholder GUID could affect
 * unrelated players. Apply the same key checks as ordinary !setlevel. */
qboolean G_shrubbot_setlevel_lua(int slot, int shrubbotlevel) {
	char *guid;
	char n2[MAX_NAME_LENGTH];
	int i;
	gentity_t *vic;
	qboolean updated = qfalse;
	g_shrubbot_admin_t *a;
	qboolean found = qfalse;

	// slot check
	if (slot < 0 || slot >= MAX_CLIENTS || slot >= level.maxclients) {
		G_Printf("G_shrubbot_setlevel_lua: invalid slot. Aborting.\n");
			return qfalse;
	}

	// check if level structure is loaded ...
	// tjw: if somone sets g_shrubbot on a running server then uses
	//      setlevel in the console, we need to make sure we have
	//      at least the default levels loaded.
	// IRATA: if g_shrubbot.string is set the levels are loaded - see G_InitGame
	if ( !(g_shrubbot.string[0]) ) {
		G_Printf("G_shrubbot_setlevel_lua: levels are not loaded. Aborting.\n");
			return qfalse;
	}

	// check if level definition does exist
	for(i=0; g_shrubbot_levels[i]; ++i) {
		if(g_shrubbot_levels[i]->level == shrubbotlevel) {
			found = qtrue;
			break;
		}
	}
	if(!found) {
		G_Printf("G_shrubbot_setlevel_lua: level is not defined. Aborting.\n");
		return qfalse;
	}

    // get the ent from slot
	vic = &g_entities[slot];
	// do a sanity check if slot is used ...
	if (vic->client && (vic->client->pers.connected == CON_CONNECTING || vic->client->pers.connected == CON_CONNECTED)) {

		guid = level.clients[slot].pers.cl_guid;

		if (!G_shrubbot_usable_guid(guid)) {
			G_Printf("G_shrubbot_setlevel_lua: client has no usable GUID. Aborting.\n");
			return qfalse;
		}

		// tjw: use raw name
		Q_strncpyz(n2, vic->client->pers.netname, sizeof(n2));

		for(i=0;g_shrubbot_admins[i];++i) {
			if(G_shrubbot_usable_guid(guid) && !Q_stricmp(g_shrubbot_admins[i]->guid, guid)) {
				g_shrubbot_admins[i]->level = shrubbotlevel;
				Q_strncpyz(g_shrubbot_admins[i]->name, n2,
					sizeof(g_shrubbot_admins[i]->name));
				updated = qtrue;
			}
		}
		if(!updated) {
			if(i == MAX_SHRUBBOT_ADMINS) {
				G_Printf("G_shrubbot_setlevel_lua: too many admins\n");
				return qfalse;
			}
			a = (g_shrubbot_admin_t*)malloc(sizeof(g_shrubbot_admin_t));
			if (!a) {
				G_Printf("G_shrubbot_setlevel_lua: out of memory\n");
				return qfalse;
			}
			a->level = shrubbotlevel;
			Q_strncpyz(a->name, n2, sizeof(a->name));
			Q_strncpyz(a->guid, guid, sizeof(a->guid));
			*a->flags = '\0';
			g_shrubbot_admins[i] = a;
		}

		return qtrue;
	}

	// should never happen ...
	G_Printf("G_shrubbot_setlevel_lua: player not in game. Aborting.\n");
	return qfalse;
}

qboolean G_shrubbot_kick(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], *reason, err[MAX_STRING_CHARS];
	int minargc;

	/* NewMod, Arena.ai: check fine-grained kick permission first */
	if (ent && !NM_FineGrainedAllowed(ent, SBF_CAN_KICK)) {
		SP("^dkick: ^9you do not have permission to kick\n");
		return qfalse;
	}

	minargc = 3+skiparg;
	if(G_shrubbot_permission(ent, SBF_UNACCOUNTABLE)) {
		minargc = 2+skiparg;
	}

	if(Q_SayArgc() < minargc) {
		SP("^dkick usage: ^2!kick ^9[name] [reason]^7\n");
		return qfalse;
	}
	Q_SayArgv(1+skiparg, name, sizeof(name));
	reason = Q_SayConcatArgs(2+skiparg);
	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("^dkick: ^9%s\n", err));
		return qfalse;
	}

	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP(va("^dkick: ^9sorry, %s ^9is a higher level admin than you are.\n",
			g_entities[pids[0]].client->pers.netname));
		return qfalse;
	}

	// IRATA: still kick bots
	if ( g_entities[pids[0]].client->pers.localClient ) {
		SP("^dkick: ^9Cannot kick server host. Use bot commands to control bot count.\n");
		return qfalse;
	}

	if(G_shrubbot_permission(&g_entities[pids[0]], SBF_IMMUTABLE)) {
		SP( "Can't kick immune admins!\n" );
		return qfalse;
	}

	// IRATA - sound for kick
	if( g_announcer.integer & ANNOUNCE_KICKSOUND) {
		G_globalSoundEnum(GAMESOUND_MISC_KICKED);
	}

	// forty - enforce the temp ban consistently using shrubbot.
	if ( g_autoTempBan.integer && g_autoTempBanTime.integer ) {
		G_shrubbot_tempban(pids[0],
			va("^9You have been kicked, Reason: %s^7",
			(*reason) ? reason : "^9kicked by admin^7"),
			g_autoTempBanTime.integer);
	}


	//josh: 2.60 won't kick from the engine. This will call
	//      ClientDisconnect
	// 120 seconds. Make #define?
	trap_DropClient(pids[0],
		va("^9You have been kicked, Reason: %s^7",
		(*reason) ? reason : "^9kicked by admin^7"),
		120);
	return qtrue;
}

// TODO: check this for unkown GUID - R.I.P. punkbuster
qboolean G_shrubbot_tempban(int clientnum, char *reason, int length) {
	char *guid, *ip;
#ifdef HW_BAN
	char *hwguid;
#endif // HW_BAN
	char tmp[MAX_NAME_LENGTH];
	int i;
	g_shrubbot_ban_t *b = NULL;
	time_t t;
	struct tm *lt;
	gentity_t *vic;

	if(!time(&t)) return qfalse;

	vic = &g_entities[clientnum];
	guid 	= vic->client->pers.cl_guid;
	if (length <= 0) return qfalse;

	// TODO?
	// IRATA: tempban unknown GUID users doesn't make sense ?!
	// if we keep it all unknown GUID players get kicked/banned
	// if we don't keep it there no way to kick/ban annoying players
	// possible solution kick by name/ip ... but this isn't the premium way to go :/
	// -> inspect!

#ifdef HW_BAN
	hwguid 	= vic->client->pers.cg_hwguid;
#endif // HW_BAN
	ip 		= vic->client->pers.client_ip;
	b 		= (g_shrubbot_ban_t*)malloc(sizeof(g_shrubbot_ban_t));

	if(!b) {
		return qfalse;
	}

	Q_strncpyz(b->name, vic->client->pers.netname, sizeof(b->name));
	Q_strncpyz(b->guid, guid, sizeof(b->guid));
#ifdef HW_BAN
	Q_strncpyz(b->hwguid, hwguid, sizeof(b->hwguid));
#endif // HW_BAN
	// strip port off of ip
	for(i=0; *ip; *ip++) {
		if(i >= (int)sizeof(tmp) - 1 || *ip == ':') break;
		tmp[i++] = *ip;
	}
	tmp[i] = '\0';
	Q_strncpyz(b->ip, tmp, sizeof(b->ip));

	lt = localtime(&t);
	strftime(b->made, sizeof(b->made), "%m/%d/%y %H:%M:%S", lt);
	Q_strncpyz(b->banner, "Temp Ban System", sizeof(b->banner));

	b->expires = NM_BanExpiration(t, length);
	if(!*reason) {
		Q_strncpyz(b->reason,
			"banned by Temp Ban System",
			sizeof(b->reason));
	} else {
		Q_strncpyz(b->reason, reason, sizeof(b->reason));
	}

	for(i=0; g_shrubbot_bans[i]; i++);
	if(i == MAX_SHRUBBOT_BANS) {
		free(b);
		return qfalse;
	}
	g_shrubbot_bans[i] = b;
	_shrubbot_writeconfig();
	return qtrue;
}


// TODO: check for unknown GUID !!!
qboolean G_shrubbot_ban(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	int seconds;
	char name[MAX_NAME_LENGTH], secs[7];
	char *reason, err[MAX_STRING_CHARS];
	char *guid, *ip;
#ifdef HW_BAN
	char *hwguid;
#endif // HW_BAN
	char tmp[MAX_NAME_LENGTH];
	int i;
	g_shrubbot_ban_t *b = NULL;
	time_t t;
	struct tm *lt;
	gentity_t *vic;
	int minargc;
	char duration[MAX_STRING_CHARS];
	int modifier = 1;

	if(!time(&t)) return qfalse;

	if(G_shrubbot_permission(ent, SBF_CAN_PERM_BAN) &&
		G_shrubbot_permission(ent, SBF_UNACCOUNTABLE)) {
		minargc = 2+skiparg;
	}
	else if(G_shrubbot_permission(ent, SBF_CAN_PERM_BAN) ||
		G_shrubbot_permission(ent, SBF_UNACCOUNTABLE)) {

		minargc = 3+skiparg;
	}
	else {
		minargc = 4+skiparg;
	}
	if(Q_SayArgc() < minargc) {
		SP("ban usage: !ban [name] [seconds] [reason]\n");
		return qfalse;
	}
	Q_SayArgv(1+skiparg, name, sizeof(name));
	Q_SayArgv(2+skiparg, secs, sizeof(secs));

	// tjw: support "w" (weeks), "d" (days), "h" (hours),
	//      and "m" (minutes) modifiers
	// Jaybird - you cannot assume they indeed do use a modifier.
	// This was chopping the last character off regardless of what it was.
	// This resulted in normal seconds being 1/10th of their intended length.
	if(*secs) {
		int lastchar = strlen(secs) - 1;
		if (secs[lastchar] < '0' || secs[lastchar] > '9') {
			if(secs[lastchar] == 'w')
				modifier = 60*60*24*7;
			else if(secs[lastchar] == 'd')
				modifier = 60*60*24;
			else if(secs[lastchar] == 'h')
				modifier = 60*60;
			else if(secs[lastchar] == 'm')
				modifier = 60;
			secs[lastchar] = '\0';
		}
	}
	seconds = atoi(secs);
	if(seconds > 0)
		seconds = NM_ScaledBanSeconds(seconds, modifier);

	if(seconds <= 0) {
		if(G_shrubbot_permission(ent, SBF_CAN_PERM_BAN)) {
			seconds = 0;
		}
		else {
			SP("ban: seconds must be a positive integer\n");
			return qfalse;
		}
		reason = Q_SayConcatArgs(2+skiparg);
	}
	else {
		reason = Q_SayConcatArgs(3+skiparg);
	}

	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("ban: %s\n", err));
		return qfalse;
	}
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP("ban: sorry, but your intended victim has a higher admin"
			" level than you do.\n");
		return qfalse;
	}

	if ( g_entities[pids[0]].client->pers.localClient ) {
		if ( g_entities[pids[0]].r.svFlags & SVF_BOT ) {
			SP("^dban: ^9Cannot ban a bot.\n");
			return qfalse;
		}
		else {
			SP("^dban: ^9Cannot ban server host.\n");
			return qfalse;
		}
	}

	if(G_shrubbot_permission(&g_entities[pids[0]], SBF_IMMUTABLE)) {
		SP( "Can't ban immune admins!\n" );
		return qfalse;
	}


	vic 	= &g_entities[pids[0]];
	guid 	= vic->client->pers.cl_guid;


// TODO: ban unknown GUIDs ?...better: ban by IP instead ...

#ifdef HW_BAN
	hwguid 	= vic->client->pers.cg_hwguid;
#endif // HW_BAN
	ip 		= vic->client->pers.client_ip;
	b 		= (g_shrubbot_ban_t*)malloc(sizeof(g_shrubbot_ban_t));

	if(!b) {
		return qfalse;
	}

	Q_strncpyz(b->name, vic->client->pers.netname, sizeof(b->name));
	Q_strncpyz(b->guid, guid, sizeof(b->guid));

#ifdef HW_BAN
	Q_strncpyz(b->hwguid, hwguid, sizeof(b->hwguid));
#endif // HW_BAN

	// strip port off of ip
	for(i=0; *ip; *ip++) {
		if(i >= (int)sizeof(tmp) - 1 || *ip == ':') break;
		tmp[i++] = *ip;
	}
	tmp[i] = '\0';
	Q_strncpyz(b->ip, tmp, sizeof(b->ip));

	lt = localtime(&t);
	strftime(b->made, sizeof(b->made), "%m/%d/%y %H:%M:%S", lt);
	if(ent) {
		Q_strncpyz(b->banner,
			ent->client->pers.netname,
			sizeof(b->banner));
	}
	else Q_strncpyz(b->banner, "console", sizeof(b->banner));
	if(!seconds)
		b->expires = 0;
	else
		b->expires = NM_BanExpiration(t, seconds);
	if(!*reason) {
		Q_strncpyz(b->reason,
			"banned by admin",
			sizeof(b->reason));
	}
	else {
		Q_strncpyz(b->reason, reason, sizeof(b->reason));
	}

	for(i=0; g_shrubbot_bans[i]; i++);
	if(i == MAX_SHRUBBOT_BANS) {
		SP("ban: too many bans\n");
		free(b);
		return qfalse;
	}
	g_shrubbot_bans[i] = b;

	SP(va("ban: %s^7 is now banned\n", vic->client->pers.netname));
	_shrubbot_writeconfig();
	// 0 seconds. Let bans handle time
	if(seconds) {
		Com_sprintf(duration,
			sizeof(duration),
			"for %i seconds",
			seconds);
	}
	else {
		Q_strncpyz(duration, "PERMANENTLY", sizeof(duration));
	}

	// IRATA - sound for ban
	if( g_announcer.integer & ANNOUNCE_BANSOUND) {
		G_globalSoundEnum(GAMESOUND_MISC_BANNED);
	}

	trap_DropClient(pids[0],
		va("You have been banned %s, Reason: %s",
		duration,
		(*reason) ? reason : "banned by admin"),
		0);
	return qtrue;
}

// core: ban a player on GUID only..
// This can be used to ban a player that is not online/ingame.
// If however the player is ingame, then his data will be stored in the ban (netname, ip etc)..
qboolean G_shrubbot_banguid(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	int seconds;
	char name[MAX_NAME_LENGTH], secs[7];
	char *reason;
	char guid[PB_GUID_LENGTH+1];
	char *guidOnline = NULL;
	char *ip = NULL;
	char tmp[MAX_NAME_LENGTH];
	int i;
	g_shrubbot_ban_t *b = NULL;
	time_t t;
	struct tm *lt;
	gentity_t *vic = NULL;
	int minargc;
	char duration[MAX_STRING_CHARS];
	int modifier = 1;
	qboolean isOnline = qfalse;
	gclient_t *p;

	if(!time(&t)) return qfalse;

	if(G_shrubbot_permission(ent, SBF_CAN_PERM_BAN) &&
		G_shrubbot_permission(ent, SBF_UNACCOUNTABLE)) {
		minargc = 2+skiparg;
	}
	else if(G_shrubbot_permission(ent, SBF_CAN_PERM_BAN) ||
		G_shrubbot_permission(ent, SBF_UNACCOUNTABLE)) {

		minargc = 3+skiparg;
	}
	else {
		minargc = 4+skiparg;
	}
	if(Q_SayArgc() < minargc) {
		SP("banguid usage: !banguid [GUID] [seconds] [reason]\n");
		return qfalse;
	}
	Q_SayArgv(1+skiparg, guid, sizeof(guid));
	Q_SayArgv(2+skiparg, secs, sizeof(secs));

	// check length of passed GUID-string..
	if ( strlen(guid) != PB_GUID_LENGTH ) {
		SP( va("banguid: GUID must have %i characters \n", PB_GUID_LENGTH) );
		return qfalse;
	}

	// tjw: support "w" (weeks), "d" (days), "h" (hours),
	//      and "m" (minutes) modifiers
	// Jaybird - you cannot assume they indeed do use a modifier.
	// This was chopping the last character off regardless of what it was.
	// This resulted in normal seconds being 1/10th of their intended length.
	if(*secs) {
		int lastchar = strlen(secs) - 1;
		if (secs[lastchar] < '0' || secs[lastchar] > '9') {
			if(secs[lastchar] == 'w')
				modifier = 60*60*24*7;
			else if(secs[lastchar] == 'd')
				modifier = 60*60*24;
			else if(secs[lastchar] == 'h')
				modifier = 60*60;
			else if(secs[lastchar] == 'm')
				modifier = 60;
			secs[lastchar] = '\0';
		}
	}
	seconds = atoi(secs);
	if(seconds > 0) {
		seconds *= modifier;
	}

	if(seconds <= 0) {
		if(G_shrubbot_permission(ent, SBF_CAN_PERM_BAN)) {
			seconds = 0;
		}
		else {
			SP("banguid: seconds must be a positive integer\n");
			return qfalse;
		}
		reason = Q_SayConcatArgs(2+skiparg);
	}
	else {
		reason = Q_SayConcatArgs(3+skiparg);
	}

	// check if the player is online..
	pids[0] = -1;

	// TODO: check if player has unkown guid
// if we ban unkown GUID here ALL players with no guid are gone ...

	for(i=0; i < level.maxclients; i++) {
		p = &level.clients[i];
		if(p->pers.connected != CON_CONNECTED && p->pers.connected != CON_CONNECTING) {
			continue;
		}
		if ( !Q_strncmp( p->pers.cl_guid, guid, PB_GUID_LENGTH ) ) {
			// player is online..
			isOnline = qtrue;
			pids[0] = i;
			pids[1] = -1;
			strncpy( name, p->pers.netname, sizeof(name) );
			break;
		}
	}

	if ( isOnline ) {
		if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
			SP("banguid: sorry, but your intended victim has a higher admin"
				" level than you do.\n");
			return qfalse;
		}

		if ( g_entities[pids[0]].client->pers.localClient ) {
			if ( g_entities[pids[0]].r.svFlags & SVF_BOT ) {
				SP("^dbanguid: ^9Cannot ban bot.\n");
				return qfalse;
			}
			else {
				SP("^dbanguid: ^9Cannot ban server host.\n");
				return qfalse;
			}
		}

		vic 		= &g_entities[pids[0]];
		guidOnline 	= vic->client->pers.cl_guid;
		ip 			= vic->client->pers.client_ip;
	}

	b = (g_shrubbot_ban_t*)malloc(sizeof(g_shrubbot_ban_t));
	if(!b)
		return qfalse;

	if ( isOnline ) {
		Q_strncpyz(b->name, vic->client->pers.netname, sizeof(b->name));
		Q_strncpyz(b->guid, guidOnline, sizeof(b->guid));

		// strip port off of ip
		for(i=0; *ip; *ip++) {
			if(i >= sizeof(tmp) || *ip == ':') break;
			tmp[i++] = *ip;
		}
		tmp[i] = '\0';
		Q_strncpyz(b->ip, tmp, sizeof(b->ip));
	}
	else {
		Q_strncpyz(b->name, "unknown", sizeof(b->name));
		Q_strncpyz(b->guid, guid, sizeof(b->guid));
		Q_strncpyz(b->ip, "offline", sizeof(b->ip));
	}

	lt = localtime(&t);
	strftime(b->made, sizeof(b->made), "%m/%d/%y %H:%M:%S", lt);
	if(ent) {
		Q_strncpyz(b->banner, ent->client->pers.netname, sizeof(b->banner));
	}
	else Q_strncpyz(b->banner, "console", sizeof(b->banner));

	if(!seconds)
		b->expires = 0;
	else
		b->expires = NM_BanExpiration(t, seconds);

	if(!*reason) {
		Q_strncpyz(b->reason, "banned by admin", sizeof(b->reason));
	}
	else {
		Q_strncpyz(b->reason, reason, sizeof(b->reason));
	}

	for(i=0; g_shrubbot_bans[i]; i++);
	if(i == MAX_SHRUBBOT_BANS) {
		SP("banguid: too many bans\n");
		free(b);
		return qfalse;
	}
	g_shrubbot_bans[i] = b;

	if ( isOnline ) {
		SP(va("banguid: %s^7 is now banned, GUID = %s\n", vic->client->pers.netname, guidOnline));
	} else {
		SP(va("banguid: %s^7 is now banned\n", guid));
	}

	_shrubbot_writeconfig();
	// 0 seconds. Let bans handle time
	if (seconds) {
		Com_sprintf(duration, sizeof(duration), "for %i seconds", seconds);
	}
	else {
		Q_strncpyz(duration, "PERMANENTLY", sizeof(duration));
	}

	// IRATA - sound for ban
	if( g_announcer.integer & ANNOUNCE_BANSOUND) {
		G_globalSoundEnum(GAMESOUND_MISC_BANNED);
	}

	if ( isOnline ) {
		trap_DropClient(pids[0], va("You have been banned %s, Reason: %s", duration, (*reason) ? reason : "banned by admin"), 0);
	}
	return qtrue;
}

// core: ban a player by IP only..
// This can be used to ban a player that is not online/ingame.
// If however the player is ingame, then his data will be stored in the ban (netname, ip etc)..
qboolean G_shrubbot_banIP(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	int seconds;
	char name[MAX_NAME_LENGTH], secs[7], ip[MAX_IP_LENGTH];
	char *reason;
	char guid[PB_GUID_LENGTH+1];
	char *guidOnline = NULL;
	char *ipOnline = NULL;
	char tmp[MAX_NAME_LENGTH];
	int i;
	g_shrubbot_ban_t *b = NULL;
	time_t t;
	struct tm *lt;
	gentity_t *vic = NULL;
	int minargc;
	char duration[MAX_STRING_CHARS];
	int modifier = 1;
	qboolean isOnline = qfalse;
	gclient_t *p;

	if(!time(&t)) return qfalse;

	if(G_shrubbot_permission(ent, SBF_CAN_PERM_BAN) &&
		G_shrubbot_permission(ent, SBF_UNACCOUNTABLE)) {
		minargc = 2+skiparg;
	}
	else if(G_shrubbot_permission(ent, SBF_CAN_PERM_BAN) ||
		G_shrubbot_permission(ent, SBF_UNACCOUNTABLE)) {
		minargc = 3+skiparg;
	}
	else {
		minargc = 4+skiparg;
	}
	if(Q_SayArgc() < minargc) {
		SP("banIP usage: !banIP [IP] [seconds] [reason]\n");
		return qfalse;
	}
	Q_SayArgv(1+skiparg, ip, sizeof(ip));
	Q_SayArgv(2+skiparg, secs, sizeof(secs));

	// tjw: support "w" (weeks), "d" (days), "h" (hours),
	//      and "m" (minutes) modifiers
	// Jaybird - you cannot assume they indeed do use a modifier.
	// This was chopping the last character off regardless of what it was.
	// This resulted in normal seconds being 1/10th of their intended length.
	if(*secs) {
		int lastchar = strlen(secs) - 1;
		if (secs[lastchar] < '0' || secs[lastchar] > '9') {
			if(secs[lastchar] == 'w')
				modifier = 60*60*24*7;
			else if(secs[lastchar] == 'd')
				modifier = 60*60*24;
			else if(secs[lastchar] == 'h')
				modifier = 60*60;
			else if(secs[lastchar] == 'm')
				modifier = 60;
			secs[lastchar] = '\0';
		}
	}
	seconds = atoi(secs);
	if(seconds > 0) {
		seconds *= modifier;
	}

	if(seconds <= 0) {
		if(G_shrubbot_permission(ent, SBF_CAN_PERM_BAN)) {
			seconds = 0;
		}
		else {
			SP("banIP: seconds must be a positive integer\n");
			return qfalse;
		}
		reason = Q_SayConcatArgs(2+skiparg);
	}
	else {
		reason = Q_SayConcatArgs(3+skiparg);
	}

	// check if the player is online..
	pids[0] = -1;
	for(i=0; i < level.maxclients; i++) {
		p = &level.clients[i];
		if(p->pers.connected != CON_CONNECTED && p->pers.connected != CON_CONNECTING) {
			continue;
		}
		if ( !Q_strncmp( p->pers.client_ip, ip, MAX_IP_LENGTH ) ) {
			// player is online..
			isOnline = qtrue;
			pids[0] = i;
			pids[1] = -1;
			strncpy( name, p->pers.netname, sizeof(name) );
			break;
		}
	}

	if ( isOnline ) {
		if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
			SP("banIP: sorry, but your intended victim has a higher admin"
				" level than you do.\n");
			return qfalse;
		}

		if ( g_entities[pids[0]].client->pers.localClient ) {
			if ( g_entities[pids[0]].r.svFlags & SVF_BOT ) {
				SP("^dbanIP: ^9Cannot ban bot.\n");
				return qfalse;
			}
			else {
				SP("^dbanIP: ^9Cannot ban server host.\n");
				return qfalse;
			}
		}

		vic 		= &g_entities[pids[0]];
		guidOnline 	= vic->client->pers.cl_guid;
		ipOnline	= vic->client->pers.client_ip;
	}

	b = (g_shrubbot_ban_t*)malloc(sizeof(g_shrubbot_ban_t));
	if(!b) {
		return qfalse;
	}

	if ( isOnline ) {
		Q_strncpyz(b->name, vic->client->pers.netname, sizeof(b->name));
		Q_strncpyz(b->guid, guidOnline, sizeof(b->guid));

		// strip port off of ip
		for(i=0; *ipOnline; *ipOnline++) {
			if(i >= (int)sizeof(tmp) - 1 || *ipOnline == ':') break;
			tmp[i++] = *ipOnline;
		}
		tmp[i] = '\0';
		Q_strncpyz(b->ip, tmp, sizeof(b->ip));
	}
	else {
		if (!NM_BanIPValid(ip)) {
			free(b);
			SP("^dbanIP: ^9invalid IPv4 address\n");
			return qfalse;
		}
		Q_strncpyz(b->name, "unknown", sizeof(b->name));
		Q_strncpyz(b->guid, "offline", sizeof(b->guid));
		Q_strncpyz(b->ip, ip, sizeof(b->ip));
	}

	lt = localtime(&t);
	strftime(b->made, sizeof(b->made), "%m/%d/%y %H:%M:%S", lt);
	if(ent) {
		Q_strncpyz(b->banner, ent->client->pers.netname, sizeof(b->banner));
	}
	else Q_strncpyz(b->banner, "console", sizeof(b->banner));

	if(!seconds)
		b->expires = 0;
	else
		b->expires = NM_BanExpiration(t, seconds);

	if(!*reason) {
		Q_strncpyz(b->reason, "banned by admin", sizeof(b->reason));
	}
	else {
		Q_strncpyz(b->reason, reason, sizeof(b->reason));
	}

	for(i=0; g_shrubbot_bans[i]; i++);
	if(i == MAX_SHRUBBOT_BANS) {
		SP("banIP: too many bans\n");
		free(b);
		return qfalse;
	}
	g_shrubbot_bans[i] = b;

	if ( isOnline ) {
		SP(va("banIP: %s^7 is now banned, IP = %s\n", vic->client->pers.netname, ip));
	}
	else {
		SP(va("banIP: %s^7 is now banned\n", guid));
	}

	_shrubbot_writeconfig();
	// 0 seconds. Let bans handle time
	if (seconds) {
		Com_sprintf(duration, sizeof(duration), "for %i seconds", seconds);
	}
	else {
		Q_strncpyz(duration, "PERMANENTLY", sizeof(duration));
	}

	// IRATA - sound for ban
	if( g_announcer.integer & ANNOUNCE_BANSOUND) {
		G_globalSoundEnum(GAMESOUND_MISC_BANNED);
	}

	if ( isOnline ) {
		trap_DropClient(pids[0], va("You have been banned %s, Reason: %s", duration, (*reason) ? reason : "banned by admin"), 0);
	}
	return qtrue;
}

qboolean G_shrubbot_unban(gentity_t *ent, int skiparg) {
	/* NewMod, Arena.ai: check fine-grained unban permission first */
	if (ent && !NM_FineGrainedAllowed(ent, SBF_CAN_UNBAN)) {
		SP("^dunban: ^9you do not have permission to unban\n");
		return qfalse;
	}
	int bnum;
	char bs[4];
	time_t t;

	if(!time(&t)) return qfalse;
	if(Q_SayArgc() < 2+skiparg) {
		SP("^dunban usage: ^2!unban [ban #]\n");
		return qfalse;
	}
	Q_SayArgv(1+skiparg, bs, sizeof(bs));
	bnum = atoi(bs);
	if ( (bnum < 1) || (!g_shrubbot_bans[bnum-1]) ) {
		SP("^dunban: ^9invalid ban #\n");
		return qfalse;
	}
	g_shrubbot_bans[bnum-1]->expires = t - SHRUBBOT_BAN_EXPIRE_OFFSET;
	SP(va("^dunban: ^9ban #%d removed\n", bnum));
	_shrubbot_writeconfig();
	return qtrue;
}

#define DEFAULT_MUTE_SECONDS 300

/* A numeric duration is seconds or a single m/h/d/w suffix. Text instead of
 * a duration remains a reason, as in NQ; reject numeric overflow explicitly. */
static qboolean NM_MuteDuration(char *text, int *seconds, qboolean *hasDuration) {
	char *end;
	long number, modifier = 1;
	*hasDuration = qfalse;
	if (!text[0] || (text[0] != '+' && (text[0] < '0' || text[0] > '9'))) return qtrue;
	if (strlen(text) >= 63) return qfalse; /* may have been truncated */
	errno = 0;
	number = strtol(text, &end, 10);
	if (end == text || errno == ERANGE) return qfalse;
	if (number <= 0) return qtrue;
	if (*end) {
		if (end[1]) return qfalse;
		switch (*end) {
		case 's': case 'S': break;
		case 'm': case 'M': modifier = 60; break;
		case 'h': case 'H': modifier = 60 * 60; break;
		case 'd': case 'D': modifier = 60 * 60 * 24; break;
		case 'w': case 'W': modifier = 60 * 60 * 24 * 7; break;
		default: return qfalse;
		}
	}
	if (number > INT_MAX / modifier) return qfalse;
	*seconds = (int)(number * modifier);
	*hasDuration = qtrue;
	return qtrue;
}

qboolean G_shrubbot_mute(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES], seconds = DEFAULT_MUTE_SECONDS, i, minargc; /* CHANGES: match sentinel */

	/* NewMod, Arena.ai: check fine-grained mute permission first */
	if (ent && !NM_FineGrainedAllowed(ent, SBF_CAN_MUTE)) {
		SP("^dmute: ^9you do not have permission to mute\n");
		return qfalse;
	}
	char name[MAX_NAME_LENGTH], safeName[MAX_NAME_LENGTH], secs[64], ipbuf[MAX_NAME_LENGTH];
	char *reason, *guid, *ip;
	g_shrubbot_ban_t *m;
	time_t now;
	struct tm *lt;
	gentity_t *victim;
	qboolean hasDuration;
	long long expires, deadline;

	if (time(&now) == (time_t)-1) return qfalse;
	minargc = (G_shrubbot_permission(ent, SBF_UNACCOUNTABLE) ? 2 : 3) + skiparg;
	if (Q_SayArgc() < minargc) {
		SP("^dmute usage: ^2!mute [name|slot#] [seconds] [reason]\n");
		return qfalse;
	}
	Q_SayArgv(1 + skiparg, name, sizeof(name));
	Q_SayArgv(2 + skiparg, secs, sizeof(secs));
	if (!NM_MuteDuration(secs, &seconds, &hasDuration)) {
		SP("^dmute: ^9invalid or overflowing duration\n");
		return qfalse;
	}
	reason = Q_SayConcatArgs((hasDuration ? 3 : 2) + skiparg);
	expires = (long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET + seconds;
	deadline = (long long)level.time + (long long)seconds * SECONDS_1;
	/* Both persisted NQ expiries and the map's mute deadline are signed ints;
	 * silently wrapping them can lift a mute immediately or make it permanent. */
	if (expires <= 0 || expires > INT_MAX || deadline < INT_MIN || deadline > INT_MAX) {
		SP("^dmute: ^9duration is outside the supported timer range\n");
		return qfalse;
	}
	if (ClientNumbersFromString(name, pids) != 1) {
		SP("^dmute: ^9target not found or name is ambiguous\n");
		return qfalse;
	}
	victim = &g_entities[pids[0]];
	if (!victim->client || victim->client->pers.connected != CON_CONNECTED ||
	    !_shrubbot_admin_higher(ent, victim) || G_shrubbot_permission(victim, SBF_IMMUTABLE)) {
		SP("^dmute: ^9target is unavailable or protected\n");
		return qfalse;
	}
	guid = victim->client->pers.cl_guid;
	ip = victim->client->pers.client_ip;
	if (!G_shrubbot_usable_guid(guid)) {
		SP("^dmute: ^9target has no usable GUID; no persistent mute was created\n");
		return qfalse;
	}
	if (victim->client->sess.muted || NM_FindActiveMute(guid, now)) {
		SP("^dmute: ^9already muted\n");
		return qfalse;
	}
	if (victim->r.svFlags & SVF_BOT) {
		SP("^dmute: ^9Cannot mute a bot.\n");
		return qfalse;
	}
	if (victim->client->pers.localClient) {
		SP("^dmute: ^9Cannot mute server host.\n");
		return qfalse;
	}
	for (i = 0; i < MAX_SHRUBBOT_BANS && g_shrubbot_mutes[i]; ++i) {}
	if (i == MAX_SHRUBBOT_BANS) {
		SP("^dmute: ^9too many mutes\n");
		return qfalse;
	}
	lt = localtime(&now);
	if (!lt) { SP("^dmute: ^9clock unavailable\n"); return qfalse; }
	m = (g_shrubbot_ban_t*)calloc(1, sizeof(*m));
	if (!m) { SP("^dmute: ^9out of memory\n"); return qfalse; }
	Q_strncpyz(m->name, victim->client->pers.netname, sizeof(m->name));
	Q_strncpyz(m->guid, guid, sizeof(m->guid));
	for (i = 0; *ip && *ip != ':' && i + 1 < (int)sizeof(ipbuf); ++ip) ipbuf[i++] = *ip;
	ipbuf[i] = '\0';
	Q_strncpyz(m->ip, ipbuf, sizeof(m->ip));
	if (!m->ip[0]) { free(m); SP("^dmute: ^9target has no IP\n"); return qfalse; }
	strftime(m->made, sizeof(m->made), "%m/%d/%y %H:%M:%S", lt);
	Q_strncpyz(m->banner, ent ? ent->client->pers.netname : "console", sizeof(m->banner));
	Q_strncpyz(m->reason, *reason ? reason : "muted by admin", sizeof(m->reason));
	m->expires = (int)expires;
	for (i = 0; g_shrubbot_mutes[i]; ++i) {}
	g_shrubbot_mutes[i] = m;
	victim->client->sess.muted = qtrue;
	victim->client->sess.auto_mute_time = (int)deadline;
	victim->client->sess.muted_by = MUTED_BY_SHRUB;
	ClientConfigStringChanged(victim);
	NM_ShrubDisplay(victim->client->pers.netname, safeName, sizeof(safeName));
	AP(va("chat \"^dmute: ^*%s ^9has been muted for %d seconds\"", safeName, seconds));
	_shrubbot_writeconfig();
	return qtrue;
}

/* Remove a live mute by player slot/name, or a stored offline record by
 * name. Record indices are never interpreted as client entity numbers. */
qboolean G_shrubbot_unmute(gentity_t *ent, int skiparg) {
	int clients[MAX_CLIENT_MATCHES], matches[MAX_SHRUBBOT_BANS];
	int argc = Q_SayArgc() - skiparg, n, count = 0, selected = -1, i, removed = 0;
	char input[MAX_NAME_LENGTH], search[MAX_STRING_CHARS], clean[MAX_STRING_CHARS];
	char name[MAX_NAME_LENGTH], guid[PB_GUID_LENGTH + 1], selection[64];
	gentity_t *target = NULL;
	time_t now;
	long long relativeNow;

	if (time(&now) == (time_t)-1) return qfalse;
	relativeNow = (long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET;
	if (relativeNow < INT_MIN) { SP("^dunmute: ^9clock outside supported range\n"); return qfalse; }
	if (argc < 2 || argc > 3 || !Q_SayArgv(1 + skiparg, input, sizeof(input))) goto usage;
	n = ClientNumbersFromString(input, clients);
	if (n > 1) { SP("^dunmute: ^9target name is ambiguous\n"); return qfalse; }
	guid[0] = name[0] = '\0';
	if (n == 1) {
		if (argc != 2) goto usage;
		target = &g_entities[clients[0]];
		if (!target->client || target->client->pers.connected != CON_CONNECTED) {
			SP("^dunmute: ^9target is unavailable\n"); return qfalse;
		}
		Q_strncpyz(guid, target->client->pers.cl_guid, sizeof(guid));
		NM_ShrubDisplay(target->client->pers.netname, name, sizeof(name));
	} else {
		SanitizeString(input, search, qtrue);
		if (!search[0]) goto usage;
		for (i = 0; i < MAX_SHRUBBOT_BANS && g_shrubbot_mutes[i]; ++i) {
			g_shrubbot_ban_t *m = g_shrubbot_mutes[i];
			if (m->expires && (long long)m->expires <= relativeNow) continue;
			SanitizeString(m->name, clean, qtrue);
			if (strstr(clean, search)) matches[count++] = i;
		}
		if (!count) { SP("^dunmute: ^9no matching active mute\n"); return qfalse; }
		if (argc == 3) {
			char *end;
			long number;
			Q_SayArgv(2 + skiparg, selection, sizeof(selection));
			errno = 0; number = strtol(selection, &end, 10);
			if (end == selection || *end || errno == ERANGE || number < 1 || number > count) goto usage;
			selected = matches[number - 1];
		} else if (count == 1) selected = matches[0];
		else {
			SBP_begin();
			SBP("^dunmute: ^9multiple active records; choose !unmute <name> <match#>:\n");
			for (i = 0; i < count && i < 32; ++i) {
				NM_ShrubDisplay(g_shrubbot_mutes[matches[i]]->name, name, sizeof(name));
				SBP(va("^7%d: %s\n", i + 1, name));
			}
			if (count > 32) SBP("^9More matches exist; use a more specific name.\n");
			SBP_end();
			return qfalse;
		}
		Q_strncpyz(guid, g_shrubbot_mutes[selected]->guid, sizeof(guid));
		NM_ShrubDisplay(g_shrubbot_mutes[selected]->name, name, sizeof(name));
	}

	/* A GUID may currently be shared. Refuse if the selected client or any
	 * connected client sharing the record key is protected from this actor. */
	for (i = 0; i < level.maxclients && i < MAX_CLIENTS; ++i) {
		gentity_t *other = &g_entities[i];
		if (!other->client || other->client->pers.connected != CON_CONNECTED) continue;
		if (other != target && (!G_shrubbot_usable_guid(guid) ||
		    Q_stricmp(other->client->pers.cl_guid, guid))) continue;
		if (!_shrubbot_admin_higher(ent, other) || G_shrubbot_permission(other, SBF_IMMUTABLE)) {
			SP("^dunmute: ^9target is protected\n"); return qfalse;
		}
	}
	for (i = 0; i < MAX_SHRUBBOT_BANS && g_shrubbot_mutes[i]; ++i) {
		g_shrubbot_ban_t *m = g_shrubbot_mutes[i];
		if (m->expires && (long long)m->expires <= relativeNow) continue;
		if ((G_shrubbot_usable_guid(guid) && !Q_stricmp(m->guid, guid)) ||
		    (!G_shrubbot_usable_guid(guid) && i == selected)) {
			m->expires = INT_MIN; /* nonzero, expired, and serializable */
			++removed;
		}
	}
	if (target && target->client->sess.muted) {
		/* As in NQ, an explicit live !unmute clears the target's session mute,
		 * even if it came from a referee, censor or Lua. */
		target->client->sess.muted = qfalse;
		target->client->sess.auto_mute_time = -1;
		target->client->sess.muted_by = UNMUTED;
		ClientConfigStringChanged(target);
	} else if (!removed) {
		SP("^dunmute: ^9target is not muted\n");
		return qfalse;
	}
	/* Other clients with the same GUID only lose a purely shrub-owned mute;
	 * conservatively retain any combined/other source with its shared timer. */
	if (removed && G_shrubbot_usable_guid(guid)) {
		for (i = 0; i < level.maxclients && i < MAX_CLIENTS; ++i) {
			gentity_t *other = &g_entities[i];
			if (other == target || !other->client || other->client->pers.connected != CON_CONNECTED ||
			    Q_stricmp(other->client->pers.cl_guid, guid)) continue;
			if (other->client->sess.muted && other->client->sess.muted_by == MUTED_BY_SHRUB) {
				other->client->sess.muted = qfalse;
				other->client->sess.auto_mute_time = -1;
				other->client->sess.muted_by = UNMUTED;
				ClientConfigStringChanged(other);
			}
		}
	}
	SP(va("^dunmute: ^9removed mute for %s^9 (%d stored record%s)\n", name,
	      removed, removed == 1 ? "" : "s"));
	if (removed) _shrubbot_writeconfig();
	return qtrue;

usage:
	SP("^dunmute usage: ^2!unmute [name|slot#] (offline match#)\n");
	return qfalse;
}

qboolean G_shrubbot_putteam(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], team[7], err[MAX_STRING_CHARS];
	gentity_t *vic;

	Q_SayArgv(1+skiparg, name, sizeof(name));
	Q_SayArgv(2+skiparg, team, sizeof(team));

	if(Q_SayArgc() < 3+skiparg) {
		SP("^dputteam usage: ^2!put ^9[name] [r|b|s]\n");
		return qfalse;
	}
	Q_SayArgv(1+skiparg, name, sizeof(name));
	Q_SayArgv(2+skiparg, team, sizeof(team));

	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("^dput: ^9%s\n", err));
		return qfalse;
	}
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP(va("^dput: ^9sorry, %s ^9is a higher level admin than you are.\n",
			g_entities[pids[0]].client->pers.netname));
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	if(!SetTeam(vic, team, qtrue, (weapon_t)-1, (weapon_t)-1, qfalse)) {
		SP("^dput: ^9Put team failed^7\n");
		return qfalse;
	}
	return qtrue;
}

/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): adapt the Oz/Jaymod war commands to NQ's
 * existing nq_War bitmask. Modes can coexist with other NQ war options. */
qboolean G_shrubbot_war(gentity_t *ent, int skiparg) {
	char command[MAX_SHRUBBOT_CMD_LEN], action[16];
	const char *name;
	int bit, mask;
	const int modes = WARMODE_KNIFE | WARMODE_PANZER | WARMODE_SNIPER |
		WARMODE_NADE | WARMODE_RIFLE | WARMODE_FLAMER | WARMODE_SHOTGUN |
		WARMODE_POISON | WARMODE_SMOKE | WARMODE_BINOCS | WARMODE_PLIERS;

	Q_SayArgv(skiparg, command, sizeof(command));
	name = command[0] == '!' ? command + 1 : command;
	if (!Q_stricmp(name, "panzerwar")) bit = WARMODE_PANZER;
	else if (!Q_stricmp(name, "sniperwar")) bit = WARMODE_SNIPER;
	else if (!Q_stricmp(name, "riflewar")) bit = WARMODE_RIFLE;
	else return qfalse;

	/* The cvar may have just been changed by a preceding console command. */
	mask = trap_Cvar_VariableIntegerValue("nq_War");
	if (Q_SayArgc() == 1 + skiparg) {
		SP(va("^d%s: ^9%s\n", name,
			(mask & WARMODE_ENABLE) && (mask & bit) ? "enabled" : "disabled"));
		return qtrue;
	}
	if (Q_SayArgc() != 2 + skiparg) {
		SP(va("^d%s: ^9usage: !%s [on|off]\n", name, name));
		return qfalse;
	}
	Q_SayArgv(1 + skiparg, action, sizeof(action));
	if (!Q_stricmp(action, "on")) {
		if ((mask & WARMODE_ENABLE) && (mask & bit)) {
			SP(va("^d%s: ^9already enabled\n", name));
			return qfalse;
		}
		mask |= WARMODE_ENABLE | bit;
	} else if (!Q_stricmp(action, "off")) {
		if (!(mask & bit)) {
			SP(va("^d%s: ^9already disabled\n", name));
			return qfalse;
		}
		mask &= ~bit;
		if (!(mask & modes)) mask &= ~WARMODE_ENABLE;
	} else {
		SP(va("^d%s: ^9usage: !%s [on|off]\n", name, name));
		return qfalse;
	}
	trap_Cvar_Set("nq_War", va("%d", mask));
	AP(va("chat \"^d%s: ^9%s^7\"", name, action));
	return qtrue;
}

/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): spectate like Oz/Jaymod while preserving
 * NQ's admin hierarchy, spectator locks and limbo restrictions. */
qboolean G_shrubbot_spec(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES], team; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH];
	gentity_t *vic;

	if (!ent || !ent->client) {
		SP("^dspec: ^9only a player can spectate\n");
		return qfalse;
	}
	if (Q_SayArgc() != 2 + skiparg) {
		SP("^dspec: ^9usage: !spec [name|slot#]\n");
		return qfalse;
	}
	Q_SayArgv(1 + skiparg, name, sizeof(name));
	if (ClientNumbersFromString(name, pids) != 1) {
		/* Do not embed unsanitized matching names in a server command. */
		SP("^dspec: ^9target not found or name is ambiguous\n");
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	if (!vic->client || vic->client->pers.connected != CON_CONNECTED ||
	    vic == ent || !_shrubbot_admin_higher(ent, vic)) {
		SP("^dspec: ^9invalid or higher-level target\n");
		return qfalse;
	}
	team = vic->client->sess.sessionTeam;
	if ((team != TEAM_AXIS && team != TEAM_ALLIES) ||
	    (vic->client->ps.pm_flags & PMF_LIMBO)) {
		SP("^dspec: ^9target is not an active player\n");
		return qfalse;
	}
	/* Check before moving the caller, so a rejected request has no team side effect. */
	if (teamInfo[team].spec_lock && !(ent->client->sess.spec_invite & team)) {
		SP("^dspec: ^9that team is locked from spectators\n");
		return qfalse;
	}
	if (ent->client->sess.sessionTeam != TEAM_SPECTATOR &&
	    !SetTeam(ent, "s", qtrue, (weapon_t)-1, (weapon_t)-1, qfalse)) {
		SP("^dspec: ^9could not join spectators\n");
		return qfalse;
	}
	if (!G_allowFollow(ent, team)) {
		SP("^dspec: ^9following that team is not allowed\n");
		return qfalse;
	}
	ent->client->sess.spec_team = 0;
	ent->client->sess.spectatorState = SPECTATOR_FOLLOW;
	ent->client->sess.spectatorClient = pids[0];
	SP(va("^dspec: ^9now following player #%d\n", pids[0]));
	return qtrue;
}

/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): Oz-inspired private reports. The log retains
 * player names and GUIDs, but not IP addresses or hardware identifiers. */
static void NM_ReportText(const char *src, char *dst, size_t size) {
	size_t i = 0;
	if (!size) return;
	while (*src && i + 1 < size) {
		unsigned char c = (unsigned char)*src++;
		dst[i++] = (c < 32 || c == 127 || c == '"' || c == '\\') ? ' ' : (char)c;
	}
	dst[i] = '\0';
}

qboolean G_shrubbot_report(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES], i, len; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], reason[256];
	char reporter[MAX_NAME_LENGTH], target[MAX_NAME_LENGTH];
	char reporterGuid[PB_GUID_LENGTH + 1], targetGuid[PB_GUID_LENGTH + 1];
	char record[1024], rawName[MAX_STRING_CHARS];
	const char *p;
	size_t n;
	qtime_t now;
	fileHandle_t f = 0;
	gentity_t *vic;

	if (!ent || !ent->client || ent->client->pers.connected != CON_CONNECTED) {
		SP("^dreport: ^9only connected players can submit reports\n");
		return qfalse;
	}
	if (!g_ReportFile.string[0]) {
		SP("^dreport: ^9reporting is disabled\n");
		return qfalse;
	}
	if (ent->client->pers.reportsSent >= 3) {
		SP("^dreport: ^9report limit reached for this map connection\n");
		return qfalse;
	}
	/* The normal Q_SayArgv parser only understands literal spaces and can
	 * misparse leading whitespace. Chat and direct console input are parsed
	 * separately; direct console targets may be quoted by the engine. */
	if (skiparg) {
		p = ConcatArgs(1);
		while (*p && (unsigned char)*p <= ' ') ++p;
		if (strlen(p) < 7 || Q_stricmpn(p, "!report", 7) ||
		    (p[7] && (unsigned char)p[7] > ' ')) goto usage;
		p += 7;
		while (*p && (unsigned char)*p <= ' ') ++p;
		n = 0;
		while (*p && (unsigned char)*p > ' ') {
			if (n + 1 >= sizeof(name)) goto usage;
			name[n++] = *p++;
		}
		name[n] = '\0';
		while (*p && (unsigned char)*p <= ' ') ++p;
		NM_ReportText(p, reason, sizeof(reason));
	} else {
		trap_Argv(1, rawName, sizeof(rawName));
		if (strlen(rawName) >= sizeof(name)) goto usage;
		Q_strncpyz(name, rawName, sizeof(name));
		NM_ReportText(ConcatArgs(2), reason, sizeof(reason));
	}
	if (!name[0]) goto usage;
	for (i = 0; reason[i] == ' '; ++i) {}
	if (!reason[i]) {
		SP("^dreport: ^9a reason is required\n");
		return qfalse;
	}
	if (ClientNumbersFromString(name, pids) != 1) {
		/* Match listings can contain untrusted names: don't put them in SP. */
		SP("^dreport: ^9target not found or name is ambiguous\n");
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	if (!vic->client || vic->client->pers.connected != CON_CONNECTED) {
		SP("^dreport: ^9target is not connected\n");
		return qfalse;
	}
	NM_ReportText(ent->client->pers.netname, reporter, sizeof(reporter));
	NM_ReportText(vic->client->pers.netname, target, sizeof(target));
	NM_ReportText(ent->client->pers.cl_guid, reporterGuid, sizeof(reporterGuid));
	NM_ReportText(vic->client->pers.cl_guid, targetGuid, sizeof(targetGuid));
	memset(&now, 0, sizeof(now));
	trap_RealTime(&now);
	Com_sprintf(record, sizeof(record),
		"[%04d-%02d-%02d %02d:%02d:%02d] reporter=\"%s\" reporter_guid=\"%s\" target=\"%s\" target_guid=\"%s\" reason=\"%s\"\n",
		now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec,
		reporter, reporterGuid, target, targetGuid, reason);
	if (trap_FS_FOpenFile(g_ReportFile.string, &f, FS_APPEND) < 0 || !f) {
		G_Printf("report: could not open %s\n", g_ReportFile.string);
		SP("^dreport: ^9could not save report\n");
		return qfalse;
	}
	len = strlen(record);
	i = trap_FS_Write(record, len, f);
	trap_FS_FCloseFile(f);
	if (i != len) {
		SP("^dreport: ^9could not save report\n");
		return qfalse;
	}
	++ent->client->pers.reportsSent;
	SP("^dreport: ^9your report was saved\n");
	for (i = 0; i < level.numConnectedClients; ++i) {
		int slot = level.sortedClients[i];
		gentity_t *admin = &g_entities[slot];
		if (admin->client && admin->client->pers.connected == CON_CONNECTED &&
		    G_shrubbot_level(admin) > 0 &&
		    G_shrubbot_level(admin) >= g_viewreport_level.integer) {
			trap_SendServerCommand(slot, va("chat \"^dreport: ^7%s ^9reported ^7%s^9: %s\"", reporter, target, reason));
		}
	}
	return qtrue;

usage:
	SP("^dreport: ^9usage: !report [name|slot#] [reason]\n");
	return qfalse;
}

qboolean G_shrubbot_pause(gentity_t *ent, int skiparg) {
	G_refPause_cmd(ent, qtrue);
	return qtrue;
}

qboolean G_shrubbot_unpause(gentity_t *ent, int skiparg) {
	G_refPause_cmd(ent, qfalse);
	return qtrue;
}

char* G_SystemInfo(int clientnum) {
	char* buffer;

#ifdef SYSTEM_CHECK
	if (g_entities[clientnum].r.svFlags & SVF_BOT) {
		return buffer = va("%s", "(B) ");
	}

	// system
	if (level.clients[clientnum].sess.nqClientSystemInfo & (128|256)) {
		buffer = va("%s", "(W) ");
	}
	else if (level.clients[clientnum].sess.nqClientSystemInfo & (512|1024)) {
		buffer = va("%s", "(L) ");
	}
	else if (level.clients[clientnum].sess.nqClientSystemInfo & (2048|4096)) {
		buffer = va("%s", "(M) ");
	}
	else {
		buffer = va("%s", "(?) ");
	}

	// client exec
	if (level.clients[clientnum].sess.nqClientSystemInfo & 1) {
		buffer = va("%s%s", buffer , "2.60b  ");
	}
	else if (level.clients[clientnum].sess.nqClientSystemInfo & 2) {
		buffer = va("%s%s", buffer , "2.60   ");
	}
	else if (level.clients[clientnum].sess.nqClientSystemInfo & 4) {
		buffer = va("%s%s", buffer , "OWolf  ");
	}
	else if (level.clients[clientnum].sess.nqClientSystemInfo & 8) {
		buffer = va("%s%s", buffer , "ETleg  ");
	}
	else if (level.clients[clientnum].sess.nqClientSystemInfo & 16) {
		buffer = va("%s%s", buffer , "GPL    ");
	}
	else if (level.clients[clientnum].sess.nqClientSystemInfo & 32) { // FREE
		buffer = va("%s%s", buffer , "TODO   ");
	}
	else if (level.clients[clientnum].sess.nqClientSystemInfo & 64) { // if we can't detect
		buffer = va("%s%s", buffer , "OTHER  ");
	}

	if (level.clients[clientnum].sess.nqClientSystemInfo & 32768) {
		buffer = va("%s%s", buffer , "*untrusted*");
	}
#else
	buffer = "";
#endif
	return buffer;
}

// TODO
qboolean G_shrubbot_listplayers(gentity_t *ent, int skiparg) {
	int i,j;
	gclient_t *p;
	char c[3], t[2]; // color and team letter
	char n[MAX_NAME_LENGTH] = {""};
	char n2[MAX_NAME_LENGTH] = {""};
	char n3[MAX_NAME_LENGTH] = {""};
	char lname[MAX_NAME_LENGTH];
	char lname2[MAX_NAME_LENGTH];
	char* guid;
	char* ip;
	char guid_stub[9];
	char fireteam[2];
	char muted[2];
	int l;
	fireteamData_t *ft;
	int lname_max = 1;
	char lname_fmt[5];
	char cleanLevelName[MAX_NAME_LENGTH];
	int spaces;

	// detect the longest level name length
	for(i=0; g_shrubbot_levels[i]; i++) {
		//SanitizeString(g_shrubbot_levels[i]->name, n, qtrue);
		DecolorString(g_shrubbot_levels[i]->name, n);
		if(strlen(n) > lname_max)
			lname_max = strlen(n);
		Com_sprintf(n, sizeof(n), "%d", g_shrubbot_levels[i]->level);
		if(strlen(n) > lname_max)
			lname_max = strlen(n);
	}

	SBP_begin();
	SBP(va("listplayers: %d players connected:\n", level.numConnectedClients));

	//SBP(" # T L  Levelname               GUID-Stub   M FT Info       Name\n");

	for(i=0; i < level.maxclients; i++) {
		p = &level.clients[i];
		Q_strncpyz(t, "S", sizeof(t));
		Q_strncpyz(c, S_COLOR_YELLOW, sizeof(c));
		if(p->sess.sessionTeam == TEAM_ALLIES) {
			Q_strncpyz(t, "B", sizeof(t));
			if (ent) {
				Q_strncpyz(c, S_COLOR_BLUE, sizeof(c));
			}
		}
		else if(p->sess.sessionTeam == TEAM_AXIS) {
			Q_strncpyz(t, "R", sizeof(t));
			if (ent) {
				Q_strncpyz(c, S_COLOR_RED, sizeof(c));
			}
		}

		if(p->pers.connected == CON_CONNECTING) {
			Q_strncpyz(t, "C", sizeof(t));
			if (ent) {
				Q_strncpyz(c, S_COLOR_ORANGE, sizeof(c));
			}
		}
		else if(p->pers.connected != CON_CONNECTED) {
			continue;
		}

		guid 	= level.clients[i].pers.cl_guid;
		ip		= level.clients[i].pers.client_ip;

		// pheno: Fixed wrong GUID in !listplayers output when following a bot
		if (g_entities[i].r.svFlags & SVF_BOT) {
			Q_strncpyz(guid_stub, "OMNIBOT*", sizeof(guid_stub));
		}
		else if (!*guid) {  // IRATA: have never seen this case
            Q_strncpyz(guid_stub, "NONE", sizeof(guid_stub));
        }
		else if (!Q_stricmp(guid, "unknown")) { // IRATA: there is no GUID
			 Q_strncpyz(guid_stub, "unknown", sizeof(guid_stub));
		}
		else {
			for(j=0; j<=8; j++) {
				guid_stub[j] = guid[j+24];
			}
		}

		fireteam[0] = '\0';
		if(G_IsOnFireteam(i, &ft)) {
			Q_strncpyz(fireteam,
				bg_fireteamNames[ft->ident - 1],
				sizeof(fireteam));
		}

		muted[0] = '\0';
		// CHECK/FIXME/TODO - mute check is done by IP ... do we do mute by IP only ?
		if ( p->sess.muted || G_shrubbot_mute_check(ip, guid) ) {
			Q_strncpyz(muted,"M",sizeof(muted));
		}

		l = 0;
		SanitizeString(p->pers.netname, n2, qtrue);
		n[0] = '\0';
		for(j=0; g_shrubbot_admins[j]; j++) {
			if(G_shrubbot_usable_guid(guid) && !Q_stricmp(g_shrubbot_admins[j]->guid, guid)) {
				// tjw: don't gather aka or level info if
				//      the admin is incognito
				if(G_shrubbot_permission(&g_entities[i],
					SBF_INCOGNITO)) {
					break;
				}
				l = g_shrubbot_admins[j]->level;
				SanitizeString(g_shrubbot_admins[j]->name,
					n3, qtrue);
				if(Q_stricmp(n2, n3)) {
					Q_strncpyz(n,
						g_shrubbot_admins[j]->name,
						sizeof(n));
				}
				break;
			}
		}
		lname[0] = '\0';
		for(j=0; g_shrubbot_levels[j]; j++) {
			if(g_shrubbot_levels[j]->level == l)
				Q_strncpyz(lname,
					g_shrubbot_levels[j]->name,
					sizeof(lname));
		}
		if(!*lname)  {
			Com_sprintf(lname,
				sizeof(lname),
				"%i",
				l);
		}
		// gabriel: level names can have color codes in them. Vary the number of
		// spaces in the print mask so the level names have the correct number
		// of leading spaces
		DecolorString(lname, cleanLevelName);
		spaces = lname_max - strlen(cleanLevelName);
		//Com_sprintf(lname_fmt, sizeof(lname_fmt), "%%%is", spaces + strlen(lname));
#if defined(__x86_64__) || defined(__aarch64__) || defined(_WIN64)
		Com_sprintf(lname_fmt, sizeof(lname_fmt), "%%%ds", spaces + (int)strlen(lname)); // IRATA: int might overflow - but we shouldnt have these high values
#else
		Com_sprintf(lname_fmt, sizeof(lname_fmt), "%%%ds", spaces + strlen(lname));
#endif
		Com_sprintf(lname2, sizeof(lname2), lname_fmt, lname);

		if (!ent) { // console with no colors
			SBP(va("%2i %s %-2i '%s' (*%s)  %1s %1s %s %s %s%s%s\n",
				i,
				t,
				l,
				lname2,
				guid_stub,
				muted,
				fireteam,
				G_SystemInfo(i), // TODO: only for special shrubbot flag,
				p->pers.netname,
				(*n) ? "(a.k.a. " : "",
				n,
				(*n) ? ")" : ""
				));
		}
		else {
			SBP(va("%2i %s%s^7 %-2i '%s'^7 (*%s)  ^1%1s ^3%1s^7 %s %s^7 %s%s^7%s\n",
				i,
				c,
				t,
				l,
				lname2,
				guid_stub,
				muted,
				fireteam,
				G_SystemInfo(i), // TODO: only for special shrubbot flag,
				p->pers.netname,
				(*n) ? "(a.k.a. " : "",
				n,
				(*n) ? ")" : ""
				));
		}
	}
	SBP_end();
	return qtrue;
}

qboolean G_shrubbot_listteams(gentity_t *ent, int skiparg) {
	int playerCount[3], pings[3], totalXP[3];
	int i, j;
	gclient_t *p;

	playerCount[0] = pings[0] = totalXP[0] = 0;
	playerCount[1] = pings[1] = totalXP[1] = 0;
	playerCount[2] = pings[2] = totalXP[2] = 0;

	for(i=0; i<level.maxclients; i++) {
		p = &level.clients[i];
		if(p->pers.connected != CON_CONNECTED) {
			continue;
		}
		if(p->sess.sessionTeam == TEAM_ALLIES) {
			playerCount[2]++;
			pings[2] += p->ps.ping;
			for(j = 0; j < SK_NUM_SKILLS; j++) {
				totalXP[2] += p->sess.skillpoints[j];
			}
		}
		else if(p->sess.sessionTeam == TEAM_AXIS) {
			playerCount[1]++;
			pings[1] += p->ps.ping;
			for(j = 0; j < SK_NUM_SKILLS; j++) {
				totalXP[1] += p->sess.skillpoints[j];
			}
		}
		else if(p->sess.sessionTeam == TEAM_SPECTATOR) {
			playerCount[0]++;
			pings[0] += p->ps.ping;
			for(j = 0; j < SK_NUM_SKILLS; j++) {
				totalXP[0] += p->sess.skillpoints[j];
			}
		}
	}
	SBP_begin();
	SBP(va("Desc       ^4Allies^7(%s^7)      ^1Axis^7(%s^7)       ^3Specs\n",
			teamInfo[TEAM_ALLIES].team_lock ? "^1L" : "^2U",
			teamInfo[TEAM_AXIS].team_lock ? "^1L" : "^2U"));
	SBP("^7---------------------------------------------\n");
	SBP(va("^2Players     ^7%8d     %8d    %8d\n",
			playerCount[2],
			playerCount[1],
			playerCount[0]));
	SBP(va("^2Avg Ping    ^7%8.2f     %8.2f    %8.2f\n",
			playerCount[2] > 0 ? (float)(pings[2]) / playerCount[2] : 0,
			playerCount[1] > 0 ? (float)(pings[1]) / playerCount[1] : 0,
			playerCount[0] > 0 ? (float)(pings[0]) / playerCount[0] : 0));
	SBP(va("^2Map XP      ^7%8d     %8d          --\n",
			level.teamScores[TEAM_ALLIES],
			level.teamScores[TEAM_AXIS]));
	SBP(va("^2Total XP    ^7%8d     %8d    %8d\n",
			totalXP[2],
			totalXP[1],
			totalXP[0]));
	SBP(va("^2Avg Map XP  ^7%8.2f     %8.2f          --\n",
			playerCount[2] > 0 ? (float)(level.teamScores[TEAM_ALLIES]) / playerCount[2] : 0,
			playerCount[1] > 0 ? (float)(level.teamScores[TEAM_AXIS])   / playerCount[1] : 0 ));
	SBP(va("^2Avg Tot XP  ^7%8.2f     %8.2f    %8.2f\n",
			playerCount[2] > 0 ? (float)(totalXP[2]) / playerCount[2] : 0,
			playerCount[1] > 0 ? (float)(totalXP[1]) / playerCount[1] : 0,
			playerCount[0] > 0 ? (float)(totalXP[0]) / playerCount[0] : 0 ));
	SBP_end();
	return qfalse;
}

/* NewMod, Arena.ai, 2026-11-21: keep display searches read-only and keep
 * paging state per connection rather than indexing it by mutable ACL order. */
static qboolean NM_ShrubContains(const char *text, const char *needle) {
	size_t n = strlen(needle);
	if (!n) return qtrue;
	for (; *text; ++text)
		if (!Q_stricmpn(text, needle, (int)n)) return qtrue;
	return qfalse;
}

static void NM_ShrubDisplay(const char *src, char *dst, size_t size) {
	size_t n = 0;
	while (*src && n + 1 < size) {
		unsigned char c = (unsigned char)*src++;
		dst[n++] = (c < 32 || c == 127 || c == '"' || c == '\\') ? ' ' : (char)c;
	}
	if (size) dst[n] = '\0';
}

typedef struct {
	qboolean valid;
	int kind, start;
	char filter[64];
} nm_ban_page_t;
static nm_ban_page_t nm_ban_pages[MAX_CLIENTS + 1]; /* console has its own slot */

void G_shrubbot_clear_ban_pages(int slot) {
	if (slot < 0) memset(nm_ban_pages, 0, sizeof(nm_ban_pages));
	else if (slot < MAX_CLIENTS) memset(&nm_ban_pages[slot], 0, sizeof(nm_ban_pages[slot]));
}

qboolean G_shrubbot_showbans(gentity_t *ent, int skiparg) {
	time_t now;
	long long relativeNow;
	int indexes[MAX_SHRUBBOT_BANS], count = 0, kind = 0, start = 0;
	int i, pos, end, pages, viewer = MAX_CLIENTS, args = Q_SayArgc() - skiparg;
	char action[64] = "", filter[64] = "", page[64] = "";
	char safeFilter[64], name[MAX_NAME_LENGTH], banner[MAX_NAME_LENGTH];
	char reason[512], date[50], duration[64];
	const char *filterName = "";
	nm_ban_page_t *cursor;

	if (ent) {
		viewer = (int)(ent - g_entities);
		if (viewer < 0 || viewer >= level.maxclients || viewer >= MAX_CLIENTS || !ent->client)
			return qfalse;
	}
	if (time(&now) == (time_t)-1) return qfalse;
	relativeNow = (long long)now - SHRUBBOT_BAN_EXPIRE_OFFSET;
	cursor = &nm_ban_pages[viewer];

	if (args < 1 || args > 4) goto usage;
	if (args > 1) {
		Q_SayArgv(1 + skiparg, action, sizeof(action));
		if (!Q_stricmp(action, "find")) { kind = 1; filterName = "find"; }
		else if (!Q_stricmp(action, "banner")) { kind = 2; filterName = "banner"; }
		else if (!Q_stricmp(action, "reason")) { kind = 3; filterName = "reason"; }
		if (kind) {
			if (args < 3 || !Q_SayArgv(2 + skiparg, filter, sizeof(filter))) goto usage;
			if (args == 4) Q_SayArgv(3 + skiparg, page, sizeof(page));
		} else {
			if (args != 2) goto usage;
			Q_strncpyz(page, action, sizeof(page));
		}
	}

	/* Build a dense, bounded list of matching active records. The printed ID
	 * remains the original ban number for !unban, including expired gaps. */
	for (i = 0; i < MAX_SHRUBBOT_BANS && g_shrubbot_bans[i]; ++i) {
		g_shrubbot_ban_t *b = g_shrubbot_bans[i];
		const char *field = kind == 1 ? b->name : kind == 2 ? b->banner : b->reason;
		if (b->expires && (long long)b->expires <= relativeNow) continue;
		if (kind && !NM_ShrubContains(field, filter)) continue;
		indexes[count++] = i;
	}

	if (page[0]) {
		if (!Q_stricmp(page, "next") || !Q_stricmp(page, "prev")) {
			if (cursor->valid && cursor->kind == kind && !Q_stricmp(cursor->filter, filter)) {
				start = cursor->start + (!Q_stricmp(page, "next") ? SHRUBBOT_MAX_SHOWBANS : -SHRUBBOT_MAX_SHOWBANS);
				if (start < 0) start = 0;
			}
		} else {
			char *tail;
			long number;
			errno = 0;
			number = strtol(page, &tail, 10);
			if (tail == page || *tail || errno == ERANGE || number < INT_MIN || number > INT_MAX) goto usage;
			if (number < 0) {
				start = number < -count ? 0 : count + (int)number;
			} else if (number > 0) {
				while (start < count && indexes[start] + 1 < number) ++start;
			}
		}
	}
	if (start >= count && count) {
		SPC("^dshowbans: ^9no matching bans at that position\n");
		return qfalse;
	}
	end = start + SHRUBBOT_MAX_SHOWBANS;
	if (end > count) end = count;
	pages = count ? (count + SHRUBBOT_MAX_SHOWBANS - 1) / SHRUBBOT_MAX_SHOWBANS : 1;
	NM_ShrubDisplay(filter, safeFilter, sizeof(safeFilter));

	SBP_begin();
	for (pos = start; pos < end; ++pos) {
		g_shrubbot_ban_t *b = g_shrubbot_bans[indexes[pos]];
		long long remaining = (long long)b->expires - relativeNow;
		char *space;
		NM_ShrubDisplay(b->name, name, sizeof(name));
		NM_ShrubDisplay(b->banner, banner, sizeof(banner));
		NM_ShrubDisplay(b->reason, reason, sizeof(reason));
		NM_ShrubDisplay(b->made, date, sizeof(date));
		space = strchr(date, ' ');
		if (space) *space = '\0';
		G_shrubbot_duration(!b->expires ? -1 : remaining > INT_MAX ? INT_MAX : (int)remaining,
		                   duration, sizeof(duration));
		SBP(va("^F%4d^7 %-*s ^F%-10s^7 %-*s ^F%-9s^7 %s\n", indexes[pos] + 1,
		       MAX_NAME_LENGTH - 1, name, date, MAX_NAME_LENGTH - 1, banner, duration, reason));
	}
	if (kind)
		SBP(va("^dshowbans: ^9%d active bans matching ^2%s ^9(%d page%s)\n",
		       count, safeFilter, pages, pages == 1 ? "" : "s"));
	else
		SBP(va("^dshowbans: ^9showing ^7%d-%d ^9of ^7%d ^9(Page ^7%d ^9of ^7%d^9)\n",
		       count ? start + 1 : 0, end, count, start / SHRUBBOT_MAX_SHOWBANS + 1, pages));
	if (end < count) {
		if (kind) SBP(va("^9(type !showbans ^2%s %s next ^9for more)\n", filterName, safeFilter));
		else SBP("^9(type !showbans ^2next ^9for more)\n");
	}
	SBP_end();
	cursor->valid = qtrue;
	cursor->kind = kind;
	cursor->start = start;
	Q_strncpyz(cursor->filter, filter, sizeof(cursor->filter));
	return qtrue;

usage:
	SPC("^dusage: ^2!showbans ^9[start|next|prev|find <match> [start|next|prev]|banner <match> [start|next|prev]|reason <match> [start|next|prev]]\n");
	return qfalse;
}

qboolean G_shrubbot_help(gentity_t *ent, int skiparg) {
	int i;

	if(Q_SayArgc() < 2+skiparg) {
		//!help
		int j = 0;
		int count = 0;
		char *str = "";

		for (i=0; g_shrubbot_cmds[i].keyword[0]; i++) {

			if(G_shrubbot_permission(ent, g_shrubbot_cmds[i].flag)) {
				str = va( "%s^f%-12s", str, g_shrubbot_cmds[i].keyword);
				j++;
				count++;
			}
			// show 6 commands per line
			if( j == 6) {
				str = va("%s\n",str);
				j = 0;
			}
		}
		SBP_begin();
		SBP(str);
		// tjw: break str into at least two parts to try to avoid
		//      1022 char limitation
		str = "";
		for (i=0; g_shrubbot_commands[i]; i++) {
			if(!_shrubbot_command_permission(ent,
				g_shrubbot_commands[i]->command)) {

				continue;
			}
			str = va( "%s^f%-12s", str,
				va("%s", g_shrubbot_commands[i]->command));
			j++;
			count++;
			// show 6 commands per line
			if( j == 6) {
				str = va("%s\n",str);
				j = 0;
			}
		}
		if(count) {
			str = va("%s\n",str);
		}
		CP(va("chat \"^dhelp: ^9%i available commands (open console for the full list)\" -1", count));

		SBP( str );
		SBP("^9Type ^2!help ^d[command] ^9for help with a specific command.^7\n");
		SBP_end();

		return qtrue;
	}
	else {
		//!help param
		char param[20];

		Q_SayArgv(1+skiparg, param, sizeof(param));
		SBP_begin();
		for( i=0; g_shrubbot_cmds[i].keyword[0]; i++ ){
			if(	!Q_stricmp(param, g_shrubbot_cmds[i].keyword ) ) {
				if(!G_shrubbot_permission(ent, g_shrubbot_cmds[i].flag)) {
					SBP(va("^dhelp: ^9you have no permission to use '%s'\n",
						g_shrubbot_cmds[i].keyword ) );
					return qfalse;
				}
				SBP( va("^dhelp: ^9help for '^2%s^9':\n", g_shrubbot_cmds[i].keyword ) );
				SBP( va("^dFunction: ^9%s\n", g_shrubbot_cmds[i].function ) );
				SBP( va("^dSyntax: ^2!%s ^9%s\n", g_shrubbot_cmds[i].keyword,
					g_shrubbot_cmds[i].syntax ) );
				SBP( va("^dFlag: ^9'^2%c^9'\n", g_shrubbot_cmds[i].flag) );
				SBP_end();
				return qtrue;
			}
		}
		for( i=0; g_shrubbot_commands[i]; i++ ){
			if(	!Q_stricmp(param, g_shrubbot_commands[i]->command ) ) {
				if(!_shrubbot_command_permission(ent,
					g_shrubbot_commands[i]->command)) {

					SBP(va("^dhelp: ^9you have no permission to use '%s'\n",
						g_shrubbot_commands[i]->command ) );
					SBP_end();
					return qfalse;
				}
				SBP( va("^dhelp: ^9help for '%s':\n",
					g_shrubbot_commands[i]->command ) );
				SBP( va("^dDescription: ^9%s\n",
					g_shrubbot_commands[i]->desc ) );
				SBP( va("^dSyntax: ^9!%s\n",
					g_shrubbot_commands[i]->command) );
				SBP_end();
				return qtrue;
			}
		}
		SBP( va("^dhelp: ^9no help found for '%s'\n", param ) );
		SBP_end();
		return qfalse;
	}
}

qboolean G_shrubbot_admintest(gentity_t *ent, int skiparg) {
	int i, l = 0;
	char *guid;
	qboolean found = qfalse;
	qboolean lname = qfalse;

	if(!ent) {
		SP("^dadmintest: ^9you are on the console.^7\n");
		return qtrue;
	}

	guid = level.clients[ent-g_entities].pers.cl_guid;
	for(i=0; g_shrubbot_admins[i]; i++) {
		if(G_shrubbot_usable_guid(guid) && !Q_stricmp(g_shrubbot_admins[i]->guid, guid)) {
			found = qtrue;
			break;
		}
	}

	if(found) {
		l = g_shrubbot_admins[i]->level;
		for(i=0; g_shrubbot_levels[i]; i++) {
			if(g_shrubbot_levels[i]->level != l)
				continue;
			if(*g_shrubbot_levels[i]->name) {
				lname = qtrue;
				break;
			}
		}
	}

	AP(va("chat \"^dadmintest: ^7%s^9 is a level %d user %s%s%s\" -1",
		ent->client->pers.netname,
		l,
		(lname) ? "^9(^7" : "",
		(lname) ? g_shrubbot_levels[i]->name : "",
		(lname) ? "^9)^7" : ""));
	return qtrue;
}

qboolean G_shrubbot_cancelvote(gentity_t *ent, int skiparg) {

	level.voteInfo.voteNo = level.numConnectedClients;
	CheckVote();
	SP("^dcancelvote: ^9turns out everyone voted ^3No\n");
	return qtrue;
}

qboolean G_shrubbot_passvote(gentity_t *ent, int skiparg) {
	level.voteInfo.voteYes = level.numConnectedClients;
	level.voteInfo.voteNo = 0;
	CheckVote();
	SP("^dpassvote: ^9turns out everyone voted ^3Yes\n");
	return qtrue;
}

qboolean G_shrubbot_spec999(gentity_t *ent, int skiparg) {
	int i;
	gentity_t *vic;

	for(i = 0; i < level.maxclients; i++) {
		vic = &g_entities[i];
		if(!vic->client) continue;
		if(vic->client->pers.connected != CON_CONNECTED) continue;
		if(vic->client->sess.sessionTeam == TEAM_SPECTATOR) continue; // IRATA
		// if(vic->r.svFlags & SVF_BOT) continue; // IRATA: might happen if server is busy
		if(vic->client->ps.ping >= 500 && vic->client->ps.ping <= 999) {
			SetTeam(vic, "s", qtrue, (weapon_t)-1, (weapon_t)-1, qfalse);
			AP(va("chat \"^dspec999: ^*%s^9 moved to spectators^7\" -1",
				vic->client->pers.netname));
		}
	}
	return qtrue;
}

qboolean G_shrubbot_shuffle(gentity_t *ent, int skiparg) {
	G_shuffleTeams();
	return qtrue;
}

void ClientCleanName( const char *in, char *out, int outSize );

qboolean G_shrubbot_rename(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], *newname, *oldname,err[MAX_STRING_CHARS];
	char userinfo[MAX_INFO_STRING];

	if(Q_SayArgc() < 3+skiparg) {
		SP("^drename usage: ^2!rename ^9[name] [newname]^7\n");
		return qfalse;
	}
	Q_SayArgv(1+skiparg, name, sizeof(name));
	newname = Q_SayConcatArgs(2+skiparg);
	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("rename: %s\n", err));
		return qfalse;
	}
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP(va("^drename: ^9sorry, %s ^9is a higher level admin than you are.^7\n",
			g_entities[pids[0]].client->pers.netname));
		return qfalse;
	}
	SP(va("^d!rename: ^9renaming ^7%s ^9to ^7%s\n",name,newname));
	trap_GetUserinfo( pids[0], userinfo, sizeof( userinfo ) );
	oldname = Info_ValueForKey( userinfo, "name" );
	// Send to chat for shame
	AP(va("chat \"^drename: ^*%s ^9has been renamed to ^7%s\"",
		oldname,
		newname));
	Info_SetValueForKey( userinfo, "name", newname);
	trap_SetUserinfo( pids[0], userinfo );
	// jaquboss
	ClientCleanName( newname, g_entities[pids[0]].client->pers.netname, sizeof(level.clients[level.sortedClients[pids[0]]].pers.netname) );
	ClientUserinfoChanged(pids[0]);

	return qtrue;
}

qboolean G_shrubbot_gib(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	gentity_t *vic;
	qboolean doAll = qfalse;

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^dgib: ^9not allowed during intermission.^7\n");
		return qfalse;
	}

	if(Q_SayArgc() < 2+skiparg) {
		doAll = qtrue;
	}

	Q_SayArgv(1+skiparg, name, sizeof(name));

	if( !Q_stricmp( name, "-1" ) || doAll ) {
		int it, count=0;

		for( it = 0; it < level.numConnectedClients; it++ ) {
			vic = g_entities + level.sortedClients[it];
			if( !_shrubbot_admin_higher( ent, vic ) ||
				!(vic->client->sess.sessionTeam == TEAM_AXIS ||
				  vic->client->sess.sessionTeam == TEAM_ALLIES))
				continue;
			G_Damage(vic, NULL, NULL, NULL, NULL, 500, 0, MOD_UNKNOWN);
			count++;
		}
		AP(va("chat \"^dgib: ^9%d players gibbed^7\"", count));
		return qtrue;
	}

	/*
	Q_SayArgv(1+skiparg, name, sizeof(name));
	*/
	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("gib: %s\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP("^dgib: ^9sorry, %s ^9is a higher level admin than you are.^7\n");
		return qfalse;
	}
	if(!(vic->client->sess.sessionTeam == TEAM_AXIS ||
			vic->client->sess.sessionTeam == TEAM_ALLIES)) {
		SP("^dgib: ^9player must be on a team to be gibbed^7\n");
		return qfalse;
	}

	G_Damage(vic, NULL, NULL, NULL, NULL, 500, 0, MOD_UNKNOWN);
	AP(va("chat \"^dgib: ^7%s ^9was gibbed^7\"", vic->client->pers.netname));
	return qtrue;
}

/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): Oz-style all-player gib, with NQ's
 * admin hierarchy plus the immutable flag respected for every target. */
qboolean G_shrubbot_giba(gentity_t *ent, int skiparg) {
	int i, count = 0;
	gentity_t *vic;
	if (level.intermissiontime || Q_SayArgc() != 1 + skiparg) {
		SP("^dgiba: ^9usage: !giba (not available in intermission)\n");
		return qfalse;
	}
	for (i = 0; i < level.numConnectedClients; ++i) {
		int healthBefore;
		vic = &g_entities[level.sortedClients[i]];
		if (!vic->inuse || !vic->takedamage || !vic->client ||
		    vic->client->pers.connected != CON_CONNECTED ||
		    (vic->client->ps.pm_flags & PMF_LIMBO) ||
		    (vic->client->sess.sessionTeam != TEAM_AXIS &&
		     vic->client->sess.sessionTeam != TEAM_ALLIES) ||
		    !_shrubbot_admin_higher(ent, vic) ||
		    (ent && ent != vic && G_shrubbot_permission(vic, SBF_IMMUTABLE)))
			continue;
		healthBefore = vic->health;
		G_Damage(vic, NULL, NULL, NULL, NULL, 500, 0, MOD_UNKNOWN);
		/* Report actual outcomes, not attempts rejected by G_Damage. */
		if (!vic->inuse || !vic->takedamage ||
		    (vic->health <= GIB_HEALTH && vic->health < healthBefore)) ++count;
	}
	AP(va("chat \"^dgiba: ^9%d players gibbed^7\"", count));
	return qtrue;
}

qboolean G_shrubbot_slap(gentity_t *ent, int skiparg) {
	int			pids[MAX_CLIENT_MATCHES], dmg; /* CHANGES: match sentinel */
	char		name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	char		damage[4], *reason;
	gentity_t	*vic;

	if(Q_SayArgc() < 2+skiparg) {
		SP("^dusage: ^2!slap ^8[name|slot#] [damage] [reason]^7\n");
		return qfalse;
	}
	Q_SayArgv(1+skiparg, name,	 sizeof(name));
	Q_SayArgv(2+skiparg, damage, sizeof(damage));

	dmg = atoi(damage);
	if(dmg <= 0) {
		dmg = 20;
		reason = Q_SayConcatArgs(2 + skiparg);
	}
	else {
		reason = Q_SayConcatArgs(3 + skiparg);
	}

	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("slap: %s\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP("^dslap: ^9sorry, but your intended victim has a higher admin"
			"^9 level than you do.^7\n");
		return qfalse;
	}
	if(!(vic->client->sess.sessionTeam == TEAM_AXIS || vic->client->sess.sessionTeam == TEAM_ALLIES)) {
		SP("^dslap: ^3denied ^9(target must be currently playing).^7\n");
		return qfalse;
	}
	if(vic->health < 1 || vic->client->ps.pm_flags & PMF_LIMBO) {
		SP(va("^dslap: ^3denied ^9(^*%s ^9is not alive).^7\n", vic->client->pers.netname));
		return qfalse;
	}

	if ( vic->health > dmg ) {
		vic->health -= dmg;
	}
	else {
		vic->health = 1;
	}

	// play the sound
	G_AddEvent(vic, EV_GENERAL_SOUND, GAMESOUND_MISC_WHACK);

	// announce to the world
	AP(va("chat \"^dslap: ^*%s ^9was slapped^7\"", vic->client->pers.netname));

	CPx(pids[0], va("cp \"%s ^9slapped you%s%s\"", (ent?ent->client->pers.netname:"^3SERVER CONSOLE"),
		(*reason) ? " ^9because:\n" : "", (*reason) ? reason : ""));
	return qtrue;
}

// don't allow to use these clientcmds to prevent total abuse
#ifdef __CLIENTCMD__
typedef struct clientCmd_s
{
	const char *name;
	float maxvalue; // -1 to not allow any change
	float minvalue;
} clientCmd_t;

static clientCmd_t protectedcmds[] =
{
	{"r_mode",				-1, -1},
	{"r_measureOverdraw",	-1, -1},
	{"r_showimages",		-1, -1}, // we could allow this so you can really fuck up someone's game...
	{"cg_shadows",			 1,  0}, // cos cg_shadows 2 bug
	{"disconnect",			-1, -1},
	{"connect",				-1, -1},
	{"quit",				-1, -1},
	{"cl_pitchspeed",		-1, -1},
	{"cl_autoupdate",		-1, -1},
	{"sensitivity",			-1, -1},
	{"cl_allowDownload",	-1, -1},
	{"cl_wwwDownload",		-1, -1},
	{"cl_doubletapdelay",	-1, -1},
	{"m_pitch",				-1, -1},
	{"m_yaw",				-1, -1},
	{"m_forward",			-1, -1},
	{"m_side",				-1, -1},
	{"cl_maxPing",			-1, -1},
	{"cl_anonymous",		-1, -1},
	{"cl_punkbuster",		-1, -1},
	{"cl_language",			-1, -1},
	{"r_picmip",			-1, -1},
	{"r_roundImagesDown",	-1, -1},
	{"r_rmse",				-1, -1},
	{"r_flares",			-1, -1},
	{"r_subdivisions",		-1, -1},
	{"r_fullscreen",		-1, -1},
	{"r_ignorehwgamma",		-1, -1},
	{"r_overBrightBits",	-1, -1},
	{"r_mapOverBrightBits",	-1, -1},
	{"r_gamma",				 2, 1.3},
	{"s_volume",			-1, -1}, // Meyer - don't allow at all
	{"s_musicvolume",		-1, -1}, // Meyer - don't allow at all
	{"r_texturebits",		-1, -1},
	{"r_colorbits",			-1, -1},
	{"r_stereo",			-1, -1},
	{"r_smp",				-1, -1},
	{"r_depthbits",			-1, -1},
	{"r_stencilbits",		-1, -1},
	{"r_shownormals",		-1, -1},
	{"say",					-1, -1},
	{"say_team",			-1, -1},
	{"say_buddy",			-1, -1},
	{"vsay",				-1, -1},
	{"vsay_team",			-1, -1},
	{"vsay_buddy",			-1, -1},
	{"com_maxFPS",			-1, -1}, // Meyer - don't let anyone change your FPS
	{"bind",				-1, -1},
	{"unbind",				-1, -1},
	{"cg_thirdperson",		-1, -1},
	{"vid_restart",			-1, -1},
#ifdef _DEBUG
	{"cg_gunx",				-1, -1},
	{"cg_guny",				-1, -1},
	{"cg_gunz",				-1, -1},
#endif
	{"cl_maxpackets",		100, 15},
	{"rate",				25000, 15000},
	{"cg_fov",				120, 90},
	{"r_customHeight"		-1, -1},
	{"r_customWidth"		-1, -1},
	{"ENDOFLIST",			-1, -1} // KEEP ME LAST!!!!!!!!!
};

qboolean G_shrubbot_clientcmd(gentity_t *ent, int skiparg) {
	int			pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char		name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS], cmd[MAX_NAME_LENGTH], arg[MAX_STRING_CHARS];
	gentity_t	*vic;
	int			i;

	if(Q_SayArgc() < 2+skiparg) {
		SP("^dclientcmd usage: ^2!clientcmd ^9[name|slot#] [cmd] [args]^7\n");
		return qfalse;
	}

	Q_SayArgv(1 + skiparg, name, sizeof(name));
	Q_SayArgv(2 + skiparg, cmd, sizeof(cmd));
	Q_SayArgv(3 + skiparg, arg, sizeof(arg));

	for ( i=0; protectedcmds[i].name; ++i ) {

		if ( !Q_stricmp(protectedcmds[i].name, "ENDOFLIST") )
			break;

		if ( !Q_stricmp(protectedcmds[i].name, cmd) ){
			if ( protectedcmds[i].maxvalue == -1 ){
				SP("^dclientcmd: this command is not allowed to issue^7\n");
				return qfalse;
			}
			if ( atof(arg) > protectedcmds[i].maxvalue ){
				SP("^dclientcmd: too big value issued for this command^7\n");
				return qfalse;

			}
			if ( atof(arg) < protectedcmds[i].minvalue ){
				SP("^dclientcmd: too small value issued for this command^7\n");
				return qfalse;
			}
		break;
		}

	}

	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("clientcmd: %s\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];

		if( _shrubbot_admin_higher( ent, vic ))
	trap_SendServerCommand(vic-g_entities, va("clientcmd \"%s\" \"%s\"", cmd, arg));

	return qtrue;
}
#endif

//G_shrubbot_rocket
// Meyer - The concept was to shoot a rocket from the player.
// core: We always fire 1 rocket now.. and it seems that it only works for the one calling this command.
qboolean G_shrubbot_rocket(gentity_t *ent, int skiparg) {
	vec3_t		dir;
	int			backupWeapon;
	vec3_t		origin;

	if ( !ent || !ent->client ) {
		return qfalse; // jaquboss
	}

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^drocket: ^9not allowed during intermission.^7\n");
		return qfalse;
	}

	AngleVectors( ent->client->ps.viewangles, dir, NULL, NULL );
	VectorCopy(ent->client->ps.origin,origin);
	origin[2] += ent->client->ps.viewheight;

	backupWeapon = ent->s.weapon;
	ent->s.weapon = WP_PANZERFAUST;

	fire_rocket(ent, origin, dir, WP_PANZERFAUST);

	ent->s.weapon = backupWeapon;

	return qtrue;
}

qboolean ReviveEntity(gentity_t *ent, gentity_t *traceEnt, qboolean resuscitation);

qboolean G_shrubbot_revive(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	char *reason;
	gentity_t *vic;

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^drevive: ^9not allowed during intermission.^7\n");
		return qfalse;
	}

	if(Q_SayArgc() < 2+skiparg) {
		SP("^drevive usage: ^2!revive ^9[name|slot#] [reason]^7\n");
		return qfalse;
	}

	Q_SayArgv(1+skiparg, name, sizeof(name));
	reason = Q_SayConcatArgs(2+skiparg);

	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("revive: %s\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];

	if(!(vic->client->sess.sessionTeam == TEAM_AXIS || vic->client->sess.sessionTeam == TEAM_ALLIES)) {
		SP("^drevive: ^9player must be on a team^7\n");
		return qfalse;
	}

	if(vic->health > 0 || vic->client->ps.pm_flags & PMF_LIMBO) {
		SP(va("^drevive: ^7%s ^9is not dead!^7\n", vic->client->pers.netname));
		return qfalse;
	}

	ReviveEntity(ent? ent : NULL, vic, qfalse);

	AP(va("chat \"^drevive: ^*%s ^9was revived^7\"", vic->client->pers.netname));


	CPx(pids[0], va("cp \"%s ^9revived you^7%s%s\"",
			(ent?ent->client->pers.netname:"^3SERVER CONSOLE"),
			(*reason) ? "^9 because:\n" : "",
			(*reason) ? reason : ""));

	return qtrue;
}

qboolean G_shrubbot_disguise(gentity_t *ent, int skiparg) {
	int			pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char		name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	gentity_t	*vic;

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^ddisguise: ^9not allowed during intermission.^7\n");
		return qfalse;
	}

	if(Q_SayArgc() < 3+skiparg) {
		SP("^ddisguise usage: ^2!disguise ^9[name|slot#] [class]^7\n");
		return qfalse;
	}

	Q_SayArgv(1+skiparg, name, sizeof(name));

	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("disguise: %s\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];

	if(!(vic->client->sess.sessionTeam == TEAM_AXIS || vic->client->sess.sessionTeam == TEAM_ALLIES)) {
		SP("^ddisguise: ^9player must be on a team!^7\n");
		return qfalse;
	}

	if (ent && ent->client && ent->client->ps.clientNum == vic->client->ps.clientNum) {
		AP(va( "chat \"^ddisguise: ^7%s ^9This admin cheats! ^7\"", vic->client->pers.netname));
	}
	else {
		AP(va( "chat \"^ddisguise: ^7%s ^9was disguised by %s^7\"", vic->client->pers.netname, (ent?ent->client->pers.netname:"^3SERVER CONSOLE") ));
	}

	Cmd_GiveDisguise_f(vic, 1+skiparg);

	return qtrue;
}

qboolean G_shrubbot_poison(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	char *reason;
	gentity_t *vic;
	qboolean doAll = qfalse;

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^dpoison: ^9not allowed during intermission.^7\n");
		return qfalse;
	}

	if(Q_SayArgc() < 2+skiparg) {
		doAll = qtrue;
	}

	Q_SayArgv(1+skiparg, name, sizeof(name));
	reason = Q_SayConcatArgs(2+skiparg);

	if( !Q_stricmp( name, "-1" ) || doAll ) {
		int it, count=0;

		for( it = 0; it < level.numConnectedClients; it++ ) {

			vic = g_entities + level.sortedClients[it];
			if( !_shrubbot_admin_higher( ent, vic ) || !(vic->client->sess.sessionTeam == TEAM_AXIS ||
				  vic->client->sess.sessionTeam == TEAM_ALLIES)) {
				continue;
			}
			vic->client->ps.eFlags |= EF_POISONED;
			// core: the poisoner is the one issuing the poison-command..
			// If the console is the poisoner, the message will say: tasted his own poison.
			vic->client->pmext.poisonerEnt = (ent)? ent->s.number : vic->client->ps.clientNum;
			vic->client->pmext.poisonAmnt = 1;
			// IlDuca - fix : set poisonTime
			vic->client->poisonTime = level.time;

			TossClientItems(vic);

			count++;
		}
		AP(va("chat \"^dpoison: ^9%d players poisoned^7\"", count));
		return qtrue;
	}


	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("poison: %s\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP("^dpoison: ^9sorry, but your intended victim has a higher admin level than you do.^7\n");
		return qfalse;
	}
	if(!(vic->client->sess.sessionTeam == TEAM_AXIS || vic->client->sess.sessionTeam == TEAM_ALLIES)) {
		SP("^dpoison: ^9player must be on a team^7\n");
		return qfalse;
	}

	vic->client->ps.eFlags |= EF_POISONED;
	// core: the poisoner is the one issuing the poison-command..
	// If the console is the poisoner, the message will say: tasted his own poison.
	vic->client->pmext.poisonerEnt = (ent)? ent->s.number : vic->client->ps.clientNum;
	vic->client->pmext.poisonAmnt = 1;
	// IlDuca - fix : set poisonTime
	vic->client->poisonTime = level.time;

	TossClientItems(vic);

	AP(va("chat \"^dpoison: ^7%s ^9was poisoned^7\"", vic->client->pers.netname));

	CPx(pids[0], va("cp \"%s ^9poisoned you^7%s%s\"",
			((ent && ent->client)?ent->client->pers.netname:"^3SERVER CONSOLE"),
			(*reason) ? "^9 because:\n" : "",
			(*reason) ? reason : ""));

	return qtrue;
}

// annoying players cmd
qboolean G_shrubbot_dropweapon(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	char *reason;
	gentity_t *vic;
	qboolean doAll = qfalse;

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^ddw: ^9not allowed during intermission.^7\n");
		return qfalse;
	}

	if(Q_SayArgc() < 2+skiparg) {
		doAll = qtrue;
	}

	Q_SayArgv(1+skiparg, name, sizeof(name));
	reason = Q_SayConcatArgs(2+skiparg);

	if( !Q_stricmp( name, "-1" ) || doAll ) {
		int it, count=0;
		for( it = 0; it < level.numConnectedClients; it++ ) {

			vic = g_entities + level.sortedClients[it];
			if( !_shrubbot_admin_higher( ent, vic ) ||
				!(vic->client->sess.sessionTeam == TEAM_AXIS ||
				  vic->client->sess.sessionTeam == TEAM_ALLIES))
				continue;

			TossClientItems(vic);

			count++;
		}
		AP(va("chat \"^ddw: ^9%d players drop their weapons^7\"", count));
		return qtrue;
	}

	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("dw: %s\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP("^ddw: ^9sorry, but your intended victim has a higher admin level than you do.^7\n");
		return qfalse;
	}
	if(!(vic->client->sess.sessionTeam == TEAM_AXIS || vic->client->sess.sessionTeam == TEAM_ALLIES)) {
		SP("^ddw: ^9player must be on a team^7\n");
		return qfalse;
	}

	TossClientItems(vic);

	AP(va("chat \"^ddw: ^7%s ^9was forced to drop weapons^7\"", vic->client->pers.netname));

	CPx(pids[0], va("cp \"%s ^9forced you to drop weapons^7%s%s\"",
			(ent?ent->client->pers.netname:"^3SERVER CONSOLE"),
			(*reason) ? "^9 because:\n" : "",
			(*reason) ? reason : ""));

	return qtrue;
}

qboolean G_shrubbot_glow(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	char *reason;
	gentity_t *vic;
	qboolean doAll = qfalse;

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^dglow: ^9not allowed during intermission.^7\n");
		return qfalse;
	}

	if(Q_SayArgc() < 2+skiparg) {
		doAll = qtrue;
	}

	Q_SayArgv(1+skiparg, name, sizeof(name));
	reason = Q_SayConcatArgs(2+skiparg);

	if( !Q_stricmp( name, "-1" ) || doAll ) {
		int it, count=0;
		for( it = 0; it < level.numConnectedClients; it++ ) {
			vic = g_entities + level.sortedClients[it];
			if( !_shrubbot_admin_higher( ent, vic ) ||
				!(vic->client->sess.sessionTeam == TEAM_AXIS ||
				  vic->client->sess.sessionTeam == TEAM_ALLIES))
				continue;
				vic->client->ps.eFlags |= EF_GLOW; // jaquboss fixme, what a waste
			count++;
		}
		AP(va("chat \"^dglow: ^9%d players are now glowing^7\"", count));
		return qtrue;
	}

	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("^dglow: ^7%s^7\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP("^dglow: ^9sorry, but your intended victim has a higher admin level than you do.^7\n");
		return qfalse;
	}
	if(!(vic->client->sess.sessionTeam == TEAM_AXIS ||
			vic->client->sess.sessionTeam == TEAM_ALLIES)) {
		SP("^dglow: ^9player must be on a team^7\n");
		return qfalse;
	}

	vic->client->ps.eFlags ^= EF_GLOW;

	if ( vic->client->ps.eFlags & EF_GLOW)
	AP(va("chat \"^dglow: ^7%s ^9is now glowing^7\"",vic->client->pers.netname));
	else
	AP(va("chat \"^dglow: ^7%s ^9stopped glowing^7\"",vic->client->pers.netname));

	CPx(pids[0], va("cp \"^7%s ^9changed your glow status^7%s%s\"",
		(ent?ent->client->pers.netname:"^3SERVER CONSOLE"),
			(*reason) ? "^9 because:\n" : "",
			(*reason) ? reason : ""));
	return qtrue;
}

qboolean G_shrubbot_pants(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	char *reason;
	gentity_t *vic;
	qboolean doAll = qfalse;

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^dpants: ^9not allowed during intermission.^7\n");
		return qfalse;
	}

	if(Q_SayArgc() < 2+skiparg) {
		doAll = qtrue;
	}

	Q_SayArgv(1+skiparg, name, sizeof(name));
	reason = Q_SayConcatArgs(2+skiparg);

	if( !Q_stricmp( name, "-1" ) || doAll ) {
		int it, count=0;
		for( it = 0; it < level.numConnectedClients; it++ ) {
			vic = g_entities + level.sortedClients[it];
			if( !_shrubbot_admin_higher( ent, vic ) ||
				!(vic->client->sess.sessionTeam == TEAM_AXIS ||
				  vic->client->sess.sessionTeam == TEAM_ALLIES))
				continue;
				vic->client->ps.eFlags |= EF_PANTSED;
			count++;
		}
		AP(va("chat \"^dpants: ^9%d players pantsed^7\"", count));
		return qtrue;
	}

	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("^dpants: ^7%s^7\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP("^dpants: ^9sorry, but your intended victim has a higher admin^"
			" level than you do.^7\n");
		return qfalse;
	}
	if(!(vic->client->sess.sessionTeam == TEAM_AXIS ||
			vic->client->sess.sessionTeam == TEAM_ALLIES)) {
		SP("^dpants: ^9player must be on a team^7\n");
		return qfalse;
	}

	vic->client->ps.eFlags |= EF_PANTSED;

	AP(va("chat \"^dpants: ^7%s ^9was pantsed^7\"",
			vic->client->pers.netname));

	CPx(pids[0], va("cp \"^7%s ^9pantsed you^7%s%s\"",
			(ent?ent->client->pers.netname:"^3SERVER CONSOLE"),
			(*reason) ? "^9 because:\n" : "",
			(*reason) ? reason : ""));
	return qtrue;
}

qboolean G_shrubbot_freeze(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	char *reason;
	gentity_t *vic;
	qboolean doAll = qfalse;

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^dfreeze: ^9not allowed during intermission.^7\n");
		return qfalse;
	}

	if(Q_SayArgc() < 2+skiparg) {
		doAll = qtrue;
	}

	Q_SayArgv(1+skiparg, name, sizeof(name));
	reason = Q_SayConcatArgs(2+skiparg);

	if( !Q_stricmp( name, "-1" ) || doAll ) {
		int it, count=0;
		for( it = 0; it < level.numConnectedClients; it++ ) {
			vic = g_entities + level.sortedClients[it];
			if( !_shrubbot_admin_higher( ent, vic ) ||
				!(vic->client->sess.sessionTeam == TEAM_AXIS ||
				  vic->client->sess.sessionTeam == TEAM_ALLIES))
				continue;
				vic->client->freezed = qtrue;
				vic->takedamage = qfalse;
			count++;
		}
		AP(va("chat \"^dfreeze: ^9%d players are frozen^7\"", count));
		return qtrue;
	}

	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("^dfreeze: ^7%s^7\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP("^dfreeze: ^9sorry, but your intended victim has a higher admin^"
			" level than you do.^7\n");
		return qfalse;
	}
	if(!(vic->client->sess.sessionTeam == TEAM_AXIS ||
			vic->client->sess.sessionTeam == TEAM_ALLIES)) {
		SP("^dfreeze: ^9player must be on a team^7\n");
		return qfalse;
	}

	vic->client->freezed = qtrue;
	vic->takedamage = qfalse;

	AP(va("chat \"^dfreeze: ^7%s ^9is frozen^7\"",
			vic->client->pers.netname));

	CPx(pids[0], va("cp \"^7%s ^9froze you^7%s%s\"",
			(ent?ent->client->pers.netname:"^3SERVER CONSOLE"),
			(*reason) ? "^9 because:\n" : "",
			(*reason) ? reason : ""));
	return qtrue;
}

qboolean G_shrubbot_unfreeze(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	char *reason;
	gentity_t *vic;
	qboolean doAll = qfalse;

	if(Q_SayArgc() < 2+skiparg)
		doAll = qtrue;

	Q_SayArgv(1+skiparg, name, sizeof(name));
	reason = Q_SayConcatArgs(2+skiparg);

	if( !Q_stricmp( name, "-1" ) || doAll ) {
		int it, count=0;
		for( it = 0; it < level.numConnectedClients; it++ ) {
			vic = g_entities + level.sortedClients[it];
			if( !_shrubbot_admin_higher( ent, vic ) ||
				!(vic->client->sess.sessionTeam == TEAM_AXIS ||
				  vic->client->sess.sessionTeam == TEAM_ALLIES))
				continue;
				vic->client->freezed = qfalse;
				vic->takedamage = qtrue;
			count++;
		}
		AP(va("chat \"^dunfreeze: ^9%d players are unfrozen^7\"", count));
		return qtrue;
	}

	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("^dunfreeze: ^7%s^7\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP("^dunfreeze: ^9sorry, but your intended victim has a higher admin^"
			" level than you do.^7\n");
		return qfalse;
	}
	if(!(vic->client->sess.sessionTeam == TEAM_AXIS ||
			vic->client->sess.sessionTeam == TEAM_ALLIES)) {
		SP("^dunfreeze: ^9player must be on a team^7\n");
		return qfalse;
	}

	vic->client->freezed = qfalse;
	vic->takedamage = qtrue;

	AP(va("chat \"^dunfreeze: ^7%s ^9is unfrozen^7\"",
			vic->client->pers.netname));

	CPx(pids[0], va("cp \"^7%s ^9unfroze you^7%s%s\"",
			(ent?ent->client->pers.netname:"^3SERVER CONSOLE"),
			(*reason) ? "^9 because:\n" : "",
			(*reason) ? reason : ""));
	return qtrue;
}

qboolean G_shrubbot_burn(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	char *reason;
	gentity_t *vic;
	qboolean doAll = qfalse;

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^dburn: ^9not allowed during intermission.^7\n");
		return qfalse;
	}

	if(Q_SayArgc() < 2+skiparg) {
		doAll = qtrue;
	}

	Q_SayArgv(1+skiparg, name, sizeof(name));
	reason = Q_SayConcatArgs(2+skiparg);

	if( !Q_stricmp( name, "-1" ) || doAll ) {
		int it, count=0;
		for( it = 0; it < level.numConnectedClients; it++ ) {
			vic = g_entities + level.sortedClients[it];
			if( !_shrubbot_admin_higher( ent, vic ) ||
				!(vic->client->sess.sessionTeam == TEAM_AXIS ||
				  vic->client->sess.sessionTeam == TEAM_ALLIES))
				continue;
				G_BurnMeGood(vic, vic, NULL);
			count++;
		}
		AP(va("chat \"^dburn: ^9%d players burned^7\"", count));
		return qtrue;
	}

	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("^dburn: ^7%s^7\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP("^burn: ^9sorry, but your intended victim has a higher admin^"
			" level than you do.^7\n");
		return qfalse;
	}
	if(!(vic->client->sess.sessionTeam == TEAM_AXIS ||
			vic->client->sess.sessionTeam == TEAM_ALLIES)) {
		SP("^dburn: ^9player must be on a team^7\n");
		return qfalse;
	}

	G_BurnMeGood(vic, vic, NULL);
	AP(va("chat \"^dburn: ^7%s ^9was set ablaze^7\"",
			vic->client->pers.netname));

	CPx(pids[0], va("cp \"^7%s ^9burned you^7%s%s\"",
			(ent?ent->client->pers.netname:"^3SERVER CONSOLE"),
			(*reason) ? "^9 because:\n" : "",
			(*reason) ? reason : ""));
	return qtrue;
}

qboolean G_shrubbot_warn(gentity_t *ent, int skiparg) {
	/* NewMod, Arena.ai: check fine-grained warn permission first */
	if (ent && !NM_FineGrainedAllowed(ent, SBF_CAN_WARN)) {
		SP("^dwarn: ^9you do not have permission to warn\n");
		return qfalse;
	}
	/* NewMod, Arena.ai, 2026-11-21: preserve this entry point while using
	 * sanitized NQ notifications and bounded connection-local Oz-style
	 * warning history. It is not Oz's persistent warning database. */
	return G_nm_warn(ent, skiparg);
}

qboolean G_shrubbot_give(gentity_t *ent, int skiparg) {
	int			pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char		name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	char		*item;
	char		*amt;
	char		*amt2;
	gentity_t	*vic;

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^dgive: ^9not allowed during intermission.^7\n");
		return qfalse;
	}

	if(Q_SayArgc() < 2+skiparg) {
		SP("^dgive usage: ^2!give ^9[name|slot#] [item] [args]^7\n");
		return qfalse;
	}

	Q_SayArgv(1 + skiparg, name, sizeof(name));
	item = Q_SayConcatArgs(2 + skiparg);

	amt = Q_SayConcatArgs(3 + skiparg);
 	amt2 = Q_SayConcatArgs(4 + skiparg);

	if(ClientNumbersFromString(name, pids) != 1){
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("give: %s\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];

	if(!(vic->client->sess.sessionTeam == TEAM_AXIS || vic->client->sess.sessionTeam == TEAM_ALLIES)) {
		SP(va("^dgive: ^3denied ^9(^7%s ^9is not in game)^7\n", vic->client->pers.netname));
		return qfalse;
	}

	if ( ExecGive( vic, item, amt?amt:(char*)"", amt2?amt2:(char*)"") ) {
		if (ent && ent->client && ent->client->ps.clientNum == vic->client->ps.clientNum) {
			AP(va( "chat \"^dgive: ^7%s ^7%s^9 This admin cheats! ^7\"", vic->client->pers.netname, item ));
		}
		else {
			AP(va( "chat \"^dgive: ^7%s ^9received ^7%s ^9from ^7%s^7\"", vic->client->pers.netname, item, (ent?ent->client->pers.netname:"^3SERVER CONSOLE") ));
		}
	}

	return qtrue;
}

/* IRATA: removed ... way to annoying + fills bufferered sound scripts, global channel is blocked
qboolean G_shrubbot_news(gentity_t *ent, int skiparg) {
	char mapname[MAX_STRING_CHARS];

	if(Q_SayArgc() < 2+skiparg) {
		Q_strncpyz(mapname, level.rawmapname,
				sizeof(mapname));
	}
	else {
		Q_SayArgv(1+skiparg, mapname, sizeof(mapname));
	}

	G_globalSound(va("sound/vo/%s/news_%s.wav",mapname,mapname));

	return qtrue;
}
*/

qboolean G_shrubbot_lock(gentity_t *ent, int skiparg) {
	return G_shrubbot_lockteams(ent, skiparg, qtrue);
}

qboolean G_shrubbot_unlock(gentity_t *ent, int skiparg) {
	return G_shrubbot_lockteams(ent, skiparg, qfalse);
}

qboolean G_shrubbot_lockteams(gentity_t *ent, int skiparg, qboolean toLock) {
	char team[4];
	char command[MAX_SHRUBBOT_CMD_LEN], *cmd;

	Q_SayArgv(skiparg, command, sizeof(command));
	cmd = command;
	if(*cmd == '!')
		*cmd++;

	if(Q_SayArgc() < 2+skiparg) {
		SP(va("^d%s usage: ^2!%s ^9r|b|s|all^7\n",cmd,cmd));
		return qfalse;
	}
	Q_SayArgv(1+skiparg, team, sizeof(team));

	if ( !Q_stricmp(team, "all") ) {
		teamInfo[TEAM_AXIS].team_lock =
			(TeamCount(-1, TEAM_AXIS)) ? toLock : qfalse;
		teamInfo[TEAM_ALLIES].team_lock =
			(TeamCount(-1, TEAM_ALLIES)) ? toLock : qfalse;
		G_updateSpecLock(TEAM_AXIS,
			(TeamCount(-1, TEAM_AXIS)) ? toLock : qfalse);
		G_updateSpecLock(TEAM_ALLIES,
			(TeamCount(-1, TEAM_ALLIES)) ? toLock : qfalse);
		if( toLock ) {
			level.server_settings |= CV_SVS_LOCKSPECS;
		}
		else {
			level.server_settings &= ~CV_SVS_LOCKSPECS;
		}
		AP(va("chat \"^d%s: ^9All teams %sed^7\" -1", cmd, cmd));
	}
	else if ( !Q_stricmp(team, "r") ) {
		teamInfo[TEAM_AXIS].team_lock =
			(TeamCount(-1, TEAM_AXIS)) ? toLock : qfalse;
		AP(va("chat \"^d%s: ^9Axis team %sed^7\" -1", cmd, cmd));
	}
	else if ( !Q_stricmp(team, "b") ) {
		teamInfo[TEAM_ALLIES].team_lock =
			(TeamCount(-1, TEAM_ALLIES)) ? toLock : qfalse;
		AP(va("chat \"^d%s: ^9Allied team %sed^7\" -1", cmd, cmd));
	}
	else if ( !Q_stricmp(team, "s") ) {
		G_updateSpecLock(TEAM_AXIS,
			(TeamCount(-1, TEAM_AXIS)) ? toLock : qfalse);
		G_updateSpecLock(TEAM_ALLIES,
			(TeamCount(-1, TEAM_ALLIES)) ? toLock : qfalse);
		if( toLock ) {
			level.server_settings |= CV_SVS_LOCKSPECS;
		}
		else {
			level.server_settings &= ~CV_SVS_LOCKSPECS;
		}
		AP(va("chat \"^d%s: ^9Spectators %sed^7\" -1", cmd, cmd));
	} else {
		SP(va("^d%s usage: ^2!%s ^9r|b|s|all^7\n",cmd,cmd));
		return qfalse;
	}

	if( toLock ) {
		level.server_settings |= CV_SVS_LOCKTEAMS;
	} else {
		level.server_settings &= ~CV_SVS_LOCKTEAMS;
	}
	trap_SetConfigstring(CS_SERVERTOGGLES,
			va("%d", level.server_settings));

	return qtrue;
}

// created by: dvl
qboolean G_shrubbot_lol(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS], numNades[4];
	gentity_t *vic;
	qboolean doAll = qfalse;
	int pcount, nades = 0;

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^dnade: ^9not allowed during intermission.^7\n");
		return qfalse;
	}

	// IlDuca - patheticcockroach's fix for !nade overflow bug
	if( level.time - lol_flood_control < 600) {
		SP("^dnade: ^9denied by entities overflow protection.^7\n");
		return qfalse;
	}

	lol_flood_control = level.time;

	if(Q_SayArgc() < 2+skiparg)
		doAll = qtrue;
	else if(Q_SayArgc() >= 3+skiparg) {
		Q_SayArgv( 2+skiparg, numNades, sizeof( numNades ) );
		nades = atoi( numNades );
		if( nades < 1 )
			nades = 1;
		else if( nades > SHRUBBOT_MAX_LOL_NADES )
			nades = SHRUBBOT_MAX_LOL_NADES;
	}
	Q_SayArgv( 1+skiparg, name, sizeof( name ) );
	if( !Q_stricmp( name, "-1" ) || doAll ) {
		int it, count=0;

		// IlDuca - patheticcockroach's fix for !nade overflow bug
		if( G_GentitiesAvailable() - level.numConnectedClients * nades * 2 < MIN_SPARE_GENTITIES ) {
			SP("^dnade: ^9too many entities, use fewer nades or nade fewer players.^7\n");
			return qfalse;
		}

		for( it = 0; it < level.numConnectedClients; it++ ) {
			vic = g_entities + level.sortedClients[it];
			if( !_shrubbot_admin_higher(ent, vic) ||
				!(vic->client->sess.sessionTeam == TEAM_AXIS ||
				  vic->client->sess.sessionTeam == TEAM_ALLIES))
				continue;
			if( nades )
				G_createClusterNade( vic, 2 );//nades );
			else
				G_createClusterNade( vic, 1 );
			count++;
		}
		AP(va("chat \"^dnade: ^9%d players naded^7\"", count));
		return qtrue;
	}

	pcount = ClientNumbersFromString(name, pids);

	if(pcount > 1) {

		int it;

		// IlDuca - patheticcockroach's fix for !nade overflow bug
		if( G_GentitiesAvailable() - pcount * nades * 3 < MIN_SPARE_GENTITIES ) {
			SP("^dnade: ^9too many entities, use fewer nades or nade fewer players.^7\n");
			return qfalse;
		}

		for( it = 0; it < pcount; it++) {
			vic = &g_entities[pids[it]];
			if(!_shrubbot_admin_higher(ent, vic)) {
				SP(va("^dnade: ^9sorry, but ^7%s^9 has a higher "
					"admin level than you do.^7\n",
					vic->client->pers.netname));
				continue;
			}
			else if(!(vic->client->sess.sessionTeam == TEAM_AXIS ||
					vic->client->sess.sessionTeam == TEAM_ALLIES)) {
				SP(va("^dnade: ^7%s^9 must be on a "
						"team to be naded^7\n",
						vic->client->pers.netname));
				continue;
			}

			if( nades )
				G_createClusterNade( vic, nades );
			else
				G_createClusterNade( vic, 8 );
			AP(va("chat \"^dnade: ^7%s\"",
					vic->client->pers.netname));
		}
		return qtrue;
	}
	else if( pcount < 1 ) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("^dnade: ^9%s^7\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP("^dnade: ^9sorry, but your intended victim has a higher "
			"admin level than you do.^7\n");
		return qfalse;
	}

	if( nades )
		G_createClusterNade( vic, 2);//nades );
	else
		G_createClusterNade( vic, 1 );
	AP(va("chat \"^dnade: ^7%s^7\"", vic->client->pers.netname));
	return qtrue;

}

// Created by: dvl
qboolean G_shrubbot_pop( gentity_t *ent, int skiparg ) {
	vec3_t dir = { 5, 5, 5 };
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	gentity_t *vic;
	qboolean doAll = qfalse;
	int pcount;

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^dpop: ^9not allowed during intermission.^7\n");
		return qfalse;
	}

	if(Q_SayArgc() < 2+skiparg) {
		doAll = qtrue;
	}

	Q_SayArgv( 1+skiparg, name, sizeof( name ) );
	if( !Q_stricmp( name, "-1" ) || doAll ) {
		int it, count=0;

		for( it = 0; it < level.numConnectedClients; ++it ) {
			vic = g_entities + level.sortedClients[it];
			if( !_shrubbot_admin_higher( ent, vic ) ||
				!(vic->client->sess.sessionTeam == TEAM_AXIS ||
				  vic->client->sess.sessionTeam == TEAM_ALLIES) ||
				 (vic->client->ps.eFlags & EF_HEADSHOT))
				continue;
			// tjw: actually take of the players helmet instead of
			//      giving all players an unlimited supply of
			//      helmets.
			vic->client->ps.eFlags |= EF_HEADSHOT;
			G_AddEvent( vic, EV_LOSE_HAT, DirToByte(dir) );
			count++;
		}
		AP(va("chat \"^dpop: ^9%d players popped\"", count));
		return qtrue;
	}
	pcount = ClientNumbersFromString(name, pids);
	if(pcount > 1) {
		int it;
		for( it = 0; it < pcount; it++) {
			vic = &g_entities[pids[it]];
			if(!_shrubbot_admin_higher(ent, vic)) {
				SP( va("^dpop: ^9sorry, but ^7%s^9 has a higher admin"
					" level than you do.^7\n",
					vic->client->pers.netname));
				continue;
			}
			if(!(vic->client->sess.sessionTeam == TEAM_AXIS ||
		 		vic->client->sess.sessionTeam == TEAM_ALLIES)) {
				SP(va("^dpop: ^7%s^9 must be on a team"
					"^9to be popped^7\n",
					vic->client->pers.netname));
				continue;
			}
			G_AddEvent( vic, EV_LOSE_HAT, DirToByte(dir) );
			AP(va("chat \"^dpop: ^7%s ^9lost his hat^7\"",
				vic->client->pers.netname));
		}
		return qtrue;
	}
	else if(pcount < 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("^dpop: ^7%s^7\n", err));
		return qfalse;
	}

	vic = &g_entities[pids[0]];
	if(!_shrubbot_admin_higher(ent, vic)) {
		SP( "^dpop: ^9sorry, but your intended victim has a higher "
			"^9admin level than you do.^7\n" );
		return qfalse;
	}
	if(!(vic->client->sess.sessionTeam == TEAM_AXIS ||
		 vic->client->sess.sessionTeam == TEAM_ALLIES)) {
		SP("^dpop: ^9player must be on a team to be popped^7\n");
		return qfalse;
	}

	G_AddEvent( vic, EV_LOSE_HAT, DirToByte(dir) );
	AP(va("chat \"^dpop: ^7%s ^9lost his hat^7\" -1",
			vic->client->pers.netname));
	return qtrue;
}

// Created by: dvl
qboolean G_shrubbot_pip( gentity_t *ent, int skiparg ) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	gentity_t *vic;
	qboolean doAll = qfalse;
	int pcount;

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^dpip: ^9not allowed during intermission.^7\n");
		return qfalse;
	}

	if(Q_SayArgc() < 2+skiparg) {
		doAll = qtrue;
	}

	Q_SayArgv( 1+skiparg, name, sizeof( name ) );
	if( !Q_stricmp( name, "-1" ) || doAll ) {
		int it, count=0;

		for( it = 0; it < level.numConnectedClients; it++ ) {
			vic = g_entities + level.sortedClients[it];
			if( !_shrubbot_admin_higher( ent, vic ) ||
				!(vic->client->sess.sessionTeam == TEAM_AXIS ||
				  vic->client->sess.sessionTeam == TEAM_ALLIES))
				continue;
			G_MakePip( vic );
			count++;
		}
		AP(va("chat \"^dpip: ^9%d players pipped^7\"", count));
		return qtrue;
	}
	pcount = ClientNumbersFromString(name, pids);
	if(pcount > 1) {
		int it;

		for( it = 0; it < pcount; it++) {
			vic = &g_entities[pids[it]];
			if(!_shrubbot_admin_higher(ent, vic)) {
				SP( va("^dpip: ^9sorry, but ^7%s^9 has a higher admin"
					"level than you do.\n",
					vic->client->pers.netname));
				continue;
			}
			if(!(vic->client->sess.sessionTeam == TEAM_AXIS ||
		 		vic->client->sess.sessionTeam == TEAM_ALLIES)) {
				SP(va("^dpip: ^7%s^9 must be on a team"
					" to be pipped^7\n",
					vic->client->pers.netname));
				continue;
			}
			G_MakePip( vic );
			AP(va("chat \"^dpip: ^7%s ^9was pipped^7\"",
					vic->client->pers.netname));
		}
		return qtrue;
	}
	else if(pcount < 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("^dpip: ^7%s\n", err));
		return qfalse;
	}

	vic = &g_entities[pids[0]];
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP("^dpip: ^9sorry, but your intended victim has a higher"
			" admin level than you do.^7\n");
		return qfalse;
	}
	if(!(vic->client->sess.sessionTeam == TEAM_AXIS ||
			vic->client->sess.sessionTeam == TEAM_ALLIES)) {
		SP("^dpip: ^9player must be on a team to be pipped\n");
		return qfalse;
	}

	G_MakePip( vic );
	AP(va("chat \"^dpip: ^7%s ^9was pipped^7\"", vic->client->pers.netname));
	return qtrue;
}

qboolean G_shrubbot_reset(gentity_t *ent, int skiparg) {
	char command[MAX_SHRUBBOT_CMD_LEN];

	Q_SayArgv(skiparg, command, sizeof(command));
	if(Q_stricmp(command, "reset")) {
		Svcmd_ResetMatch_f(qtrue, qtrue);
	}
	else {
		Svcmd_ResetMatch_f(qfalse, qtrue);
	}
	return qtrue;
}

qboolean G_shrubbot_swap(gentity_t *ent, int skiparg) {
	Svcmd_SwapTeams_f();
	AP("chat \"^dswap: ^9Teams were swapped^7\"");
	return qtrue;
}

qboolean G_shrubbot_fling(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	char fling[9], pastTense[9];
	char *flingCmd;
	gentity_t *vic;
	int flingType=-1; // 0 = fling, 1 = throw, 2 = launch
	int i, count = 0, pcount;
	qboolean doAll = qfalse;
	qboolean allAlias;

	Q_SayArgv(0+skiparg, fling, sizeof(fling));
	if(fling[0] == '!') {
		flingCmd = &fling[1];
	}
	else {
		flingCmd = &fling[0];
	}

	/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): the Oz/Jaymod *a variants mean all. */
	allAlias = !Q_stricmp(flingCmd, "throwa") || !Q_stricmp(flingCmd, "launcha") ||
		!Q_stricmp(flingCmd, "flinga");
	if(!Q_stricmp(flingCmd, "throw") || !Q_stricmp(flingCmd, "throwa")) {
		flingType = 1;
		Q_strncpyz(pastTense, "thrown", sizeof(pastTense));
	}
	else if (!Q_stricmp(flingCmd, "launch") || !Q_stricmp(flingCmd, "launcha")) {
		flingType = 2;
		Q_strncpyz(pastTense, "launched", sizeof(pastTense));
	}
	else if (!Q_stricmp(flingCmd, "fling") || !Q_stricmp(flingCmd, "flinga")) {
		flingType = 0;
		Q_strncpyz(pastTense, "flung", sizeof(pastTense));
	}
	else {
		return qfalse;
	}
	if (allAlias && Q_SayArgc() != 1 + skiparg) {
		SP(va("^d%s: ^9this command takes no arguments\n", flingCmd));
		return qfalse;
	}

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP(va("^d%s: ^9not allowed during intermission.^7\n", flingCmd));
		return qfalse;
	}

	if( allAlias || Q_SayArgc() < 2+skiparg) {
		doAll = qtrue;
	}

	Q_SayArgv(1+skiparg, name, sizeof(name));

	if( doAll ) {
		for( i = 0; i < level.numConnectedClients; i++ ) {
			vic = g_entities + level.sortedClients[i];
			if( !_shrubbot_admin_higher( ent, vic ) ||
			    (ent && ent != vic && G_shrubbot_permission(vic, SBF_IMMUTABLE)) )
				continue;
			count += G_FlingClient( vic, flingType );
		}
		AP(va("chat \"^d%s: ^9%d players %s^7\"",
					flingCmd, count, pastTense));
		return qtrue;
	}
	pcount = ClientNumbersFromString(name, pids);
	if(pcount > 1) {
		for( i = 0; i < pcount; i++) {
			vic = &g_entities[pids[i]];
			if( !_shrubbot_admin_higher( ent, vic ) )
				continue;
			count += G_FlingClient( vic, flingType );
		}
		AP(va("chat \"^d%s: ^9%d players %s^7\"",
					flingCmd, count, pastTense));
		return qtrue;
	} else if(pcount < 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("^d%s: ^9%s^7\n", flingCmd, err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP(va("^d%s: ^9sorry, but your intended victim has a higher"
			" admin level than you do.^7\n",flingCmd));
		return qfalse;
	}

	if(G_FlingClient( vic, flingType ))
		AP(va("chat \"^d%s: ^7%s ^9was %s^7\"",
				flingCmd,
				vic->client->pers.netname,
				pastTense));
	return qtrue;
}

qboolean G_shrubbot_disorient(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	char *reason;
	gentity_t *vic;

	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^ddisorient: ^9not allowed during intermission.^7\n");
		return qfalse;
	}

	if(Q_SayArgc() < 2+skiparg) {
		SP("usage: !disorient [name|slot#] [reason]\n");
		return qfalse;
	}
	Q_SayArgv(1+skiparg, name, sizeof(name));
	reason = Q_SayConcatArgs(2+skiparg);

	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("disorient: %s\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP("disorient: sorry, but your intended victim has a"
			"higher admin level than you do.\n");
		return qfalse;
	}
	if(!(vic->client->sess.sessionTeam == TEAM_AXIS ||
			vic->client->sess.sessionTeam == TEAM_ALLIES)) {
		SP("disorient: player must be on a team\n");
		return qfalse;
	}
	if(vic->client->pmext.disoriented) {
		SP(va("disorient: %s^7 is already disoriented\n",
			vic->client->pers.netname));
		return qfalse;
	}
	vic->client->pmext.disoriented = qtrue;
	AP(va("chat \"^ddisorient: ^7%s ^9is disoriented\" -1",
			vic->client->pers.netname));

	CPx(pids[0], va("cp \"%s ^7disoriented you%s%s\"",
			(ent?ent->client->pers.netname:"^3SERVER CONSOLE"),
			(*reason) ? " ^9because:\n" : "",
			(*reason) ? reason : ""));
	return qtrue;
}

qboolean G_shrubbot_orient(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	gentity_t *vic;

	if(Q_SayArgc() < 2+skiparg) {
		SP("usage: !disorient [name|slot#]\n");
		return qfalse;
	}
	Q_SayArgv(1+skiparg, name, sizeof(name));

	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("orient: %s\n", err));
		return qfalse;
	}
	vic = &g_entities[pids[0]];

	if(!vic->client->pmext.disoriented) {
		SP(va("^dorient: ^7%s^9 is not currently disoriented\n",
			vic->client->pers.netname));
		return qfalse;
	}
	if(vic->client->ps.eFlags & EF_POISONED) {
		SP(va("^dorient: ^7%s^9 is poisoned at the moment\n",
			vic->client->pers.netname));
		return qfalse;
	}
	vic->client->pmext.disoriented = qfalse;
	AP(va("chat \"^dorient: ^7%s ^9is no longer disoriented\"",
			vic->client->pers.netname));

	CPx(pids[0], va("cp \"%s ^9oriented you\"",
			(ent?ent->client->pers.netname:"^3SERVER CONSOLE")));
	return qtrue;
}

qboolean G_shrubbot_resetxp(gentity_t *ent, int skiparg) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	char *reason;
	gentity_t *vic;

	if(Q_SayArgc() < 2+skiparg) {
		SP("^dusage: ^2!resetxp ^9[name|slot#] [reason]\n");
		return qfalse;
	}
	Q_SayArgv(1+skiparg, name, sizeof(name));
	reason = ConcatArgs(2+skiparg);

	if(ClientNumbersFromString(name, pids) != 1) {
		G_MatchOnePlayer(pids, err, sizeof(err));
		SP(va("resetxp: %s\n", err));
		return qfalse;
	}
	if(!_shrubbot_admin_higher(ent, &g_entities[pids[0]])) {
		SP("^dresetxp: ^9sorry, but your intended victim has a higher "
			"^9 admin level than you do.\n");
		return qfalse;
	}
	vic = &g_entities[pids[0]];
	G_ResetXP(vic);

	AP(va("chat \"^dresetxp: ^9XP have been reset for player %s\"",
			vic->client->pers.netname));

	if(ent && (ent - g_entities) == pids[0])
		return qtrue;

	CPx(pids[0], va("cp \"%s^9 has reset your XP %s%s\"",
			(ent?ent->client->pers.netname:"^3SERVER CONSOLE"),
			(*reason) ? "^9 because:\n" : "",
			(*reason) ? reason : ""));
	return qtrue;
}

qboolean G_shrubbot_resetmyxp(gentity_t *ent, int skiparg) {
	if(!ent || !ent->client) {
		return qfalse;
	}

	G_ResetXP(ent);
	SP("^dresetmyxp: ^9you have reset your XP\n");
	return qtrue;
}

qboolean G_shrubbot_nextmap(gentity_t *ent, int skiparg) {

	/*
	// ignore in intermission
	if ( level.intermissiontime ) {
		SP("^dnextmap: ^9not allowed during intermission.^7\n");
		return qfalse;
	}
	*/

	// copied (basically) from G_Nextmap_v() in g_vote.c
	switch (g_gametype.integer) {
		case GT_WOLF_CAMPAIGN:
			{
				g_campaignInfo_t *campaign = &g_campaigns[level.currentCampaign];

				if( campaign->current + 1 < campaign->mapCount ) {
					 trap_Cvar_Set("g_currentCampaignMap", va( "%i", campaign->current + 1));
					 trap_SendConsoleCommand(EXEC_APPEND,
							 va( "map %s\n", campaign->mapnames[campaign->current + 1]));
					 AP("cp \"^3*** Loading next map in campaign ***\n\"");
				}
				else {
					 // Load in the nextcampaign
					 trap_SendConsoleCommand(EXEC_APPEND, "vstr nextcampaign\n");
					 AP("cp \"^3*** Loading next campaign ***\n\"");
				}
			}
			break;

		case GT_WOLF_MAPVOTE:
			if ( g_gamestate.integer == GS_PLAYING // don't do in intermission // check warmup, warmup-countdown?
					&& g_mapVoteFlags.integer & MAPVOTE_NEXTMAP_VOTEMAP) {
				AP("cp \"^3*** Prepare for next map voting ***\n\"");
				LogExit( "Nextmap passed" );
			}
			else {
				// Load in the nextmap (common behaviour)
				AP("cp \"^3*** Loading next map ***\n\"");
				trap_SendConsoleCommand(EXEC_APPEND, "vstr nextmap\n");
			}
			break;

		default:
			AP("cp \"^3*** Loading next map ***\n\"");
			// Load in the nextmap
			trap_SendConsoleCommand(EXEC_APPEND, "vstr nextmap\n");
			break;
	}

	 AP("chat \"^dnextmap: ^9Next map was loaded\"");
	 return qtrue;
}

/* Dens: (SPC)
Exactly the same as G_shrubbot_print, but this time the text is printed in the
chat and console area of the client instead of console only. (If you want to
switch from SP to SPC, you need to remove the \n at the end, otherwise the text
isn't displayed right).
*/
void G_shrubbot_print_chat(gentity_t *ent, char *m) {
	char temp[MAX_STRING_CHARS], *p, *s;

	if(ent){
		Q_strncpyz(temp, m ,sizeof(temp));
		s = p = temp;
		while(*p) {
			if(*p == '\n') {
				*p++ = '\0';
				CP(va("chat \"%s\" -1", s));
				s = p;
			} else {
				p++;
			}
		}
		CP(va("chat \"%s\" -1", s));
	}
	else {
		char m2[MAX_STRING_CHARS];

		/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): bound the input before decoloring,
		 * leave room for a newline, and never use text as a printf format. */
		Q_strncpyz(temp, m, sizeof(temp) - 1);
		DecolorString(temp, m2);
		G_Printf("%s\n", m2);
	}
}

/*
 * This function facilitates the SP define.  SP() is similar to CP except that
 * it prints the message to the server console if ent is not defined.
 */
void G_shrubbot_print(gentity_t *ent, char *m) {
	if(ent) {
		CP(va("print \"%s\"", m));
	}
	else {
		char temp[MAX_STRING_CHARS], m2[MAX_STRING_CHARS];

		/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): bound the decolor input and do
		 * not interpret user text as a format. */
		Q_strncpyz(temp, m, sizeof(temp));
		DecolorString(temp, m2);
		G_Printf("%s", m2);
	}
}

void G_shrubbot_buffer_begin() {
	bigTextBuffer[0] = '\0';
}

void G_shrubbot_buffer_end(gentity_t *ent) {
	SP(bigTextBuffer);
}

void G_shrubbot_buffer_print(gentity_t *ent, char *m) {
	// gabriel: Slightly different processing for console prints (engine does
	// not like to send huge rcon replies)
	if (!ent) {
		// Remove color from console reply string (instead of waiting for
		// G_shrubbot_print() to do it) to spread console responses over as few
		// print replies as possible
		char temp[MAX_STRING_CHARS], m2[MAX_STRING_CHARS];
		/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): DecolorString has no size argument. */
		Q_strncpyz(temp, m, sizeof(temp));
		DecolorString(temp, m2);

		// We use a "magic number" for console prints.  If someone can get an
		// exact number for the max char limit that can be sent via rcon print
		// reply, we would be grateful :)
		if(strlen(m2) + strlen(bigTextBuffer) > 239) {
			SP(bigTextBuffer);
			bigTextBuffer[0] = '\0';
		}
		Q_strcat(bigTextBuffer, sizeof(bigTextBuffer), m2);
	}
	else {
		// tjw: 1022 - strlen("print 64 \"\"") - 1
		if(strlen(m) + strlen(bigTextBuffer) >= 1009) {
			SP(bigTextBuffer);
			bigTextBuffer[0] = '\0';
		}
		Q_strcat(bigTextBuffer, sizeof(bigTextBuffer), m);
	}
}

void G_shrubbot_cleanup() {
		int i = 0;

		for(i=0; g_shrubbot_levels[i]; i++) {
			free(g_shrubbot_levels[i]);
			g_shrubbot_levels[i] = NULL;
		}
		for(i=0; g_shrubbot_admins[i]; i++) {
			free(g_shrubbot_admins[i]);
			g_shrubbot_admins[i] = NULL;
		}
		for(i=0; g_shrubbot_bans[i]; i++) {
			free(g_shrubbot_bans[i]);
			g_shrubbot_bans[i] = NULL;
		}
		for(i=0; g_shrubbot_mutes[i]; i++) {
			free(g_shrubbot_mutes[i]);
			g_shrubbot_mutes[i] = NULL;
		}
		for(i=0; g_shrubbot_commands[i]; i++) {
			free(g_shrubbot_commands[i]);
			g_shrubbot_commands[i] = NULL;
		}
}

/*
====================
G_shrubbot_adminlevel
--------------------
Jaybird

Gets level via GUID
====================
*/
static int G_shrubbot_adminlevel( gentity_t *ent ) {
	int i;
	// Get GUID and hash
	char *guid = level.clients[ent-g_entities].pers.cl_guid;
	long hash;

// TODO
	// Do not run if no GUID
	if( !*guid ) {
		return 0;
	}

	hash = BG_StringHashValue( guid );

	for( i = 0; g_shrubbot_admins[i]; i++ ) {

		// Problematic admin
		if( !g_shrubbot_admins[i]->guid || !*g_shrubbot_admins[i]->guid )
			continue;

		// Match?
		if( G_shrubbot_usable_guid(guid) && !Q_stricmp( g_shrubbot_admins[i]->guid, guid )) {
			return g_shrubbot_admins[i]->level;
		}
	}
	return 0;
}

/*
====================
!finger
--------------------
Jaybird
====================
*/
qboolean G_shrubbot_finger( gentity_t *ent, int skiparg ) {
	int pids[MAX_CLIENT_MATCHES]; /* CHANGES: match sentinel */
	char name[MAX_NAME_LENGTH], err[MAX_STRING_CHARS];
	char *guid, *ip, *tmp;
	gentity_t *vic;

	if( Q_SayArgc() != 2 + skiparg ) {
		SP("^dusage: ^2!finger ^9[name|slot#]\n");
		return qfalse;
	}

	Q_SayArgv( 1 + skiparg, name, sizeof(name) );

	if( ClientNumbersFromString( name, pids ) != 1 ) {
		G_MatchOnePlayer( pids, err, sizeof(err) );
		SP( va( "^dfinger: ^2%s\n", err ));
		return qfalse;
	}

	vic 	= &g_entities[pids[0]];
	guid 	= vic->client->pers.cl_guid;
	ip 		= vic->client->pers.client_ip;

	// Strip port off of ip.
	tmp = ip;
	while( *tmp ) {
		if( *tmp == ':' ) {
			*tmp = 0;
			break;
		}
		tmp++;
	}

	SP( va( "^dInformation about ^7%s^d:\n", vic->client->pers.netname ));
	SP( va( "^dSlot:    ^2%i\n", vic->client->ps.clientNum ));
	SP( va( "^dAdmin:   ^2%i\n", G_shrubbot_adminlevel( vic )));
	SP( va( "^dGUID:    ^2%s\n", guid ));

#ifdef AUTO_GUID
	SP( va( "^dNQ-GUID: ^2%s\n", level.clients[vic->client->ps.clientNum].sess.nqKeyInfo.nquid) );
#endif

#ifdef SYSTEM_CHECK	
	SP( va( "^dSystem:  ^2%s\n", G_SystemInfo(vic->client->ps.clientNum) ));
#endif

#ifdef HW_BAN
	if ( nq_security.integer & NQS_USE_HW_BANS )
		SP( va( "^dHWGUID:  ^2%s\n", vic->client->pers.cg_hwguid ));
#endif // HW_BAN

	SP( va( "^dIP:      ^2%s\n", ip ));

	return qtrue;
}

/*
====================
!uptime
--------------------
Jaybird
====================
*/
qboolean G_shrubbot_uptime( gentity_t *ent, int skiparg ) {
	int rawtime;
	int days;
	int hours;
	int minutes;

	rawtime = level.time / SECONDS_1;
	days = rawtime / (60 * 60 * 24);
	rawtime -= (days * 60 * 60 * 24);
	hours = rawtime / (60 * 60);
	rawtime -= (hours * 60 * 60);
	minutes = rawtime / 60;

	SPC(va("^duptime: ^2%i days, %i hours, %i minutes.", days, hours, minutes));
	return qtrue;
}

/*
 * IRATA: 	displays a server-side list of disabled and restricted weapons (todo: implement this as client cmd?)
 *			I don't use arguments (weapon numbers) instead we only print infos
 *			about weapons which are disabled or restricted
 *			see g_svcmds.c "weap" cmd
 */
qboolean G_shrubbot_weapons(gentity_t *ent, int skiparg) {
	int i;
	int 	count = 0;
	char	minplayers[4], maxweap[4], percentage[4];

	SBP_begin();
	SBP(va("weapons: %d players in game:\n", level.numPlayingClients));

	if (!ent) {
		SBP("Used values from the config - see also server cmd /weap:\n");
	}

	// show all restricted weapons..
	for (i=WP_NONE+1; i<WP_NUM_WEAPONS; ++i) {
		if (!weapon_restrictions[i].restricted) continue;
		// print header..
		if (count == 0) {
			SBP("Weapon        Min Max Per Status\n");
			SBP("------------------------------------------------------------\n");
		}
		++count;
		// make up some strings..
		// not restricted? == empty string, which makes it more clear to read.
		if (weapon_restrictions[i].disabled) {
			SBP(va("%-25s %s\n", BG_Weapons[i].statname, "^1disabled by admin^7"));
		}
		else {
			// TODO: display what's required to get the weapon ??? ...

			if (weapon_restrictions[i].minplayers <= 1) {
				Q_strncpyz(minplayers, "    ", sizeof(minplayers));
			}
			else {
				Q_strncpyz(minplayers, va("%3i",weapon_restrictions[i].minplayers), sizeof(minplayers));
			}
			if (weapon_restrictions[i].maxweap <= 0) {
				Q_strncpyz(maxweap, "    ", sizeof(maxweap));
			}
			else {
				Q_strncpyz(maxweap, va("%3i",weapon_restrictions[i].maxweap), sizeof(maxweap));
			}
			if (weapon_restrictions[i].percentage <= 0 || weapon_restrictions[i].percentage >= 100) {
				Q_strncpyz(percentage, "    ", sizeof(percentage));
			}
			else {
				Q_strncpyz(percentage, va("%3i",weapon_restrictions[i].percentage), sizeof(percentage));
			}

			if (ent) {
				SBP(va("%-13s %s %s %s %s\n", BG_Weapons[i].statname, minplayers, maxweap, percentage, G_IsWeaponDisabled( ent, (weapon_t)i, qtrue )? "^1weapon is restricted by rule^7":"^2weapon is available^7"));
			}
			else {
				SBP(va("%-13s %s %s %s %s\n", BG_Weapons[i].statname, minplayers, maxweap, percentage, "cant check rule - not connected"));
			}

		}
	}
	if (count == 0) {
		SBP("There are no weapon restrictions.\n");
	}
	else {
		SBP("------------------------------------------------------------\n");
		SBP(va("In total %d restriction rules set.\n", count));
	}

	SBP_end();
	return qtrue;
}
