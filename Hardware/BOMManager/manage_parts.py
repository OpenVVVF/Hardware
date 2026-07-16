#!/usr/bin/env python3
"""Interactive editor for the part database and SendCutSend manifest."""

import csv
import sys
from pathlib import Path

from bom_manager.config import Config
from bom_manager.db import PartDatabase
from bom_manager.descriptor_registry import DescriptorRegistry
from bom_manager.part_numbers import PartNumberRegistry
from bom_manager.vendors.sendcutsend import SendCutSendClient


def edit_database(db: PartDatabase, pn_registry: PartNumberRegistry) -> None:
    print("\nAdd/Edit Part Database Entry")
    print("Enter 'DNO' as all vendor P/Ns to exclude a part from the BOM.")
    footprint = input("Footprint (exact from BOM): ").strip()
    designation = input("Designation (exact from BOM): ").strip()

    existing = db.lookup(footprint, designation)
    def default(field: str, fallback: str = "") -> str:
        return existing.get(field, fallback) if existing else fallback

    description = input(f"Description [{default('description')}]: ").strip() or default("description")
    customer_part = input(f"Customer P/N [{default('customer_part')}]: ").strip() or default("customer_part")
    comp_type = input(f"Type (resistor/capacitor/chip/other) [{default('type', 'other')}]: ").strip() or default("type", "other")
    mouser = input(f"Mouser P/N [{default('mouser_part')}]: ").strip() or default("mouser_part")
    digikey = input(f"DigiKey P/N [{default('digikey_part')}]: ").strip() or default("digikey_part")
    octopart = input(f"Octopart UID [{default('octopart_uid')}]: ").strip() or default("octopart_uid")
    mcmaster = input(f"McMaster P/N [{default('mcmaster_part')}]: ").strip() or default("mcmaster_part")
    scs = input(f"SendCutSend ID [{default('sendcutsend_id')}]: ").strip() or default("sendcutsend_id")
    price = input(f"Manual unit price USD [{default('manual_price')}]: ").strip() or default("manual_price")
    notes = input(f"Notes [{default('notes')}]: ").strip() or default("notes")

    entry = {
        "description": description,
        "customer_part": customer_part,
        "type": comp_type,
        "mouser_part": mouser,
        "digikey_part": digikey,
        "octopart_uid": octopart,
        "mcmaster_part": mcmaster,
        "sendcutsend_id": scs,
        "manual_price": float(price) if price else None,
        "price_currency": "USD",
        "price_updated": None,
        "notes": notes,
    }
    db.add(footprint, designation, entry)
    db.save()
    print("Saved.")


def edit_sendcutsend(client: SendCutSendClient) -> None:
    print("\nAdd SendCutSend Custom Part")
    part_name = input("Part name (e.g., 'DC+ Bus Bar'): ").strip()
    material = input("Material (e.g., 'C11000 copper'): ").strip()
    thickness = input("Thickness (mm): ").strip()
    qty = input("Qty per chassis: ").strip()
    dims = input("Dimensions (mm, e.g., 200x25): ").strip()
    finish = input("Finish (e.g., 'as cut', 'tinned'): ").strip()
    price = input("Known unit price USD (or blank): ").strip()
    url = input("SendCutSend instant-quote URL (or blank): ").strip()
    notes = input("Notes: ").strip()

    client.add_part({
        "PartName": part_name,
        "Material": material,
        "Thickness_mm": thickness,
        "Qty": qty,
        "Dimensions_mm": dims,
        "Finish": finish,
        "UnitPrice": price,
        "URL": url,
        "Notes": notes,
    })
    print("Saved to sendcutsend_manifest.csv")


def list_missing(db: PartDatabase) -> None:
    print(f"\nTotal database entries: {len(db.all_entries())}")
    print("\nFirst 20 entries:")
    for i, (key, val) in enumerate(db.all_entries().items()):
        if i >= 20:
            break
        status = val.get("mouser_part") or val.get("digikey_part") or val.get("mcmaster_part") or "no P/N"
        print(f"  {key}: {status} ({val.get('description', '')})")


def manage_part_numbers(
    pn_registry: PartNumberRegistry, descriptor_registry: DescriptorRegistry
) -> None:
    print("\nPart Number Manager")
    print("1. Generate new part number")
    print("2. List assigned part numbers")
    print("3. Bump revision")
    print("4. Back")
    choice = input("Select: ").strip()
    if choice == "1":
        category = input("Category (pcb/busbar/plate/bracket/mech/fastener/electrical/etc.): ").strip()
        description = input("Description: ").strip()
        chassis = input("Chassis [Chassis2]: ").strip() or "Chassis2"
        default_desc = ""
        if category.lower() == "pcb":
            default_desc = DescriptorRegistry.default_for_board(description)
        descriptor = input(f"Descriptor [{default_desc}]: ").strip() or default_desc
        rev = input("Revision [A]: ").strip() or "A"
        pn = pn_registry.generate_pn(category, description, descriptor=descriptor, chassis=chassis)
        pn_registry.save()
        print(f"Assigned: {pn}")
    elif choice == "2":
        for key, entry in pn_registry.list_all().items():
            print(f"  {entry['part_number']} - {entry['category']} - {entry['description']}")
    elif choice == "3":
        key = input("Key (chassis|category|description): ").strip()
        new_rev = input("New revision (or blank to auto-increment): ").strip() or None
        pn = pn_registry.bump_revision(key, new_rev)
        if pn:
            pn_registry.save()
            print(f"Bumped to: {pn}")
        else:
            print("Key not found.")


def main() -> int:
    root = Path(__file__).resolve().parent
    db_path = root / "bom_manager" / "data" / "part_database.json"
    scs_path = root / "bom_manager" / "data" / "sendcutsend_manifest.csv"
    pn_path = root / "bom_manager" / "data" / "part_numbers.json"
    desc_path = root / "bom_manager" / "data" / "part_descriptors.json"
    config = Config(root / "config.yaml")
    pn_format = config.get("part_number.format", PartNumberRegistry.DEFAULT_FORMAT)
    db = PartDatabase(db_path)
    scs = SendCutSendClient(scs_path)
    pn_registry = PartNumberRegistry(pn_path, format=pn_format)
    descriptor_registry = DescriptorRegistry(desc_path, allow_prompt=True)

    while True:
        print("\nBOM Manager - Part Editor")
        print("1. Add/Edit part database entry")
        print("2. Add SendCutSend custom part")
        print("3. List database entries")
        print("4. Manage internal part numbers")
        print("5. Exit")
        choice = input("Select: ").strip()
        if choice == "1":
            edit_database(db, pn_registry)
        elif choice == "2":
            edit_sendcutsend(scs)
        elif choice == "3":
            list_missing(db)
        elif choice == "4":
            manage_part_numbers(pn_registry, descriptor_registry)
        elif choice == "5":
            break
        else:
            print("Invalid option.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
