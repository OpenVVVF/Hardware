"""Part-database workflow helpers: live-BOM join, query resolution, add wizard.

These back the interactive shell commands (parts / add / price / pack / stock /
exclude). Everything works against the *live* BOM (discovery -> parse ->
aggregate) joined with the part database, so you always see the parts your
project actually uses.
"""

from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime
from typing import Dict, List, Optional, Tuple

from .bom import BomLine, aggregate_bom
from .context import Context
from .db import VENDOR_PN_FIELD
from .descriptor_registry import DescriptorRegistry
from .discover import discover_boms
from .parsers import LineItem, harness_qty, parse_source
from .part_numbers import line_identity


def collect_items(ctx: Context, chassis_filter=None, board_filter=None, hardware_root=None):
    """Discover and parse all BOM sources, then add one synthetic assembly line
    per board (PCB fab) and per wiring harness.

    Returns (num_sources, items_by_chassis, boards_by_chassis, harnesses_by_chassis).
    """
    hw_root = hardware_root or ctx.hardware_root
    sources = list(discover_boms(hw_root, chassis_filter, board_filter))
    items_by_chassis: dict = defaultdict(list)
    boards_by_chassis: dict = defaultdict(set)
    harnesses_by_chassis: dict = defaultdict(set)
    for src in sources:
        for item in parse_source(src):
            items_by_chassis[src.chassis].append(item)
            if src.category == "board":
                boards_by_chassis[src.chassis].add(src.board)
            elif src.category == "harness":
                harnesses_by_chassis[src.chassis].add(src.board)

    for chassis, boards in boards_by_chassis.items():
        for board in boards:
            items_by_chassis[chassis].append(
                LineItem(
                    chassis=chassis,
                    source=board,
                    category="pcb_fab",
                    footprint="PCB",
                    designation=board,
                    quantity=1,
                    designators="",
                    vendor_hint="pcb",
                )
            )

    for chassis, harnesses in harnesses_by_chassis.items():
        for harness in harnesses:
            asm_qty = max(
                (harness_qty(src.path) for src in sources
                 if src.category == "harness" and src.chassis == chassis and src.board == harness),
                default=1,
            )
            items_by_chassis[chassis].append(
                LineItem(
                    chassis=chassis,
                    source=harness,
                    category="harness_asm",
                    footprint="WH",
                    designation=harness,
                    quantity=asm_qty,
                    designators="",
                    vendor_hint="assembly",
                )
            )
    return len(sources), items_by_chassis, boards_by_chassis, harnesses_by_chassis


def collect_lines(ctx: Context, chassis: Optional[str] = None) -> List[Tuple[str, BomLine]]:
    """Aggregate live BOM lines, tagged with their chassis."""
    _, items_by_chassis, _, _ = collect_items(
        ctx, [chassis] if chassis else None, None
    )
    out: List[Tuple[str, BomLine]] = []
    for ch in sorted(items_by_chassis):
        for line in aggregate_bom(iter(items_by_chassis[ch]), ctx.db, inventory=ctx.inventory):
            out.append((ch, line))
    return out


def existing_ipn(ctx: Context, chassis: str, line: BomLine) -> str:
    """The internal PN a line already has in the registry (no creation, no prompts)."""
    abbr = DescriptorRegistry.abbreviate_chassis(chassis)
    reg = ctx.pn_registry
    entry = reg.lookup(reg._make_identity_key(line.type, line_identity(line), abbr))
    if entry is None:
        # Legacy (description-based) key from before identity keys existed.
        entry = reg.lookup(reg._make_key(line.type, line.description, abbr))
    return entry["part_number"] if entry else ""


def known_price(ctx: Context, line: BomLine) -> Tuple[Optional[float], str]:
    """Best offline-known price for a line: folder info, cache, then manual.
    Never hits the network. Returns (unit_price, source)."""
    vendor = line.primary_vendor()
    if vendor == "sendcutsend":
        meta = line.metadata.get("UnitPrice", "")
        if meta:
            try:
                return float(meta), "info.txt"
            except ValueError:
                pass
        if line.manual_price is not None:
            return line.manual_price, "manual"
        return None, ""
    pn = line.vendor_part_number(vendor)
    if vendor and pn:
        cached = ctx.cache.get(vendor, pn)
        if cached and cached.unit_price is not None:
            return cached.unit_price, cached.source
    if line.manual_price is not None:
        return line.manual_price, "manual"
    return None, ""


@dataclass
class PartMatch:
    """A part-database key plus the live BOM line and DB entry behind it."""

    key: str
    footprint: str
    designation: str
    entry: Optional[dict] = None
    line: Optional[BomLine] = None
    chassis: str = ""
    internal_pn: str = ""
    score: int = 1  # 0 = exact, 1 = fuzzy

    def label(self) -> str:
        bits = []
        if self.internal_pn:
            bits.append(self.internal_pn)
        desc = (self.entry or {}).get("description") or (
            self.line.description if self.line else self.designation
        )
        bits.append(desc)
        if self.line:
            bits.append(f"qty {self.line.quantity}")
        return " | ".join(bits)


def resolve(ctx: Context, query: str, chassis: Optional[str] = None) -> List[PartMatch]:
    """Find parts matching a query: exact vendor/internal PN first, then fuzzy
    substring over designation and description."""
    q = query.strip()
    if not q:
        return []
    ql, qu = q.lower(), q.upper()
    matches: Dict[str, PartMatch] = {}

    def add(footprint, designation, entry, line, ch, score):
        key = ctx.db.normalize_key(footprint, designation)
        m = matches.get(key)
        if m is None:
            m = PartMatch(key, footprint, designation, entry, line, ch, "", score)
            matches[key] = m
        else:
            if entry is not None:
                m.entry = entry
            if line is not None:
                m.line = line
                m.chassis = ch
            m.score = min(m.score, score)

    # Live BOM lines (also yields internal PNs).
    for ch, line in collect_lines(ctx, chassis):
        entry = ctx.db.lookup(line.footprint, line.designation)
        ipn = existing_ipn(ctx, ch, line)
        score = None
        if ipn and ipn.upper() == qu:
            score = 0
        elif line.designation.lower() == ql:
            score = 0
        elif ql in line.description.lower() or ql in line.designation.lower() or (ipn and qu in ipn.upper()):
            score = 1
        if score is not None:
            add(line.footprint, line.designation, entry, line, ch, score)
            matches[ctx.db.normalize_key(line.footprint, line.designation)].internal_pn = ipn

    # DB entries not in the live BOM (e.g. stocked hardware from another build).
    for key, entry in ctx.db.all_entries().items():
        pns = [entry.get(f) or "" for f in VENDOR_PN_FIELD.values()]
        pns.append(entry.get("customer_part") or "")
        exact = any(p.lower() == ql for p in pns if p)
        fuzzy = ql in key or ql in (entry.get("description") or "").lower()
        if exact or fuzzy:
            footprint, _, designation = key.partition("|")
            add(footprint, designation, entry, None, "", 0 if exact else 1)

    return sorted(matches.values(), key=lambda m: (m.score, m.label().lower()))


def pick(matches: List[PartMatch], what: str = "part") -> Optional[PartMatch]:
    """Resolve ambiguity: return the single match, or prompt with a numbered list."""
    if not matches:
        return None
    if len(matches) == 1:
        return matches[0]
    print(f"Multiple {what}s match:")
    for i, m in enumerate(matches, 1):
        print(f"  {i}. {m.label()}")
    try:
        ans = input(f"Select 1-{len(matches)} (blank to cancel): ").strip()
    except EOFError:
        return None
    if not ans:
        return None
    try:
        idx = int(ans)
        if 1 <= idx <= len(matches):
            return matches[idx - 1]
    except ValueError:
        pass
    print("Invalid selection.")
    return None


def ensure_entry(ctx: Context, match: PartMatch) -> dict:
    """Return the DB entry for a match, creating a minimal one when needed."""
    if match.entry is not None:
        return match.entry
    entry = {
        "description": match.line.description if match.line else match.designation,
        "customer_part": "",
        "type": match.line.type if match.line else "other",
        "mouser_part": "",
        "digikey_part": "",
        "octopart_uid": "",
        "mcmaster_part": "",
        "sendcutsend_id": "",
        "manual_price": None,
        "price_currency": "USD",
        "price_updated": None,
        "notes": "",
    }
    # Vendor-list rows (MechanicalBOM) carry the PN in the designation — seed
    # the matching vendor field so imports and lookups attach cleanly.
    if match.line:
        hint = (match.line.vendor_hint or "").lower()
        field = VENDOR_PN_FIELD.get(hint)
        if field and match.line.footprint.strip().lower() == hint:
            entry[field] = match.designation
    ctx.db.add(match.footprint, match.designation, entry)
    match.entry = entry
    return entry


def needs_attention(ctx: Context, chassis: Optional[str] = None) -> List[Tuple[str, BomLine, str]]:
    """Live lines that need human input, with a reason for each.

    Fabricated parts (SendCutSend folders) and PCB fab lines are priced via
    info.txt / fab prices, not vendor PNs — they're only flagged when unpriced.
    """
    out = []
    for ch, line in collect_lines(ctx, chassis):
        entry = ctx.db.lookup(line.footprint, line.designation)
        price, _ = known_price(ctx, line)
        vendor = line.primary_vendor()
        if vendor == "assembly":
            # Harness assembly lines: cost is the sum of their components;
            # an optional manual price can cover labor, but isn't required.
            continue
        if vendor == "sendcutsend":
            if price is None:
                out.append((ch, line, "no price in info.txt (run: import sendcutsend)"))
            continue
        if line.type == "pcb":
            if price is None:
                out.append((ch, line, "no fab price (use: fab pcb-price)"))
            continue
        if entry is None:
            out.append((ch, line, "no database entry"))
            continue
        has_pn = any(entry.get(f) for f in VENDOR_PN_FIELD.values())
        if not has_pn and price is None:
            out.append((ch, line, "no vendor PN or price"))
    return out


def add_wizard(ctx: Context, chassis: Optional[str] = None) -> int:
    """Walk through lines that need attention and add DB entries for them."""
    pending = needs_attention(ctx, chassis)
    if not pending:
        print("Nothing needs attention — every BOM line has a database entry and a price/PN.")
        return 0

    print(f"\n{len(pending)} line(s) need attention:\n")
    for i, (ch, line, reason) in enumerate(pending, 1):
        srcs = ", ".join(s.split("/", 1)[-1] for s in line.sources[:3])
        print(f"  {i:>2}. [{line.type}] {line.description}  (qty {line.quantity}, {reason}; {srcs})")
    try:
        sel = input("\nAdd which? (numbers/ranges, 'all', blank to cancel): ").strip()
    except EOFError:
        return 1
    if not sel:
        return 1

    indices: List[int] = []
    if sel.lower() == "all":
        indices = list(range(len(pending)))
    else:
        for token in sel.replace(",", " ").split():
            if "-" in token:
                a, _, b = token.partition("-")
                if a.isdigit() and b.isdigit():
                    indices.extend(range(int(a) - 1, int(b)))
                continue
            if token.isdigit():
                indices.append(int(token) - 1)
    indices = [i for i in dict.fromkeys(indices) if 0 <= i < len(pending)]
    if not indices:
        print("Nothing selected.")
        return 1

    saved = 0
    for i in indices:
        ch, line, reason = pending[i]
        print(f"\n--- [{line.type}] {line.description} (qty {line.quantity}, footprint: {line.footprint or '-'})")

        if line.type == "pcb":
            print("  PCB fab lines are priced with: fab pcb-price <board> <usd>")
            continue
        if line.primary_vendor() == "sendcutsend":
            print("  Fabricated-part pricing lives in the part's info.txt (or run: import sendcutsend).")
            continue

        entry = ctx.db.lookup(line.footprint, line.designation) or {}
        existing = ctx.db.lookup(line.footprint, line.designation)

        def ask(prompt_text: str, current: str = "") -> str:
            suffix = f" [{current}]" if current else ""
            try:
                ans = input(f"  {prompt_text}{suffix}: ").strip()
            except EOFError:
                ans = ""
            return ans or current

        description = ask("Description", entry.get("description", line.description))
        comp_type = ask("Type", entry.get("type", line.type))
        mouser = ask("Mouser P/N (or DNO)", entry.get("mouser_part", ""))
        digikey = ask("DigiKey P/N", entry.get("digikey_part", ""))
        mcmaster = ask("McMaster P/N", entry.get("mcmaster_part", ""))
        price_txt = ask("Manual unit price USD", str(entry.get("manual_price") or ""))
        pack_txt = ask("Pack size (units per pack, 1 = each)", str(entry.get("pack_size") or "1"))
        notes = ask("Notes", entry.get("notes", ""))

        try:
            manual_price = float(price_txt) if price_txt else None
        except ValueError:
            print(f"  Skipped: invalid price {price_txt!r}.")
            continue
        try:
            pack_size = max(1, int(pack_txt))
        except ValueError:
            pack_size = 1

        new_entry = {
            "description": description,
            "customer_part": entry.get("customer_part", ""),
            "type": comp_type,
            "mouser_part": mouser,
            "digikey_part": digikey,
            "octopart_uid": entry.get("octopart_uid", ""),
            "mcmaster_part": mcmaster,
            "sendcutsend_id": entry.get("sendcutsend_id", ""),
            "manual_price": manual_price,
            "price_currency": "USD",
            "price_updated": datetime.utcnow().date().isoformat() if manual_price is not None else entry.get("price_updated"),
            "pack_size": pack_size,
            "notes": notes,
        }
        ctx.db.add(line.footprint, line.designation, new_entry)
        ctx.db.save()
        saved += 1
        print(f"  Saved ({'updated' if existing else 'new'} entry).")

    print(f"\n{saved} entr{'ies' if saved != 1 else 'y'} saved.")
    return 0
