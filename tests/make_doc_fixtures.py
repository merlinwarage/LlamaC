"""Writes test files for tests/docs_tests.c into the folder given as argv[1] (Python standard library only).

- zlib_*.bin / zlib_*.txt  deflate streams (levels 0, 1, 9, fixed and dynamic blocks) and their expected content
- sample.docx               heading, list item, table, entities, accented text
- sample.xlsx               two sheets, shared and inline strings, numbers, booleans, a far-right column
- simple.pdf                WinAnsi font, literal and hex strings, TJ word gaps, two pages, uncompressed stream
- cmap.pdf                  Identity-H font with a ToUnicode CMap (bfchar + bfrange), Flate content,
                            fonts and pages inside an object stream (PDF 1.5)
"""
import os
import sys
import zlib
import zipfile
import random

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

# ---- zlib ----
random.seed(7)
words = "the quick brown fox jumps over the lazy dog árvíztűrő tükörfúrógép".split()
text = " ".join(random.choice(words) for _ in range(20000)).encode("utf-8")
noise = bytes(random.getrandbits(8) for _ in range(5000))
for name, data, level in [("text9", text, 9), ("text1", text, 1), ("stored", text[:3000], 0), ("noise", noise, 6), ("tiny", b"a", 9)]:
    open(os.path.join(out, "zlib_%s.bin" % name), "wb").write(zlib.compress(data, level))
    open(os.path.join(out, "zlib_%s.txt" % name), "wb").write(data)

# ---- DOCX ----
W = 'xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"'
doc = f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document {W}><w:body>
<w:p><w:pPr><w:pStyle w:val="Heading1"/></w:pPr><w:r><w:t>Quarterly report</w:t></w:r></w:p>
<w:p><w:r><w:t xml:space="preserve">Sales &amp; costs rose by </w:t></w:r><w:r><w:rPr><w:b/></w:rPr><w:t>12%</w:t></w:r><w:r><w:t>.</w:t></w:r></w:p>
<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr></w:pPr><w:r><w:t>First point</w:t></w:r></w:p>
<w:p><w:r><w:t>Árvíztűrő tükörfúrógép</w:t><w:tab/><w:t>tab&lt;here&gt;</w:t></w:r></w:p>
<w:tbl><w:tr><w:tc><w:p><w:r><w:t>Region</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>Total</w:t></w:r></w:p></w:tc></w:tr>
<w:tr><w:tc><w:p><w:r><w:t>North</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>1&#160;200</w:t></w:r></w:p></w:tc></w:tr></w:tbl>
<w:p><w:r><w:t>End.</w:t></w:r></w:p>
</w:body></w:document>"""
with zipfile.ZipFile(os.path.join(out, "sample.docx"), "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("[Content_Types].xml", '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"/>')
    z.writestr("word/document.xml", doc)

# ---- XLSX ----
wb = """<?xml version="1.0" encoding="UTF-8"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
<sheets><sheet name="Budget &amp; plan" sheetId="1" r:id="rId1"/><sheet name="Notes" sheetId="2" r:id="rId2"/></sheets></workbook>"""
rels = """<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId2" Type="x" Target="worksheets/sheetB.xml"/><Relationship Id="rId1" Type="x" Target="/xl/worksheets/sheetA.xml"/></Relationships>"""
ss = """<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="3" uniqueCount="3">
<si><t>Item</t></si><si><t>Cost</t></si><si><r><t>Rich </t></r><r><rPr><b/></rPr><t>text</t></r></si></sst>"""
sa = """<?xml version="1.0" encoding="UTF-8"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>
<row r="1"><c r="A1" t="s"><v>0</v></c><c r="B1" t="s"><v>1</v></c></row>
<row r="2"><c r="A2" t="s"><v>2</v></c><c r="B2"><v>1250.5</v></c><c r="D2" t="b"><v>1</v></c></row>
<row r="3"/>
<row r="4"><c r="A4" t="inlineStr"><is><t>Inline &lt;x&gt;</t></is></c><c r="BZ4"><v>7</v></c></row>
</sheetData></worksheet>"""
sb = """<?xml version="1.0" encoding="UTF-8"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>
<row r="5"><c r="C5" t="str"><f>A1</f><v>Formula result</v></c></row></sheetData></worksheet>"""
with zipfile.ZipFile(os.path.join(out, "sample.xlsx"), "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("xl/workbook.xml", wb)
    z.writestr("xl/_rels/workbook.xml.rels", rels)
    z.writestr("xl/sharedStrings.xml", ss)
    z.writestr("xl/worksheets/sheetA.xml", sa)
    z.writestr("xl/worksheets/sheetB.xml", sb)


# ---- PDF helpers ----
def pdf(objects, trailer_root):
    """objects: list of (num, bytes body incl. stream). Writes a classic xref table."""
    b = bytearray(b"%PDF-1.5\n%\xe2\xe3\xcf\xd3\n")
    offs = {}
    for num, body in objects:
        offs[num] = len(b)
        b += b"%d 0 obj\n" % num + body + b"\nendobj\n"
    xref = len(b)
    n = max(offs) + 1
    b += b"xref\n0 %d\n0000000000 65535 f \n" % n
    for i in range(1, n):
        b += b"%010d 00000 n \n" % offs[i] if i in offs else b"0000000000 65535 f \n"
    b += b"trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (n, trailer_root, xref)
    return bytes(b)


def stream(dict_extra, data, flate):
    if flate:
        data = zlib.compress(data)
        dict_extra += b" /Filter /FlateDecode"
    return b"<< /Length %d%s >>\nstream\n" % (len(data), dict_extra) + data + b"\nendstream"


# simple.pdf: two pages, WinAnsi Helvetica, TJ arrays, Td moves, octal escape, hex string
p1 = (b"BT /F1 12 Tf 72 720 Td (Hello World) Tj 0 -14 Td [(Ker) 20 (ning) -300 (works)] TJ "
      b"0 -14 Td (Caf\\351 \\(paren\\)) Tj 0 -14 Td <48657820737472696E67> Tj ET")
p2 = b"BT /F1 12 Tf 72 720 Td (Second page) Tj T* (next line via T*) ' ET"
objs = [
    (1, b"<< /Type /Catalog /Pages 2 0 R >>"),
    (2, b"<< /Type /Pages /Kids [3 0 R 6 0 R] /Count 2 /Resources << /Font << /F1 5 0 R >> >> >>"),
    (3, b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R >>"),
    (4, stream(b"", p1, False)),
    (5, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"),
    (6, b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents [7 0 R] >>"),
    (7, stream(b"", b"14 TL " + p2, True)),
]
open(os.path.join(out, "simple.pdf"), "wb").write(pdf(objs, 1))

# cmap.pdf: CID font with ToUnicode; glyph ids 1..N map to "Árvíz test" etc.; pages + fonts inside an ObjStm
cmap = b"""/CIDInit /ProcSet findresource begin 12 dict begin begincmap
1 begincodespacerange <0000> <FFFF> endcodespacerange
3 beginbfchar <0001> <00C1> <0002> <0072> <0003> <0020> endbfchar
2 beginbfrange <0010> <0012> <0076> <0020> <0021> [<00ED> <0171>] endbfrange
endcmap CMapName currentdict /CMap defineresource pop end end"""
# 0001 Á, 0002 r, 0010 v, 0011 w, 0012 x, 0020 í, 0021 ű, 0003 space
content = b"BT /C0 1 Tf 12 0 0 12 72 700 Tm <00010002001000200012> Tj 1 0 0 1 0 -20 Tm ET BT /C0 1 Tf 12 0 0 12 72 680 Tm [<0021> -500 <0010>] TJ ET"
inner = [
    (1, b"<< /Type /Catalog /Pages 2 0 R >>"),
    (2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>"),
    (3, b"<< /Type /Page /Parent 2 0 R /Resources << /Font << /C0 5 0 R >> >> /Contents 4 0 R >>"),
    (5, b"<< /Type /Font /Subtype /Type0 /BaseFont /X /Encoding /Identity-H /ToUnicode 6 0 R >>"),
]
hdr = b""
body = b""
for num, ob in inner:
    hdr += b"%d %d " % (num, len(body))
    body += ob + b"\n"
objstm = stream(b" /Type /ObjStm /N %d /First %d" % (len(inner), len(hdr)), hdr + body, True)
objs = [(4, stream(b"", content, True)), (6, stream(b"", cmap, True)), (7, objstm)]
data = pdf(objs, 1)
open(os.path.join(out, "cmap.pdf"), "wb").write(data)

# sample.html for an optional browser-printed PDF (realistic fonts, subsetting, kerning)
open(os.path.join(out, "sample.html"), "w", encoding="utf-8").write("""<!doctype html><meta charset="utf-8">
<h1>Browser PDF test</h1><p>The quick brown fox jumps over the lazy dog. Árvíztűrő tükörfúrógép.</p>
<table border=1><tr><td>Region</td><td>Total</td></tr><tr><td>North</td><td>1200</td></tr></table>
<p style="page-break-before:always">Second page text.</p>""")
print("fixtures written to", out)
