/* What `truetype.c` adds to stb_truetype, from the other side.
 *
 * `c.sysl` reads this so that the sysl `extern`s are checked against the text the shim was compiled
 * from, and so that the statuses below are asked for by name rather than transcribed.
 */

#ifndef SYSL_STBTT_SHIM_H
#define SYSL_STBTT_SHIM_H

#include "stb_truetype.h"

#define SYSL_STBTT_OK 0
#define SYSL_STBTT_REFUSED 1
#define SYSL_STBTT_NO_MEMORY 2

/* A font and the bytes it reads, in one allocation.
 *
 * `stbtt_InitFont` keeps a pointer to the font's bytes and reads through it on every later call, so
 * the bytes have to outlive the `stbtt_fontinfo` -- and nothing in C says so. This copies `len` bytes
 * from `data` into storage placed directly behind the `stbtt_fontinfo` and initialises the info over
 * that copy, so the two are born together and freed together, and the caller's slice may go the
 * moment this returns.
 *
 * Answers the info, with `*status` `SYSL_STBTT_OK`; or null, with `*status` saying why. The pointer is
 * the allocation itself, so `sysl_stbtt_close` frees it whole.
 */
stbtt_fontinfo *sysl_stbtt_open(const unsigned char *data, int len, int offset, int *status);

/* Releases what `sysl_stbtt_open` answered: the info and the bytes behind it. */
void sysl_stbtt_close(stbtt_fontinfo *info);

#endif /* SYSL_STBTT_SHIM_H */
