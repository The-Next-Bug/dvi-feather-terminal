#!/usr/bin/env python3
"""Check the built guide's internal links and images (scripts/docs.sh).

Usage: checklinks.py HTML_DIR
Every href and src in the guide's own pages that is not an absolute URL
must name a file that exists, and a #fragment an id in that page. Exits
1 with the broken ones listed.
"""

import html.parser
import os
import sys
import urllib.parse

# mdBook's own pages and assets, not the guide's.
SKIP = {"print.html", "404.html", "toc.html"}


class Page(html.parser.HTMLParser):
    def __init__(self):
        super().__init__()
        self.links, self.ids = [], set()

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if "id" in a:
            self.ids.add(a["id"])
        for key in ("href", "src"):
            if a.get(key):
                self.links.append(a[key])


def parse(path):
    p = Page()
    with open(path, encoding="utf-8") as f:
        p.feed(f.read())
    return p


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    root = sys.argv[1]
    pages = {}
    for dirpath, _, files in os.walk(root):
        for name in files:
            if name.endswith(".html"):
                pages[os.path.normpath(os.path.join(dirpath, name))] = None
    broken = []
    for path in sorted(pages):
        if os.path.basename(path) in SKIP:
            continue
        page = parse(path)
        for link in page.links:
            url = urllib.parse.urlsplit(link)
            if url.scheme or url.netloc or link.startswith(("mailto:", "javascript:")):
                continue
            target = os.path.normpath(os.path.join(os.path.dirname(path), urllib.parse.unquote(url.path))) \
                if url.path else path
            if not os.path.exists(target):
                broken.append(f"{os.path.relpath(path, root)}: {link} (no {os.path.relpath(target, root)})")
                continue
            if url.fragment and target.endswith(".html"):
                if pages.get(target) is None:
                    pages[target] = parse(target).ids
                if url.fragment not in pages[target]:
                    broken.append(f"{os.path.relpath(path, root)}: {link} (no #{url.fragment})")
    if broken:
        print("broken links:", *broken, sep="\n  ", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
