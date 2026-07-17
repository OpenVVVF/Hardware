"""Import DigiKey BOM-tool results into the price cache and part database.

Upload digikey_bom.csv to DigiKey's BOM tool, download the result
(.xlsx despite the '.csv' name), and import it here. Matched lines update
prices; unmatched lines are reported so you can fix the part numbers.
"""

import argparse
import sys
from pathlib import Path

from ..context import Context
from ..pricing import PriceInfo
from . import persist_prices_to_db


def _parse_price(value):
    try:
        if isinstance(value, str):
            value = value.replace("$", "").replace(",", "").strip()
        price = float(value)
        return price if price > 0 else None
    except (ValueError, TypeError):
        return None


def parse_digikey_file(path: Path):
    """Parse a DigiKey BOM-tool export. Returns (prices, unmatched):
    prices: {digikey_pn: unit_price}, unmatched: list of requested strings."""
    import pandas as pd

    # The export carries two disclaimer rows before the 'Index' header row.
    if path.suffix.lower() in (".xlsx", ".xls"):
        df = pd.read_excel(path, skiprows=2)
    else:
        df = pd.read_csv(path, skiprows=2)

    prices = {}
    unmatched = []
    for _, row in df.iterrows():
        dk_pn = str(row.get("Digi-Key Part Number 1", "")).strip()
        if not dk_pn or dk_pn.lower() == "nan":
            requested = str(row.get("Requested Part Number", "")).strip()
            if requested and requested.lower() != "nan":
                unmatched.append(requested)
            continue
        price = _parse_price(row.get("Unit Price 1"))
        if price is not None:
            prices[dk_pn] = price
    return prices, unmatched


def run(argv, ctx: Context) -> int:
    parser = argparse.ArgumentParser(prog="import digikey", description="Import DigiKey BOM-tool prices.")
    parser.add_argument("input", help="DigiKey BOM-tool export (.xlsx or .csv)")
    args = parser.parse_args(argv)

    path = Path(args.input)
    if not path.is_file():
        print(f"File not found: {path}", file=sys.stderr)
        return 1

    prices, unmatched = parse_digikey_file(path)
    if not prices and not unmatched:
        print("No DigiKey lines found. Expected a BOM-tool export with an 'Index' header row.", file=sys.stderr)
        return 1

    for requested in unmatched:
        print(f"Unmatched (fix or ignore): {requested}")

    for pn, price in sorted(prices.items()):
        ctx.cache.set(PriceInfo(vendor="digikey", part_number=pn, unit_price=price, source="digikey_cart_import"))
        print(f"Updated {pn}: ${price:.2f}")
    ctx.cache.save()
    print(f"\nSaved {len(prices)} price(s), {len(unmatched)} unmatched.")

    if prices:
        updated = persist_prices_to_db(ctx.db, "digikey", prices)
        if updated:
            ctx.db.save()
            print(f"Persisted {updated} price(s) into the part database.")
    return 0
