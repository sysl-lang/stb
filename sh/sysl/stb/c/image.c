/* The one translation unit that compiles stb_image's decoders.
 *
 * `STB_IMAGE_IMPLEMENTATION` is the whole mechanism of a single-header library: the header is
 * declarations everywhere it is included and a body in exactly one place, and this file is that
 * place. It is kept apart from the writer and the resizer so that each becomes an object of its
 * own -- a consumer that only decodes then links neither of the other two, and the resizer alone
 * is sixty thousand lines of vector code nobody who is not resizing should carry.
 *
 * The compile-time options are in `package.hocon`'s `defines` block rather than here, because they
 * are configuration a reader may want to change and this file is not where anybody would look for
 * them. The one exception is the implementation macro above, which is not an option at all.
 */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
