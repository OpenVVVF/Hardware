"""Suggestion engine for generic passives and missing parts."""

import re
from typing import Any, Dict, List, Optional

from .db import PartDatabase
from .parsers import LineItem
from .vendors import DigiKeyClient, MouserClient, OctopartClient


def _value_to_query(designation: str, footprint: str) -> str:
    """Turn a KiCad passive value into a search query."""
    des = designation.strip().lower()
    fp = footprint.lower()
    size = "1210"
    if "1210" in fp:
        size = "1210"
    elif "0805" in fp:
        size = "0805"
    elif "1206" in fp:
        size = "1206"

    # Expand shorthand values
    val = des
    if val.endswith("k"):
        val = val.replace("k", "kohm")
    elif val.endswith("m"):
        val = val.replace("m", "mohm")
    elif re.fullmatch(r"\d+", val):
        val = val + " ohm"
    elif re.fullmatch(r"\d+pf", val):
        val = val.replace("pf", "pF")
    elif re.fullmatch(r"\d+nf", val):
        val = val.replace("nf", "nF")
    elif re.fullmatch(r"\d+uf", val):
        val = val.replace("uf", "uF")

    if fp.startswith("r_"):
        return f"{size} {val} resistor"
    if fp.startswith("c_"):
        return f"{size} {val} capacitor"
    return f"{size} {val}"


def suggest_part(
    item: LineItem,
    db: PartDatabase,
    mouser: Optional[MouserClient] = None,
    digikey: Optional[DigiKeyClient] = None,
    octopart: Optional[OctopartClient] = None,
) -> Optional[Dict[str, Any]]:
    """Return the best suggested vendor record for a missing generic passive."""
    if not _is_generic_passive(item.footprint, item.designation):
        return None

    query = _value_to_query(item.designation, item.footprint)
    candidates: List[Dict[str, Any]] = []

    for client in [octopart, mouser, digikey]:
        if client and client.enabled():
            # Try value as a part number first, then fallback to keyword search if available.
            result = client.search(query)
            if result and result.get("unit_price") is not None:
                candidates.append(result)

    if not candidates:
        return None

    # Prefer cheapest in-stock option
    def score(c: Dict[str, Any]) -> float:
        price = c.get("unit_price") or 1e9
        stock = str(c.get("stock", "")).lower()
        in_stock = "in stock" in stock or any(ch.isdigit() for ch in stock)
        return price if in_stock else price + 1e6

    return min(candidates, key=score)


def _is_generic_passive(footprint: str, designation: str) -> bool:
    fp = footprint.lower()
    return (fp.startswith("r_1210") or fp.startswith("c_1210")) and bool(designation.strip())


def interactive_suggest(
    items: List[LineItem],
    db: PartDatabase,
    mouser: Optional[MouserClient] = None,
    digikey: Optional[DigiKeyClient] = None,
    octopart: Optional[OctopartClient] = None,
) -> None:
    """Interactively suggest and store substitutions for missing parts."""
    missing = db.list_missing(items)
    for item in missing:
        key = db.normalize_key(item.footprint, item.designation)
        print(f"\nMissing: {item.footprint} | {item.designation} (qty {item.quantity})")
        suggestion = suggest_part(item, db, mouser, digikey, octopart)
        if suggestion:
            print(f"  Suggestion: {suggestion.get('description')}")
            print(f"  Price: ${suggestion.get('unit_price')} from {suggestion.get('source')}")
        else:
            print(f"  Search query: {_value_to_query(item.designation, item.footprint)}")

        ans = input("  Add to database? [y/n/edit] ").strip().lower()
        if ans == "y" and suggestion:
            entry = {
                "description": suggestion.get("description", ""),
                "customer_part": suggestion.get("manufacturer_pn", item.designation),
                "type": "resistor" if item.footprint.lower().startswith("r_") else "capacitor",
                "mouser_part": suggestion.get("mouser_part", ""),
                "digikey_part": suggestion.get("digikey_part", ""),
                "octopart_uid": suggestion.get("octopart_uid", ""),
                "mcmaster_part": "",
                "sendcutsend_id": "",
                "manual_price": suggestion.get("unit_price"),
                "price_currency": "USD",
                "price_updated": None,
                "notes": f"Suggested from {suggestion.get('source', '')}",
            }
            db.add(item.footprint, item.designation, entry)
            db.save()
            print("  Saved.")
        elif ans in ("e", "edit"):
            desc = input("  Description: ").strip()
            mouser = input("  Mouser P/N: ").strip()
            digikey = input("  DigiKey P/N: ").strip()
            price = input("  Manual price USD (or blank): ").strip()
            entry = {
                "description": desc or item.designation,
                "customer_part": item.designation,
                "type": "resistor" if item.footprint.lower().startswith("r_") else "capacitor",
                "mouser_part": mouser,
                "digikey_part": digikey,
                "octopart_uid": "",
                "mcmaster_part": "",
                "sendcutsend_id": "",
                "manual_price": float(price) if price else None,
                "price_currency": "USD",
                "price_updated": None,
                "notes": "",
            }
            db.add(item.footprint, item.designation, entry)
            db.save()
            print("  Saved.")
