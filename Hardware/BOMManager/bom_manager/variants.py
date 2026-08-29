"""Build-configuration variants: per-chassis `variants.yaml` rules applied to
parsed line items.

Distinct from the `--variants` spares tiers (quantity-only adjustments written
under BOMs/Variants/). A build variant transforms the LineItem list before
aggregation — exclude rules drop parts, add rules inject parts — so a chassis
can describe e.g. a 450 V build that swaps the DC-link capacitor bank without
touching the KiCad design (the schematic stays the 200 V design truth).

Schema (Hardware/<Chassis>/variants.yaml):

    default: 200v            # variant whose outputs land at FabricationData/ root
    variants:
      200v:
        description: "..."
        rules: []            # committed tree as-is
      450v:
        description: "..."
        rules:
          - exclude: {designation: UCS2D331MHD}           # case-insensitive exact
          - exclude: {source: HW-C2-DCLMH-200-PRINTED-B}  # board / fab folder name
          - add: {designation: UCS2W680MHD, footprint: CAPPRD750W80D1800H3700,
                  category: board, source: DCBusCapacitorBoard, quantity: 60,
                  description: "Nichicon 68uF 450V electrolytic"}
          - add: {fab: HW-C2-DCLMH-250-450-PRINTED-A, quantity: 1}
          - setqty: {designation: 98044A224, quantity: 30}  # qty-only change

A fab folder named by an `add: {fab: ...}` rule is variant-only: it is left
out of the base BOM and injected only into the variants that add it. Part
descriptions for added parts come from the part database, same as any other
line; the rule's `description` is recorded in the change log only.
"""

import dataclasses
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import yaml

from .parsers import LineItem, parse_sendcutsend_folder

VARIANTS_FILE = "variants.yaml"


@dataclass
class Change:
    """One applied rule (feeds the comparison report and variants.json)."""

    action: str       # 'exclude' or 'add'
    designation: str
    quantity: int     # pieces removed (negative) or added (positive)
    source: str = ""
    detail: str = ""


@dataclass
class Variant:
    name: str
    description: str = ""
    rules: List[dict] = field(default_factory=list)


@dataclass
class VariantSet:
    default: Optional[str]
    variants: Dict[str, Variant]
    path: Path

    def get(self, name: str) -> Variant:
        try:
            return self.variants[name]
        except KeyError:
            known = ", ".join(sorted(self.variants))
            raise ValueError(f"unknown variant {name!r} in {self.path} (declared: {known})")

    def names(self) -> List[str]:
        """Declared variant names, default first."""
        names = list(self.variants)
        if self.default in self.variants:
            names.remove(self.default)
            names.insert(0, self.default)
        return names

    def variant_fab_sources(self) -> set:
        """Folder names referenced by `add: {fab: ...}` rules (variant-only parts)."""
        out = set()
        for variant in self.variants.values():
            for rule in variant.rules:
                spec = rule.get("add")
                if isinstance(spec, dict) and spec.get("fab"):
                    out.add(str(spec["fab"]).strip().lower())
        return out


def load_chassis_variants(chassis_dir: Path) -> Optional[VariantSet]:
    """Parse Hardware/<Chassis>/variants.yaml; None when the chassis has none."""
    path = Path(chassis_dir) / VARIANTS_FILE
    if not path.is_file():
        return None
    with open(path, "r", encoding="utf-8") as f:
        raw = yaml.safe_load(f) or {}
    if not isinstance(raw, dict) or not isinstance(raw.get("variants"), dict):
        raise ValueError(f"{path}: expected a 'variants' mapping")
    variants: Dict[str, Variant] = {}
    for name, spec in raw["variants"].items():
        spec = spec or {}
        rules = spec.get("rules") or []
        if not isinstance(rules, list):
            raise ValueError(f"{path}: variant {name!r}: 'rules' must be a list")
        for rule in rules:
            kinds = set(rule) if isinstance(rule, dict) else set()
            if kinds not in ({"exclude"}, {"add"}, {"setqty"}) or not isinstance(next(iter(rule.values()), None), dict):
                raise ValueError(
                    f"{path}: variant {name!r}: malformed rule {rule!r} "
                    "(want '- exclude: {{...}}', '- add: {{...}}', or '- setqty: {{...}}')")
        variants[str(name)] = Variant(str(name), str(spec.get("description", "")), rules)
    default = raw.get("default")
    default = str(default) if default is not None else None
    if default is not None and default not in variants:
        raise ValueError(f"{path}: default variant {default!r} is not declared")
    return VariantSet(default, variants, path)


def _rule_qty(spec: dict, default: int) -> int:
    try:
        qty = int(spec.get("quantity", default))
    except (TypeError, ValueError):
        qty = default
    return max(1, qty)


def _fab_item(folder: str, chassis_dir: Optional[Path], items: List[LineItem], variant: Variant) -> LineItem:
    """LineItem for a Mechanical/Fab folder (from its info.txt), for `add: {fab: ...}`."""
    if chassis_dir is not None:
        fab_dir = Path(chassis_dir) / "Mechanical" / "Fab" / folder
        if fab_dir.is_dir():
            return next(parse_sendcutsend_folder(fab_dir, Path(chassis_dir).name, folder))
    for item in items:
        if item.source.lower() == folder.lower():
            return dataclasses.replace(item)
    raise ValueError(
        f"variant {variant.name!r}: add fab {folder!r} — "
        f"no Mechanical/Fab/{folder}/ folder found")


def apply_variant(
    items: List[LineItem],
    variant: Variant,
    chassis_dir: Optional[Path] = None,
) -> Tuple[List[LineItem], List[Change]]:
    """Apply a variant's rules to parsed line items.

    Exclude rules match on designation (case-insensitive exact) or on source
    (board name / fab folder name). Add rules inject a synthetic LineItem —
    either a plain part or, via `fab: FOLDER-NAME`, a Mechanical/Fab folder.
    A rule that matches nothing raises, guarding against silent typo no-ops.
    """
    out = list(items)
    changes: List[Change] = []
    for rule in variant.rules:
        kind, spec = next(iter(rule.items()))
        spec = spec or {}
        if kind == "exclude":
            designation = str(spec.get("designation", "")).strip()
            source = str(spec.get("source", "")).strip()
            if not designation and not source:
                raise ValueError(f"variant {variant.name!r}: exclude rule needs 'designation' or 'source'")
            matched = [i for i in out
                       if (designation and i.designation.lower() == designation.lower())
                       or (source and i.source.lower() == source.lower())]
            if not matched:
                raise ValueError(
                    f"variant {variant.name!r}: exclude rule {spec!r} matched nothing "
                    "(check the designation/source for typos)")
            for item in matched:
                changes.append(Change("exclude", item.designation, -item.quantity, item.source))
            out = [i for i in out if i not in matched]
        elif kind == "setqty":
            designation = str(spec.get("designation", "")).strip()
            source = str(spec.get("source", "")).strip()
            if not designation and not source:
                raise ValueError(f"variant {variant.name!r}: setqty rule needs 'designation' or 'source'")
            if "quantity" not in spec:
                raise ValueError(f"variant {variant.name!r}: setqty rule {spec!r} needs 'quantity'")
            new_qty = _rule_qty(spec, default=1)
            matched = [i for i in out
                       if (designation and i.designation.lower() == designation.lower())
                       or (source and i.source.lower() == source.lower())]
            if not matched:
                raise ValueError(
                    f"variant {variant.name!r}: setqty rule {spec!r} matched nothing "
                    "(check the designation/source for typos)")
            for item in matched:
                if item.quantity != new_qty:
                    changes.append(Change("setqty", item.designation, new_qty - item.quantity,
                                          item.source, detail=f"qty {item.quantity} -> {new_qty}"))
                    out[out.index(item)] = dataclasses.replace(item, quantity=new_qty)
        elif kind == "add":
            fab = str(spec.get("fab", "")).strip()
            if fab:
                # Replace semantics: drop any base items from that folder first.
                out = [i for i in out if i.source.lower() != fab.lower()]
                item = _fab_item(fab, chassis_dir, items, variant)
                item.quantity = _rule_qty(spec, default=item.quantity)
                out.append(item)
                changes.append(Change("add", item.designation, item.quantity, item.source,
                                      detail=f"fab:{fab}"))
            else:
                designation = str(spec.get("designation", "")).strip()
                if not designation:
                    raise ValueError(f"variant {variant.name!r}: add rule needs 'designation' or 'fab'")
                category = str(spec.get("category", "board")).strip()
                chassis = chassis_dir.name if chassis_dir is not None else (out[0].chassis if out else "")
                item = LineItem(
                    chassis=chassis,
                    source=str(spec.get("source", variant.name)).strip(),
                    category=category,
                    footprint=str(spec.get("footprint", "")).strip(),
                    designation=designation,
                    quantity=_rule_qty(spec, default=1),
                    designators="",
                    vendor_hint=str(spec.get("vendor", "")).strip().lower()
                    or ("mouser" if category == "board" else None),
                )
                out.append(item)
                changes.append(Change("add", designation, item.quantity, item.source,
                                      detail=str(spec.get("description", "")).strip()))
        else:
            raise ValueError(f"variant {variant.name!r}: unknown rule kind {kind!r}")
    return out, changes
