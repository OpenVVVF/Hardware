"""Vendor cart/order importers: pasted text -> price cache + committed part DB."""

from datetime import datetime
from typing import Dict

from ..db import VENDOR_PN_FIELD, PartDatabase


def persist_prices_to_db(db: PartDatabase, vendor: str, prices: Dict[str, float]) -> int:
    """Record imported vendor prices in the committed part database.

    The price cache is gitignored, so without this the real paid prices are
    lost on a fresh clone. Matches entries by the vendor PN field, or by the
    '<vendor>|<pn>' key shape used for vendor-list (MechanicalBOM) rows.
    Returns the number of entries updated.
    """
    field = VENDOR_PN_FIELD.get(vendor.lower())
    today = datetime.utcnow().date().isoformat()
    lowered = {pn.strip().lower(): price for pn, price in prices.items()}
    count = 0
    for key, entry in db.all_entries().items():
        candidates = set()
        if field:
            candidates.add((entry.get(field) or "").strip().lower())
        if "|" in key and key.split("|", 1)[0] == vendor.lower():
            candidates.add(key.split("|", 1)[1])
        hit = next((c for c in candidates if c and c in lowered), None)
        if hit is not None:
            entry["manual_price"] = lowered[hit]
            entry["price_updated"] = today
            count += 1
    return count
