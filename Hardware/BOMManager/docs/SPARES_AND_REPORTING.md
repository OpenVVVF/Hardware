# Spares Policies and Price Reporting

## Spares Policies

Controlled by `--spares`:

| Policy | Behavior |
|--------|----------|
| `none` | Exact BOM quantity. |
| `cheap` | Round resistors, capacitors, and mechanical fasteners to standard pack sizes (10, 25, 50, 100, 200, 500, 1000). |
| `all` | Add `--spares-pct` percent to every line (default 10%). |

Examples:

```bash
python3 generate_bom.py --spares cheap
python3 generate_bom.py --spares all --spares-pct 20
```

## Price Report

`Hardware/Chassis2/FabricationData/Pricing_Report.md` is regenerated on every run and contains:

- One section per vendor with line items, unit price, line total, and price source.
- Vendor subtotals.
- Grand total for the requested quantity.
- A quantity-scaling table (default 3, 5, 10, 25 units).
- A list of parts with unknown/missing prices.
- A SendCutSend details section with material, thickness, finish, STEP path, and embedded `info.png` images.

The report is Markdown so it renders nicely in GitHub/GitLab and can be committed to track project cost over time. Each chassis gets its own report under `Hardware/<Chassis>/FabricationData/`.

## Quantity Scaling

Use `--qty N` to scale the whole BOM:

```bash
python3 generate_bom.py --qty 5
```

The consolidated CSV quantities are multiplied by N, and the report grand total reflects N units. You can change the scaling table with `--extra-qtys`:

```bash
python3 generate_bom.py --qty 1 --extra-qtys 2,5,10,25,50
```

## Price Sources

Each line shows where the price came from:

- `mouser_api`, `digikey_api`, `octopart_api` — live lookup.
- `mcmaster_scrape`, `sendcutsend_manifest` — vendor-specific fallback.
- `manual` — from the part database or manifest.
- `cache` — from `price_cache.json`.
- `unknown` — no price found; listed in the report for follow-up.
