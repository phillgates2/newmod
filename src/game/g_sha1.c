/* NewMod, Arena.ai, 2026-11-21: changes in this file; see CHANGES. */
/*
 * Used for Lua
 *
 * This code is taken from ETPub. All credits go to their team especially quad and pheno!
 * http://etpub.org
 *
 * Find the ETPub Lua code by doing a text seach for "*LUA*"
 *
 * IRATA [*] 03/2009
 *
*/
#include "g_local.h"

#include "../sha-1/sha1.h"

/* NewMod, Arena.ai, 2026-11-21 (see CHANGES): an explicit byte count
 * prevents binary Lua chunks sharing a truncated, NUL-terminated digest. */
char *G_SHA1Buffer(const void *data, unsigned int length)
{
	SHA1Context sha;

	SHA1Reset(&sha);
	SHA1Input(&sha, (const unsigned char *)data, length);

	if ( !SHA1Result(&sha) ) {
        G_Error("sha1: could not compute message digest");
		return "";
    } else {
		return va("%08X%08X%08X%08X%08X",
			sha.Message_Digest[0],
			sha.Message_Digest[1],
			sha.Message_Digest[2],
			sha.Message_Digest[3],
			sha.Message_Digest[4]);
	}
}

/* Preserve the inherited string helper for any other native callers. */
char *G_SHA1(char *string)
{
	return G_SHA1Buffer(string, (unsigned int)strlen(string));
}
