/* What `shim.c` answers, from the other side.
 *
 * `c.sysl` reads these declarations so that the sysl `extern`s beside them are checked against the
 * same text the shim was compiled from, and reads the status constants below rather than
 * transcribing a 0 and a 1.
 */

#ifndef SYSL_STB_SHIM_H
#define SYSL_STB_SHIM_H

/* What an encode answered. A pointer and a length come back through out-parameters, so the return
 * value is free to say which of the two things went wrong -- and they are genuinely different: one
 * is an image stb will not encode, the other is a machine that ran out of memory part way through.
 */
#define SYSL_STB_OK 0
#define SYSL_STB_REFUSED 1
#define SYSL_STB_NO_MEMORY 2

/* The five encoders, each collecting into one `malloc`'d buffer.
 *
 * stb_image_write publishes a `_to_mem` for PNG alone; every other format is reachable only through
 * a `stbi_write_func` callback that is handed one chunk at a time. Rather than bind two shapes,
 * all five go through the same growing collector here, so a caller sees one shape and PNG is not
 * special. `*out` is `malloc`'d storage the caller releases with `sysl_stb_free`.
 *
 * `stride` is the distance between rows in bytes; 0 means the rows are packed, which is `w * comp`.
 */
int sysl_stb_write_png(const unsigned char *pixels, int w, int h, int comp, int stride,
                       unsigned char **out, int *len);
int sysl_stb_write_jpg(const unsigned char *pixels, int w, int h, int comp, int quality,
                       unsigned char **out, int *len);
int sysl_stb_write_bmp(const unsigned char *pixels, int w, int h, int comp,
                       unsigned char **out, int *len);
int sysl_stb_write_tga(const unsigned char *pixels, int w, int h, int comp,
                       unsigned char **out, int *len);
/* **There is deliberately no HDR writer here, and it is not an oversight.** stb wraps its entire
 * Radiance writer -- `stbi_write_hdr_to_func` included, not merely the file variant -- in
 * `#ifndef STBI_WRITE_NO_STDIO`, because the one line that emits the header uses `sprintf`. The
 * header says so itself: "also disable HDR writing, because it requires stdio for formatted
 * output". Keeping the writer would mean giving this package `<stdio.h>`, and with it the file
 * entry points and the capability claim they cost. Reading a Radiance image is unaffected --
 * `stbi_loadf_from_memory` is in the other library and has no such guard.
 */

/* Releases what an encoder above handed back.
 *
 * This is `free`, named rather than bound directly, so that the encoders and the thing that
 * releases them are the same file's decision. A decode is released by `stbi_image_free` instead,
 * which is stb's own and is bound as itself -- the two are the same call today and are not this
 * package's to keep in step.
 */
void sysl_stb_free(void *p);

#endif /* SYSL_STB_SHIM_H */
