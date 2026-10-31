# stb

Image decoding, encoding and resizing, and font outlines, for [sysl](https://github.com/sysl-lang/sysl),
bound to [Sean Barrett's stb libraries](https://github.com/nothings/stb) — `stb_image`,
`stb_image_write`, `stb_image_resize2` and `stb_truetype`.

*An independent sysl package for stb. It is not affiliated with or endorsed by stb's authors or trademark holders.*

Nine image formats in, four out, a resampler between them, and text turned into geometry:

| | |
|---|---|
| **read** | PNG, JPEG, BMP, TGA, PSD, GIF (first frame), HDR, PIC, PNM |
| **write** | PNG, JPEG, BMP, TGA |
| **resize** | 8-bit and floating point, sRGB or linear, seventeen channel layouts |
| **fonts** | TrueType and CFF (`.ttf`, `.otf`, `.ttc`): metrics, kerning, glyph outlines as path commands or flattened polygons, whole strings laid out — module `sh.sysl.stb.truetype` |

**All four are vendored**, because that is how stb is distributed: there is no library to install
and never was, and the headers are in this repository. That is the same judgement `miniz`, `qoi` and
`monocypher` made, and it is checkable rather than a matter of taste — see *Vendoring* below.

```
sh/sysl/stb/
    stb.sysl            the image binding
    tests.sysl          22 tests, most of them against fixtures ImageMagick wrote
    truetype/
        truetype.sysl        the font binding, module sh.sysl.stb.truetype
        check.sysl           everything stb_truetype will read, checked before it may read it
        tests.sysl           55 tests against FreeType and fontTools
        fonts/               Inter cut to ASCII (OFL.txt beside it), the scripts that made the
                             fixtures and the oracle, and oracle.txt
    c/
        c.sysl               stb as C declares it, and the numbers its headers compute
        image.c              one line: STB_IMAGE_IMPLEMENTATION
        write.c              one line: STB_IMAGE_WRITE_IMPLEMENTATION
        resize.c             one line: STB_IMAGE_RESIZE_IMPLEMENTATION
        truetype.c           STB_TRUETYPE_IMPLEMENTATION, and one allocation for a font and its bytes
        truetype_shim.h      what truetype.c adds, so both languages can name it
        shim.c               the growing buffer an encode collects into
        shim.h               the three statuses the shim answers, so both languages can name them
        stb_image.h          vendored, unmodified — nothings/stb, MIT / public domain
        stb_image_write.h    vendored, unmodified
        stb_image_resize2.h  vendored, unmodified
        stb_truetype.h       vendored, unmodified
sysl.hocon              who this package is, what it needs, and the compile-time options
```

The module is **`sh.sysl.stb`**, and the directories are that name. **Everything that is C lives in
`c/`**, module `sh.sysl.stb.c`: the two-layer shape every binding in this organisation uses, where
the raw layer has to be *faithful* and the layer above it has to be *pleasant*. A program imports
`sh.sysl.stb`; one that wants a corner this binding has not wrapped may import `sh.sysl.stb.c` too.

**Four libraries and one package**, which is the right count here. The org's rule is that the test
is the package manager rather than the linker — SDL3's companions are four packages because they are
four *formulae*, and a machine may genuinely not have the one an unpruned `-lSDL3_ttf` names. Nothing
is linked here at all, so there is no second thing for a consumer to go and get, and splitting would
charge four coordinates and four versions to keep in step for nothing. What splitting would have
bought is bought another way: each library is compiled in **its own translation unit**, so a program
that only decodes does not link the other three. That matters most for the resizer, which is sixty
thousand lines of vector code.

## Using it

```hocon
dependencies {
  stb { git = "github.com/sysl-lang/stb", version = "0.2.0" }
}
```

The coordinate is an identity rather than a URL, so it carries no `https://`, and `version` is the
tag `v0.2.0` here. Nothing has to be installed and no flags are needed — `sysl build` and
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

## Fonts

`sh.sysl.stb.truetype` reads TrueType and CFF fonts and collections, and is shaped for turning text
into geometry — glyph outlines to fill, mesh or extrude:

```sysl
import sh.sysl.stb.truetype.{load_font, text_outline, signed_area}

val font = load_font(read_file("Inter.ttf")?)?      // index = 1 picks a collection's second font

// A whole string, laid out with advances and kerning, as closed polygons, y up, baseline at 0.
val text = text_outline(font, "Hello", 10.0)?        // 10 = ascent minus descent, in your units
for contour in text.contours.view()
    print(contour.len(), signed_area(contour.view()))

// One glyph at a time, in font units.
val g = font.glyph('o')                              // 0 (.notdef) for a character the font lacks
val m = font.hmetrics(g)?                            // advance, left side bearing
val cmds = font.shape(g)?                            // Buf of MoveTo / LineTo / QuadTo / CubicTo
val polys = font.outline(g, 0.5)?                    // flattened, every edge within 0.5 of the curve
```

| | |
|---|---|
| font | `font_count`, `font_offset`, `load_font(bytes, index)`, `glyph_count`, `units_per_em`, `outlines` (`TrueType` or `Cff`) |
| scale | `scale_for_pixel_height` (ascent − descent = pixels), `scale_for_em` (one em = pixels) |
| metrics | `vmetrics` (`hhea`), `typo_vmetrics` (`OS/2`), `bounding_box`, `hmetrics`, `glyph_box`, `is_empty` |
| kerning | `kern(glyph, glyph)`, `kern_chars(char, char)` — GPOS, or the `kern` table where there is no GPOS; `kerning_table` lists the `kern` table's pairs |
| outlines | `shape` (path commands, font units), `outline` (polygons at a tolerance and scale), `signed_area`, `can_draw` |
| text | `layout` (each glyph and its origin), `text_outline` (every contour, the width, the line count); `\n` starts a new line |

### The font owns its bytes

stb's font record points into the font's bytes and reads through that pointer on every later call,
so in C the bytes must outlive the record and nothing says so. **`load_font` copies the bytes into the
same allocation as the record** — `truetype.c` places them directly behind it — so the two are born
and freed together, the slice handed in may be dropped or overwritten the moment `load_font`
returns, and there is no way to hold a `Font` whose bytes are gone. `Font` is a `Drop` type and
`load_font` answers `&Font`. A test overwrites and clears the source buffer after loading and reads
the same outlines back.

### Which way the contours wind, and which fill rule to use

Contours come back in the font's own order and direction, so holes are holes:

| outlines | outer contour (y up) | hole |
|---|---|---|
| TrueType (`glyf`, most `.ttf`) | clockwise, negative `signed_area` | anticlockwise, positive |
| CFF (`CFF `, most `.otf`) | anticlockwise, positive | clockwise, negative |

**Fill with non-zero, for both.** Both formats specify the non-zero rule, and non-zero gives the right
answer whichever way a font winds. Even-odd agrees on a well-made glyph but differs wherever two
contours overlap — common in fonts cut from variable masters. A rule that needs a particular
direction (Manifold's `Positive`, say) is right for one kind of font and empty for the other; check
`font.outlines`, or reverse contours whose `signed_area` has the wrong sign. The tests pin `o` and `A`
with exactly one hole each, in both kinds of font, with the signs above.

### stb trusts the font; this binding does not

stb is handed a pointer and never a length, follows every offset inside the font without a bounds
check, and **asserts — ending the program — on several kinds of malformed font**: a `cmap` subtable
in format 2, a composite glyph placed by matching points, a CFF INDEX lookup past its count, a CID
glyph with no font dict, a real number where it wants an integer. Its own header says not to use it on
untrusted fonts.

So `load_font` first walks the same structure stb walks, with the length in hand (`check.sysl`):
the table directory; the fixed-size tables; `hmtx` against `maxp`; the `cmap` subtable stb will
choose and every lookup its binary search can make; every glyph in `loca`/`glyf`, flag by flag;
every CFF INDEX, DICT, Private DICT and FDSelect; the `kern` table and every GPOS pair-adjustment
subtable stb searches. A font whose structure is broken is refused with `Malformed` and a reason. **A
glyph broken inside itself does not refuse the font** — real fonts carry the odd broken glyph (macOS's
Times has seven, ZapfDingbats three) — that glyph alone, and any composite built from it, answers
`Malformed` from `shape`, `outline` and `glyph_box`, and `can_draw` says so in advance. Every glyph
index is range-checked, because stb reads `hmtx` at an offset computed from the index without asking.

What that buys was measured, not assumed:

- **Under AddressSanitizer, 60,000 randomly corrupted fonts** (TrueType, CFF and `kern`-table
  fixtures, one to eight bytes flipped each, about 90% of them still loading) were driven through
  every reader for every glyph with no report. Each check has a test that breaks the font in exactly
  the way it guards against, and the mutation pass disabled each check in turn to see that test go
  red; with the `cmap` format or composite check disabled, the suite's own flipped-bytes test reaches
  stb's assert within its 600 rounds.
- **Every font on this Mac** was loaded and every drawable glyph's outline read, under
  AddressSanitizer, with no report: 787 faces under `/System/Library/Fonts` and `/Library/Fonts`, of
  which 771 load. The 16 refused are each something stb cannot read either: SFIndia's nine faces
  have neither `glyf` nor `CFF ` outlines, four symbol fonts have no Unicode `cmap` (Webdings,
  Wingdings 1–3), one has no `head` table, and two faces of Shree714 carry `cmap` search fields that
  would send stb's binary search past its segments.

A font that passes can still draw nonsense — a glyph's coordinates are whatever the bytes say — but
nothing stb does with it reads outside the copy it was given.

### Kerning: what stb's GPOS reading leaves out

stb reads GPOS pair adjustment (lookup type 2, formats 1 and 2, X advance only) and the legacy `kern`
table's first format-0 subtable. **It does not read Extension lookups (type 9), and it stops at the
first lookup that covers the left glyph instead of adding every lookup up.** Many modern fonts keep
their kerning in Extension lookups — Inter, the fixture here, keeps most of it in one — so for them
`kern` answers only part of what a shaper would. Of the 1,048 kerned ASCII pairs in the fixture,
fontTools' full reading of GPOS and stb agree on the 142 whose kerning comes only from the lookup stb
reads and differ on the other 906; the suite checks stb against fontTools on every pair as stb's
reading defines it, and carries the full comparison as an ignored test. The legacy
`kern` table is read completely: all 1,048 pairs of the `kern`-table fixture match FreeType.

### What is deliberately absent, and why

- **Bitmaps.** stb's rasteriser asserts on some outlines a malformed font can produce
  (`stb_truetype.h:3221` and `:3350`) even with every edge inside the bitmap — fuzzing under
  AddressSanitizer reached both. Nothing else in this module can end the program on bad bytes, and a
  bitmap function that could would undo that. The polygons `outline` answers are what a rasteriser of
  your choosing fills. **SDFs, texture-atlas packing and baking** (`GetGlyphSDF`, `PackFontRanges`,
  `BakeFontBitmap`) are built on the same rasteriser.
- **Font names** (`GetFontNameString`, `FindMatchingFont`) answer UTF-16BE pointers into the font and
  are not needed to draw text. **SVG glyphs** are colour-emoji documents, not outlines.
- **Subpixel positioning and the codepoint forms** of each glyph function: `glyph(char)` then the glyph
  form is one way to do each, and `kern_chars` is kept because kerning by character is the common
  question.
- **Shaping.** `layout` places one glyph per character, left to right, with GPOS/`kern` pair
  adjustment — no ligatures, no contextual forms, no bidirectional text. That is a shaper's job
  (HarfBuzz), and stb is not one.

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

**The font half needs `heap` too**, for the one block that holds stb's record and the font's bytes
together, and for the outlines it answers. `stb_truetype` itself reads no files.

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
| `stb_truetype.h` | v1.26 | `ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab` |

All four are nothings/stb at commit `2c980bb59875b0d32144a71867fbdebb2f77cd20` — `master` on
2026-09-04, when the first three were fetched; `stb_truetype.h` was taken from that same commit
later, and is unchanged on `master` since v1.26 in any case. Unmodified.

## Testing

```
sysl test .
```

**76 passed, 0 failed, 1 ignored** — 22 for images, 54 for fonts, and the ignored one is stb's
partial GPOS reading (*Kerning* above).

### The font tests' expected values are FreeType's and fontTools', never stb's

`fonts/oracle.txt` was written by `fonts/oracle.py`, which never calls stb. Every glyph outline and
glyph box is **FreeType**'s (loaded unscaled and unhinted, so in font units); the cmap, metrics and
GPOS kerning are **fontTools**' reading of the tables; legacy `kern` pairs are FreeType's
`FT_Get_Kerning`; and every glyph's area is fontTools' `AreaPen`, which integrates the curves exactly.
Every printable ASCII glyph is compared, in a TrueType and a CFF build of the same font: its glyph
index, advance, bearing and box, its outline segment for segment (within one font unit, the rounding
of an implied on-curve midpoint), and the signed area of its flattened polygons (within a strip one
tolerance wide along the outline).

The fixtures are [Inter](https://github.com/rsms/inter) 4.1 Regular (SIL Open Font License 1.1, no
Reserved Font Name; `fonts/OFL.txt`), cut to U+0020–U+007E by `fonts/make_fixtures.py` with
fontTools: `inter-ascii.ttf` (TrueType outlines), `inter-ascii.otf` (CFF), `inter-ascii-kern.ttf`
(the TrueType one with its GPOS kerning rewritten as a legacy `kern` table, so stb's other kerning
path has a fixture) and `inter-ascii.ttc` (the first two as a collection). Seven of the TrueType
glyphs are composites (`i`, `j`, `"`, `,`, `:`, `;`, `` ` ``), so stb's composite path is compared
too.

The oracle proved it could disagree the first time it ran: every `kern` pair failed, because
freetype-py's `get_kerning` maps character codes to glyphs itself and the script had already done
so. stb was right; the script was fixed.

### The image fixtures are an oracle too

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

**76 passed, 0 failed, 1 ignored, `instrumented: address (1 binary)`** — sysl confirms the
instrumentation itself. The font tests are where it earns its keep: the flipped-bytes test drives 600
corrupted fonts through every reader, and a longer run of the same idea (60,000 fonts) and a run over
every font on the machine are described under *Fonts* above. The same fuzzing is what reaches the
rasteriser asserts that keep bitmaps out of this binding.

> **A green ASan run over an unchanged tree proves nothing.** `sysl test` caches its built artifact
> and the cache is not keyed on `SYSL_EXTRA_CFLAGS`, so re-running with the flag on a tree that has
> not changed silently reuses the uninstrumented binary and reports green. It did exactly that here
> the first time. Check `nm -u <binary> | grep -c asan` before believing a run; a real one is also
> about 3× the size and 40× slower.

## Licence

The four vendored headers are Sean Barrett's, dual-licensed MIT and public domain. The test fonts
under `sh/sysl/stb/truetype/fonts/` are subsets of Inter, © The Inter Project Authors, under the SIL
Open Font License 1.1, which is in `fonts/OFL.txt` beside them. Everything else here is ISC.
`LICENSE` carries the first and the last.
