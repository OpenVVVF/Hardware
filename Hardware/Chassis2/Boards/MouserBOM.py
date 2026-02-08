import os
import csv
import json
import argparse
import re
import random
import string
from pathlib import Path

SUBSTITUTIONS_FILE = "bom_substitutions.json"
MOUSER_BOM_HEADER = ["Quantity 1", "Description", "Customer Part No.", "Mouser Part Number"]
STANDARD_ROUND_VALUES = [10, 25, 50, 100]

def load_substitutions():
    try:
        with open(SUBSTITUTIONS_FILE, 'r') as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}

def normalize_key_part(text):
    """Normalize for case-insensitive comparison and whitespace handling"""
    return re.sub(r'\s+', '', text.strip().lower())

def prompt_for_substitution(footprint, designation):
    """Prompt user to add a substitution for a missing component"""
    print(f"\nMissing substitution for: {footprint}|{designation}")

    if (footprint.startswith('r_') or footprint.startswith('c_')) and '1210' in footprint:
        print("This appears to be a 1210 resistor or capacitor that needs a substitution.")
        description = input("Enter description (e.g., '10k 1210'): ").strip()
        mouser_part = input("Enter Mouser P/N (or DNO to exclude): ").strip().upper()

        # Generate customer part similar to SubstitutionManager.py
        rand_str = ''.join(random.choices(string.ascii_uppercase + string.digits, k=5))
        clean_value = re.sub(r'[^a-zA-Z0-9]', '', designation)
        customer_part = f"CBPN-{rand_str}-{clean_value}"

        # Determine type
        if footprint.startswith('r_'):
            comp_type = 'resistor'
        else:
            comp_type = 'capacitor'

        # Create new substitution
        key = f"{footprint.lower()}|{designation.lower()}"
        new_sub = {
            'description': description,
            'customer_part': customer_part,
            'mouser_part': mouser_part,
            'type': comp_type
        }

        # Load current substitutions
        substitutions = load_substitutions()
        substitutions[key] = new_sub

        # Save back to file
        with open(SUBSTITUTIONS_FILE, 'w') as f:
            json.dump(substitutions, f, indent=2)

        print(f"Added new substitution for {designation} ({footprint})")
        return new_sub

    return None

def process_bom_file(file_path, substitutions, args):
    components = {}

    with open(file_path, 'r', encoding='utf-8-sig') as f:
        reader = csv.DictReader(f, delimiter=';', quotechar='"')
        for row in reader:
            try:
                # Normalize input values
                footprint = normalize_key_part(row['Footprint'])
                designation = normalize_key_part(row['Designation'])
                qty = int(row['Quantity'])
                key = f"{footprint}|{designation}"

                print(f"Processing '{file_path}': {footprint}|{designation}")

                # Skip DNO items
                if key in substitutions and substitutions[key]['mouser_part'] == "DNO":
                    continue

                # Apply substitution or use direct values
                if key in substitutions:
                    sub = substitutions[key]
                    cust_part = sub['customer_part']
                    mouser_part = sub['mouser_part']
                    desc = sub['description']
                    comp_type = sub.get('type', 'other')
                else:
                    # Check if this is a resistor or capacitor that needs substitution
                    if (footprint.startswith('r_') or footprint.startswith('c_')) and '1210' in footprint:
                        sub = prompt_for_substitution(footprint, designation)
                        if sub:
                            cust_part = sub['customer_part']
                            mouser_part = sub['mouser_part']
                            desc = sub['description']
                            comp_type = sub['type']
                        else:
                            cust_part = row['Designation'].strip()
                            mouser_part = row['Designation'].strip()
                            desc = row['Designation'].strip()
                            comp_type = 'other'
                    else:
                        cust_part = row['Designation'].strip()
                        mouser_part = row['Designation'].strip()
                        desc = row['Designation'].strip()
                        comp_type = 'other'

                # Aggregate components
                agg_key = (cust_part, mouser_part)
                if agg_key not in components:
                    components[agg_key] = {
                        'qty': qty,
                        'desc': desc,
                        'type': comp_type
                    }
                else:
                    components[agg_key]['qty'] += qty

            except Exception as e:
                print(f"Error processing row: {e}\nRow data: {row}")

    return components

def apply_quantity_adjustments(components, args):
    for entry in components.values():
        if args.round_extras and entry['type'] in ['capacitor', 'resistor']:
            entry['qty'] = max(entry['qty'], STANDARD_ROUND_VALUES[0])
            for val in sorted(STANDARD_ROUND_VALUES):
                if entry['qty'] <= val:
                    entry['qty'] = val
                    break
        elif args.chip_extras and entry['type'] == 'chip':
            entry['qty'] += args.chip_extras

def write_output_file(components, output_path):
    # Ensure directory exists
    try:
        os.makedirs(os.path.dirname(output_path), exist_ok=True)
    except:
        pass

    # Write with proper overwrite handling
    with open(output_path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(MOUSER_BOM_HEADER)
        for (cust_part, mouser_part), data in components.items():
            writer.writerow([
                data['qty'],
                data['desc'],
                cust_part,
                mouser_part
            ])

def main(args):
    substitutions = load_substitutions()
    all_components = {}
    excluded_count = 0

    # Determine the output directory
    output_dir = Path(args.output_dir) if args.output_dir else Path("BOMs")
    output_dir.mkdir(exist_ok=True)

    for dirpath, _, filenames in os.walk(args.input_dir):
        # Skip processing files in the output directory
        if Path(dirpath) == output_dir:
            continue

        for filename in filenames:
            if filename == args.output_file:
                continue
            if filename.lower().endswith('.csv'):
                file_path = Path(dirpath) / filename
                print(f"Processing: {file_path}")
                components = process_bom_file(file_path, substitutions, args)

                # Count excluded components
                with open(file_path, 'r', encoding='utf-8-sig') as f:
                    reader = csv.DictReader(f, delimiter=';', quotechar='"')
                    for row in reader:
                        footprint = row['Footprint'].strip()
                        designation = row['Designation'].strip()
                        key = f"{footprint.lower()}|{designation.lower()}"
                        if key in substitutions and substitutions[key]['mouser_part'] == "DNO":
                            excluded_count += 1

                # Write individual BOM file
                individual_output_path = output_dir / filename
                write_output_file(components, individual_output_path)
                print(f"Saved individual BOM to: {individual_output_path}")

                # Merge components for combined output
                for key, data in components.items():
                    if key in all_components:
                        all_components[key]['qty'] += data['qty']
                    else:
                        all_components[key] = data

    # Write combined output
    combined_output_path = output_dir / "Combined.csv"
    apply_quantity_adjustments(all_components, args)
    write_output_file(all_components, combined_output_path)
    print(f"\nCombined output saved to {combined_output_path}")

    print(f"\nProcessed {len(all_components)} unique components across all BOMs")
    print(f"Excluded {excluded_count} components marked DNO")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='BOM Processor with Substitutions')
    parser.add_argument('input_dir', help='Directory containing BOM CSV files')
    parser.add_argument('-o', '--output-file', default='', help='Optional combined output CSV filename')
    parser.add_argument('-r', '--round-extras', action='store_true',
                      help='Round resistor/capacitor quantities to standard values')
    parser.add_argument('-c', '--chip-extras', type=int, default=0,
                      help='Add extra quantity for chips')
    parser.add_argument('-d', '--output-dir', default='', help='Directory to save output files')
    args = parser.parse_args()

    main(args)
