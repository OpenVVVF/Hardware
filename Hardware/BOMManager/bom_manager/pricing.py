"""Price caching, lookup, and markdown report generation."""

import json
import os
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional

from .bom import BomLine
from .vendors import DigiKeyClient, McMasterClient, MouserClient, OctopartClient, SendCutSendClient


@dataclass
class PriceInfo:
    vendor: str
    part_number: str
    unit_price: Optional[float]
    currency: str = "USD"
    stock: str = ""
    source: str = ""
    url: str = ""
    last_updated: Optional[str] = None


class PriceCache:
    def __init__(self, cache_path: Optional[Path] = None):
        if cache_path is None:
            self.cache_path = Path(__file__).resolve().parent / "data" / "price_cache.json"
        else:
            self.cache_path = Path(cache_path)
        self._data: Dict[str, Any] = {}
        self._load()

    def _key(self, vendor: str, part_number: str) -> str:
        return f"{vendor.lower()}:{part_number.strip().lower()}"

    def _load(self) -> None:
        if self.cache_path.exists():
            with open(self.cache_path, "r", encoding="utf-8") as f:
                self._data = json.load(f)
        else:
            self._data = {}

    def save(self) -> None:
        self.cache_path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.cache_path, "w", encoding="utf-8") as f:
            json.dump(self._data, f, indent=2)

    def get(self, vendor: str, part_number: str) -> Optional[PriceInfo]:
        key = self._key(vendor, part_number)
        if key not in self._data:
            return None
        d = self._data[key]
        return PriceInfo(
            vendor=d["vendor"],
            part_number=d["part_number"],
            unit_price=d.get("unit_price"),
            currency=d.get("currency", "USD"),
            stock=d.get("stock", ""),
            source=d.get("source", "cache"),
            url=d.get("url", ""),
            last_updated=d.get("last_updated"),
        )

    def set(self, info: PriceInfo) -> None:
        key = self._key(info.vendor, info.part_number)
        self._data[key] = {
            "vendor": info.vendor,
            "part_number": info.part_number,
            "unit_price": info.unit_price,
            "currency": info.currency,
            "stock": info.stock,
            "source": info.source,
            "url": info.url,
            "last_updated": info.last_updated or datetime.utcnow().isoformat() + "Z",
        }


class PricingEngine:
    def __init__(
        self,
        cache: PriceCache,
        mouser: Optional[MouserClient] = None,
        digikey: Optional[DigiKeyClient] = None,
        octopart: Optional[OctopartClient] = None,
        mcmaster: Optional[McMasterClient] = None,
        sendcutsend: Optional[SendCutSendClient] = None,
    ):
        self.cache = cache
        self.mouser = mouser
        self.digikey = digikey
        self.octopart = octopart
        self.mcmaster = mcmaster
        self.sendcutsend = sendcutsend

    def lookup(self, line: BomLine, refresh: bool = False) -> PriceInfo:
        vendor = line.primary_vendor()
        part_number = line.vendor_part_number(vendor)

        if vendor == "sendcutsend":
            return self._lookup_sendcutsend(line, refresh)

        if not vendor or not part_number or part_number == "DNO":
            return PriceInfo(vendor or "unknown", part_number or "", line.manual_price, source="manual")

        if not refresh:
            cached = self.cache.get(vendor, part_number)
            if cached:
                return cached

        client = None
        if vendor == "mouser":
            client = self.mouser
        elif vendor == "digikey":
            client = self.digikey
        elif vendor == "mcmaster":
            client = self.mcmaster

        if client and client.enabled():
            result = client.search(part_number)
            if result:
                info = PriceInfo(
                    vendor=vendor,
                    part_number=part_number,
                    unit_price=result.get("unit_price"),
                    currency=result.get("currency", "USD"),
                    stock=str(result.get("stock", "")),
                    source=result.get("source", vendor + "_api"),
                    url=result.get("url", ""),
                )
                self.cache.set(info)
                return info

        # Fallback to octopart
        if self.octopart and self.octopart.enabled():
            result = self.octopart.search(part_number)
            if result and result.get("unit_price") is not None:
                info = PriceInfo(
                    vendor=vendor,
                    part_number=part_number,
                    unit_price=result.get("unit_price"),
                    currency=result.get("currency", "USD"),
                    stock=str(result.get("stock", "")),
                    source="octopart_api",
                    url=result.get("url", ""),
                )
                self.cache.set(info)
                return info

        # Manual price fallback
        if line.manual_price is not None:
            info = PriceInfo(vendor, part_number, line.manual_price, source="manual")
            self.cache.set(info)
            return info

        info = PriceInfo(vendor, part_number, None, source="unknown")
        self.cache.set(info)
        return info

    def _lookup_sendcutsend(self, line: BomLine, refresh: bool) -> PriceInfo:
        part_name = line.sendcutsend_id or line.designation.split("|")[0].strip()
        if not refresh:
            cached = self.cache.get("sendcutsend", part_name)
            if cached:
                return cached

        price = None
        url = ""
        source = "sendcutsend_manifest"

        # Folder info.txt price
        unit_price_meta = line.metadata.get("UnitPrice", "")
        if unit_price_meta:
            try:
                price = float(unit_price_meta) or None
                source = "sendcutsend_folder"
            except ValueError:
                price = None

        # Manifest fallback
        if price is None and self.sendcutsend:
            manifest_row = self.sendcutsend.lookup(part_name)
            if manifest_row:
                try:
                    price = float(manifest_row.get("UnitPrice", "") or 0) or None
                except ValueError:
                    price = None
                url = manifest_row.get("URL", "")
                if price is None and url:
                    price = self.sendcutsend.fetch_price(url)

        if price is None:
            price = line.manual_price
        info = PriceInfo("sendcutsend", part_name, price, source=source, url=url)
        self.cache.set(info)
        return info

    def price_lines(self, lines: List[BomLine], refresh: bool = False) -> List[Dict[str, Any]]:
        priced = []
        for line in lines:
            info = self.lookup(line, refresh=refresh)
            total = (info.unit_price or 0) * line.quantity
            priced.append({
                "line": line,
                "price": info,
                "total": total,
            })
        return priced


def generate_markdown_report(
    priced_lines: List[Dict[str, Any]],
    output_path: Path,
    qty: int = 1,
    extra_qtys: Optional[List[int]] = None,
) -> None:
    """Write a markdown price report."""
    if extra_qtys is None:
        extra_qtys = [3, 5, 10, 25]

    lines = sorted(priced_lines, key=lambda x: x["line"].primary_vendor() or "zzzz")

    # Group by vendor
    vendor_groups: Dict[str, List[Dict[str, Any]]] = {}
    for entry in lines:
        vendor = entry["line"].primary_vendor() or "unknown"
        vendor_groups.setdefault(vendor, []).append(entry)

    md = []
    md.append("# Hardware BOM Pricing Report")
    md.append(f"\nGenerated: {datetime.utcnow().isoformat()}Z")
    md.append(f"\nBase quantity: {qty} unit(s)")
    md.append("\n---\n")

    grand_total = 0.0
    unknown = []

    VENDOR_DISPLAY = {
        "mcmaster": "McMaster-Carr",
        "mouser": "Mouser",
        "digikey": "Digi-Key",
        "sendcutsend": "SendCutSend",
        "pcb": "PCB Fabrication",
        "unknown": "Unknown / Missing",
    }
    for vendor, entries in sorted(vendor_groups.items()):
        display = VENDOR_DISPLAY.get(vendor, vendor.title())
        md.append(f"\n## {display}\n")
        md.append("| Qty | Internal P/N | Description | Part Number | Unit Price | Line Total | Source |")
        md.append("|-----|--------------|-------------|-------------|------------|------------|--------|")
        vendor_total = 0.0
        for entry in entries:
            line = entry["line"]
            price = entry["price"]
            unit = price.unit_price
            line_total = entry["total"] * qty
            vendor_total += line_total
            if unit is None:
                unknown.append(f"{line.description} ({vendor})")
            md.append(
                f"| {line.quantity * qty} | {line.internal_pn} | {line.description} | {price.part_number} | "
                f"{'$' + f'{unit:.4f}' if unit is not None else 'N/A'} | "
                f"{'$' + f'{line_total:.2f}' if unit is not None else 'N/A'} | {price.source} |"
            )
        md.append(f"\n**{display} subtotal:** ${vendor_total:.2f}\n")
        grand_total += vendor_total

    md.append("\n---\n")
    md.append(f"## Grand Total ({qty} unit{'s' if qty != 1 else ''}): **${grand_total:.2f}**\n")

    if extra_qtys:
        md.append("\n## Quantity Scaling\n")
        md.append("| Quantity | Estimated Total |")
        md.append("|----------|----------------|")
        for q in extra_qtys:
            total = sum(e["total"] for e in lines) * q
            md.append(f"| {q} | ${total:.2f} |")
        md.append("")

    if unknown:
        md.append("\n## Unknown / Missing Prices\n")
        for u in unknown:
            md.append(f"- {u}")
        md.append("")

    # SendCutSend part details with images
    scs_entries = [e for e in lines if e["line"].primary_vendor() == "sendcutsend"]
    if scs_entries:
        md.append("\n## SendCutSend Part Details\n")
        for entry in scs_entries:
            line = entry["line"]
            price = entry["price"]
            md.append(f"### {line.description}\n")
            md.append(f"- **Qty (per unit):** {line.quantity}")
            md.append(f"- **Material:** {line.metadata.get('Material', 'N/A')}")
            md.append(f"- **Thickness:** {line.metadata.get('Thickness_mm', 'N/A')} mm")
            md.append(f"- **Finish:** {line.metadata.get('Finish', 'N/A')}")
            md.append(f"- **Unit Price:** {'$' + f'{price.unit_price:.2f}' if price.unit_price is not None else 'N/A'}")
            if line.step_path:
                rel_step = os.path.relpath(line.step_path, output_path.parent)
                md.append(f"- **STEP:** `{rel_step}`")
            if line.image_path:
                rel_img = os.path.relpath(line.image_path, output_path.parent)
                md.append(f"\n![{line.description}]({rel_img})\n")
            md.append("")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(md))
