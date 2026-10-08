#!/usr/bin/env python3
"""Makes Content/Data/research.json: the Historical Research Sheet and the
licence texts, for the game's own Research Sheet and Licenses screens.

Nothing is written by hand here. The sheet is read from the finished page
the web version made (Documents/Web pages and reports/
Ports-of-Plague-Research-Sheet.html), so the game shows it word for word
(leaving out only a paragraph about the web version's own files and
commands), and the licence texts are the files in Documents/Licenses.

Text comes out in the game's own markup: <b>, <i>, <small>, <id> (a fact
code) and <debate>, never one inside another.

Run:  python3 Tools/make_research.py
"""
import json
import os
from html.parser import HTMLParser

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHEET = os.path.join(ROOT, 'Documents', 'Web pages and reports', 'Ports-of-Plague-Research-Sheet.html')
LICENSES = os.path.join(ROOT, 'Documents', 'Licenses')
OUT = os.path.join(ROOT, 'Content', 'Data', 'research.json')

# The licence texts the game shows, in this order. The two Supabase library
# licences are left out: this version talks to Supabase with its own code.
LICENSE_FILES = [
    ('OFL-EB-Garamond.txt', 'EB Garamond (font)'),
    ('OFL-Cinzel.txt', 'Cinzel (font)'),
    ('OFL-UnifrakturMaguntia.txt', 'UnifrakturMaguntia (font)'),
    ('MIT-roll-a-die.txt', 'roll-a-die (dice)'),
    ('music-credits.txt', 'Music'),
]


# What marks a paragraph as being about how the web version is built, not about the history.
DEVELOPER_WORDS = ('npm run', '.json', '.js<', 'node ')


def esc(text):
    return text.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')


class Sheet(HTMLParser):
    """Turns the page into a list of blocks: headings, paragraphs, tables and the boxed section."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.blocks = []
        self.target = self.blocks   # where finished blocks go (the page, or the box)
        self.text = None            # the markup being collected, or None between blocks
        self.kind = None
        self.styles = []            # inline styles open around the current text
        self.base = None            # the block's own style (a small paragraph)
        self.table = None
        self.row = None
        self.skip = 0
        self.title = self.subtitle = ''

    # ---- inline text ----
    def style(self):
        for s in reversed(self.styles):
            if s:
                # Inside small print, bold and italic have their own small styles.
                if self.base == 'small' and s in ('b', 'i'):
                    return 's' + s
                return s
        return self.base

    def handle_data(self, data):
        if self.skip or self.text is None:
            return
        data = ' '.join(data.split('\n')) if '\n' in data else data
        if not data:
            return
        s = self.style()
        self.text.append(f'<{s}>{esc(data)}</>' if s and data.strip() else esc(data))

    def start(self, kind, base=None):
        self.text, self.kind, self.base, self.styles = [], kind, base, []

    def finish(self):
        out = ''.join(self.text).strip()
        self.text = None
        return ' '.join(out.split(' ')) if out else ''

    # ---- structure ----
    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        cls = a.get('class', '')
        if self.skip or cls == 'print-tip':
            self.skip += 1
            return
        if tag == 'h1':
            self.start('title')
        elif tag == 'h2':
            self.start('h2')
        elif tag == 'p' and cls == 'doc-sub':
            self.start('subtitle')
        elif tag == 'p':
            self.start('p', 'small' if cls == 'small' else None)
        elif tag == 'div' and cls == 'box':
            box = {'type': 'box', 'blocks': []}
            self.blocks.append(box)
            self.target = box['blocks']
        elif tag == 'table':
            self.table = {'type': 'table', 'facts': cls == 'facts-table', 'head': [], 'rows': []}
        elif tag == 'tr':
            self.row = {'cells': [], 'head': False, 'span': False}
        elif tag in ('td', 'th'):
            self.row['head'] = self.row['head'] or tag == 'th'
            self.row['span'] = self.row['span'] or 'colspan' in a
            self.start('cell', 'small' if cls == 'small' else None)
        elif tag == 'br':
            if self.text is not None:
                self.text.append('\n')
        elif tag in ('strong', 'code'):
            self.styles.append('b')
        elif tag == 'em':
            self.styles.append('i')
        elif tag == 'span':
            self.styles.append({'fact-ref': 'id', 'debate': 'debate', 'small': 'small'}.get(cls))

    def handle_endtag(self, tag):
        if self.skip:
            self.skip -= 1
            return
        if tag == 'h1':
            self.title = self.finish()
        elif tag == 'h2':
            self.target.append({'type': 'h2', 'text': self.finish()})
        elif tag == 'p':
            if self.kind == 'subtitle':
                self.subtitle = self.finish()
            else:
                small = self.base == 'small'
                text = self.finish()
                # A paragraph about the web version's own files and commands means nothing to a player: left out.
                if not any(word in text for word in DEVELOPER_WORDS):
                    self.target.append({'type': 'small' if small else 'p', 'text': text})
        elif tag == 'div' and self.target is not self.blocks:
            self.target = self.blocks
        elif tag in ('td', 'th'):
            self.row['cells'].append(self.finish())
        elif tag == 'tr':
            r = self.row
            if r['span']:
                self.table['rows'].append({'group': r['cells'][0]})
            elif r['head']:
                self.table['head'] = r['cells']
            else:
                self.table['rows'].append({'cells': r['cells']})
            self.row = None
        elif tag == 'table':
            self.target.append(self.table)
            self.table = None
        elif tag in ('strong', 'code', 'em', 'span'):
            self.styles.pop()


def main():
    sheet = Sheet()
    with open(SHEET, encoding='utf-8') as f:
        page = f.read()
    sheet.feed(page[page.index('<body'):])
    licenses = []
    for name, title in LICENSE_FILES:
        with open(os.path.join(LICENSES, name), encoding='utf-8') as f:
            licenses.append({'title': title, 'file': name, 'text': f.read().strip()})
    out = {
        'note': 'Made by Tools/make_research.py from the web version\'s finished Research Sheet and licence files. Do not edit by hand.',
        'title': sheet.title, 'subtitle': sheet.subtitle, 'blocks': sheet.blocks, 'licenses': licenses,
    }
    with open(OUT, 'w', encoding='utf-8') as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
        f.write('\n')
    facts = sum(1 for b in sheet.blocks if b['type'] == 'table' and b['facts'] for r in b['rows'] if 'cells' in r)
    print(f'{OUT}: {len(sheet.blocks)} blocks, {facts} facts in the table, {len(licenses)} licence texts')


if __name__ == '__main__':
    main()
