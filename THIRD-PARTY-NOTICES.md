# Third-party notices

## Code

| | Licence | Use |
|---|---|---|
| [MuPDF](https://mupdf.com/) | AGPL-3.0-or-later | linked statically; fetched by `tools/fetch-deps.sh`. Includes FreeType (*Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved.*), libjpeg (*This software is based in part on the work of the Independent JPEG Group.*), OpenJPEG, jbig2dec, Little CMS, zlib and the URW fonts |
| [FBInk](https://github.com/NiLuJe/FBInk) | GPL-3.0-or-later | linked statically into the Kobo build; fetched |
| [ncnn](https://github.com/Tencent/ncnn) | BSD-3-Clause | linked statically; fetched |
| [SDL2](https://libsdl.org/) | zlib | desktop build |
| [SQLite](https://sqlite.org/) | public domain | `src/vendor` |
| [stb_truetype, stb_image](https://github.com/nothings/stb) | public domain / MIT | `src/vendor` |
| [HanziWriter](https://hanziwriter.org), [KOReader](https://github.com/koreader/koreader) | MIT, AGPL-3.0 | no code used; their published approach to stroke matching, and to handing the Kobo's screen back, were followed |

## Data

| | Licence | Where |
|---|---|---|
| [CC-CEDICT](https://cc-cedict.org/) | CC BY-SA 4.0 | `data/dicts/cedict_ts.u8`; reformatted into content.db |
| [Make Me a Hanzi](https://github.com/skishore/makemeahanzi) graphics | Arphic Public License | `data/dicts/makemeahanzi_graphics.txt`; derived from the Arphic PL KaitiM GB and UKai fonts, © 1999 Arphic Technology. [Licence text](data/dicts/ARPHICPL.txt) |
| Make Me a Hanzi dictionary | LGPL-3.0-or-later | `data/dicts/makemeahanzi_dictionary.txt`; also the readings and glosses of the handwriting deck |
| [HSK 3.1 word lists](https://github.com/becky82/mteh) via [leonsilicon/hsk3.1](https://github.com/leonsilicon/hsk3.1) | CC BY-SA 4.0, © 2025 Rebecca J. Stones | `build/data/hsk31` |
| HSK handwriting characters | the 书写字 list of the 2025 HSK syllabus (中外语言交流合作中心) | `data/hsk` |
| [chinese-poetry](https://github.com/chinese-poetry/chinese-poetry) | MIT (below) | `data/texts`: 论语, 孟子, 大学, 中庸, 三字经, 千字文; converted to simplified |
| [Chinese Wikisource](https://zh.wikisource.org/) | works in the public domain; transcription CC BY-SA 4.0 | `data/texts`: 道德经 (王弼本), 孙子兵法; converted to simplified; each file names its revision |
| [LXGW WenKai Lite](https://github.com/lxgw/LxgwWenKai-Lite) | SIL OFL 1.1 | `data/fonts`, unmodified |
| [PaddleOCR](https://github.com/PaddlePaddle/PaddleOCR) PP-OCRv4 | Apache-2.0 | `data/models`; converted to ncnn |

```
chinese-poetry:
The MIT License (MIT)

Copyright (c) 2016 JackeyGao

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```
