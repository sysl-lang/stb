/* The one thing sysl cannot reach in stb_image_write, and the reason it cannot.
 *
 * stb writes an encoding by calling a `stbi_write_func` with one chunk at a time. That callback
 * takes only pointers and an `int`, so sysl could take its address perfectly well -- what it could
 * not do is be the *growing buffer*, because the chunks arrive during a call the caller is already
 * inside and the final size is not known until the last one has landed. Collecting them here costs
 * one `realloc` loop and buys a caller a single slice.
 *
 * PNG is the one format stb also publishes a `_to_mem` for. It goes through the collector anyway:
 * two shapes for five formats would make PNG special in the binding above for a reason that is an
 * accident of which entry points upstream chose to publish.
 */

#include <stdlib.h>
#include <string.h>

#include "stb_image_write.h"
#include "shim.h"

/* Where the chunks accumulate. `failed` is checked at the top of every append rather than
 * propagated, because a `stbi_write_func` returns nothing -- there is no way to tell stb to stop,
 * so the remaining chunks are dropped on the floor and the failure is reported once the encoder
 * has run to the end of whatever it was doing.
 */
typedef struct {
  unsigned char *data;
  int len;
  int cap;
  int failed;
} collector;

static void collect(void *context, void *data, int size) {
  collector *c = (collector *)context;

  if (c->failed || size <= 0) {
    return;
  }

  if (size > (2147483647 - c->len)) { /* the length is reported as an `int`, so it must fit in one */
    c->failed = 1;
    return;
  }

  if (c->len + size > c->cap) {
    int want = c->cap ? c->cap : 4096;
    unsigned char *grown;

    while (want < c->len + size) {
      if (want > 2147483647 / 2) {
        want = c->len + size;
        break;
      }
      want *= 2;
    }

    grown = (unsigned char *)realloc(c->data, (size_t)want);

    if (!grown) {
      c->failed = 1;
      return;
    }

    c->data = grown;
    c->cap = want;
  }

  memcpy(c->data + c->len, data, (size_t)size);
  c->len += size;
}

/* Turns a collector and stb's own `int` verdict into the three statuses `shim.h` names.
 *
 * The order matters: a collector that failed to grow is out of memory whatever stb thought, since
 * stb reports success for an encode whose chunks were all handed over and silently dropped.
 */
static int finish(collector *c, int ok, unsigned char **out, int *len) {
  if (c->failed) {
    free(c->data);
    return SYSL_STB_NO_MEMORY;
  }

  if (!ok || !c->data) {
    free(c->data);
    return SYSL_STB_REFUSED;
  }

  *out = c->data;
  *len = c->len;
  return SYSL_STB_OK;
}

int sysl_stb_write_png(const unsigned char *pixels, int w, int h, int comp, int stride,
                       unsigned char **out, int *len) {
  collector c = {0, 0, 0, 0};
  int ok = stbi_write_png_to_func(collect, &c, w, h, comp, pixels, stride);
  return finish(&c, ok, out, len);
}

int sysl_stb_write_jpg(const unsigned char *pixels, int w, int h, int comp, int quality,
                       unsigned char **out, int *len) {
  collector c = {0, 0, 0, 0};
  int ok = stbi_write_jpg_to_func(collect, &c, w, h, comp, pixels, quality);
  return finish(&c, ok, out, len);
}

int sysl_stb_write_bmp(const unsigned char *pixels, int w, int h, int comp,
                       unsigned char **out, int *len) {
  collector c = {0, 0, 0, 0};
  int ok = stbi_write_bmp_to_func(collect, &c, w, h, comp, pixels);
  return finish(&c, ok, out, len);
}

int sysl_stb_write_tga(const unsigned char *pixels, int w, int h, int comp,
                       unsigned char **out, int *len) {
  collector c = {0, 0, 0, 0};
  int ok = stbi_write_tga_to_func(collect, &c, w, h, comp, pixels);
  return finish(&c, ok, out, len);
}


void sysl_stb_free(void *p) { free(p); }
