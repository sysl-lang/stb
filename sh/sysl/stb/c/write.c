/* The one translation unit that compiles stb_image_write's encoders.
 *
 * Its own object, for the reason `image.c` gives. `STBI_WRITE_NO_STDIO` is set in
 * `package.hocon` and compiles out every entry point that names a `FILE` -- writing a file is
 * `sysl.fs`'s job, and the bytes this package hands back are what it wants anyway.
 */

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
