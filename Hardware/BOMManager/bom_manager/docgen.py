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
@page { size: A4; margin: 20mm 18mm 18mm 18mm; }
body { font-family: "Liberation Sans", sans-serif; font-size: 9.5pt; color: #212529; line-height: 1.45; }
.doc-kicker { font-size: 9pt; letter-spacing: 2pt; text-transform: uppercase; color: #1E4D2B; font-weight: 700; margin-bottom: 6mm; }
h1 { font-size: 17pt; border-bottom: 1.4pt solid #1E4D2B; padding-bottom: 2mm; }
h2 { font-size: 13pt; border-bottom: 0.8pt solid #1E4D2B; padding-bottom: 1.2mm; margin-top: 7mm; }
h3 { font-size: 11pt; margin-top: 5mm; }
table { border-collapse: collapse; width: 100%; margin: 3mm 0; font-size: 8.5pt; }
th { background: #1E4D2B; color: white; text-align: left; padding: 1.6mm 2mm; }
td { border: 0.4pt solid #B9C2B9; padding: 1.4mm 2mm; vertical-align: top; }
tr:nth-child(even) td { background: #EDF3EE; }
code { font-family: "Liberation Mono", monospace; font-size: 8.5pt; background: #f2f2f2; padding: 0 1mm; }
pre { background: #f6f6f6; border: 0.4pt solid #ddd; padding: 3mm; font-size: 8pt; white-space: pre-wrap; }
img { max-width: 100%; }
a { color: #1E4D2B; text-decoration: none; }
blockquote { border-left: 2pt solid #1E4D2B; margin-left: 0; padding-left: 4mm; color: #495057; }
"""


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
    m = re.search(r"^#\s+(.+)$", text, re.MULTILINE)
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
    Markdown is wrapped in the project doc style."""
    import weasyprint

    out_pdf.parent.mkdir(parents=True, exist_ok=True)
    try:
        if info.src.suffix.lower() == ".html":
            weasyprint.HTML(str(info.src)).write_pdf(str(out_pdf))
        else:
            import markdown
            body = markdown.markdown(
                info.src.read_text(encoding="utf-8", errors="replace"),
                extensions=["tables", "fenced_code"],
            )
            html = (
                f"<html><head><meta charset='utf-8'><style>{_MD_CSS}</style></head>"
                f"<body><div class='doc-kicker'>{info.ipn} · Rev {info.rev}</div>{body}</body></html>"
            )
            weasyprint.HTML(string=html, base_url=str(info.src.parent)).write_pdf(str(out_pdf))
    except Exception as e:
        print(f"  doc compile failed for {info.src.name}: {e}")
        return False
    return True
