#!/usr/bin/env python3
"""Fetch the public-domain texts and write them, simplified, to data/texts/.

    pip install opencc && python3 tools/fetch_texts.py

The results are committed, so a build needs neither this nor the network.
Sources: chinese-poetry (MIT) and Chinese Wikisource (the works are in the
public domain). Nothing is supplied or corrected here: the conversion to
simplified characters is the only change made to the words.

A file is "# title", "# author", "# source", then "## chapter" and one
sentence per line.
"""
import json, os, re, urllib.parse, urllib.request
import opencc

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "texts")
CP = "https://raw.githubusercontent.com/chinese-poetry/chinese-poetry/master/"
t2s = opencc.OpenCC("t2s").convert


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "kobo-hanzi-text-fetch"})
    return json.load(urllib.request.urlopen(req, timeout=60))


def poetry(path):
    return get(CP + urllib.parse.quote(path))


def wikisource(page):
    d = get("https://zh.wikisource.org/w/api.php?" + urllib.parse.urlencode(
        {"action": "parse", "page": page, "prop": "wikitext|revid", "format": "json",
         "formatversion": 2, "redirects": 1}))["parse"]
    return d["wikitext"], "zh.wikisource.org, %s, revision %d" % (page, d["revid"])


def plain(s):
    """Wikitext of a line as text: links and -{}- kept as their words."""
    s = re.sub(r"<ref[^>]*/>|<ref.*?</ref>|<!--.*?-->", "", s)
    s = re.sub(r"-\{([^|{}]*)\}-", r"\1", s)
    s = re.sub(r"\[\[(?:[^\]|]*\|)?([^\]]*)\]\]", r"\1", s)
    s = re.sub(r"\{\{另\|([^|{}]*)\|[^{}]*\}\}", r"\1", s)    # a variant: the page's own reading
    s = re.sub(r"\{\{[*小注][^{}]*\}\}", "", s)
    return re.sub(r"'{2,}|<[^>]+>|\s+", "", s)


def sentences(s):
    s = t2s(s).replace("「", "“").replace("」", "”").replace("『", "‘").replace("』", "’")
    return [x for x in re.findall(r"[^。！？]*[。！？]+[”’]*|[^。！？]+$", s) if x.strip()]


def write(title, author, source, chapters):
    with open(os.path.join(OUT, title + ".txt"), "w", encoding="utf-8") as f:
        f.write("# %s\n# %s\n# %s\n" % (title, author, source))
        for name, lines in chapters:
            f.write("## %s\n" % t2s(name))
            f.write("".join(l + "\n" for l in lines))
    bad = [l for _, ls in chapters for l in ls if re.search(r"[{}\[\]|=<>]", l)]
    print(title, len(chapters), "chapters", sum(len(l) for _, l in chapters), "lines", "MARKUP LEFT" if bad else "", bad[:3])


def main():
    os.makedirs(OUT, exist_ok=True)
    src = "github.com/chinese-poetry/chinese-poetry, "
    chaps = lambda d: [(c["chapter"], [s for p in c["paragraphs"] for s in sentences(p)]) for c in d]
    one = lambda t, p: [(t, [s for x in p for s in sentences(x)])]

    write("论语", "孔子弟子", src + "论语/lunyu.json", chaps(poetry("论语/lunyu.json")))
    write("孟子", "孟子", src + "四书五经/mengzi.json", chaps(poetry("四书五经/mengzi.json")))
    for t, f, a in (("大学", "daxue", "曾子"), ("中庸", "zhongyong", "子思")):
        write(t, a, src + "四书五经/%s.json" % f, chaps([poetry("四书五经/%s.json" % f)]))
    write("三字经", "王应麟", src + "蒙学/sanzijing-traditional.json",
          one("三字经", poetry("蒙学/sanzijing-traditional.json")["paragraphs"]))
    p = poetry("蒙学/qianziwen.json")["paragraphs"]
    write("千字文", "周兴嗣", src + "蒙学/qianziwen.json", one("千字文", p))

    text, rev = wikisource("孫子兵法")
    ch = []
    for m in re.finditer(r"(?ms)^==\s*([^=\n]+?)\s*==\s*\n(.*?)(?=^==[^=]|\Z)", text):
        body = re.split(r"(?m)^\{\{(?:footer|PD)|^\[\[(?:Category|分類)", m.group(2))[0]
        ls = [s for l in body.split("\n") if l.strip() and not l.startswith(("{{", "[[Cat"))
              for s in sentences(plain(l))]
        if ls and "第" in m.group(1):
            ch.append((m.group(1), ls))
    write("孙子兵法", "孙武", rev, ch)

    text, rev = wikisource("道德經 (王弼本)")
    ch = []
    for m in re.finditer(r"(?ms)^==\s*([^=\n]+?)\s*==\s*\n(.*?)(?=^=|\Z)", text):
        if not m.group(1).endswith("章"):
            continue
        # a line beginning with a colon is 王弼's commentary
        body = "".join(plain(l) for l in m.group(2).split("\n") if l.strip() and not l.startswith(":"))
        ch.append(("第" + m.group(1), sentences(body)))
    write("道德经", "老子", rev, ch)


main()
