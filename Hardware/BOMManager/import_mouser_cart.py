#!/usr/bin/env python3
"""Import Mouser cart/order prices into the price cache.

Paste Mouser cart, order confirmation, or BOM tool output and this script will
extract Mouser part numbers with prices and update price_cache.json.

If you paste a BOM tool output without prices, the tool will list the detected
part numbers so you can confirm the upload matched your BOM.

Example usage:
    python3 import_mouser_cart.py
    # paste text, then press Ctrl+D

    python3 import_mouser_cart.py order.txt
    python3 import_mouser_cart.py mouser_bom.xlsx
"""

import argparse
import re
import sys
from pathlib import Path

from bom_manager.pricing import PriceCache, PriceInfo


MOUSER_PN_RE = re.compile(r"\b(\d{2,3}-[A-Z0-9]+(?:-[A-Z0-9]+)*)\b")

# Known bad part-number mappings from our BOM that Mouser resolved incorrectly.
# Maps input Mouser PN -> corrected PN to use as the cache key.
INPUT_PN_OVERRIDES = {
    "80-C1210C221K1GACTU": "80-C1210C221J2G",
}
PRICE_RE = re.compile(r"\$?([0-9,]+\.\d{2,4})", re.IGNORECASE)
PRICE_HINT_RE = re.compile(
    r"(?:Each|Unit|Price|Total|Ext Price|Order Total)[\s:]*\$?([0-9,]+\.\d{2,4})",
    re.IGNORECASE,
)


def _looks_like_header(line: str) -> bool:
    lowered = line.lower()
    return any(
        word in lowered
        for word in [
            "mouser part number",
            "manufacturer part",
            "customer part",
            "description",
            "quantity",
            "vendor",
            "internal p/n",
            "bill of materials",
        ]
    )


def _tabular_columns(line: str) -> list:
    """Split a tabular pasted line into columns."""
    return [c.strip() for c in re.split(r"\s{2,}|\t", line.strip()) if c.strip()]


def _find_qty_from_columns(cols: list, pn: str) -> int | None:
    """Guess the quantity column from a tabular row that contains a Mouser PN."""
    try:
        pn_index = next(i for i, c in enumerate(cols) if pn in c)
    except StopIteration:
        pn_index = -1

    # Common layouts:
    #   row#  qty  internal_pn  description  customer_pn  mpn  vendor  mouser_pn
    #   qty   mouser_pn
    # If pn is far to the right, the quantity is usually the second column.
    if pn_index >= 0 and len(cols) > pn_index:
        for idx in (1, 0, pn_index - 1):
            if 0 <= idx < len(cols):
                val = cols[idx].replace(",", "").split()[0]
                if val.isdigit():
                    qty = int(val)
                    if 1 <= qty <= 100000:
                        return qty
    return None


def parse_mouser_text(text: str) -> dict:
    """Extract Mouser part numbers and any nearby prices from pasted text.

    Returns a dict mapping mouser_part -> {"unit_price": float|None, "qty": int|None}
    """
    lines = [line.strip() for line in text.splitlines()]
    results = {}

    for i, line in enumerate(lines):
        for m in MOUSER_PN_RE.finditer(line):
            pn = m.group(1)
            if _looks_like_header(line):
                continue
            if pn in results:
                continue

            unit_price = None
            qty = None

            # Try tabular quantity extraction first.
            cols = _tabular_columns(line)
            qty = _find_qty_from_columns(cols, pn)

            # Look in this line and a few following lines for price hints.
            window = lines[i : min(i + 8, len(lines))]
            for wline in window:
                price_hint = PRICE_HINT_RE.search(wline)
                if price_hint:
                    try:
                        unit_price = float(price_hint.group(1).replace(",", ""))
                    except ValueError:
                        pass
                    break

            # If no explicit price hint, look for any dollar amount nearby.
            if unit_price is None:
                for wline in window:
                    price_match = PRICE_RE.search(wline)
                    if price_match:
                        try:
                            unit_price = float(price_match.group(1).replace(",", ""))
                        except ValueError:
                            pass
                        break

            # Fallback quantity search on nearby lines (skip the PN line to avoid
            # matching the leading digits of the Mouser part number itself).
            if qty is None:
                for wline in lines[max(0, i - 2) : min(i + 5, len(lines))]:
                    if pn in wline:
                        continue
                    qty_match = re.search(
                        r"(?:qty|quantity|pack of|order qty)[\s:]*(\d{1,4})\b",
                        wline,
                        re.IGNORECASE,
                    )
                    if qty_match:
                        qty = int(qty_match.group(1))
                        break

            results[pn] = {"unit_price": unit_price, "qty": qty}

    return results


def parse_mouser_excel(path: Path) -> dict:
    """Extract Mouser part numbers and unit prices from a Mouser BOM Excel export.

    Uses the 'Mouser Part Number (Input)' and 'Order Unit Price (USD)' columns.
    If the input PN is known to resolve incorrectly, INPUT_PN_OVERRIDES maps it
    to the corrected cache key.
    """
    import pandas as pd

    df = pd.read_excel(path)
    results = {}
    for _, row in df.iterrows():
        input_pn = str(row.get("Mouser Part Number (Input)", "")).strip()
        matched_pn = str(row.get("Mouser Part Number", "")).strip()
        price_raw = row.get("Order Unit Price (USD)", "")
        qty_raw = row.get("Quantity 1", "")

        # Decide which PN to use as the cache key.
        pn = input_pn or matched_pn
        if pn in INPUT_PN_OVERRIDES:
            corrected = INPUT_PN_OVERRIDES[pn]
            # Only trust the price if Mouser actually matched the corrected PN.
            if matched_pn and matched_pn != corrected:
                print(f"Skipping {pn}: Mouser matched {matched_pn} instead of corrected {corrected}", file=sys.stderr)
                continue
            pn = corrected

        if not pn or pn.lower() == "nan":
            continue

        unit_price = None
        try:
            if isinstance(price_raw, str):
                price_raw = price_raw.replace("$", "").replace(",", "").strip()
            unit_price = float(price_raw)
        except (ValueError, TypeError):
            continue

        qty = None
        try:
            qty = int(float(qty_raw))
        except (ValueError, TypeError):
            pass

        results[pn] = {"unit_price": unit_price, "qty": qty}

    return results


def main() -> int:
    parser = argparse.ArgumentParser(description="Import Mouser cart/order prices.")
    parser.add_argument("input", nargs="?", help="Cart/order text or .xlsx file (default: stdin)")
    parser.add_argument("--cache", type=Path, default=None, help="Path to price_cache.json")
    parser.add_argument(
        "--allow-zero",
        action="store_true",
        help="Accept $0.00 prices instead of treating them as missing",
    )
    args = parser.parse_args()

    if args.input:
        input_path = Path(args.input)
        if input_path.suffix.lower() in (".xlsx", ".xls"):
            parts = parse_mouser_excel(input_path)
        else:
            text = input_path.read_text(encoding="utf-8")
            if not text.strip():
                print("No input text provided.", file=sys.stderr)
                return 1
            parts = parse_mouser_text(text)
    else:
        print("Paste Mouser cart/order/BOM text, then press Ctrl+D:", file=sys.stderr)
        text = sys.stdin.read()
        if not text.strip():
            print("No input text provided.", file=sys.stderr)
            return 1
        parts = parse_mouser_text(text)
    if not parts:
        print("No Mouser part numbers found.", file=sys.stderr)
        print("Hint: Mouser part numbers look like 80-C1210C104K1RAC or 667-ERJ-P14F2002U.", file=sys.stderr)
        return 1

    if args.cache is None:
        args.cache = Path(__file__).resolve().parent / "bom_manager" / "data" / "price_cache.json"
    cache = PriceCache(args.cache)

    cached_count = 0
    skipped_count = 0
    for pn, info in sorted(parts.items()):
        price = info["unit_price"]
        if price is None:
            print(f"Detected (no price): {pn}")
            skipped_count += 1
            continue
        if price == 0.0 and not args.allow_zero:
            print(f"Detected (zero price skipped): {pn}")
            skipped_count += 1
            continue

        cache.set(
            PriceInfo(
                vendor="mouser",
                part_number=pn,
                unit_price=price,
                source="mouser_cart_import",
            )
        )
        print(f"Updated {pn}: ${price:.4f}")
        cached_count += 1

    cache.save()
    print(f"\nSaved {cached_count} price(s), detected {skipped_count} part number(s) without price.")
    print(f"Cache written to {args.cache}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
