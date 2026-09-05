/* The one translation unit that compiles stb_image_resize2.
 *
 * Its own object, for the reason `image.c` gives: this is the largest of the three by a wide
 * margin, and a program that decodes and writes without resizing should not be linking it.
 *
 * No options are set for it anywhere. stb_image_resize2 picks its vector path from the target the
 * compiler was given -- NEON on aarch64, SSE2 or AVX2 on x86 -- and there is nothing here that
 * would know better than the target does.
 */

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"
