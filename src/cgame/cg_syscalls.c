/* NewMod, Arena.ai, 2026-11-21: changes in this file; see CHANGES. */
/*
 * name:		cg_syscalls.c
 *
 * desc:		syscalls
 *				cg_syscalls.c -- this file is only included when building a dll
 *				Parts of documentation are taken from http://www.quake3world.com/ubb/Forum4/HTML/005563.html - THX !
 * NQQS:
 *
 */

#include "cg_local.h"

/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): match the native pointer-width ET: Legacy syscall ABI. */
static intptr_t (QDECL *syscall)(intptr_t arg, ...) = (intptr_t (QDECL *)(intptr_t, ...))-1;
#include "../game/nm_syscall.h"

#if defined(__MACOS__)
#ifndef __GNUC__
#pragma export on
#endif
#endif
extern "C" DLLEXPORT void QDECL dllEntry( intptr_t (QDECL *syscallptr)(intptr_t arg, ...) ) {
	syscall = syscallptr;
}
#if defined(__MACOS__)
#ifndef __GNUC__
#pragma export off
#endif
#endif

/*int PASSFLOAT( float x ) {
	float	floatTemp;
	floatTemp = x;
	return *(int *)&floatTemp;
}*/

#define PASSFLOAT( x ) (*(int*)&x)

/*
 * IRATA: obsolete, see GPL
 *
 * 	case CG_PUMPEVENTLOOP:
 *  //		Com_EventLoop();
 *  //		CL_WritePacket();
 *		return 0;
void trap_PumpEventLoop( void ) {
	if( !cgs.initing ) {
		return;
	}
	NM_SYSCALL( CG_PUMPEVENTLOOP );
}
 */

void trap_Print( const char *fmt ) {
	NM_SYSCALL( CG_PRINT, fmt ); /* CHANGES: native ABI */
}

void trap_Error( const char *fmt ) {
	NM_SYSCALL( CG_ERROR, fmt ); /* CHANGES: native ABI */
	exit(1);
}

int	trap_Milliseconds( void ) {
	return NM_SYSCALL( CG_MILLISECONDS ); /* CHANGES: native ABI */
}

void trap_Cvar_Register( vmCvar_t *vmCvar, const char *varName, const char *defaultValue, int flags ) {
	NM_SYSCALL( CG_CVAR_REGISTER, vmCvar, varName, defaultValue, flags ); /* CHANGES: native ABI */
}

void trap_Cvar_Update( vmCvar_t *vmCvar ) {
	NM_SYSCALL( CG_CVAR_UPDATE, vmCvar ); /* CHANGES: native ABI */
}

void trap_Cvar_Set( const char *var_name, const char *value ) {
	NM_SYSCALL( CG_CVAR_SET, var_name, value ); /* CHANGES: native ABI */
}

void trap_Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize ) {
	NM_SYSCALL( CG_CVAR_VARIABLESTRINGBUFFER, var_name, buffer, bufsize ); /* CHANGES: native ABI */
}

void trap_Cvar_LatchedVariableStringBuffer( const char *var_name, char *buffer, int bufsize ) {
	NM_SYSCALL( CG_CVAR_LATCHEDVARIABLESTRINGBUFFER, var_name, buffer, bufsize ); /* CHANGES: native ABI */
}

int	trap_Argc( void ) {
	return NM_SYSCALL( CG_ARGC ); /* CHANGES: native ABI */
}

void trap_Argv( int n, char *buffer, int bufferLength ) {
	NM_SYSCALL( CG_ARGV, n, buffer, bufferLength ); /* CHANGES: native ABI */
}

void trap_Args( char *buffer, int bufferLength ) {
	NM_SYSCALL( CG_ARGS, buffer, bufferLength ); /* CHANGES: native ABI */
}

int	trap_FS_FOpenFile( const char *qpath, fileHandle_t *f, fsMode_t mode ) {
	return NM_SYSCALL( CG_FS_FOPENFILE, qpath, f, mode ); /* CHANGES: native ABI */
}

void trap_FS_Read( void *buffer, int len, fileHandle_t f ) {
	NM_SYSCALL( CG_FS_READ, buffer, len, f ); /* CHANGES: native ABI */
}

void trap_FS_Write( const void *buffer, int len, fileHandle_t f ) {
	NM_SYSCALL( CG_FS_WRITE, buffer, len, f ); /* CHANGES: native ABI */
}

void trap_FS_FCloseFile( fileHandle_t f ) {
	NM_SYSCALL( CG_FS_FCLOSEFILE, f ); /* CHANGES: native ABI */
}

int trap_FS_GetFileList(  const char *path, const char *extension, char *listbuf, int bufsize ) {
	return NM_SYSCALL( CG_FS_GETFILELIST, path, extension, listbuf, bufsize ); /* CHANGES: native ABI */
}

int trap_FS_Delete( const char *filename ) {
	return NM_SYSCALL( CG_FS_DELETEFILE, filename); /* CHANGES: native ABI */
}

void trap_SendConsoleCommand( const char *text ) {
	NM_SYSCALL( CG_SENDCONSOLECOMMAND, text ); /* CHANGES: native ABI */
}

void trap_AddCommand( const char *cmdName ) {
	NM_SYSCALL( CG_ADDCOMMAND, cmdName ); /* CHANGES: native ABI */
}

/*
===================
trap_RemoveCommand

cmdName: command name

Although this trap is not actually used anywhere in baseq3 game source, a reasonable assumption, based on
function name and parameters, is that this trap removes a command name from the list of command completions.
That is, the inverse of trap_AddCommand().

Unknown: case sensitivity (i.e. if capitalization matters for string matching)
===================
*/
void trap_RemoveCommand( const char *cmdName ) {
	NM_SYSCALL( CG_REMOVECOMMAND, cmdName ); /* CHANGES: native ABI */
}

/*
===================
trap_SendClientCommand

===================
*/
void trap_SendClientCommand( const char *s ) {
	NM_SYSCALL( CG_SENDCLIENTCOMMAND, s ); /* CHANGES: native ABI */
}

void trap_UpdateScreen( void ) {
	NM_SYSCALL( CG_UPDATESCREEN ); /* CHANGES: native ABI */
}

int	trap_CM_NumInlineModels( void ) {
	return NM_SYSCALL( CG_CM_NUMINLINEMODELS ); /* CHANGES: native ABI */
}

clipHandle_t trap_CM_InlineModel( int index ) {
	return NM_SYSCALL( CG_CM_INLINEMODEL, index ); /* CHANGES: native ABI */
}

clipHandle_t trap_CM_TempBoxModel( const vec3_t mins, const vec3_t maxs ) {
	return NM_SYSCALL( CG_CM_TEMPBOXMODEL, mins, maxs ); /* CHANGES: native ABI */
}

clipHandle_t trap_CM_TempCapsuleModel( const vec3_t mins, const vec3_t maxs ) {
	return NM_SYSCALL( CG_CM_TEMPCAPSULEMODEL, mins, maxs ); /* CHANGES: native ABI */
}

int	trap_CM_PointContents( const vec3_t p, clipHandle_t model ) {
	return NM_SYSCALL( CG_CM_POINTCONTENTS, p, model ); /* CHANGES: native ABI */
}

int	trap_CM_TransformedPointContents( const vec3_t p, clipHandle_t model, const vec3_t origin, const vec3_t angles ) {
	return NM_SYSCALL( CG_CM_TRANSFORMEDPOINTCONTENTS, p, model, origin, angles ); /* CHANGES: native ABI */
}

void trap_CM_BoxTrace( trace_t *results, const vec3_t start, const vec3_t end,
						  const vec3_t mins, const vec3_t maxs,
						  clipHandle_t model, int brushmask ) {
	NM_SYSCALL( CG_CM_BOXTRACE, results, start, end, mins, maxs, model, brushmask ); /* CHANGES: native ABI */
}

void trap_CM_TransformedBoxTrace( trace_t *results, const vec3_t start, const vec3_t end,
						  const vec3_t mins, const vec3_t maxs,
						  clipHandle_t model, int brushmask,
						  const vec3_t origin, const vec3_t angles ) {
	NM_SYSCALL( CG_CM_TRANSFORMEDBOXTRACE, results, start, end, mins, maxs, model, brushmask, origin, angles ); /* CHANGES: native ABI */
}

void trap_CM_CapsuleTrace( trace_t *results, const vec3_t start, const vec3_t end,
						  const vec3_t mins, const vec3_t maxs,
						  clipHandle_t model, int brushmask ) {
	NM_SYSCALL( CG_CM_CAPSULETRACE, results, start, end, mins, maxs, model, brushmask ); /* CHANGES: native ABI */
}

void trap_CM_TransformedCapsuleTrace( trace_t *results, const vec3_t start, const vec3_t end,
						  const vec3_t mins, const vec3_t maxs,
						  clipHandle_t model, int brushmask,
						  const vec3_t origin, const vec3_t angles ) {
	NM_SYSCALL( CG_CM_TRANSFORMEDCAPSULETRACE, results, start, end, mins, maxs, model, brushmask, origin, angles ); /* CHANGES: native ABI */
}

int	trap_CM_MarkFragments( int numPoints, const vec3_t *points,
				const vec3_t projection,
				int maxPoints, vec3_t pointBuffer,
				int maxFragments, markFragment_t *fragmentBuffer ) {
	return NM_SYSCALL( CG_CM_MARKFRAGMENTS, numPoints, points, projection, maxPoints, pointBuffer, maxFragments, fragmentBuffer ); /* CHANGES: native ABI */
}

// ydnar
void trap_R_ProjectDecal( qhandle_t hShader, int numPoints, vec3_t *points, vec4_t projection, vec4_t color, int lifeTime, int fadeTime ) {
	NM_SYSCALL( CG_R_PROJECTDECAL, hShader, numPoints, points, projection, color, lifeTime, fadeTime ); /* CHANGES: native ABI */
}

void trap_R_ClearDecals( void ) {
	NM_SYSCALL( CG_R_CLEARDECALS ); /* CHANGES: native ABI */
}

/*
===================
trap_S_StartSound

If origin is NULL snd.entityPositions[ch->entnum] is used.

===================
*/
void trap_S_StartSound( vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx ) {
	NM_SYSCALL( CG_S_STARTSOUND, origin, entityNum, entchannel, sfx, 127 /* Gordon: default volume always for the moment*/ ); /* CHANGES: native ABI */
}

void trap_S_StartSoundVControl( vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx, int volume ) {
	NM_SYSCALL( CG_S_STARTSOUND, origin, entityNum, entchannel, sfx, volume ); /* CHANGES: native ABI */
}

/*
===================
trap_S_StartSound - (SA)	added

If origin is NULL snd.entityPositions[ch->entnum] is used.

===================
*/
void trap_S_StartSoundEx( vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx, int flags ) {
	NM_SYSCALL( CG_S_STARTSOUNDEX, origin, entityNum, entchannel, sfx, flags, 127 /* Gordon: default volume always for the moment*/ ); /* CHANGES: native ABI */
}

void trap_S_StartSoundExVControl( vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx, int flags, int volume ) {
	NM_SYSCALL( CG_S_STARTSOUNDEX, origin, entityNum, entchannel, sfx, flags, volume ); /* CHANGES: native ABI */
}

void trap_S_StartLocalSound( sfxHandle_t sfx, int channelNum ) {
	NM_SYSCALL( CG_S_STARTLOCALSOUND, sfx, channelNum, 127 /* Gordon: default volume always for the moment*/ ); /* CHANGES: native ABI */
}

void trap_S_ClearLoopingSounds( void ) {
	NM_SYSCALL( CG_S_CLEARLOOPINGSOUNDS ); /* CHANGES: native ABI */
}

void trap_S_ClearSounds( qboolean killmusic ) {
	NM_SYSCALL( CG_S_CLEARSOUNDS, killmusic ); /* CHANGES: native ABI */
}

void trap_S_AddLoopingSound( const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx, int volume, int soundTime ) {
	NM_SYSCALL( CG_S_ADDLOOPINGSOUND, origin, velocity, 1250, sfx, volume, soundTime ); /* CHANGES: native ABI */ // volume was previously removed from CG_S_ADDLOOPINGSOUND.  I added 'range'
}

void trap_S_AddRealLoopingSound( const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx, int range, int volume, int soundTime ) {
	NM_SYSCALL( CG_S_ADDREALLOOPINGSOUND, origin, velocity, range, sfx, volume, soundTime ); /* CHANGES: native ABI */
}

void trap_S_StopStreamingSound(int entityNum) {
	NM_SYSCALL( CG_S_STOPSTREAMINGSOUND, entityNum ); /* CHANGES: native ABI */
}

void trap_S_UpdateEntityPosition( int entityNum, const vec3_t origin ) {
	NM_SYSCALL( CG_S_UPDATEENTITYPOSITION, entityNum, origin ); /* CHANGES: native ABI */
}

// Ridah, talking animations
int	trap_S_GetVoiceAmplitude( int entityNum ) {
	return NM_SYSCALL( CG_S_GETVOICEAMPLITUDE, entityNum ); /* CHANGES: native ABI */
}
// done.

void trap_S_Respatialize( int entityNum, const vec3_t origin, vec3_t axis[3], int inwater ) {
	NM_SYSCALL( CG_S_RESPATIALIZE, entityNum, origin, axis, inwater ); /* CHANGES: native ABI */
}

int trap_S_GetSoundLength(sfxHandle_t sfx) {
	return NM_SYSCALL( CG_S_GETSOUNDLENGTH, sfx ); /* CHANGES: native ABI */
}

// ydnar: for timing looped sounds
int trap_S_GetCurrentSoundTime( void ) {
	return NM_SYSCALL( CG_S_GETCURRENTSOUNDTIME ); /* CHANGES: native ABI */
}

void trap_S_StartBackgroundTrack( const char *intro, const char *loop, int fadeupTime ) {
	NM_SYSCALL( CG_S_STARTBACKGROUNDTRACK, intro, loop, fadeupTime ); /* CHANGES: native ABI */
}

void trap_S_FadeBackgroundTrack( float targetvol, int time, int num) {
	NM_SYSCALL( CG_S_FADESTREAMINGSOUND, PASSFLOAT(targetvol), time, num); /* CHANGES: native ABI */ // 'num' is '0' if it's music, '1' if it's "all streaming sounds"
}

void trap_S_FadeAllSound( float targetvol, int time, qboolean stopsounds) {
	NM_SYSCALL( CG_S_FADEALLSOUNDS, PASSFLOAT(targetvol), time, stopsounds); /* CHANGES: native ABI */
}

int	trap_S_StartStreamingSound( const char *intro, const char *loop, int entnum, int channel, int attenuation ) {
	return NM_SYSCALL( CG_S_STARTSTREAMINGSOUND, intro, loop, entnum, channel, attenuation ); /* CHANGES: native ABI */
}

//----(SA)	added
qboolean trap_R_GetSkinModel( qhandle_t skinid, const char *type, char *name ) {
	return NM_SYSCALL( CG_R_GETSKINMODEL, skinid, type, name); /* CHANGES: native ABI */
}

qhandle_t trap_R_GetShaderFromModel( qhandle_t modelid, int surfnum, int withlightmap) {
	return NM_SYSCALL( CG_R_GETMODELSHADER, modelid, surfnum, withlightmap); /* CHANGES: native ABI */
}
//----(SA)	end

void trap_R_ClearScene( void ) {
	NM_SYSCALL( CG_R_CLEARSCENE ); /* CHANGES: native ABI */
}

void trap_R_AddRefEntityToScene( const refEntity_t *re ) {
	NM_SYSCALL( CG_R_ADDREFENTITYTOSCENE, re ); /* CHANGES: native ABI */
}

void trap_R_AddPolyToScene( qhandle_t hShader , int numVerts, const polyVert_t *verts ) {
	NM_SYSCALL( CG_R_ADDPOLYTOSCENE, hShader, numVerts, verts ); /* CHANGES: native ABI */
}

void trap_R_AddPolyBufferToScene( polyBuffer_t* pPolyBuffer ) {
	NM_SYSCALL( CG_R_ADDPOLYBUFFERTOSCENE, pPolyBuffer ); /* CHANGES: native ABI */
}

// Ridah
void trap_R_AddPolysToScene( qhandle_t hShader , int numVerts, const polyVert_t *verts, int numPolys ) {
	NM_SYSCALL( CG_R_ADDPOLYSTOSCENE, hShader, numVerts, verts, numPolys ); /* CHANGES: native ABI */
}

/*
=================
trap_R_AddLightToScene

const vec3_t org: origin of the light - where the light will be.

float intensity: not the exact intensity of the light, but the radius of the light - the light is a sphere-like object
and points within that object are lighted up, and the closer something is to the origin of the light (org) the closer
the light will be to the actual colour data of the light.

float r/g/b: this is the colour data of the light (normalized, so the highest value, 255, is 1.0) - r is the amount of
red light, g green light, and b blue light.

This function will add a light to the game at org, which has a radius of intensity units, and the colour data which is shown in the variables r, g, and b. The highest number
for an RGB index is 255, but in Quake 3 the colour's index is divided by 255 to give a normalized value.

Say I want to make a purple light. The colour data for purple in RGB is 255,0,255. So the normalized values for this colour are:

255/255 = 1.0
0/255 = 0.0
255/255 = 1.0

So to add a purple light at the vector x, and radius 2.4 I would write:
trap_R_AddLightToScene( x, 2.4, 1.0, 0.0, 1.0 );
=================
*/
void trap_R_AddLightToScene( const vec3_t org, float radius, float intensity, float r, float g, float b, qhandle_t hShader, int flags ) {
	NM_SYSCALL( CG_R_ADDLIGHTTOSCENE, org, PASSFLOAT( radius ), PASSFLOAT( intensity ), /* CHANGES: native ABI */
		PASSFLOAT( r ), PASSFLOAT( g ), PASSFLOAT( b ), hShader, flags );
}

//----(SA)
void trap_R_AddCoronaToScene( const vec3_t org, float r, float g, float b, float scale, int id, qboolean visible) {
	NM_SYSCALL( CG_R_ADDCORONATOSCENE, org, PASSFLOAT(r), PASSFLOAT(g), PASSFLOAT(b), PASSFLOAT(scale), id, visible); /* CHANGES: native ABI */
}


//----(SA)
void trap_R_SetFog( int fogvar, int var1, int var2, float r, float g, float b, float density ) {
	NM_SYSCALL( CG_R_SETFOG, fogvar, var1, var2, PASSFLOAT(r), PASSFLOAT(g), PASSFLOAT(b), PASSFLOAT(density) ); /* CHANGES: native ABI */
}


void trap_R_SetGlobalFog( qboolean restore, int duration, float r, float g, float b, float depthForOpaque ) {
	NM_SYSCALL( CG_R_SETGLOBALFOG, restore, duration, PASSFLOAT(r), PASSFLOAT(g), PASSFLOAT(b), PASSFLOAT(depthForOpaque) ); /* CHANGES: native ABI */
}

void trap_R_RenderScene( const refdef_t *fd ) {
	NM_SYSCALL( CG_R_RENDERSCENE, fd ); /* CHANGES: native ABI */
}

// Mad Doctor I, 11/4/2002.
void trap_R_SaveViewParms() {
	NM_SYSCALL( CG_R_SAVEVIEWPARMS ); /* CHANGES: native ABI */
}

// Mad Doctor I, 11/4/2002.
void trap_R_RestoreViewParms() {
	NM_SYSCALL( CG_R_RESTOREVIEWPARMS ); /* CHANGES: native ABI */
}

void trap_R_SetColor( const float *rgba ) {
	NM_SYSCALL( CG_R_SETCOLOR, rgba ); /* CHANGES: native ABI */
}

void trap_R_DrawStretchPic( float x, float y, float w, float h,
							   float s1, float t1, float s2, float t2, qhandle_t hShader ) {
	NM_SYSCALL( CG_R_DRAWSTRETCHPIC, PASSFLOAT(x), PASSFLOAT(y), PASSFLOAT(w), PASSFLOAT(h), PASSFLOAT(s1), PASSFLOAT(t1), PASSFLOAT(s2), PASSFLOAT(t2), hShader ); /* CHANGES: native ABI */
}

void trap_R_DrawRotatedPic( float x, float y, float w, float h,
							   float s1, float t1, float s2, float t2, qhandle_t hShader, float angle ) {
	NM_SYSCALL( CG_R_DRAWROTATEDPIC, PASSFLOAT(x), PASSFLOAT(y), PASSFLOAT(w), PASSFLOAT(h), PASSFLOAT(s1), PASSFLOAT(t1), PASSFLOAT(s2), PASSFLOAT(t2), hShader, PASSFLOAT(angle) ); /* CHANGES: native ABI */
}

void trap_R_DrawStretchPicGradient(	float x, float y, float w, float h,
										float s1, float t1, float s2, float t2, qhandle_t hShader,
										const float *gradientColor, int gradientType ) {
	NM_SYSCALL( CG_R_DRAWSTRETCHPIC_GRADIENT, PASSFLOAT(x), PASSFLOAT(y), PASSFLOAT(w), PASSFLOAT(h), PASSFLOAT(s1), PASSFLOAT(t1), PASSFLOAT(s2), PASSFLOAT(t2), hShader, gradientColor, gradientType  ); /* CHANGES: native ABI */
}

void trap_R_Add2dPolys( polyVert_t* verts, int numverts, qhandle_t hShader ) {
	NM_SYSCALL( CG_R_DRAW2DPOLYS, verts, numverts, hShader ); /* CHANGES: native ABI */
}


void trap_R_ModelBounds( clipHandle_t model, vec3_t mins, vec3_t maxs ) {
	NM_SYSCALL( CG_R_MODELBOUNDS, model, mins, maxs ); /* CHANGES: native ABI */
}

int	trap_R_LerpTag( orientation_t *tag, const refEntity_t *refent, const char *tagName, int startIndex ) {
	return NM_SYSCALL( CG_R_LERPTAG, tag, refent, tagName, startIndex ); /* CHANGES: native ABI */
}

void trap_R_RemapShader( const char *oldShader, const char *newShader, const char *timeOffset ) {
	NM_SYSCALL( CG_R_REMAP_SHADER, oldShader, newShader, timeOffset ); /* CHANGES: native ABI */
}

void trap_GetGlconfig( glconfig_t *glconfig ) {
	NM_SYSCALL( CG_GETGLCONFIG, glconfig ); /* CHANGES: native ABI */
}

void trap_GetGameState( gameState_t *gamestate ) {
	NM_SYSCALL( CG_GETGAMESTATE, gamestate ); /* CHANGES: native ABI */
}

#ifdef _DEBUG
//#define FAKELAG
#ifdef FAKELAG
#define	MAX_SNAPSHOT_BACKUP	256
#define	MAX_SNAPSHOT_MASK	(MAX_SNAPSHOT_BACKUP - 1)

static snapshot_t snaps[MAX_SNAPSHOT_BACKUP];
static int curSnapshotNumber;
int snapshotDelayTime;
static qboolean skiponeget;
#endif // FAKELAG
#endif // _DEBUG

void trap_GetCurrentSnapshotNumber( int *snapshotNumber, int *serverTime ) {
	NM_SYSCALL( CG_GETCURRENTSNAPSHOTNUMBER, snapshotNumber, serverTime ); /* CHANGES: native ABI */

#ifdef FAKELAG
	{
		char s[MAX_STRING_CHARS];
		int fakeLag;

		trap_Cvar_VariableStringBuffer( "g_fakelag", s, sizeof(s) );
		fakeLag = atoi(s);
		if( fakeLag < 0 )
			fakeLag = 0;

		if( fakeLag ) {
			if( curSnapshotNumber < cg.latestSnapshotNum ) {
				*snapshotNumber = cg.latestSnapshotNum + 1;
				curSnapshotNumber = cg.latestSnapshotNum + 2;	// skip one ahead and we're good to go on the next frame
				skiponeget = qtrue;
			} else
				*snapshotNumber = curSnapshotNumber;
		}
	}
#endif // FAKELAG
}

qboolean trap_GetSnapshot( int snapshotNumber, snapshot_t *snapshot ) {
#ifndef FAKELAG
	return NM_SYSCALL( CG_GETSNAPSHOT, snapshotNumber, snapshot ); /* CHANGES: native ABI */
#else
	{
		char s[MAX_STRING_CHARS];
		int fakeLag;

		if( skiponeget)
		NM_SYSCALL( CG_GETSNAPSHOT, snapshotNumber, snapshot ); /* CHANGES: native ABI */

		trap_Cvar_VariableStringBuffer( "g_fakelag", s, sizeof(s) );
		fakeLag = atoi(s);
		if( fakeLag < 0 )
			fakeLag = 0;

		if( fakeLag ) {
			int i;
			int realsnaptime, thissnaptime;

			// store our newest usercmd
			curSnapshotNumber++;
			memcpy( &snaps[curSnapshotNumber & MAX_SNAPSHOT_MASK], snapshot, sizeof(snapshot_t) );

			// find a usercmd that is fakeLag msec behind
			i = curSnapshotNumber & MAX_SNAPSHOT_MASK;
			realsnaptime = snaps[i].serverTime;
			i--;
			do {
				thissnaptime = snaps[i & MAX_SNAPSHOT_MASK].serverTime;

				if( realsnaptime - thissnaptime > fakeLag ) {
					// found the right one
					snapshotDelayTime = realsnaptime - thissnaptime;
                    snapshot = &snaps[i & MAX_SNAPSHOT_MASK];
					//*snapshotNumber = i & MAX_SNAPSHOT_MASK;
					return qtrue;
				}

				i--;
			} while ( (i & MAX_SNAPSHOT_MASK) != (curSnapshotNumber & MAX_SNAPSHOT_MASK) );

			// didn't find a proper one, just use the oldest one we have
			snapshotDelayTime = realsnaptime - thissnaptime;
			snapshot = &snaps[(curSnapshotNumber - 1) & MAX_SNAPSHOT_MASK];
			//*snapshotNumber = (curSnapshotNumber - 1) & MAX_SNAPSHOT_MASK;
			return qtrue;
		} else
			return NM_SYSCALL( CG_GETSNAPSHOT, snapshotNumber, snapshot ); /* CHANGES: native ABI */
	}
#endif // FAKELAG
}

qboolean trap_GetServerCommand( int serverCommandNumber ) {
	return NM_SYSCALL( CG_GETSERVERCOMMAND, serverCommandNumber ); /* CHANGES: native ABI */
}

int	trap_GetCurrentCmdNumber( void ) {
	return NM_SYSCALL( CG_GETCURRENTCMDNUMBER ); /* CHANGES: native ABI */
}

qboolean trap_GetUserCmd( int cmdNumber, usercmd_t *ucmd ) {
	return NM_SYSCALL( CG_GETUSERCMD, cmdNumber, ucmd ); /* CHANGES: native ABI */
}

void trap_SetUserCmdValue( int stateValue, int flags, float sensitivityScale, int mpIdentClient ) {
	NM_SYSCALL( CG_SETUSERCMDVALUE, stateValue, flags, PASSFLOAT(sensitivityScale), mpIdentClient ); /* CHANGES: native ABI */
}

void trap_SetClientLerpOrigin( float x, float y, float z ) {
	NM_SYSCALL( CG_SETCLIENTLERPORIGIN, PASSFLOAT(x), PASSFLOAT(y), PASSFLOAT(z) ); /* CHANGES: native ABI */
}

void testPrintInt( char *string, int i ) {
	NM_SYSCALL( CG_TESTPRINTINT, string, i ); /* CHANGES: native ABI */
}

void testPrintFloat( char *string, float f ) {
	NM_SYSCALL( CG_TESTPRINTFLOAT, string, PASSFLOAT(f) ); /* CHANGES: native ABI */
}

int trap_MemoryRemaining( void ) {
	return NM_SYSCALL( CG_MEMORY_REMAINING ); /* CHANGES: native ABI */
}

qboolean trap_loadCamera( int camNum, const char *name ) {
	return NM_SYSCALL( CG_LOADCAMERA, camNum, name ); /* CHANGES: native ABI */
}

void trap_startCamera( int camNum, int time ) {
	NM_SYSCALL( CG_STARTCAMERA, camNum, time ); /* CHANGES: native ABI */
}

void trap_stopCamera( int camNum ) {
	NM_SYSCALL( CG_STOPCAMERA, camNum ); /* CHANGES: native ABI */
}

qboolean trap_getCameraInfo( int camNum, int time, vec3_t *origin, vec3_t *angles, float *fov ) {
	return NM_SYSCALL( CG_GETCAMERAINFO, camNum, time, origin, angles, fov ); /* CHANGES: native ABI */
}

/*
=================
trap_Key_IsDown

keynum: A key keynum

Returns true is the indicated key is held down, returns false if not held down (released).

The values of keynum range from 0 to 255, inclusive. For the most part, a key's keynum corresponds to its character's ASCII value (e.g. the 'A' key is 65).

I have a list of keynames used by the bind command with their keynums which I may append at a later date. In the meantime, if you want to know them now, you can do this:

Create a keynums.cfg with a pattern like:
bind 0x00 Key 0x00
bind 0x01 Key 0x01
...
bind 0xfe Key 0xfe
bind 0xff Key 0xff

(case matters for the hex values, and a scripting language to automate this really helps)

Back up your q3config.cfg, run Q3, "/exec keynums.cfg; bindlist; condump keynums.txt; quit"

In the file "keynums.txt" you should now have lines like:
UPARROW "Key 0x84"

There's your keynum list.


(Generating keynums.cfg in GNU bash:
for i in $(seq 0 255); do x=$(printf "%02x" $i); echo bind 0x$x Key 0x$x; done > keynums.cfg
)
=================
*/
qboolean trap_Key_IsDown( int keynum ) {
	return NM_SYSCALL( CG_KEY_ISDOWN, keynum ); /* CHANGES: native ABI */
}

/*
=================
trap_Key_GetCatcher

Returns a bitmask representing active key catchers.

Bitmask values are:
KEYCATCH_CONSOLE
KEYCATCH_UI
KEYCATCH_MESSAGE
KEYCATCH_CGAME

(see also trap_Key_SetCatcher())

To check if CGAME key-catching is active:
if (trap_Key_GetCatcher() & KEYCATCH_UI) {
	// happy happy
}
=================
*/
int trap_Key_GetCatcher( void ) {
	return NM_SYSCALL( CG_KEY_GETCATCHER ); /* CHANGES: native ABI */
}

qboolean trap_Key_GetOverstrikeMode( void ) {
	return NM_SYSCALL( CG_KEY_GETOVERSTRIKEMODE ); /* CHANGES: native ABI */
}

void trap_Key_SetOverstrikeMode( qboolean state ) {
	NM_SYSCALL( CG_KEY_SETOVERSTRIKEMODE, state ); /* CHANGES: native ABI */
}

// binding MUST be lower case
void trap_Key_KeysForBinding( const char* binding, int* key1, int* key2 ) {
	NM_SYSCALL( CG_KEY_BINDINGTOKEYS, binding, key1, key2 ); /* CHANGES: native ABI */
}

/*
=================
catcher: Key catchers bitmask

Redirects key events to different module(s).

Normally, key events are interpreted by the engine, affected by the key bindings. This trap allows you redirect key events to other components.

Valid catchers are:
KEYCATCH_CONSOLE: the console
KEYCATCH_UI: the ui module
KEYCATCH_MESSAGE: the little message editor thingy
KEYCATCH_CGAME: the cgame module

These are bitmasked values, meaning you can combine them with the OR operator (|), as in (KEYCATCH_UI|KEYCATCH_CGAME), but I recommend against trying multiple catchers (it may get confusing).

As a mods programmer, you may be most interested in KEYCATCH_CGAME, and on this I will concentrate this entry.

--- The Keyboard ---

When key-catching is enabled (trap_Key_SetCatcher(KEYCATCH_CGAME);), upon a change in key state (gets pressed or gets released), the engine calls the vmMain() function in cg_main.c with the following parameters:
command = CG_KEY_EVENT
arg0 = keynum
arg1 = 1 if pressed, 0 if released.

In baseq3 game source, this event is already handled and delegated to CG_KeyEvent(), which is empty.

The values for keynum are the same as for trap_Key_IsDown().

Key catching is disabled by calling with a parameter of 0 (as in, no catchers). Key catching is also terminated if the user presses the ESC key, which means you won't receive events on the ESC key.

--- The Mouse ---

When key-catching is enabled, mouse movement is also redirected to the specified module. When the engine detects mouse movement, it calls vmMain() with the following parameters:
command = CG_MOUSE_EVENT
arg0 = relative X position
arg1 = relative Y position

The game source for baseq3 delegates this event to CG_MouseEvent().

Pointer Device Movement - Reported Value:
Left - Negative X
Right - Positive X
Up - Negative Y
Down - Positive Y

The engine only reports mouse movement -- if the mouse doesn't move, there is no mouse event. The engine reports mouse buttons and wheels (if any) as key events, for binding purposes.

Cvars do not influence the reported values.

=================
*/
void trap_Key_SetCatcher( int catcher ) {
	NM_SYSCALL( CG_KEY_SETCATCHER, catcher ); /* CHANGES: native ABI */
}

/*
=================
trap_Key_GetKey

binding: command string presumably bound to a key.

Returns keynum of the key that has the binding specified by binding. Case-sensitive.

Unknown: what happens when many keys have this binding.
=================
*/
int trap_Key_GetKey( const char *binding ) {
	return NM_SYSCALL( CG_KEY_GETKEY, binding ); /* CHANGES: native ABI */
}

int trap_PC_AddGlobalDefine( char *define ) {
	return NM_SYSCALL( CG_PC_ADD_GLOBAL_DEFINE, define ); /* CHANGES: native ABI */
}

int trap_PC_LoadSource( const char *filename ) {
	return NM_SYSCALL( CG_PC_LOAD_SOURCE, filename ); /* CHANGES: native ABI */
}

int trap_PC_FreeSource( int handle ) {
	return NM_SYSCALL( CG_PC_FREE_SOURCE, handle ); /* CHANGES: native ABI */
}

int trap_PC_ReadToken( int handle, pc_token_t *pc_token ) {
	return NM_SYSCALL( CG_PC_READ_TOKEN, handle, pc_token ); /* CHANGES: native ABI */
}

int trap_PC_SourceFileAndLine( int handle, char *filename, int *line ) {
	return NM_SYSCALL( CG_PC_SOURCE_FILE_AND_LINE, handle, filename, line ); /* CHANGES: native ABI */
}

int trap_PC_UnReadToken( int handle ) {
	return NM_SYSCALL( CG_PC_UNREAD_TOKEN, handle ); /* CHANGES: native ABI */
}

void trap_S_StopBackgroundTrack( void ) {
	NM_SYSCALL( CG_S_STOPBACKGROUNDTRACK ); /* CHANGES: native ABI */
}

int trap_RealTime(qtime_t *qtime) {
	return NM_SYSCALL( CG_REAL_TIME, qtime ); /* CHANGES: native ABI */
}

void trap_SnapVector( float *v ) {
	NM_SYSCALL( CG_SNAPVECTOR, v ); /* CHANGES: native ABI */
}

#if 0
/*
=================
trap_CIN_PlayCinematic

arg0 - the name of the .RoQ file to load.
xpos - the X coordinate on-screen for the left edge of the cinematic display.
ypos - the Y coordinate on-screen for the top edge of the cinematic display.
width - the screen width of the cinematic display.
height - the screen height of the cinematic display.

bits - combinations of the following flags:
# CIN_system
# CIN_loop
# CIN_hold
# CIN_silent
# CIN_shader
OR them together to use multiple. eg. CIN_loop|CIN_silent

This function loads up and begins playing an ROQ file at the coordinates given.
If the CIN_hold flag is specified, the ROQ is loaded in a paused state.
The function returns a handle to the cinematic instance or zero on failure.
=================
*/
int trap_CIN_PlayCinematic( const char *arg0, int xpos, int ypos, int width, int height, int bits) {
	return NM_SYSCALL(CG_CIN_PLAYCINEMATIC, arg0, xpos, ypos, width, height, bits); /* CHANGES: native ABI */
}


/*
=================
trap_CIN_StopCinematic

handle - the handle returned by a previous call to trap_CIN_PlayCinematic

Stops playing the cinematic and ends it. should always return FMV_EOF
cinematics must be stopped in reverse order of when they are started
=================
*/
e_status trap_CIN_StopCinematic(int handle) {
	return NM_SYSCALL(CG_CIN_STOPCINEMATIC, handle); /* CHANGES: native ABI */
}


/*
=================
trap_CIN_RunCinematic

handle a handle returned by a previous call to trap_CIN_PlayCinematic

Will run a frame of the cinematic but will not draw it. Will return FMV_EOF if the end of the cinematic has been reached.
=================
*/
e_status trap_CIN_RunCinematic(int handle) {
	return NM_SYSCALL(CG_CIN_RUNCINEMATIC, handle); /* CHANGES: native ABI */
}


/*
===================
trap_CIN_DrawCinematic

handle - a handle returned by a previous call to trap_CIN_PlayCinematic

This function renders the current frame of the cinematic to the screen with the current position coordinates.
===================
*/
void trap_CIN_DrawCinematic (int handle) {
	NM_SYSCALL(CG_CIN_DRAWCINEMATIC, handle); /* CHANGES: native ABI */
}


/*
===================
trap_CIN_SetExtents

handle - a handle returned by a previous call to trap_CIN_PlayCinematic

x - the new X coordinate on-screen of the left edge of the cinematic.
y - the new Y coordinate on-screen of the top edge of the cinematic.
w - the new width on-screen of the cinematic.
h - the new height on-screen of the cinematic.

Allows you to resize the animation dynamically
===================
*/
void trap_CIN_SetExtents (int handle, int x, int y, int w, int h) {
	NM_SYSCALL(CG_CIN_SETEXTENTS, handle, x, y, w, h); /* CHANGES: native ABI */
}
#endif


qboolean trap_GetEntityToken( char *buffer, int bufferSize ) {
	return NM_SYSCALL( CG_GET_ENTITY_TOKEN, buffer, bufferSize ); /* CHANGES: native ABI */
}

//----(SA)	added
// bring up a popup menu
extern void	Menus_OpenByName(const char *p);

//void trap_UI_Popup( const char *arg0) {
void trap_UI_Popup( int arg0 ) {
	NM_SYSCALL(CG_INGAME_POPUP, arg0); /* CHANGES: native ABI */
}

void trap_UI_ClosePopup( const char *arg0) {
	NM_SYSCALL(CG_INGAME_CLOSEPOPUP, arg0); /* CHANGES: native ABI */
}

void trap_Key_GetBindingBuf( int keynum, char *buf, int buflen ) {
	NM_SYSCALL( CG_KEY_GETBINDINGBUF, keynum, buf, buflen ); /* CHANGES: native ABI */
}

void trap_Key_SetBinding( int keynum, const char *binding ) {
	NM_SYSCALL( CG_KEY_SETBINDING, keynum, binding ); /* CHANGES: native ABI */
}

void trap_Key_KeynumToStringBuf( int keynum, char *buf, int buflen ) {
	NM_SYSCALL( CG_KEY_KEYNUMTOSTRINGBUF, keynum, buf, buflen ); /* CHANGES: native ABI */
}

void trap_TranslateString( const char *string, char *buf ) {
	NM_SYSCALL( CG_TRANSLATE_STRING, string, buf ); /* CHANGES: native ABI */
}
// -NERVE - SMF

// Media register functions
#ifdef _DEBUG

#if 0 // show loading time
#define DEBUG_REGISTERPROFILE_INIT int dbgTime = trap_Milliseconds();
#define DEBUG_REGISTERPROFILE_EXEC(f,n) if( developer.integer ) CG_Printf("%s : loaded %s in %i msec\n", f, n, trap_Milliseconds()-dbgTime );
#else // always show hunkmegs for debug builds
#define DEBUG_REGISTERPROFILE_INIT int size = trap_MemoryRemaining();
#define DEBUG_REGISTERPROFILE_EXEC(f,n) if ( size-trap_MemoryRemaining() > 0 ) CG_WriteToLog("%s : %s %i\n", f, n, size-trap_MemoryRemaining() );
#endif

sfxHandle_t	trap_S_RegisterSound( const char *sample, qboolean compressed ) {
	sfxHandle_t snd;
	DEBUG_REGISTERPROFILE_INIT
	CG_DrawInformation( qtrue );
	snd = NM_SYSCALL( CG_S_REGISTERSOUND, sample, qfalse /* compressed */ ); /* CHANGES: native ABI */
	if(!*sample) {
		Com_Printf("^1Warning: Null Sample filename\n");
	}
	if(snd == 0) {
		Com_Printf("^1Warning: Failed to load sound: %s\n", sample);
	}
	DEBUG_REGISTERPROFILE_EXEC("trap_S_RegisterSound",sample)
	// trap_PumpEventLoop();
	return snd;
}

qhandle_t trap_R_RegisterModel( const char *name ) {
	qhandle_t handle;
	DEBUG_REGISTERPROFILE_INIT
	CG_DrawInformation( qtrue );
	handle = NM_SYSCALL( CG_R_REGISTERMODEL, name ); /* CHANGES: native ABI */
	if (!*name) {
		Com_Printf("^1Warning: Null model filename\n");
	}
	if (handle == 0) {
		Com_Printf("^1Warning: Failed to load model: %s\n", name);
	}
	DEBUG_REGISTERPROFILE_EXEC("trap_R_RegisterModel",name)
	// trap_PumpEventLoop();
	return handle;
}

qhandle_t trap_R_RegisterSkin( const char *name ) {
	qhandle_t handle;
	DEBUG_REGISTERPROFILE_INIT
	CG_DrawInformation( qtrue );
	handle = NM_SYSCALL( CG_R_REGISTERSKIN, name ); /* CHANGES: native ABI */
	if (!*name) {
		Com_Printf("^1Warning: Null skin filename\n");
	}
	if (handle == 0) {
		Com_Printf("^1Warning: Failed to load skin: %s\n", name);
	}
	DEBUG_REGISTERPROFILE_EXEC("trap_R_RegisterSkin",name)
	// trap_PumpEventLoop();
	return handle;
}

qhandle_t trap_R_RegisterShader( const char *name ) {
	qhandle_t handle;
	DEBUG_REGISTERPROFILE_INIT
	CG_DrawInformation( qtrue );
	handle = NM_SYSCALL( CG_R_REGISTERSHADER, name ); /* CHANGES: native ABI */
	if (!*name) {
		Com_Printf("^1Warning: Null shader filename\n");
	}
	if (handle == 0) {
		Com_Printf("^1Warning: Failed to load shader: %s\n", name);
	}
	DEBUG_REGISTERPROFILE_EXEC("trap_R_RegisterShader",name)
	// trap_PumpEventLoop();
	return handle;
}

qhandle_t trap_R_RegisterShaderNoMip( const char *name ) {
	qhandle_t handle;
	DEBUG_REGISTERPROFILE_INIT
	CG_DrawInformation( qtrue );
	handle = NM_SYSCALL( CG_R_REGISTERSHADERNOMIP, name ); /* CHANGES: native ABI */
	if (!*name) {
		Com_Printf("^1Warning: Null shader no mip filename\n");
	}
	if (handle == 0) {
		Com_Printf("^1Warning: Failed to load shader no mip: %s\n", name);
	}
	// trap_PumpEventLoop();
	DEBUG_REGISTERPROFILE_EXEC("trap_R_RegisterShaderNpMip", name);
	return handle;
}

void trap_R_RegisterFont(const char *fontName, int pointSize, fontInfo_t *font) {
	DEBUG_REGISTERPROFILE_INIT
	CG_DrawInformation( qtrue );
	NM_SYSCALL(CG_R_REGISTERFONT, fontName, pointSize, font ); /* CHANGES: native ABI */
	DEBUG_REGISTERPROFILE_EXEC("trap_R_RegisterFont", fontName)
	// trap_PumpEventLoop();
}

void trap_CM_LoadMap( const char *mapname ) {
	DEBUG_REGISTERPROFILE_INIT
	CG_DrawInformation( qtrue );
	NM_SYSCALL( CG_CM_LOADMAP, mapname ); /* CHANGES: native ABI */
	DEBUG_REGISTERPROFILE_EXEC("trap_CM_LoadMap", mapname)
	// trap_PumpEventLoop();
}

void trap_R_LoadWorldMap( const char *mapname ) {
	DEBUG_REGISTERPROFILE_INIT
	CG_DrawInformation( qtrue );
	NM_SYSCALL( CG_R_LOADWORLDMAP, mapname ); /* CHANGES: native ABI */
	DEBUG_REGISTERPROFILE_EXEC("trap_R_LoadWorldMap", mapname)
	// trap_PumpEventLoop();
}
#else
sfxHandle_t	trap_S_RegisterSound( const char *sample, qboolean compressed ) {
	CG_DrawInformation( qtrue );
	// trap_PumpEventLoop();
	// CG_Printf( "trap_S_RegisterSound %s\n", sample );
	return NM_SYSCALL( CG_S_REGISTERSOUND, sample, qfalse /* compressed */ ); /* CHANGES: native ABI */
}

qhandle_t trap_R_RegisterModel( const char *name ) {
	CG_DrawInformation( qtrue );
	// trap_PumpEventLoop();
	//CG_Printf( "trap_S_RegisterModel %s\n", name );
	return NM_SYSCALL( CG_R_REGISTERMODEL, name ); /* CHANGES: native ABI */
}

qhandle_t trap_R_RegisterSkin( const char *name ) {
	CG_DrawInformation( qtrue );
	// trap_PumpEventLoop();
	//CG_Printf( "trap_S_RegisterSkin %s\n", name );
	return NM_SYSCALL( CG_R_REGISTERSKIN, name ); /* CHANGES: native ABI */
}

/*
=================
trap_R_RegisterShader

const char *name: pointer to the name of the shader (?)

This function registers the name shader and gives it a unique qhandle_t.
=================
*/
qhandle_t trap_R_RegisterShader( const char *name ) {
	CG_DrawInformation( qtrue );
	// trap_PumpEventLoop();
	//CG_Printf( "trap_S_RegisterShader %s\n", name );
	return NM_SYSCALL( CG_R_REGISTERSHADER, name ); /* CHANGES: native ABI */
}

qhandle_t trap_R_RegisterShaderNoMip( const char *name ) {
	CG_DrawInformation( qtrue );
	// trap_PumpEventLoop();
	return NM_SYSCALL( CG_R_REGISTERSHADERNOMIP, name ); /* CHANGES: native ABI */
}

void trap_R_RegisterFont(const char *fontName, int pointSize, fontInfo_t *font) {
	CG_DrawInformation( qtrue );
	// trap_PumpEventLoop();
	NM_SYSCALL(CG_R_REGISTERFONT, fontName, pointSize, font ); /* CHANGES: native ABI */
}

void trap_CM_LoadMap( const char *mapname ) {
	CG_DrawInformation( qtrue );
	// trap_PumpEventLoop();
	NM_SYSCALL( CG_CM_LOADMAP, mapname ); /* CHANGES: native ABI */
}

void trap_R_LoadWorldMap( const char *mapname ) {
	CG_DrawInformation( qtrue );
	// trap_PumpEventLoop();
	NM_SYSCALL( CG_R_LOADWORLDMAP, mapname ); /* CHANGES: native ABI */
}
#endif // _DEBUG

qboolean trap_R_inPVS( const vec3_t p1, const vec3_t p2 ) {
	return NM_SYSCALL( CG_R_INPVS, p1, p2 ); /* CHANGES: native ABI */
}

void trap_GetHunkData( int* hunkused, int* hunkexpected ) {
	NM_SYSCALL( CG_GETHUNKDATA, hunkused, hunkexpected ); /* CHANGES: native ABI */
}

qboolean isET260( void ) {
	static char versionStr[256];

	trap_Cvar_VariableStringBuffer( "version", versionStr, 256 );

	if (!Q_stricmp(versionStr, va("ET 2.60 %s Mar 10 2005", CPUSTRING))) {
		return qtrue;
	}

	if (!Q_stricmp(versionStr, va("ET 2.60b %s May  8 2006", CPUSTRING))) {
		return qtrue;
	}
	
#if defined(__MACOS__)
	//if (!Q_stricmp(versionStr, "ET 2.60d OSX-universal JAN 20 2007"))
	if (!Q_stricmp(versionStr, "ET 2.60d")) {// check at least for 2.60d
		return qtrue;
	}
#endif

	return qfalse;
}

// ET 2.60 functions ...

// zinx - binary message channel
void trap_SendMessage( char *buf, int buflen ) {
	NM_SYSCALL( CG_SENDMESSAGE, buf, buflen ); /* CHANGES: native ABI */
}

messageStatus_t trap_MessageStatus( void ) {
	return (messageStatus_t)NM_SYSCALL( CG_MESSAGESTATUS ); /* CHANGES: native ABI */
}

//bani - dynamic shaders
//see TEST_API_DYNAMICSHADER in vanilla code
qboolean trap_R_LoadDynamicShader( const char *shadername, const char *shadertext ) {
	return NM_SYSCALL( CG_R_LOADDYNAMICSHADER, shadername, shadertext ); /* CHANGES: native ABI */
}

// fretn - render to texture
void trap_R_RenderToTexture( int textureid, int x, int y, int w, int h ) {
	NM_SYSCALL( CG_R_RENDERTOTEXTURE, textureid, x, y, w, h ); /* CHANGES: native ABI */
}

int trap_R_GetTextureId( const char *name ) {
	return NM_SYSCALL( CG_R_GETTEXTUREID, name ); /* CHANGES: native ABI */
}

// bani - sync rendering
void trap_R_Finish( void ) {
	NM_SYSCALL( CG_R_FINISH ); /* CHANGES: native ABI */
}
