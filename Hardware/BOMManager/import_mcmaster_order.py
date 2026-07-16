#!/usr/bin/env python3
"""Import McMaster order/cart prices into the price cache.

Paste McMaster order confirmation, cart, or "Paste part numbers and quantities"
output and this script will extract McMaster part numbers with prices and update
price_cache.json.

If you paste text without prices (e.g. the cart paste format), the tool will list
the detected part numbers so you can confirm the upload matched your BOM.

Example usage:
    python3 import_mcmaster_order.py
    # paste text, then press Ctrl+D

    python3 import_mcmaster_order.py order.txt
"""

import argparse
import re
import sys
from pathlib import Path

from bom_manager.pricing import PriceCache, PriceInfo


MCMASTER_PN_RE = re.compile(r"\b([A-Z0-9]{5,12})\b")
PRICE_RE = re.compile(r"\$?([0-9,]+\.\d{2})")
TOTAL_HINT_RE = re.compile(r"(?:Merchandise|Order Total|Subtotal)[\s:]*\$?([0-9,]+\.\d{2})", re.IGNORECASE)
EACH_PACK_RE = re.compile(r"\b(Each|Pack)\b", re.IGNORECASE)


def _is_probably_mcmaster_pn(text: str) -> bool:
    """McMaster PNs are typically 5-12 alphanumeric chars with at least one letter and one digit."""
    if not re.fullmatch(r"[A-Z0-9]{5,12}", text.upper()):
        return False
    has_letter = any(c.isalpha() for c in text)
    has_digit = any(c.isdigit() for c in text)
    return has_letter and has_digit


def _trailing_prices(line: str) -> list:
    """Return the trailing decimal-number tokens on a line (McMaster table view)."""
    tokens = line.strip().split()
    prices = []
    for t in reversed(tokens):
        if re.fullmatch(r"[0-9,]+\.\d{2}", t.replace(",", "")):
            prices.append(float(t.replace(",", "")))
        elif prices:
            # Stop once we hit a non-price after collecting prices
            break
    return list(reversed(prices))


def _extract_pns_with_positions(lines: list) -> list:
    """Return list of (line_index, pn) for every likely McMaster part number."""
    found = []
    for i, line in enumerate(lines):
        for m in MCMASTER_PN_RE.finditer(line):
            pn = m.group(1).upper()
            if _is_probably_mcmaster_pn(pn):
                found.append((i, pn))
    return found


def _extract_price_groups(lines: list) -> list:
    """Extract price groups from PDF-style McMaster text.

    A price group is a short window containing at least two dollar prices and
    an 'Each' or 'Pack' indicator. Returns list of (first_price_line, qty,
    unit_price, total_price). The first-price line is used to match the group to
    the preceding part number.
    """
    groups = []
    i = 0
    while i < len(lines):
        window = lines[i : i + 6]
        prices = []
        for j, line in enumerate(window):
            for m in PRICE_RE.finditer(line.strip()):
                prices.append((j, m))
        if len(prices) >= 2:
            has_indicator = any(
                EACH_PACK_RE.search(line) for line in window
            )
            if has_indicator:
                p1_line, p1 = prices[0]
                p2_line, p2 = prices[1]
                unit = float(p1.group(1).replace(",", ""))
                total = float(p2.group(1).replace(",", ""))
                # Sanity: unit price should not exceed line total for qty >= 1.
                if unit > total:
                    unit, total = total, unit

                # Derive quantity from total/unit when possible. This is more
                # reliable than parsing integer lines because PDF text often
                # interleaves line numbers, quantities, and page markers.
                qty = 1
                if unit > 0:
                    derived = round(total / unit)
                    if abs(derived - total / unit) < 0.01 and 1 <= derived <= 9999:
                        qty = int(derived)

                first_price_line = i + p1_line
                groups.append((first_price_line, qty, unit, total))
                i += max(p1_line, p2_line) + 1
                continue
        i += 1
    return groups


def _parse_pdf_style(text: str) -> dict:
    """Parse McMaster order/cart text copied from PDF or web confirmation.

    Returns dict mapping part_number -> {"unit_price": float|None, "qty": int|None}
    """
    lines = text.splitlines()
    pns = _extract_pns_with_positions(lines)
    groups = _extract_price_groups(lines)
    results = {}
    used_groups = set()

    for pn_line, pn in pns:
        chosen = None
        for idx, (g_line, qty, unit, total) in enumerate(groups):
            if idx in used_groups:
                continue
            if g_line > pn_line:
                chosen = idx
                break
        if chosen is not None:
            _, qty, unit, _ = groups[chosen]
            results[pn] = {"unit_price": unit, "qty": qty}
            used_groups.add(chosen)
        else:
            results[pn] = {"unit_price": None, "qty": None}

    return results


def _line_qty(line: str) -> int | None:
    """Try to extract quantity from a McMaster order line."""
    # Table view: "1       1       Pack of 100 each        90128A232 ..."
    # The first number is the line number, the second is the quantity.
    tokens = line.strip().split()
    if tokens and tokens[0].isdigit():
        idx = 1
        # Skip line number
        while idx < len(tokens) and not tokens[idx].isdigit():
            idx += 1
        if idx < len(tokens):
            try:
                return int(tokens[idx])
            except ValueError:
                pass

    # Look for explicit qty/pack-of markers
    m = re.search(r"(?:qty|quantity|pack of)[\s:]*(\d{1,4})\b", line, re.IGNORECASE)
    if m:
        return int(m.group(1))
    return None


def parse_mcmaster_text(text: str) -> dict:
    """Extract McMaster part numbers and any nearby prices from pasted text.

    Handles both the web table view and PDF/order-confirmation copy-paste.
    Returns a dict mapping mcmaster_part -> {"unit_price": float|None, "qty": int|None}
    """
    lines = [line.strip() for line in text.splitlines()]
    results = {}

    # --- Table view / simple web text extraction ---
    for i, line in enumerate(lines):
        # Skip summary lines that contain order totals without part numbers.
        if TOTAL_HINT_RE.search(line) and not MCMASTER_PN_RE.search(line):
            continue

        for m in MCMASTER_PN_RE.finditer(line):
            pn = m.group(1).upper()
            if not _is_probably_mcmaster_pn(pn):
                continue
            if pn in results:
                continue

            unit_price = None
            qty = None

            # 1) Table view: trailing prices on the same line.
            trailing = _trailing_prices(line)
            if trailing:
                # First trailing price is usually unit price; last is line total.
                unit_price = trailing[0]

            # 2) Same-line dollar price (non-table view).
            if unit_price is None:
                price_match = PRICE_RE.search(line)
                if price_match:
                    try:
                        unit_price = float(price_match.group(1).replace(",", ""))
                    except ValueError:
                        pass

            # 3) Look ahead 1-2 lines, but ignore order-total lines.
            if unit_price is None:
                for j in range(i + 1, min(i + 3, len(lines))):
                    wline = lines[j]
                    if TOTAL_HINT_RE.search(wline):
                        continue
                    price_match = PRICE_RE.search(wline)
                    if price_match:
                        try:
                            unit_price = float(price_match.group(1).replace(",", ""))
                        except ValueError:
                            pass
                        break

            # Quantity: prefer same line, then nearby.
            qty = _line_qty(line)
            if qty is None:
                for j in range(max(0, i - 1), min(i + 3, len(lines))):
                    if j == i:
                        continue
                    q = _line_qty(lines[j])
                    if q is not None:
                        qty = q
                        break

            results[pn] = {"unit_price": unit_price, "qty": qty}

    # --- PDF/order confirmation extraction ---
    # This is more robust for copy-paste from PDFs where table rows are split
    # or interleaved across page breaks. Only override table-view results when
    # the table-view parser could not find a price.
    pdf_results = _parse_pdf_style(text)
    for pn, info in pdf_results.items():
        if pn not in results:
            results[pn] = info
        elif results[pn]["unit_price"] is None and info["unit_price"] is not None:
            results[pn] = info

    return results


def main() -> int:
    parser = argparse.ArgumentParser(description="Import McMaster order/cart prices.")
    parser.add_argument("input", nargs="?", help="Order/cart text file (default: stdin)")
    parser.add_argument("--cache", type=Path, default=None, help="Path to price_cache.json")
    parser.add_argument(
        "--allow-zero",
        action="store_true",
        help="Accept $0.00 prices instead of treating them as missing",
    )
    args = parser.parse_args()

    if args.input:
        text = Path(args.input).read_text(encoding="utf-8")
    else:
        print("Paste McMaster order/cart text, then press Ctrl+D:", file=sys.stderr)
        text = sys.stdin.read()

    if not text.strip():
        print("No input text provided.", file=sys.stderr)
        return 1

    parts = parse_mcmaster_text(text)
    if not parts:
        print("No McMaster part numbers found.", file=sys.stderr)
        print("Hint: McMaster part numbers look like 94669A199 or 91502A180.", file=sys.stderr)
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
                vendor="mcmaster",
                part_number=pn,
                unit_price=price,
                source="mcmaster_order_import",
            )
        )
        print(f"Updated {pn}: ${price:.2f}")
        cached_count += 1

    cache.save()
    print(f"\nSaved {cached_count} price(s), detected {skipped_count} part number(s) without price.")
    print(f"Cache written to {args.cache}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
