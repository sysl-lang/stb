# stb

Image decoding, encoding and resizing for [sysl](https://github.com/sysl-lang/sysl), bound to
[Sean Barrett's stb libraries](https://github.com/nothings/stb) — `stb_image`, `stb_image_write` and
`stb_image_resize2`.

Nine formats in, four out, and a resampler between them:

| | |
|---|---|
| **read** | PNG, JPEG, BMP, TGA, PSD, GIF (first frame), HDR, PIC, PNM |
| **write** | PNG, JPEG, BMP, TGA |
| **resize** | 8-bit and floating point, sRGB or linear, seventeen channel layouts |

**All three are vendored**, because that is how stb is distributed: there is no library to install
and never was, and the headers are in this repository. That is the same judgement `miniz`, `qoi` and
`monocypher` made, and it is checkable rather than a matter of taste — see *Vendoring* below.

```
sh/sysl/stb/
    stb.sysl            the binding
    tests.sysl          22 tests, most of them against fixtures ImageMagick wrote
    c/
        c.sysl               stb as C declares it, and the numbers its headers compute
        image.c              one line: STB_IMAGE_IMPLEMENTATION
        write.c              one line: STB_IMAGE_WRITE_IMPLEMENTATION
        resize.c             one line: STB_IMAGE_RESIZE_IMPLEMENTATION
        shim.c               the growing buffer an encode collects into
        shim.h               the three statuses the shim answers, so both languages can name them
        stb_image.h          vendored, unmodified — nothings/stb, MIT / public domain
        stb_image_write.h    vendored, unmodified
        stb_image_resize2.h  vendored, unmodified
package.hocon           who this package is, what it needs, and the compile-time options
```

The module is **`sh.sysl.stb`**, and the directories are that name. **Everything that is C lives in
`c/`**, module `sh.sysl.stb.c`: the two-layer shape every binding in this organisation uses, where
the raw layer has to be *faithful* and the layer above it has to be *pleasant*. A program imports
`sh.sysl.stb`; one that wants a corner this binding has not wrapped may import `sh.sysl.stb.c` too.

**Three libraries and one package**, which is the right count here. The org's rule is that the test
is the package manager rather than the linker — SDL3's companions are four packages because they are
four *formulae*, and a machine may genuinely not have the one an unpruned `-lSDL3_ttf` names. Nothing
is linked here at all, so there is no second thing for a consumer to go and get, and splitting would
charge three coordinates and three versions to keep in step for nothing. What splitting would have
bought is bought another way: each library is compiled in **its own translation unit**, so a program
that only decodes does not link the other two. That matters most for the resizer, which is sixty
thousand lines of vector code.

## Using it

```hocon
dependencies {
  stb { git = "github.com/sysl-lang/stb", version = "0.1.0" }
}
```

The coordinate is an identity rather than a URL, so it carries no `https://`, and `version` is the
tag `v0.1.0` here. Nothing has to be installed and no flags are needed — `sysl build` and
`sysl test .` both work from a clean clone.

## The surface

```sysl
import sh.sysl.stb.{decode, encode_png, encode_jpg, resize, probe, Channels, Layout}

val bytes = read_file("photo.jpg")?

// What is it, without decoding it?
val shape = probe(bytes)?              // Shape { width, height, channels }

// Decode. The format is recognised from the bytes; there is nothing to tell it what to expect.
val img = decode(bytes, Channels.Rgba)?
print(f"${img.width}%d x ${img.height}%d, ${img.channels}%d channels")

// Scale it into storage you already have.
var small: []u8 = [0; 320 * 240 * 4]
resize(img.pixels(), img.width, img.height, small, 320, 240, Layout.Rgba)?

// Encode. `png.bytes()` is a whole PNG file.
val png = encode_png(small, 320, 240, 4)?
write_file("thumb.png", png.bytes())?
```

`decode_16` and `decode_f` are the same call at sixteen bits a channel and at floating point;
`encode_bmp`, `encode_tga` and `encode_jpg` are the other three writers.

## The one thing worth understanding: who owns the pixels

**A decode and an encode each answer a boxed handle, not a slice.** stb allocates the storage, so
something has to release it, and that something is a sysl destructor:

```sysl
decode(bytes)      -> Result[&Image, Error]     // Drop calls stbi_image_free
encode_png(...)    -> Result[&Encoded, Error]   // Drop calls the shim's free
```

**The `&` is load-bearing and is not a style choice.** A `Drop` fires when a *box's* strong count
reaches zero and for nothing else, so a constructor answering a plain `Image` would hand back a
copy whose destructor never runs — and the binding would leak every image it ever read while passing
every test, because the decoding is correct the whole time. The org's own file records three
packages that shipped with exactly that defect, one of them leaking 26 GB over 50,000 rounds.

So it is measured here rather than asserted. Decoding, encoding and resizing 4×3 pixels in a loop:

| rounds | peak resident set |
|---|---|
| 2,000 | 2.13 MB |
| 200,000 | 2.54 MB |

A hundredfold increase in work for a 19% increase in high-water mark, which is allocator noise. **No
test could show this** — every operation is correct whether or not anything is freed — which is why
the measurement is in the README rather than the suite.

`pixels()` and `bytes()` are *views* into that storage, so they live exactly as long as the handle
does. Keeping the bytes past it means copying them.

**Resizing owns nothing**, because stb writes into the output slice the caller already has. That
half of this package allocates nothing at all.

## Capabilities

```hocon
requires {
  heap = true
}
```

**`heap` and nothing else, and both halves took a decision.**

`heap` is here because a decode and an encode both answer storage whose size nobody knew in advance.
`qoi` is the package that took the other road — it redirects QOI's allocator at a slice the caller
already owns and asks for no capabilities at all — and **that road was considered here and is not
available**. `qoi_encode` computes the size it needs *before* it asks for it and makes exactly one
allocation; stb's decoders do neither. A PNG decode allocates its output and then grows a zlib
window by `realloc` as it goes, and no bound on the second is knowable from the header. A
caller-supplied arena would have had to be sized by guesswork and would have failed on real images.

**`posix` is absent, and `STBI_NO_STDIO` is what earns it.** stb's file entry points are the only
things in any of the three headers that touch an operating system, and they are compiled out.
Reading a file is `sysl.fs`'s job and the bytes it hands back are what `decode` wants anyway.

### What that costs: no HDR *writing*

stb guards its **entire** Radiance writer behind `STBI_WRITE_NO_STDIO` — the callback form
`stbi_write_hdr_to_func` included, not merely the file form — because the one line that emits the
header uses `sprintf`. Its own documentation says so: *"also disable HDR writing, because it requires
stdio for formatted output."*

So `encode_hdr` is not in this binding. Keeping it would have meant giving the package `<stdio.h>`
and the file entry points that come with it, and the capability claim above is worth more. **Reading
a `.hdr` is unaffected** — `stbi_loadf_from_memory` is in the other library and carries no such
guard — so `decode_f` works and a Radiance image simply cannot be written back.

Say so if you want it; the trade is `posix = true` and four more entry points.

## Vendoring

The org's rule is that vendoring is a claim about where the code can *run*, not a way to skip an
install step, and it is checkable in one command. stb passes: it is portable C reaching for
`stdlib.h`, `string.h`, `math.h` and `assert.h` and no further, and with a libc — newlib on a Pico,
Zephyr's, FreeRTOS's — it builds for a board. There is also nothing to install: stb is *distributed*
as headers you copy, so a `pkg_config` route does not exist to be taken.

`@link("m")` is the one external name, for the `pow` and `ldexp` the HDR and JPEG paths use. On
macOS that is in libSystem already; on Linux it is `-lm`.

Upstream versions carried here, so a refresh is mechanical:

| file | version | SHA-256 |
|---|---|---|
| `stb_image.h` | v2.30 | `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3` |
| `stb_image_write.h` | v1.16 | `cbd5f0ad7a9cf4468affb36354a1d2338034f2c12473cf1a8e32053cb6914a05` |
| `stb_image_resize2.h` | v2.18 | `173e654634f6ccaad98f603e686ea212eec1fe8ea6d2a5e5e8056efa10ae3880` |

Fetched from `raw.githubusercontent.com/nothings/stb/master` on 2026-09-04, unmodified.

## Testing

```
sysl test .
```

**22 passed, 0 failed.**

### The fixtures are an oracle, and that is what makes the suite worth having

A decoder tested only against its own encoder proves that the two agree — which they would even if
both were wrong about the format. So the four fixtures embedded in `tests.sysl` were **not** made by
stb. Twelve pixels were chosen, written out as a raw PPM, and encoded by **ImageMagick**: a different
implementation of all four formats, by different people. A test that decodes one and compares
against those twelve pixels is checking stb against somebody else's idea of what a PNG is.

It earned its place immediately: ImageMagick wrote the PNG as a **4-bit palette** image rather than
the 8-bit truecolour one a round trip would have produced, so the indexed path through stb's PNG
decoder is covered by the accident of using a real encoder — and would not have been covered at all
by a fixture this package made itself.

Two more tests do not trust the decoder either. `what encode_png writes is a PNG by the
specification` reads the signature, the IHDR length and type, and the big-endian width and height
out of the bytes by hand, because a round trip through stb's own reader would pass even if the
writer emitted something only stb understands.

### Two things the suite pins that were measured, not assumed

**stb reads a PNG whose IEND checksum was cut off.** Cuts of one to four bytes decode; a cut of five
is refused. Four is exactly the length of the IEND CRC, so what this records is that stb does not
verify it — a file truncated in transit by a few bytes decodes silently. It is pinned because
somebody will meet it and because an upstream tightening would change it without saying so.

**Radiance carries pure red, green, blue and white exactly.** The first four pixels come back as
`1.0, 0.0, 0.0` and so on, not approximately — so the HDR test asserts equality rather than a
tolerance, which a decoder that mixed up the shared exponent or the channel order could not pass by
accident.

### AddressSanitizer

Every line of C here is vendored, so `SYSL_EXTRA_CFLAGS` reaches all of it — this is the case where
the sanitizer is worth the most:

```
SYSL_EXTRA_CFLAGS="-fsanitize=address -g" sysl test .
```

**22 passed, 0 failed, instrumentation confirmed** — 36 `__asan_*` undefined symbols in the test
binary including `__asan_memcpy`, which is the tell that the *functions* were instrumented and not
merely that the runtime was linked.

> **A green ASan run over an unchanged tree proves nothing.** `sysl test` caches its built artifact
> and the cache is not keyed on `SYSL_EXTRA_CFLAGS`, so re-running with the flag on a tree that has
> not changed silently reuses the uninstrumented binary and reports green. It did exactly that here
> the first time. Check `nm -u <binary> | grep -c asan` before believing a run; a real one is also
> about 3× the size and 40× slower.

## Licence

The three vendored headers are Sean Barrett's, dual-licensed MIT and public domain. Everything else
here is ISC. `LICENSE` carries both.
