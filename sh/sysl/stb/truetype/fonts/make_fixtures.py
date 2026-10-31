import sys, os
from fontTools import subset
from fontTools.ttLib import TTFont, newTable
from fontTools.ttLib.ttCollection import TTCollection
from fontTools.ttLib.tables._k_e_r_n import KernTable_format_0

src_ttf, src_otf, out = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)

def sub(src, dst):
    opts = subset.Options()
    opts.layout_features = ['kern']
    opts.hinting = False
    opts.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]
    opts.notdef_outline = True
    opts.glyph_names = False
    opts.drop_tables += ['STAT', 'GDEF', 'GSUB', 'DSIG', 'meta']
    f = subset.load_font(src, opts)
    s = subset.Subsetter(opts)
    s.populate(unicodes=range(0x20, 0x7F))
    s.subset(f)
    subset.save_font(f, dst, opts)

sub(src_ttf, os.path.join(out, 'inter-ascii.ttf'))
sub(src_otf, os.path.join(out, 'inter-ascii.otf'))

# A variant whose kerning is a legacy `kern` table (format 0) instead of GPOS, so stb's other
# kerning path is exercised. The pairs are the GPOS pair adjustments fontTools resolves.
f = TTFont(os.path.join(out, 'inter-ascii.ttf'))
cmap = f.getBestCmap()
gpos = f['GPOS'].table
pairs = {}
for li in gpos.LookupList.Lookup:
    for st in li.SubTable:
        lt = li.LookupType
        if lt == 9:
            lt = st.ExtensionLookupType
            st = st.ExtSubTable
        if lt != 2:
            continue
        cov = st.Coverage.glyphs
        if st.Format == 1:
            for i, g1 in enumerate(cov):
                for pvr in st.PairSet[i].PairValueRecord:
                    v = pvr.Value1.XAdvance if pvr.Value1 and hasattr(pvr.Value1, 'XAdvance') else 0
                    if v and (g1, pvr.SecondGlyph) not in pairs:
                        pairs[(g1, pvr.SecondGlyph)] = v
        else:
            c1 = st.ClassDef1.classDefs
            c2 = st.ClassDef2.classDefs
            glyphs = f.getGlyphOrder()
            for g1 in cov:
                k1 = c1.get(g1, 0)
                for g2 in glyphs:
                    k2 = c2.get(g2, 0)
                    r = st.Class1Record[k1].Class2Record[k2]
                    v = r.Value1.XAdvance if r.Value1 and hasattr(r.Value1, 'XAdvance') else 0
                    if v and (g1, g2) not in pairs:
                        pairs[(g1, g2)] = v
kern = newTable('kern')
kern.version = 0
t = KernTable_format_0()
t.version = 0
t.coverage = 1
t.format = 0
t.kernTable = pairs
kern.kernTables = [t]
f['kern'] = kern
del f['GPOS']
f.save(os.path.join(out, 'inter-ascii-kern.ttf'))
print('kern pairs', len(pairs))

c = TTCollection()
c.fonts = [TTFont(os.path.join(out, 'inter-ascii.ttf')), TTFont(os.path.join(out, 'inter-ascii.otf'))]
c.save(os.path.join(out, 'inter-ascii.ttc'))
for n in sorted(os.listdir(out)):
    print(n, os.path.getsize(os.path.join(out, n)))
