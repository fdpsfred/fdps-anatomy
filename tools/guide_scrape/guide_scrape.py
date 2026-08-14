"""Mirror the 青衫 strategy-guide site for a game in the series, and search it.

The site hosts one guide per game under the same directory shape, so the tool
is parameterised by game code:

    python tools/guide_scrape/guide_scrape.py fetch  fdps
    python tools/guide_scrape/guide_scrape.py list   fdps
    python tools/guide_scrape/guide_scrape.py search 極光之矛
    python tools/guide_scrape/guide_scrape.py show   fdps modify2

`fetch` writes each page twice under ``docs/guide/<game>/`` — the original HTML
and a plain-text rendering with the markup removed — plus every image the pages
reference, which for the walkthrough are the chapter maps.  All of it goes into
version control: the pages are the reference material the rest of the project
quotes, and a mirror that disappears when the site does is no mirror at all.
Nothing is written until every download has succeeded, so a run that fails
part-way leaves the previous mirror intact rather than half-replaced.

There is deliberately no parser.  The pages are hand-written, every one of
them laid out differently, and a rule-based reader would quietly produce wrong
facts the first time an author broke his own format.  The text rendering keeps
the original monospaced layout intact, so answering a question means searching
the text and reading the surrounding lines — the same thing a human does.
"""

import argparse
import os
import re
import sys
import urllib.error
import urllib.request

BASE_URL = "https://chiuinan.github.io/game/game/intro/ch/c31/{game}/{game}/"

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MIRROR_ROOT = os.path.join(REPO_ROOT, "docs", "guide")

# One entry per game in the series.  `pages` maps the local file name to the
# page name on the site; the games differ in which pages exist (一代 has no
# spell list, 二代 has no cheat page) and the walkthrough is named after the
# game.  `title` is what the page calls itself, kept here so `list` can show
# what each file holds without opening it.
GAMES = {
    "fdps": {
        "name": "炎龍騎士團外傳",
        "pages": [
            ("notes", "notes", "新手提示"),
            ("trick", "trick", "遊戲密技"),
            ("walkthrough", "fdps", "遊戲攻略"),
            ("item", "item", "裝備列表"),
            ("spell", "spell", "法術列表"),
            ("list", "list", "人物屬性"),
            ("memory", "memory", "記憶體修改"),
            ("modify1", "modify1", "程式修改"),
            ("modify2", "modify2", "資訊修改"),
        ],
    },
    "fd2": {
        "name": "炎龍騎士團二",
        "pages": [
            ("notes", "notes", "新手提示"),
            ("walkthrough", "fd2", "遊戲攻略"),
            ("item", "item", "裝備列表"),
            ("spell", "spell", "法術列表"),
            ("list", "list", "人物屬性"),
            ("memory", "memory", "記憶體修改"),
            ("modify1", "modify1", "程式修改"),
            ("modify2", "modify2", "資訊修改"),
        ],
    },
    "fd": {
        "name": "炎龍騎士團",
        "pages": [
            ("notes", "notes", "新手提示"),
            ("trick", "trick", "遊戲密技"),
            ("walkthrough", "fd", "遊戲攻略"),
            ("item", "item", "裝備列表"),
            ("list", "list", "人物屬性"),
            ("memory", "memory", "記憶體修改"),
            ("modify1", "modify1", "程式修改"),
            ("modify2", "modify2", "資訊修改"),
            ("save", "save", "存檔修改"),
        ],
    },
}


class FetchError(Exception):
    pass


def game_dir(game):
    return os.path.join(MIRROR_ROOT, game)


def require_game(game):
    if game not in GAMES:
        raise SystemExit("unknown game code %r; known: %s" % (game, ", ".join(sorted(GAMES))))
    return GAMES[game]


def require_page(game, page):
    spec = require_game(game)
    known = [local for local, _, _ in spec["pages"]]
    if page not in known:
        raise SystemExit("%r has no page %r; it has: %s" % (game, page, ", ".join(known)))
    return page


# --- fetch ----------------------------------------------------------------

def fetch(game):
    """Download a game's whole guide, then write it out in one go.

    Everything is downloaded into memory first.  Writing as we go would leave
    the mirror half-replaced when a later page fails — new content for the
    pages already written, last run's content for the rest — which is exactly
    the silently-wrong state this tool is supposed to make impossible.
    """
    spec = require_game(game)
    files = {}
    images = set()
    for local, page, title in spec["pages"]:
        url = BASE_URL.format(game=game) + page + ".htm"
        text = decode(_get(url))
        if "<pre" not in text.lower():
            raise FetchError("%s has no <pre> block; the site layout changed" % url)
        sources = image_sources(text, url)
        images.update(sources)
        files[local + ".htm"] = text.encode("utf-8")
        files[local + ".txt"] = to_text(text).encode("utf-8")
        print("fetched %-12s %-10s %6d chars, %d image(s)" % (local, title, len(text),
                                                              len(sources)))

    for src in sorted(images):
        files[src] = _get(BASE_URL.format(game=game) + src)
    print("fetched %d image(s)" % len(images))

    out = game_dir(game)
    os.makedirs(out, exist_ok=True)
    for name, data in sorted(files.items()):
        with open(os.path.join(out, name), "wb") as fh:
            fh.write(data)
    print("wrote %d file(s) to %s" % (len(files), os.path.relpath(out, REPO_ROOT)))


def _get(url):
    request = urllib.request.Request(url, headers={"User-Agent": "fdps-anatomy/guide_scrape"})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            if response.status != 200:
                raise FetchError("%s returned HTTP %d" % (url, response.status))
            data = response.read()
    except urllib.error.URLError as exc:
        raise FetchError("%s failed: %s" % (url, exc))
    if not data:
        raise FetchError("%s returned an empty body" % url)
    return data


def decode(raw):
    """Decode page bytes as UTF-8, dropping the byte order mark.

    Every page on the site starts with a BOM.  ``utf-8-sig`` removes the one
    at the front; the replace afterwards catches any that survived into the
    body, where it would otherwise end up inside a value someone searches for.
    Line endings are normalised to LF so the mirror is stable whatever the
    server and the checkout do.
    """
    text = raw.decode("utf-8-sig").replace("﻿", "")
    return text.replace("\r\n", "\n").replace("\r", "\n")


# --- HTML to text ---------------------------------------------------------

_ENTITIES = (("&lt;", "<"), ("&gt;", ">"), ("&quot;", '"'), ("&nbsp;", " "), ("&amp;", "&"))
_IMG_RE = re.compile(r"<img\b[^>]*>", re.IGNORECASE)
_SRC_RE = re.compile(r"""\bsrc\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))""", re.IGNORECASE)


def image_sources(html, url):
    """Return the ``src`` of every ``<img>`` on the page.

    An ``<img>`` whose ``src`` cannot be read, or that points off this
    directory, aborts the fetch: silently dropping a chapter map would leave
    the mirror quietly incomplete.
    """
    found = []
    for tag in _IMG_RE.findall(html):
        match = _SRC_RE.search(tag)
        if not match:
            raise FetchError("%s has an <img> with no readable src: %s" % (url, tag))
        src = next(group for group in match.groups() if group is not None)
        if "/" in src or src.startswith(".."):
            raise FetchError("%s references an image outside its directory: %s" % (url, src))
        found.append(src)
    return found


def to_text(html):
    """Strip the markup, keeping the layout of the page exactly as authored.

    All the content lives inside a ``<pre>`` block whose columns line up by
    display width, so tags are deleted in place and nothing is substituted for
    them: removing ``<b>`` without leaving a space behind is what keeps the
    rest of the line where the author put it, and deleting the block tags
    (``<table>``, ``<td>``, ``<p>`` …) rather than turning them into newlines
    is what keeps the author's own blank lines — which separate the sections —
    distinguishable from ones the tool invented.  The two exceptions are
    ``<br>``, which is a line break, and ``<img>``, which becomes a readable
    reference to the mirrored image file.
    """
    text = re.sub(r"<head\b.*?</head>", "", html, flags=re.IGNORECASE | re.DOTALL)
    text = re.sub(r"<br\s*/?>", "\n", text, flags=re.IGNORECASE)
    text = _IMG_RE.sub(lambda m: "[圖 %s]" % _img_src(m.group(0)), text)
    text = re.sub(r"<[^>]*>", "", text)
    for src, dst in _ENTITIES:
        text = text.replace(src, dst)
    return text.strip() + "\n"


def _img_src(tag):
    match = _SRC_RE.search(tag)
    if not match:
        raise FetchError("<img> with no readable src: %s" % tag)
    return next(group for group in match.groups() if group is not None)


# --- reading the mirror ---------------------------------------------------

def mirrored_pages(game):
    """Yield ``(local, title, path)`` for each fetched text page of `game`."""
    spec = require_game(game)
    out = game_dir(game)
    for local, _, title in spec["pages"]:
        path = os.path.join(out, local + ".txt")
        if os.path.exists(path):
            yield local, title, path


def show(game, page):
    """Print one mirrored page as text."""
    # Validating against the registry rather than just probing the filesystem
    # keeps `show fd spell` honest ("一代 has no spell page", not "run fetch")
    # and keeps a page name from walking out of the mirror directory.
    require_page(game, page)
    path = os.path.join(game_dir(game), page + ".txt")
    if not os.path.exists(path):
        raise SystemExit("%r is not mirrored yet; run 'fetch %s'" % (page, game))
    with open(path, encoding="utf-8") as fh:
        sys.stdout.write(fh.read())


def show_list(game):
    """Print what is mirrored for `game`, and what is missing."""
    spec = require_game(game)
    out = game_dir(game)
    print("%s (%s)" % (spec["name"], game))
    missing = 0
    for local, _, title in spec["pages"]:
        path = os.path.join(out, local + ".txt")
        if not os.path.exists(path):
            missing += 1
            print("  %-12s %-10s (missing)" % (local, title))
            continue
        with open(path, encoding="utf-8") as fh:
            lines = fh.read().count("\n")
        print("  %-12s %-10s %5d lines  %s" % (local, title, lines,
                                               os.path.relpath(path, REPO_ROOT)))
    images = sorted(name for name in os.listdir(out) if not name.endswith((".htm", ".txt"))) \
        if os.path.isdir(out) else []
    if images:
        print("  %d mirrored image(s)" % len(images))
    if missing:
        print("  %d page(s) missing; run 'fetch %s'" % (missing, game))


def search(pattern, games, context, ignore_case):
    """Print every mirrored line matching `pattern`, with surrounding lines."""
    flags = re.IGNORECASE if ignore_case else 0
    try:
        regex = re.compile(pattern, flags)
    except re.error as exc:
        raise SystemExit("bad pattern %r: %s" % (pattern, exc))

    hits = 0
    for game in games:
        for local, title, path in mirrored_pages(game):
            with open(path, encoding="utf-8") as fh:
                lines = fh.read().split("\n")
            for i, line in enumerate(lines):
                if not regex.search(line):
                    continue
                hits += 1
                print("=== %s/%s (%s) line %d" % (game, local, title, i + 1))
                lo = max(0, i - context)
                hi = min(len(lines), i + context + 1)
                for n in range(lo, hi):
                    print("%s%5d  %s" % (">" if n == i else " ", n + 1, lines[n]))
                print()
    if not hits:
        print("no match for %r in %s" % (pattern, ", ".join(games)))
        return 1
    print("%d matching line(s)" % hits)
    return 0


# --- CLI ------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)

    p_fetch = sub.add_parser("fetch", help="download a game's guide pages into docs/guide/")
    p_fetch.add_argument("game", choices=sorted(GAMES))

    p_list = sub.add_parser("list", help="show which pages are mirrored")
    p_list.add_argument("game", choices=sorted(GAMES))

    p_show = sub.add_parser("show", help="print one mirrored page as text")
    p_show.add_argument("game", choices=sorted(GAMES))
    p_show.add_argument("page", help="page name as shown by 'list'")

    p_search = sub.add_parser("search", help="search the mirrored text")
    p_search.add_argument("pattern")
    p_search.add_argument("--game", action="append", choices=sorted(GAMES),
                          help="limit to a game (repeatable; default: all)")
    p_search.add_argument("-C", "--context", type=int, default=2,
                          help="lines of context around each hit (default 2)")
    p_search.add_argument("-i", "--ignore-case", action="store_true")

    args = ap.parse_args(argv)
    try:
        if args.command == "fetch":
            fetch(args.game)
        elif args.command == "list":
            show_list(args.game)
        elif args.command == "show":
            show(args.game, args.page)
        elif args.command == "search":
            return search(args.pattern, args.game or sorted(GAMES),
                          args.context, args.ignore_case)
    except FetchError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
