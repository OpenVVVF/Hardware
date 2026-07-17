"""Documents as parts: the project's design documents (HARA, TARA, SWAD,
Manual, analyses) are first-class items. Each gets an internal part number
(HW-C#-DOC-...) and is compiled from source (HTML or Markdown) into a clean
styled PDF that ships inside the release package.
"""

import re
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

from .context import Context
from .descriptor_registry import DescriptorRegistry
from .part_numbers import line_identity

_MD_CSS = """
@page {
  size: A4; margin: 22mm 18mm 18mm 18mm;
  @bottom-left { content: "Traction Inverter"; font-family: "Liberation Sans", sans-serif; font-size: 8pt; color: #5A6570; }
  @bottom-right { content: counter(page); font-family: "Liberation Sans", sans-serif; font-size: 8pt; color: #5A6570; }
}
@page cover { margin: 0; background: linear-gradient(105deg, white 62%, #1E4D2B 62%); }
.cover { page: cover; page-break-after: always; padding: 40mm 18mm 0 18mm; }
.cover-kicker { font-size: 10pt; letter-spacing: 2.5pt; text-transform: uppercase; color: #1E4D2B; font-weight: 700; margin-bottom: 10mm; }
.cover-title { font-size: 26pt; font-weight: 700; color: #212529; margin-bottom: 14mm; }
.cover-meta { font-size: 9.5pt; color: #495057; line-height: 2.1; }
.cover-meta strong { color: #212529; display: inline-block; width: 26mm; }
.cover-rule { border: 0; border-top: 1.4pt solid #1E4D2B; margin-top: 26mm; }
.cover-lab { font-size: 8pt; color: #8a8a8a; }
.toc { page-break-after: always; }
.toc-title { font-size: 16pt; font-weight: 700; border-bottom: 1.4pt solid #1E4D2B; padding-bottom: 2mm; margin-bottom: 6mm; }
.toc ol { list-style: none; padding-left: 0; }
.toc li { margin: 1.2mm 0; }
.toc li.l2 { padding-left: 5mm; font-size: 9pt; }
.toc a { color: #212529; text-decoration: none; }
.toc a::after { content: leader(". ") target-counter(attr(href), page); color: #5A6570; }
body { font-family: "Liberation Sans", sans-serif; font-size: 9.5pt; color: #212529; line-height: 1.45; }
h1 { font-size: 16pt; border-bottom: 1.2pt solid #1E4D2B; padding-bottom: 1.6mm; margin-top: 9mm; page-break-after: avoid; }
h2 { font-size: 12.5pt; border-bottom: 0.7pt solid #1E4D2B; padding-bottom: 1mm; margin-top: 7mm; page-break-after: avoid; }
h3 { font-size: 10.5pt; margin-top: 5mm; page-break-after: avoid; }
table { border-collapse: collapse; width: 100%; margin: 3mm 0; font-size: 8.5pt; page-break-inside: auto; }
th { background: #1E4D2B; color: white; text-align: left; padding: 1.6mm 2mm; }
td { border: 0.4pt solid #B9C2B9; padding: 1.4mm 2mm; vertical-align: top; }
tr:nth-child(even) td { background: #EDF3EE; }
tr { page-break-inside: avoid; }
code { font-family: "Liberation Mono", monospace; font-size: 8.5pt; background: #f2f2f2; padding: 0 1mm; }
pre { background: #f6f6f6; border: 0.4pt solid #ddd; padding: 3mm; font-size: 8pt; white-space: pre-wrap; }
img { max-width: 100%; }
a { color: #1E4D2B; text-decoration: none; }
blockquote { border-left: 2pt solid #1E4D2B; margin-left: 0; padding: 1mm 0 1mm 4mm; color: #495057; background: #EDF3EE; }
"""


def _parse_front_matter(text: str):
    """Split a --- delimited metadata block from the markdown body."""
    meta = {}
    if text.lstrip().startswith("---"):
        parts = re.split(r"^---\s*$", text.strip(), maxsplit=2, flags=re.MULTILINE)
        if len(parts) >= 3:
            for line in parts[1].splitlines():
                if ":" in line:
                    k, v = line.split(":", 1)
                    meta[k.strip().lower()] = v.strip().strip('"').strip("'")
            return meta, parts[2]
    return meta, text


def _build_cover(meta: dict, fallback_title: str, ipn: str, rev: str) -> str:
    doctype = meta.get("doctype", "Design Document")
    title = meta.get("title", fallback_title)
    rows = []
    for key, label in [("mcus", "MCUs"), ("temp", "Operating Temp"), ("version", "Version"),
                       ("prepared", "Prepared by"), ("reviewed", "Reviewed by"), ("date", "Date")]:
        if meta.get(key):
            rows.append(f"<div><strong>{label}</strong>{meta[key]}</div>")
    rows.append(f"<div><strong>Part</strong>{ipn} · Rev {rev}</div>")
    return (
        f"<div class='cover'><div class='cover-kicker'>{doctype}</div>"
        f"<div class='cover-title'>{title}</div>"
        f"<div class='cover-meta'>{''.join(rows)}</div>"
        f"<hr class='cover-rule'><div class='cover-lab'>University of California, Santa Cruz — Corzine Lab</div></div>"
    )


def _build_toc(body_html: str) -> tuple:
    """Add heading ids and build a TOC page (dotted leaders + page numbers)."""
    entries = []
    def repl(m):
        level, attrs, inner = int(m.group(1)), m.group(2), m.group(3)
        text = re.sub(r"<[^>]+>", "", inner).strip()
        anchor = f"h{len(entries)}"
        entries.append((level, text, anchor))
        return f"<h{level}{attrs} id='{anchor}'>{inner}</h{level}>"
    body_html = re.sub(r"<h([12])([^>]*)>(.*?)</h\1>", repl, body_html, flags=re.DOTALL)
    if not entries:
        return "", body_html
    items = "".join(
        f"<li class='l{lvl}'><a href='#{a}'>{t}</a></li>" for lvl, t, a in entries
    )
    return f"<div class='toc'><div class='toc-title'>Contents</div><ol>{items}</ol></div>", body_html


@dataclass
class DocumentInfo:
    src: Path
    title: str
    rev: str
    ipn: str
    slug: str


def _slugify(name: str) -> str:
    slug = re.sub(r"[^A-Z0-9]+", "-", name.upper()).strip("-")
    return slug[:20] or "DOC"


def _title_from_html(text: str, fallback: str) -> str:
    m = re.search(r"<title[^>]*>(.*?)</title>", text, re.DOTALL | re.IGNORECASE)
    if m and m.group(1).strip():
        return re.sub(r"\s+", " ", m.group(1)).strip()
    m = re.search(r'<h1[^>]*>(.*?)</h1>', text, re.DOTALL | re.IGNORECASE)
    if m:
        return re.sub(r"<[^>]+>", "", m.group(1)).strip()
    return fallback


def _title_from_md(text: str, fallback: str) -> str:
    meta, body = _parse_front_matter(text)
    if meta.get("doctype"):
        return meta["doctype"]
    if meta.get("title"):
        return meta["title"]
    m = re.search(r"^#\s+(.+)$", body, re.MULTILINE)
    return m.group(1).strip() if m else fallback


def _rev_from_html(text: str) -> str:
    m = re.search(r"Version[:\s]*</?strong>?\s*([0-9A-Za-z.]+)", text, re.IGNORECASE)
    return m.group(1) if m else "A"


def collect_documents(ctx: Context, chassis: str, docs_dir: Optional[Path] = None) -> List[DocumentInfo]:
    """Discover design documents under Docs/ and assign document IPNs."""
    docs = docs_dir or (ctx.hardware_root.parent / "Docs")
    out: List[DocumentInfo] = []
    if not docs.is_dir():
        return out
    abbr = DescriptorRegistry.abbreviate_chassis(chassis)
    for src in sorted(list(docs.glob("*.html")) + list(docs.glob("*.md"))):
        text = src.read_text(encoding="utf-8", errors="replace")
        fallback = re.sub(r"[_-]+", " ", src.stem).strip().title()
        if src.suffix.lower() == ".html":
            title = _title_from_html(text, fallback)
            rev = _rev_from_html(text)
        else:
            title = _title_from_md(text, fallback)
            rev = "A"
        slug = _slugify(src.stem)
        ipn = ctx.pn_registry.generate_pn(
            "document", title, descriptor=slug, chassis=abbr,
            identity=f"doc:{src.name.lower()}",
        )
        out.append(DocumentInfo(src=src, title=title, rev=rev, ipn=ipn, slug=slug))
    return out


def compile_document(info: DocumentInfo, out_pdf: Path) -> bool:
    """Compile one document to PDF. HTML sources keep their embedded paged CSS;
    Markdown goes through the single project template (cover, TOC, footers)."""
    import weasyprint

    out_pdf.parent.mkdir(parents=True, exist_ok=True)
    try:
        if info.src.suffix.lower() == ".html":
            weasyprint.HTML(str(info.src)).write_pdf(str(out_pdf))
        else:
            import markdown
            meta, body_md = _parse_front_matter(info.src.read_text(encoding="utf-8", errors="replace"))
            body = markdown.markdown(body_md, extensions=["tables", "fenced_code"])
            toc_html, body = _build_toc(body)
            cover = _build_cover(meta, info.title, info.ipn, info.rev)
            html = (
                f"<html><head><meta charset='utf-8'><style>{_MD_CSS}</style></head>"
                f"<body>{cover}{toc_html}{body}</body></html>"
            )
            weasyprint.HTML(string=html, base_url=str(info.src.parent)).write_pdf(str(out_pdf))
    except Exception as e:
        print(f"  doc compile failed for {info.src.name}: {e}")
        return False
    return True
