"""Import JLCPCB cart/order prices for PCB fab lines.

Paste JLCPCB shopping-cart or order-confirmation text and this will extract
each PCB prototype item (board name, quantity, total price), update
price_cache.json under vendor "pcb", and write the per-board price into the
committed part database (key pcb|<board>) so PCB fab costs are tracked.
"""

import argparse
import re
import sys
from pathlib import Path

from ..context import Context
from ..pricing import PriceInfo
from . import persist_prices_to_db


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


def _ensure_pcb_entry(ctx: Context, board: str, unit_price: float) -> None:
    """Create the pcb|<board> part-database entry if missing so the fab price
    has a committed home. The description must stay exactly the board name:
    descriptor and internal-PN registry keys are derived from it."""
    if ctx.db.lookup("PCB", board):
        return
    ctx.db.add("PCB", board, {
        "description": board,
        "customer_part": "",
        "type": "pcb",
        "mouser_part": "",
        "digikey_part": "",
        "octopart_uid": "",
        "mcmaster_part": "",
        "sendcutsend_id": "",
        "manual_price": unit_price,
        "price_currency": "USD",
        "price_updated": None,
        "notes": "Created by import jlcpcb",
    })


def run(argv, ctx: Context) -> int:
    parser = argparse.ArgumentParser(prog="import jlcpcb", description="Import JLCPCB order/cart prices.")
    parser.add_argument("input", nargs="?", help="Order/cart text file (default: stdin)")
    args = parser.parse_args(argv)

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

    cache = ctx.cache
    imported = {}

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
        imported[board] = unit_price
        _ensure_pcb_entry(ctx, board, unit_price)
        print(f"Updated {board}: ${unit_price:.2f}/board (qty {qty})")
        cached_count += 1

    cache.save()
    print(f"\nSaved {cached_count} PCB fab price(s).")

    if imported:
        updated = persist_prices_to_db(ctx.db, "pcb", imported)
        ctx.db.save()
        print(f"Persisted {updated} PCB price(s) into the part database.")
    return 0
