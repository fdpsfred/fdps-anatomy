"""Build the character stat comparison page: the model from gen_growth goes
into page_template.html's /*__DATA__*/ slot, and the page is written twice --
workspace/growth_table/index.html for local preview and
docs/character-stat-comparison/index.html, the published copy.

Usage:
    python tools/growth_table/build_page.py [--dump DIR]
    python tools/growth_table/build_page.py --check [--dump DIR]

--check writes nothing and exits non-zero when the published copy differs from
what a build would write now (the template, a generator rule or the game data
changed without a rebuild).
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "tools" / "data_tables"))
import data_tables  # noqa: E402
import gen_growth  # noqa: E402

ROOT = HERE.parents[1]
TEMPLATE = HERE / "page_template.html"
PREVIEW = ROOT / "workspace" / "growth_table" / "index.html"
PUBLISHED = ROOT / "docs" / "character-stat-comparison" / "index.html"
SLOT = "/*__DATA__*/"


class PageError(Exception):
    """The template cannot take the data."""


def render(model):
    template = TEMPLATE.read_text(encoding="utf-8")
    if template.count(SLOT) != 1:
        raise PageError(f"{TEMPLATE.name} must hold exactly one {SLOT}")
    data = json.dumps(model, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
    return template.replace(SLOT, data)


def write_page(page, targets):
    for out in targets:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(page, encoding="utf-8", newline="\n")
        print(f"wrote {out.relative_to(ROOT)} ({len(page.encode('utf-8'))} bytes)")


def build_preview(dump=data_tables.DEFAULT_DUMP):
    """The JSON and the workspace preview only -- what verify_js.py reads --
    leaving the published copy alone."""
    model = gen_growth.model(data_tables.load(dump))
    gen_growth.write(model)
    write_page(render(model), (PREVIEW,))


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dump", default=data_tables.DEFAULT_DUMP)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    try:
        model = gen_growth.model(data_tables.load(args.dump))
        page = render(model)
    except (PageError, gen_growth.GrowthError, data_tables.TableError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    if args.check:
        current = PUBLISHED.read_text(encoding="utf-8") if PUBLISHED.is_file() else None
        if current != page:
            print(f"FAIL: {PUBLISHED.relative_to(ROOT)} is not what a build writes now; "
                  "run tools/growth_table/build_page.py")
            return 1
        print(f"OK: {PUBLISHED.relative_to(ROOT)} is up to date")
        return 0
    gen_growth.write(model)
    write_page(page, (PREVIEW, PUBLISHED))
    return 0


if __name__ == "__main__":
    sys.exit(main())
