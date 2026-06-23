#!/usr/bin/env python3
"""Fix dream HTML pages: full nav, footer, prev/next nav, uniform index cards."""

import os
import re
import glob
from pathlib import Path

DREAMS_DIR = Path(__file__).parent.parent / "docs" / "dreams"

FULL_NAV = '''\
<!-- NAV -->
<nav class="nav">
  <div class="nav-inner">
    <a href="../index.html" class="nav-brand">cc<span>-</span>soul</a>
    <button class="nav-hamburger" onclick="document.querySelector(\'.nav\').classList.toggle(\'nav-open\')" aria-label="Menu">
      <span></span><span></span><span></span>
    </button>
    <ul class="nav-links">
      <li><a href="../index.html">Home</a></li>
      <li><a href="../getting-started.html">Get Started</a></li>
      <li><a href="../philosophy.html">Philosophy</a></li>
      <li><a href="../architecture.html">Architecture</a></li>
      <li><a href="../sadhana.html">Sadhana</a></li>
      <li><a href="index.html" class="active">Dreams</a></li>
      <li><a href="../tools.html">Tools</a></li>
      <li><a href="https://github.com/genomewalker/cc-soul" target="_blank" rel="noopener">GitHub</a></li>
    </ul>
  </div>
</nav>'''

FULL_HEAD_SUFFIX = '''\
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Cormorant+Garamond:ital,wght@0,400;0,500;0,600;1,400;1,500&family=IBM+Plex+Mono:ital,wght@0,400;0,500;1,400&display=swap" rel="stylesheet">
<link rel="stylesheet" href="../styles.css">
<link rel="stylesheet" href="dreams.css">'''

FOOTER_TPL = '''\

<!-- FOOTER -->
<footer class="footer">
  <div class="container">
    <p>A dream by cc-soul &middot; {date} &middot; <a href="index.html">All dreams</a> &middot; <a href="https://github.com/genomewalker/cc-soul" target="_blank" rel="noopener">GitHub</a></p>
  </div>
</footer>'''

PREVNEXT_TPL = '''\
<nav class="dream-prevnext">
  <div class="dream-prevnext-inner">
    {prev_link}
    {next_link}
  </div>
</nav>'''


def get_title(html):
    m = re.search(r'<title>(.*?) ?[-—]', html)
    return m.group(1).strip() if m else "Dream"


def get_date(html):
    m = re.search(r'Dream\s*[·&middot;]+\s*([\d-]+)', html)
    return m.group(1) if m else ""


def get_all_dreams():
    files = sorted(glob.glob(str(DREAMS_DIR / "*.html")))
    return [f for f in files if not f.endswith("index.html")]


def is_stripped(html):
    return "Get Started" not in html


def fix_head(html):
    """Replace minimal font/css links with full versions."""
    # Remove old minimal font links
    html = re.sub(
        r'<link href="https://fonts\.googleapis\.com/css2\?[^"]*"[^>]*>\s*',
        '', html
    )
    # Remove old stylesheet links (we'll re-add them)
    html = re.sub(r'<link rel="stylesheet" href="\.\.\/styles\.css">\s*', '', html)
    html = re.sub(r'<link rel="stylesheet" href="dreams\.css">\s*', '', html)
    # Inject full head suffix before </head>
    html = html.replace('</head>', FULL_HEAD_SUFFIX + '\n</head>')
    return html


def fix_nav(html):
    """Replace stripped nav with full nav."""
    html = re.sub(
        r'<nav class="nav">.*?</nav>',
        FULL_NAV,
        html,
        flags=re.DOTALL
    )
    return html


def add_footer(html, date):
    """Add footer before </body> if missing."""
    if '<footer' in html:
        return html
    footer = FOOTER_TPL.format(date=date)
    html = html.replace('</body>', footer + '\n</body>')
    return html


def add_og_meta(html, title, description, date):
    """Add OG/Twitter meta tags if missing."""
    if 'og:title' in html:
        return html
    og = f'''<meta property="og:title" content="{title} — cc-soul dreams">
<meta property="og:description" content="{description}">
<meta property="og:type" content="article">
<meta property="og:image" content="../favicon.svg">
<meta name="twitter:card" content="summary">
<meta name="twitter:title" content="{title} — cc-soul dreams">
<meta name="twitter:description" content="{description}">'''
    html = html.replace('<link rel="icon"', og + '\n<link rel="icon"')
    return html


def add_viewport(html):
    if 'viewport' in html:
        return html
    html = html.replace('<meta charset="UTF-8">', '<meta charset="UTF-8">\n<meta name="viewport" content="width=device-width, initial-scale=1.0">')
    return html


def add_prevnext(html, prev_file, next_file):
    """Inject prev/next nav after dream-meta div."""
    prev_link = ''
    next_link = ''
    if prev_file:
        prev_title = get_title(open(prev_file).read())
        prev_fname = os.path.basename(prev_file)
        prev_link = f'<a href="{prev_fname}" class="prevnext-prev">&larr; {prev_title}</a>'
    if next_file:
        next_title = get_title(open(next_file).read())
        next_fname = os.path.basename(next_file)
        next_link = f'<a href="{next_fname}" class="prevnext-next">{next_title} &rarr;</a>'

    nav_html = PREVNEXT_TPL.format(prev_link=prev_link, next_link=next_link)

    # Insert before </main>
    if 'dream-prevnext' in html:
        # Replace existing
        html = re.sub(r'<nav class="dream-prevnext">.*?</nav>', nav_html, html, flags=re.DOTALL)
    else:
        html = html.replace('</main>', nav_html + '\n</main>')
    return html


def fix_index_cards(index_path):
    html = open(index_path).read()

    # Change <h3 class="dream-title"> to <h2> for visual uniformity
    html = html.replace('<h3 class="dream-title">', '<h2 class="dream-title">')
    html = html.replace('</h3>', '</h2>')

    # Add dream-meta-row to compact cards that lack it
    # Compact cards end with </p></article> (no dream-meta-row)
    def add_meta_row(m):
        card = m.group(0)
        if 'dream-meta-row' not in card:
            card = card.replace('</article>', '<div class="dream-meta-row"><span>dream</span></div>\n</article>')
        return card
    html = re.sub(r'<article class="dream-card">.*?</article>', add_meta_row, html, flags=re.DOTALL)

    open(index_path, 'w').write(html)
    print(f"  index.html: cards standardized")


def main():
    dreams = get_all_dreams()
    print(f"Found {len(dreams)} dream pages")

    for i, fpath in enumerate(dreams):
        fname = os.path.basename(fpath)
        html = open(fpath).read()
        changed = False

        title = get_title(html)
        date = get_date(html)

        # Extract description from meta tag
        m = re.search(r'<meta name="description" content="([^"]*)"', html)
        description = m.group(1) if m else title

        if is_stripped(html):
            print(f"  FIXING stripped: {fname}")
            html = add_viewport(html)
            html = fix_head(html)
            html = add_og_meta(html, title, description, date)
            html = fix_nav(html)
            html = add_footer(html, date)
            changed = True
        else:
            print(f"  ok: {fname}")

        # Prev/next for all pages
        prev_file = dreams[i - 1] if i > 0 else None
        next_file = dreams[i + 1] if i < len(dreams) - 1 else None
        before = html
        html = add_prevnext(html, prev_file, next_file)
        if html != before:
            changed = True

        if changed:
            open(fpath, 'w').write(html)

    # Fix index cards
    fix_index_cards(DREAMS_DIR / "index.html")
    print("Done.")


if __name__ == "__main__":
    main()
