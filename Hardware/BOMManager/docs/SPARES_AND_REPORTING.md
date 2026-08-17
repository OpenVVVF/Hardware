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
python3 bom.py generate --spares cheap
python3 bom.py generate --spares all --spares-pct 20
```

Spares are applied to the *needed* quantity before pack-size rounding, so a
line needing 8 screws at pack size 25 with `--spares cheap` orders 1 pack
(need rounds to 10, then to 1 box of 25).

## Price Report

`Hardware/Chassis2/FabricationData/Pricing_Report.md` is regenerated on every run and contains:

- One section per vendor with line items, order info, unit price, line total, and price source.
- Vendor subtotals.
- Grand total for the requested quantity.
- A quantity-scaling table (default 3, 5, 10, 25 units).
- A **Pack Rounding & Stock** section: need vs. on-hand vs. packs ordered vs. leftover.
- A **Fabrication Package** checklist: per-board fab files/zip/price, per-part STEP/info/price/rev, harness CSVs, and anything not ready.
- A list of parts with unknown/missing prices.
- A SendCutSend details section with material, thickness, finish, STEP path, and embedded `info.png` images.

The report is Markdown so it renders nicely in GitHub/GitLab and can be committed to track project cost over time. Each chassis gets its own report under `Hardware/<Chassis>/FabricationData/`.

## Quantity Scaling

Use `--qty N` to scale the whole BOM:

```bash
python3 bom.py generate --qty 5
```

The consolidated CSV quantities are multiplied by N (order quantities are
computed from the scaled need), and the report grand total reflects N units.
You can change the scaling table with `--extra-qtys`:

```bash
python3 bom.py generate --qty 1 --extra-qtys 2,5,10,25,50
```

## Price Sources

Each line shows where the price came from:

- `mouser_api`, `digikey_api`, `octopart_api` — live lookup (needs API keys).
- `mouser_cart_import`, `mcmaster_order_import`, `jlcpcb_order_import` — pasted cart/order imports.
- `sendcutsend_folder` — the part's `info.txt`.
- `manual` — set by hand in the shell (`price`, `pack`, `fab pcb-price`).
- `cache` — from `price_cache.json`.
- `unknown` — no price found; listed in the report for follow-up.
