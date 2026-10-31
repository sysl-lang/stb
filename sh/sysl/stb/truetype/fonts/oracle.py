"""Writes what FreeType and fontTools say about the fixture fonts, for the sysl tests to compare
stb_truetype against.

    python3 -I oracle.py <fonts dir>   ->   <fonts dir>/oracle.txt

Nothing here calls stb. Outlines and boxes are FreeType's (FT_LOAD_NO_SCALE, so font units);
metrics, cmap and GPOS are fontTools' reading of the tables; areas are fontTools' AreaPen, which
integrates the curves exactly.
"""
import os
import sys

import freetype
from fontTools.ttLib import TTFont
from fontTools.pens.areaPen import AreaPen

fonts_dir = sys.argv[1]
out = []


def emit(*xs):
    out.append(' '.join(str(x) for x in xs))


def outline_segments(face, glyph):
    face.load_glyph(glyph, freetype.FT_LOAD_NO_SCALE | freetype.FT_LOAD_NO_HINTING)
    o = face.glyph.outline
    contours = []
    cur = []

    def move_to(a, ctx):
        nonlocal cur
        cur = []
        contours.append(cur)
        cur.append(('M', a.x, a.y))

    def line_to(a, ctx):
        cur.append(('L', a.x, a.y))

    def conic_to(c, a, ctx):
        cur.append(('Q', c.x, c.y, a.x, a.y))

    def cubic_to(c1, c2, a, ctx):
        cur.append(('C', c1.x, c1.y, c2.x, c2.y, a.x, a.y))

    o.decompose(None, move_to=move_to, line_to=line_to, conic_to=conic_to, cubic_to=cubic_to)
    cb = o.get_cbox()
    return contours, (cb.xMin, cb.yMin, cb.xMax, cb.yMax), len(o.points)


def gpos_pairs(tt):
    """Every pair's kerning two ways: as GPOS defines it (each PairPos lookup the `kern` feature
    names contributes its first applicable subtable, and the lookups add up), and counting only the
    lookups that are not Extension lookups -- the ones stb reads."""
    if 'GPOS' not in tt:
        return {}
    gpos = tt['GPOS'].table
    order = tt.getGlyphOrder()
    lookups = []
    for rec in gpos.FeatureList.FeatureRecord:
        if rec.FeatureTag == 'kern':
            for i in rec.Feature.LookupListIndex:
                if i not in lookups:
                    lookups.append(i)
    lookups.sort()

    def subtable_value(st, g1, g2):
        cov = st.Coverage.glyphs
        if g1 not in cov:
            return None
        if st.Format == 1:
            for pvr in st.PairSet[cov.index(g1)].PairValueRecord:
                if pvr.SecondGlyph == g2:
                    return getattr(pvr.Value1, 'XAdvance', 0) or 0
            return None
        k1 = st.ClassDef1.classDefs.get(g1, 0)
        k2 = st.ClassDef2.classDefs.get(g2, 0)
        r = st.Class1Record[k1].Class2Record[k2]
        return getattr(r.Value1, 'XAdvance', 0) or 0

    resolved = []
    for i in lookups:
        lk = gpos.LookupList.Lookup[i]
        ext = lk.LookupType == 9
        subs = [s.ExtSubTable for s in lk.SubTable] if ext else list(lk.SubTable)
        lt = subs[0].LookupType if ext else lk.LookupType
        if lt == 2:
            resolved.append((ext, subs))

    cmap = tt.getBestCmap()
    chars = [c for c in range(0x20, 0x7F) if c in cmap]
    pairs = {}
    for a in chars:
        for b in chars:
            g1, g2 = cmap[a], cmap[b]
            full = 0
            plain = 0
            for ext, subs in resolved:
                for st in subs:
                    v = subtable_value(st, g1, g2)
                    if v is not None:
                        full += v
                        if not ext:
                            plain += v
                        break
            if full or plain:
                pairs[(a, b)] = (full, plain)
    return pairs


def font(name, index=0):
    path = os.path.join(fonts_dir, name)
    tt = TTFont(path, fontNumber=index)
    face = freetype.Face(path, index)
    cmap = tt.getBestCmap()
    order = tt.getGlyphOrder()
    gs = tt.getGlyphSet()
    hhea = tt['hhea']
    head = tt['head']
    emit('font', name, index)
    emit('upem', head.unitsPerEm)
    emit('glyphs', tt['maxp'].numGlyphs)
    emit('vmetrics', hhea.ascent, hhea.descent, hhea.lineGap)
    os2 = tt['OS/2']
    emit('typo', os2.sTypoAscender, os2.sTypoDescender, os2.sTypoLineGap)
    emit('bbox', head.xMin, head.yMin, head.xMax, head.yMax)
    for cp in range(0x20, 0x7F):
        gname = cmap[cp]
        g = tt.getGlyphID(gname)
        assert face.get_char_index(cp) == g, (name, cp)
        adv, lsb = tt['hmtx'][gname]
        contours, box, npoints = outline_segments(face, g)
        pen = AreaPen(gs)
        gs[gname].draw(pen)
        if npoints == 0:
            emit('cp', cp, g, adv, lsb, 'empty', 0)
        else:
            emit('cp', cp, g, adv, lsb, *box, '%.3f' % pen.value)
        emit('shape', g, len(contours))
        for c in contours:
            emit('c', len(c) - 1, *[v for seg in c for v in seg])
    # glyph 0, .notdef, which a missing code point maps to
    contours, box, npoints = outline_segments(face, 0)
    emit('notdef', len(contours), *box)
    if 'kern' in tt:
        for a in range(0x20, 0x7F):
            for b in range(0x20, 0x7F):
                # freetype-py's get_kerning takes CHARACTER codes and maps them itself.
                k = face.get_kerning(a, b, freetype.FT_KERNING_UNSCALED)
                if k.x:
                    emit('kern', a, b, k.x)
    for (a, b), (full, plain) in sorted(gpos_pairs(tt).items()):
        emit('gpos', a, b, full, plain)
    emit('end')


font('inter-ascii.ttf')
font('inter-ascii.otf')
font('inter-ascii-kern.ttf')

# Where each font of the collection starts, read from its header by fontTools' own parser.
from fontTools.ttLib.ttCollection import TTCollection
import struct
with open(os.path.join(fonts_dir, 'inter-ascii.ttc'), 'rb') as f:
    raw = f.read()
count = struct.unpack('>I', raw[8:12])[0]
assert len(TTCollection(os.path.join(fonts_dir, 'inter-ascii.ttc')).fonts) == count
emit('ttc', count, *struct.unpack('>%dI' % count, raw[12:12 + 4 * count]))

with open(os.path.join(fonts_dir, 'oracle.txt'), 'w') as f:
    f.write('\n'.join(out) + '\n')
print(len(out), 'lines')
