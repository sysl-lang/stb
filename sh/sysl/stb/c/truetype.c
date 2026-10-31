/* The one translation unit that compiles stb_truetype, and the allocation that keeps a font's bytes
 * alive for as long as the font.
 *
 * `STB_TRUETYPE_IMPLEMENTATION` turns the header into its bodies here and nowhere else. It is a
 * unit of its own, like the three image libraries, so a program that never reads a font does not
 * link a font reader.
 */

#include <stdlib.h>
#include <string.h>

/* The shim's header first: it includes stb_truetype's declarations, and the implementation section
 * is not include-guarded, so it has to be reached exactly once -- by the include below. */
#include "truetype_shim.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

/* The info first, so a pointer to the info is a pointer to the allocation. */
typedef struct {
  stbtt_fontinfo info;
  unsigned char bytes[];
} held_font;

stbtt_fontinfo *sysl_stbtt_open(const unsigned char *data, int len, int offset, int *status) {
  held_font *f;

  if (len < 0) {
    *status = SYSL_STBTT_REFUSED;
    return NULL;
  }

  f = (held_font *)malloc(sizeof(held_font) + (size_t)len);
  if (f == NULL) {
    *status = SYSL_STBTT_NO_MEMORY;
    return NULL;
  }

  /* `stbtt_InitFont` never writes `userdata`, and passes it to every allocation it makes, so it is
   * zeroed here rather than left as whatever `malloc` found. */
  memset(&f->info, 0, sizeof f->info);
  memcpy(f->bytes, data, (size_t)len);

  if (!stbtt_InitFont(&f->info, f->bytes, offset)) {
    free(f);
    *status = SYSL_STBTT_REFUSED;
    return NULL;
  }

  *status = SYSL_STBTT_OK;
  return &f->info;
}

void sysl_stbtt_close(stbtt_fontinfo *info) {
  free(info);
}
