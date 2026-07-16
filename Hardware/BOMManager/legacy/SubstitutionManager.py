import json
import re
import string
import random
from pathlib import Path

SUBSTITUTIONS_FILE = "bom_substitutions.json"

def generate_customer_part(designation_value):
    """Generate consistent customer part number"""
    rand_str = ''.join(random.choices(string.ascii_uppercase + string.digits, k=5))
    clean_value = re.sub(r'[^a-zA-Z0-9]', '', designation_value)
    return f"CBPN-{rand_str}-{clean_value}"

def load_substitutions():
    try:
        with open(SUBSTITUTIONS_FILE, 'r') as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}

def save_substitutions(substitutions):
    with open(SUBSTITUTIONS_FILE, 'w') as f:
        json.dump(substitutions, f, indent=2)

def add_substitution():
    substitutions = load_substitutions()

    print("\nAdd New Substitution")
    print("Enter 'DNO' as Mouser P/N to exclude from BOM")
    footprint = input("Enter EXACT footprint from BOM: ").strip()
    designation = input("Enter EXACT designation from BOM: ").strip()
    description = input("Enter description: ").strip()
    mouser_part = input("Enter Mouser P/N (or DNO): ").strip().upper()

    key = f"{footprint.lower()}|{designation.lower()}"
    customer_part = generate_customer_part(designation)

    substitutions[key] = {
        'description': description,
        'customer_part': customer_part,
        'mouser_part': mouser_part,
        'type': determine_component_type(footprint, designation)
    }

    save_substitutions(substitutions)
    if mouser_part == "DNO":
        print(f"\nAdded exclusion for {designation} ({footprint})")
    else:
        print(f"\nAdded substitution for {designation} ({footprint})")
        print(f"Customer Part: {customer_part}")
        print(f"Mouser P/N: {mouser_part}")

def determine_component_type(footprint, designation):
    if footprint.startswith('C_'):
        return 'capacitor'
    if footprint.startswith('R_'):
        return 'resistor'
    if any(x in footprint.upper() for x in ['IC', 'SOIC', 'QFP', 'BGA']):
        return 'chip'
    return 'other'

def main():
    while True:
        print("\nSubstitution Manager")
        print("1. Add new substitution/exclusion")
        print("2. View current substitutions")
        print("3. Exit")

        choice = input("Select option: ")

        if choice == '1':
            add_substitution()
        elif choice == '2':
            subs = load_substitutions()
            print("\nCurrent Substitutions:")
            for key, val in subs.items():
                status = "EXCLUDED" if val['mouser_part'] == "DNO" else val['mouser_part']
                print(f"{key}: {status} ({val['description']})")
        elif choice == '3':
            break
        else:
            print("Invalid option")

if __name__ == '__main__':
    main()
