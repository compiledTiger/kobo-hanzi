#!/usr/bin/env python3
"""The small PDFs tests/pdf_test.cpp reads, written by hand so every box is known.

    python3 tests/data/make_test_pdfs.py [font.ttf]     (needs Pillow)

text.pdf -- a text layer, as a Chinese PDF made by a word processor has it:
  page 1  学而时习之 across (STSong-Light, not embedded: a font MuPDF is
          built without, so it has to come from the app's Kaiti), "Hello"
          in Helvetica under it, and a small grey picture in a corner (a
          page with a picture on it is not a scan)
  page 2  道可道非常道 down a column (the vertical encoding)
  page 3  天 on a page with /Rotate 90: landscape, 595 x 420 points

scan.pdf -- one page that is a picture: 天地玄黄 across the top and 宇宙洪荒
  down the right, drawn into a grey JPEG that covers the page, and over it
  an invisible text layer that is wrong -- "ABCD" over 天地玄黄 -- as a bad
  OCR layer is. The picture is drawn here with the app's font (default
  data/fonts/LXGWWenKaiLite-Regular.ttf), drawn for the test.

Every page is 420 x 595 points (A5) unless said. The numbers the test relies
on are given in the comments and repeated in pdf_test.cpp.
"""
import io
import os
import sys
import zlib

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "data", "fonts", "LXGWWenKaiLite-Regular.ttf")
PW, PH = 420, 595


def ucs2(s):
    return "<" + "".join("%04X" % ord(c) for c in s) + ">"


class Pdf:
    def __init__(self):
        self.objs = []

    def add(self, body):
        self.objs.append(body if isinstance(body, bytes) else body.encode("latin-1"))
        return len(self.objs)

    def stream(self, dict_, data):
        return self.add(b"<< " + dict_.encode() + b" /Length %d >>\nstream\n" % len(data)
                        + data + b"\nendstream")

    def write(self, path, root):
        out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
        offs = []
        for i, o in enumerate(self.objs):
            offs.append(len(out))
            out += b"%d 0 obj\n" % (i + 1) + o + b"\nendobj\n"
        xref = len(out)
        out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(self.objs) + 1)
        for o in offs:
            out += b"%010d 00000 n \n" % o
        out += b"trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (
            len(self.objs) + 1, root, xref)
        with open(path, "wb") as f:
            f.write(out)
        print("%s: %d bytes" % (path, len(out)))


def cjk_font(pdf, encoding):
    desc = pdf.add("<< /Type /FontDescriptor /FontName /STSong-Light /Flags 6"
                   " /FontBBox [-25 -254 1000 880] /ItalicAngle 0 /Ascent 880"
                   " /Descent -120 /CapHeight 880 /StemV 93 >>")
    cid = pdf.add("<< /Type /Font /Subtype /CIDFontType0 /BaseFont /STSong-Light"
                  " /CIDSystemInfo << /Registry (Adobe) /Ordering (GB1) /Supplement 4 >>"
                  " /FontDescriptor %d 0 R /DW 1000 >>" % desc)
    return pdf.add("<< /Type /Font /Subtype /Type0 /BaseFont /STSong-Light-%s"
                   " /Encoding /%s /DescendantFonts [%d 0 R] >>" % (encoding, encoding, cid))


def text_pdf(path):
    pdf = Pdf()
    f1 = cjk_font(pdf, "UniGB-UCS2-H")
    f2 = cjk_font(pdf, "UniGB-UCS2-V")
    f3 = pdf.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    # a 16x16 grey ramp, drawn 100pt square at (300, 40) from the bottom left
    raw = bytes((x * 16) for y in range(16) for x in range(16))
    im = pdf.stream("/Type /XObject /Subtype /Image /Width 16 /Height 16 /ColorSpace /DeviceGray"
                    " /BitsPerComponent 8 /Filter /FlateDecode", zlib.compress(raw))
    res = "/Resources << /Font << /F1 %d 0 R /F2 %d 0 R /F3 %d 0 R >> /XObject << /Im1 %d 0 R >> >>" % (
        f1, f2, f3, im)
    # page 1: 学而时习之 at 36pt from (60, 500): each character 36pt wide
    c1 = ("BT /F1 36 Tf 60 500 Td %s Tj ET\n"
          "BT /F3 24 Tf 60 400 Td (Hello) Tj ET\n"
          "q 100 0 0 100 300 40 cm /Im1 Do Q\n" % ucs2("学而时习之")).encode()
    # page 2: 道可道非常道 down from (200, 520), 36pt a character
    c2 = ("BT /F2 36 Tf 200 520 Td %s Tj ET\n" % ucs2("道可道非常道")).encode()
    # page 3: 天 at (60, 500) of a portrait page shown turned 90 degrees
    c3 = ("BT /F1 36 Tf 60 500 Td %s Tj ET\n" % ucs2("天")).encode()
    s1 = pdf.stream("", c1)
    s2 = pdf.stream("", c2)
    s3 = pdf.stream("", c3)
    pages_id = len(pdf.objs) + 4           # the three pages come first, then the tree
    p1 = pdf.add("<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %d %d] %s /Contents %d 0 R >>"
                 % (pages_id, PW, PH, res, s1))
    p2 = pdf.add("<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %d %d] %s /Contents %d 0 R >>"
                 % (pages_id, PW, PH, res, s2))
    p3 = pdf.add("<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %d %d] /Rotate 90 %s /Contents %d 0 R >>"
                 % (pages_id, PW, PH, res, s3))
    assert pdf.add("<< /Type /Pages /Kids [%d 0 R %d 0 R %d 0 R] /Count 3 >>" % (p1, p2, p3)) == pages_id
    root = pdf.add("<< /Type /Catalog /Pages %d 0 R >>" % pages_id)
    pdf.write(path, root)


def scan_pdf(path):
    # the picture: 525 x 744 pixels over 420 x 595 points, 0.8 points a pixel
    iw, ih = 525, 744
    img = Image.new("L", (iw, ih), 236)                       # greyish paper
    d = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT, 60)
    for i, ch in enumerate("天地玄黄"):                        # cells 75px from x=60, top y=100
        d.text((60 + 75 * i, 100), ch, font=font, fill=20)
    for i, ch in enumerate("宇宙洪荒"):                        # a column at x=420, from y=250
        d.text((420, 250 + 75 * i), ch, font=font, fill=20)
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=40)
    pdf = Pdf()
    f3 = pdf.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    im = pdf.stream("/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceGray"
                    " /BitsPerComponent 8 /Filter /DCTDecode" % (iw, ih), buf.getvalue())
    # the wrong layer: A B C D, invisible (3 Tr), one over each of 天地玄黄,
    # whose cells span x 48+60i .. 96+60i points and y 80 .. 128 from the top
    layer = "".join("BT 3 Tr /F3 40 Tf %d %d Td (%s) Tj ET\n" % (58 + 60 * i, PH - 122, c)
                    for i, c in enumerate("ABCD"))
    content = ("q %d 0 0 %d 0 0 cm /Im0 Do Q\n%s" % (PW, PH, layer)).encode()
    s = pdf.stream("", content)
    pages_id = len(pdf.objs) + 2
    p = pdf.add("<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %d %d]"
                " /Resources << /Font << /F3 %d 0 R >> /XObject << /Im0 %d 0 R >> >>"
                " /Contents %d 0 R >>" % (pages_id, PW, PH, f3, im, s))
    assert pdf.add("<< /Type /Pages /Kids [%d 0 R] /Count 1 >>" % p) == pages_id
    root = pdf.add("<< /Type /Catalog /Pages %d 0 R >>" % pages_id)
    pdf.write(path, root)


if __name__ == "__main__":
    text_pdf(os.path.join(HERE, "text.pdf"))
    scan_pdf(os.path.join(HERE, "scan.pdf"))
