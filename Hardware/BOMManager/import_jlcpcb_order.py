#!/usr/bin/env python3
"""Import JLCPCB cart/order prices into the price cache for PCB fab lines.

Paste JLCPCB shopping-cart or order-confirmation text and this script will
extract each PCB prototype item (board name, quantity, total price) and update
price_cache.json with a per-board unit price under vendor "pcb".

Example usage:
    python3 import_jlcpcb_order.py
    # paste text, then press Ctrl+D

    python3 import_jlcpcb_order.py order.txt
"""

import argparse
import re
import sys
from pathlib import Path

from bom_manager.pricing import PriceCache, PriceInfo


PRICE_RE = re.compile(r"\$?([0-9,]+\.\d{2})")
BOARD_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*_Y\d+$")


def _clean_board_name(name: str) -> str:
    """Strip the JLCPCB suffix like _Y6 from GateDriver_Y6 -> GateDriver."""
    # JLC appends _Y<n> to the board name. Remove it if present.
    name = name.strip()
    m = re.search(r"_Y\d+$", name)
    if m:
        name = name[: m.start()]
    return name


def parse_jlcpcb_text(text: str) -> dict:
    """Extract JLCPCB PCB prototype items from pasted text.

    Returns dict mapping board_name -> {"qty": int, "unit_price": float}
    where unit_price is total / qty.
    """
    lines = [line.strip() for line in text.splitlines()]
    results = {}

    for i, line in enumerate(lines):
        # Board names in JLC cart look like "IOBoard_Y6" immediately before
        # a "PCB prototype:" line.
        if not BOARD_NAME_RE.match(line):
            continue
        if i + 1 >= len(lines) or "PCB prototype:" not in lines[i + 1]:
            continue

        board = _clean_board_name(line)
        qty = None
        total = None

        # Scan the next several lines for quantity and price.
        for j in range(i + 1, min(i + 12, len(lines))):
            wline = lines[j]
            if qty is None and wline.isdigit():
                qty = int(wline)
                continue
            price_match = PRICE_RE.search(wline)
            if price_match and total is None:
                try:
                    total = float(price_match.group(1).replace(",", ""))
                except ValueError:
                    pass

        if board and qty and total is not None:
            results[board] = {"qty": qty, "unit_price": total / qty}

    return results


def main() -> int:
    parser = argparse.ArgumentParser(description="Import JLCPCB order/cart prices.")
    parser.add_argument("input", nargs="?", help="Order/cart text file (default: stdin)")
    parser.add_argument("--cache", type=Path, default=None, help="Path to price_cache.json")
    args = parser.parse_args()

    if args.input:
        text = Path(args.input).read_text(encoding="utf-8")
    else:
        print("Paste JLCPCB order/cart text, then press Ctrl+D:", file=sys.stderr)
        text = sys.stdin.read()

    if not text.strip():
        print("No input text provided.", file=sys.stderr)
        return 1

    parts = parse_jlcpcb_text(text)
    if not parts:
        print("No JLCPCB PCB items found.", file=sys.stderr)
        print("Hint: look for lines like 'IOBoard_Y6' followed by 'PCB prototype:'.", file=sys.stderr)
        return 1

    if args.cache is None:
        args.cache = Path(__file__).resolve().parent / "bom_manager" / "data" / "price_cache.json"
    cache = PriceCache(args.cache)

    cached_count = 0
    for board, info in sorted(parts.items()):
        unit_price = info["unit_price"]
        qty = info["qty"]
        cache.set(
            PriceInfo(
                vendor="pcb",
                part_number=board,
                unit_price=unit_price,
                source="jlcpcb_order_import",
            )
        )
        print(f"Updated {board}: ${unit_price:.2f}/board (qty {qty})")
        cached_count += 1

    cache.save()
    print(f"\nSaved {cached_count} PCB fab price(s).")
    print(f"Cache written to {args.cache}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
