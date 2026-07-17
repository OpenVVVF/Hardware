"""Interactive command shell and one-shot CLI entry for BOM Manager.

Run `python3 bom.py` to enter the interactive shell, or
`python3 bom.py <command> [args]` for one-shot use.
"""

import cmd
import shlex
import sys
from datetime import datetime

from . import addpart, fab, generate, mech, scaffold
from .context import Context, build_context
from .db import VENDOR_PN_FIELD
from .importers import digikey, jlcpcb, mcmaster, mouser, sendcutsend
from .pricing import PriceInfo, line_total

BANNER = (
    "BOM Manager — type 'help' for commands, 'quit' to exit.\n"
    "Fab-time loop: mech add -> price -> generate -> order -> import -> stock"
)


def detect_chassis(ctx: Context):
    """Return the chassis name when exactly one exists, else None."""
    if not ctx.hardware_root.is_dir():
        return None
    dirs = sorted(
        d.name
        for d in ctx.hardware_root.iterdir()
        if d.is_dir() and d.name.lower().startswith("chassis")
    )
    return dirs[0] if len(dirs) == 1 else None


class BomShell(cmd.Cmd):
    intro = BANNER
    prompt = "bom> "

    def __init__(self, ctx: Context):
        super().__init__()
        self.ctx = ctx
        self.chassis = detect_chassis(ctx)
        self.last_error = False

    def onecmd(self, line):
        """Keep the shell alive through argparse exits and command errors."""
        self.last_error = False
        try:
            return super().onecmd(line)
        except SystemExit:
            # argparse calls sys.exit() on bad flags; swallow it in the shell.
            self.last_error = True
        except KeyboardInterrupt:
            print("^C")
        except Exception as e:  # noqa: BLE001 - shell must not die on command errors
            print(f"Error: {e}")
            self.last_error = True
        return False

    def default(self, line):
        print(f"Unknown command: {line.split()[0] if line.split() else line!r}. Type 'help'.")

    def emptyline(self):
        pass  # do not repeat the last command on empty input

    def _need_chassis(self) -> bool:
        if not self.chassis:
            print("No chassis selected. Use 'chassis <name>'.")
            return False
        return True

    def _resolve_one(self, query: str):
        """Resolve a query to a single part, with a picker on ambiguity."""
        matches = addpart.resolve(self.ctx, query, chassis=self.chassis)
        if not matches:
            print(f"No parts match {query!r}. Try 'parts' to browse.")
            self.last_error = True
            return None
        match = addpart.pick(matches)
        if match is None:
            print("Cancelled.")
        return match

    def _apply_price(self, match, entry, amount: float) -> None:
        """Write a manual price to the DB entry and keep the cache in agreement."""
        entry["manual_price"] = amount
        entry["price_updated"] = datetime.utcnow().date().isoformat()
        self.ctx.db.save()
        pns = {
            vendor: entry.get(field)
            for vendor, field in VENDOR_PN_FIELD.items()
            if entry.get(field)
        }
        if match.line:
            vendor = match.line.primary_vendor()
            pn = match.line.vendor_part_number(vendor)
            if vendor and pn:
                pns.setdefault(vendor, pn)
        for vendor, pn in pns.items():
            self.ctx.cache.set(PriceInfo(vendor=vendor, part_number=pn, unit_price=amount, source="manual"))
        self.ctx.cache.save()

    # --- generation & imports --------------------------------------------

    def do_generate(self, arg):
        """Generate consolidated BOMs, price report, and PCB fab zips.
        Usage: generate [--qty N] [--spares none|cheap|all] [--chassis X]
                        [--board Y] [--vendors a,b] [--refresh-prices]
                        [--suggest] [--no-pcb-zips] [--no-prompt]
        """
        generate.run(shlex.split(arg), self.ctx)

    def do_suggest(self, arg):
        """Interactively suggest vendor parts for missing entries (needs API keys)."""
        generate.run(["--suggest", "--no-pcb-zips"] + shlex.split(arg), self.ctx)

    def do_import(self, arg):
        """Import vendor cart/order prices (paste text, then Ctrl+D, or give a file).
        Usage: import mouser|mcmaster|sendcutsend|jlcpcb [file] [--allow-zero] [--dry-run]
        """
        args = shlex.split(arg)
        if not args:
            print("Usage: import mouser|mcmaster|sendcutsend|jlcpcb [file] [options]")
            self.last_error = True
            return
        vendor, rest = args[0].lower(), args[1:]
        runners = {
            "mouser": lambda: mouser.run(rest, self.ctx),
            "digikey": lambda: digikey.run(rest, self.ctx),
            "mcmaster": lambda: mcmaster.run(rest, self.ctx),
            "sendcutsend": lambda: sendcutsend.run(rest, self.ctx, chassis=self.chassis),
            "jlcpcb": lambda: jlcpcb.run(rest, self.ctx),
        }
        runner = runners.get(vendor)
        if runner is None:
            print(f"Unknown vendor {vendor!r}. Choose from: {', '.join(runners)}")
            self.last_error = True
            return
        if runner() != 0:
            self.last_error = True

    # --- chassis & mechanical list ----------------------------------------

    def do_chassis(self, arg):
        """Show or set the current chassis: chassis [name]"""
        name = arg.strip()
        if not name:
            current = self.chassis or "(none)"
            print(f"Current chassis: {current}")
            found = [
                d.name
                for d in self.ctx.hardware_root.iterdir()
                if d.is_dir() and d.name.lower().startswith("chassis")
            ]
            print(f"Available: {', '.join(sorted(found)) or '(none found)'}")
            return
        if not (self.ctx.hardware_root / name).is_dir():
            print(f"No such chassis directory: {name}")
            self.last_error = True
            return
        self.chassis = name
        print(f"Current chassis: {name}")

    def do_mech(self, arg):
        """Manage MechanicalBOM.txt (the tool owns this file).
        Usage:
          mech                              list rows with prices
          mech add <vendor> <pn> <qty> [desc...]   add or update a row
          mech set <pn> <qty>               change quantity
          mech rm <pn>                      remove a row
        """
        if not self._need_chassis():
            return
        path = mech.mech_file_path(self.ctx.hardware_root, self.chassis)
        rows = mech.load(path)
        args = shlex.split(arg)

        if not args:
            self._mech_list(path, rows)
            return

        cmd_name, rest = args[0].lower(), args[1:]
        if cmd_name == "add":
            if len(rest) < 3:
                print("Usage: mech add <vendor> <pn> <qty> [desc...]")
                self.last_error = True
                return
            vendor, pn = rest[0], rest[1]
            try:
                qty = int(rest[2])
                if qty <= 0:
                    raise ValueError
            except ValueError:
                print(f"Quantity must be a positive integer, got {rest[2]!r}.")
                self.last_error = True
                return
            desc = " ".join(rest[3:])
            row, created = mech.upsert(rows, vendor, pn, qty, desc)
            mech.save(path, rows)
            if mech.ensure_db_entry(self.ctx.db, row):
                self.ctx.db.save()
            print(f"{'Added' if created else 'Updated'}: {row.qty} x {row.vendor} {row.pn}"
                  + (f" — {row.description}" if row.description else ""))
            price, _, _ = mech.row_price(self.ctx, row)
            if price is None:
                print(f"  No price known yet — set one with: price {row.pn} <usd>")
        elif cmd_name == "set":
            if len(rest) != 2:
                print("Usage: mech set <pn> <qty>")
                self.last_error = True
                return
            try:
                qty = int(rest[1])
                if qty <= 0:
                    raise ValueError
            except ValueError:
                print(f"Quantity must be a positive integer, got {rest[1]!r}.")
                self.last_error = True
                return
            row = mech.set_qty(rows, rest[0], qty)
            if not row:
                print(f"No unique row matching {rest[0]!r}. See 'mech'.")
                self.last_error = True
                return
            mech.save(path, rows)
            print(f"Updated: {row.qty} x {row.vendor} {row.pn}")
        elif cmd_name == "rm":
            if len(rest) != 1:
                print("Usage: mech rm <pn>")
                self.last_error = True
                return
            row = mech.remove(rows, rest[0])
            if not row:
                print(f"No unique row matching {rest[0]!r}. See 'mech'.")
                self.last_error = True
                return
            mech.save(path, rows)
            print(f"Removed: {row.vendor} {row.pn}")
        else:
            print(f"Unknown mech subcommand: {cmd_name}. Type 'help mech'.")
            self.last_error = True

    def _mech_list(self, path, rows) -> None:
        if not rows:
            print(f"{path}: no rows. Add one with: mech add <vendor> <pn> <qty> [desc]")
            return
        print(f"{path} — {len(rows)} row(s)\n")
        print(f"  {'Qty':>4}  {'Vendor':<11} {'PN':<18} {'Price':>10}  Description")
        print(f"  {'---':>4}  {'------':<11} {'--':<18} {'-----':>10}  -----------")
        for r in rows:
            price, source, pack_size = mech.row_price(self.ctx, r)
            if price is not None:
                price_cell = f"${price:.2f}" + ("/pk" if pack_size and pack_size > 1 else "")
            else:
                price_cell = "-"
            pack_note = f" [pack of {pack_size}]" if pack_size and pack_size > 1 else ""
            desc = r.description or ""
            print(f"  {r.qty:>4}  {r.vendor:<11} {r.pn:<18} {price_cell:>10}  {desc}{pack_note}")

    # --- part database -----------------------------------------------------

    def do_parts(self, arg):
        """List live BOM parts with database/price status.
        Usage: parts [--missing] [--unpriced] [--vendor X] [--type Y] [query]
        """
        args = shlex.split(arg)
        show_missing = "--missing" in args
        show_unpriced = "--unpriced" in args

        def flag_value(flag):
            return args[args.index(flag) + 1].lower() if flag in args and args.index(flag) + 1 < len(args) else None

        vendor_filter = flag_value("--vendor")
        type_filter = flag_value("--type")
        query = " ".join(a for a in args if not a.startswith("--")
                         and a != vendor_filter and a != type_filter).lower()

        rows = []
        for ch, line in addpart.collect_lines(self.ctx, chassis=self.chassis):
            entry = self.ctx.db.lookup(line.footprint, line.designation)
            price, _ = addpart.known_price(self.ctx, line)
            if show_missing and entry is not None:
                continue
            if show_unpriced and price is not None:
                continue
            if vendor_filter and (line.primary_vendor() or "") != vendor_filter:
                continue
            if type_filter and line.type != type_filter:
                continue
            ipn = addpart.existing_ipn(self.ctx, ch, line)
            hay = f"{line.description} {line.designation} {ipn}".lower()
            if query and query not in hay:
                continue
            rows.append((line, entry, ipn, price))

        rows.sort(key=lambda r: (r[0].type, r[0].description.lower()))
        print(f"{len(rows)} part(s)\n")
        print(f"  {'Internal P/N':<26} {'Type':<10} {'Qty':>4} {'Vendor':<11} {'Vendor P/N':<20} {'Price':>9}  Description")
        print(f"  {'------------':<26} {'----':<10} {'---':>4} {'------':<11} {'----------':<20} {'-----':>9}  -----------")
        for line, entry, ipn, price in rows:
            vendor = line.primary_vendor() or "?"
            vpn = line.vendor_part_number(line.primary_vendor()) or "-"
            price_cell = f"${price:.2f}" if price is not None else "-"
            if line.pack_size and line.pack_size > 1:
                price_cell += "/pk"
            notes = []
            if entry is None:
                notes.append("no DB entry")
            if price is None:
                notes.append("no price")
            desc = line.description[:38] + (f"  [{', '.join(notes)}]" if notes else "")
            print(f"  {ipn or '-':<26} {line.type:<10} {line.quantity:>4} {vendor:<11} {vpn:<20} {price_cell:>9}  {desc}")

    def do_add(self, arg):
        """Walk through BOM lines that need a database entry (add wizard)."""
        addpart.add_wizard(self.ctx, self.chassis)

    def do_price(self, arg):
        """Set a manual price: price <query> <usd>
        Query is a vendor PN, internal PN, or description text. Price is per
        pack when the part has a pack size, otherwise per piece.
        """
        args = shlex.split(arg)
        if len(args) != 2:
            print("Usage: price <query> <usd>")
            self.last_error = True
            return
        try:
            amount = float(args[1].replace("$", ""))
            if amount < 0:
                raise ValueError
        except ValueError:
            print(f"Invalid price: {args[1]!r}")
            self.last_error = True
            return
        match = self._resolve_one(args[0])
        if match is None:
            return
        entry = addpart.ensure_entry(self.ctx, match)
        self._apply_price(match, entry, amount)
        pack_note = " (per pack)" if entry.get("pack_size", 1) and entry.get("pack_size", 1) > 1 else ""
        print(f"Set {match.label()}: ${amount:.2f}{pack_note}")

    def do_pack(self, arg):
        """Set pack size, optionally with pack price: pack <query> <size> [price]
        Example: need 6 screws, sold in boxes of 25 -> pack 94669A199 25 9.80
        """
        args = shlex.split(arg)
        if len(args) not in (2, 3):
            print("Usage: pack <query> <size> [price]")
            self.last_error = True
            return
        try:
            size = int(args[1])
            if size < 1:
                raise ValueError
        except ValueError:
            print(f"Invalid pack size: {args[1]!r}")
            self.last_error = True
            return
        price = None
        if len(args) == 3:
            try:
                price = float(args[2].replace("$", ""))
                if price < 0:
                    raise ValueError
            except ValueError:
                print(f"Invalid price: {args[2]!r}")
                self.last_error = True
                return
        match = self._resolve_one(args[0])
        if match is None:
            return
        entry = addpart.ensure_entry(self.ctx, match)
        if size > 1:
            entry["pack_size"] = size
        else:
            entry.pop("pack_size", None)
        self.ctx.db.save()
        msg = f"{match.label()}: "
        msg += f"pack of {size}" if size > 1 else "sold individually (pack cleared)"
        if price is not None:
            self._apply_price(match, entry, price)
            msg += f" @ ${price:.2f}/pack" if size > 1 else f" @ ${price:.2f}"
        if match.line and size > 1:
            line = match.line
            line.pack_size = size
            line.on_hand = self.ctx.inventory.get(line.footprint, line.designation)
            need = line.quantity
            msg += f" — need {need} -> order {line.packs_needed(need)} pack(s), {line.leftover(need)} left"
        print(msg)

    def do_stock(self, arg):
        """View or set on-hand stock (local inventory, gitignored).
        Usage: stock                  list all stock
               stock <query> [n]    show or set count for a part (0 clears)
        """
        args = shlex.split(arg)
        if not args:
            entries = self.ctx.inventory.all_entries()
            if not entries:
                print("No stock recorded. Set some with: stock <query> <n>")
                return
            print(f"{len(entries)} stocked part(s):\n")
            for key, n in sorted(entries.items()):
                footprint, _, designation = key.partition("|")
                entry = self.ctx.db.lookup(footprint, designation)
                desc = (entry or {}).get("description", designation)
                print(f"  {n:>5} x {desc}  [{key}]")
            return
        match = self._resolve_one(args[0])
        if match is None:
            return
        if len(args) == 1:
            n = self.ctx.inventory.get(match.footprint, match.designation)
            print(f"{match.label()}: {n} on hand")
            return
        try:
            n = int(args[1])
            if n < 0:
                raise ValueError
        except ValueError:
            print(f"Invalid count: {args[1]!r}")
            self.last_error = True
            return
        self.ctx.inventory.set(match.footprint, match.designation, n)
        self.ctx.inventory.save()
        print(f"{match.label()}: {n} on hand" + (" (cleared)" if n == 0 else ""))

    def do_exclude(self, arg):
        """Exclude a part from the BOM (marks it DNO — do not order): exclude <query>
        Note: this replaces the part's database entry. 'include <query>' undoes it.
        """
        query = arg.strip()
        if not query:
            print("Usage: exclude <query>")
            self.last_error = True
            return
        match = self._resolve_one(query)
        if match is None:
            return
        self.ctx.db.exclude(match.footprint, match.designation)
        self.ctx.db.save()
        print(f"Excluded (DNO): {match.designation}. Undo with: include {query}")

    def do_include(self, arg):
        """Remove a DNO exclusion: include <query>"""
        query = arg.strip()
        if not query:
            print("Usage: include <query>")
            self.last_error = True
            return
        match = self._resolve_one(query)
        if match is None:
            return
        if not (match.entry and match.entry.get("mouser_part") == "DNO"):
            print(f"{match.designation} is not excluded.")
            return
        self.ctx.db.remove(match.footprint, match.designation)
        self.ctx.db.save()
        print(f"Exclusion removed: {match.designation} (add vendor PNs/price back with 'add' or 'price')")

    def do_forget(self, arg):
        """Remove a part's database entry entirely: forget <query>
        The part stays in the BOM; it just goes back to 'no DB entry'."""
        query = arg.strip()
        if not query:
            print("Usage: forget <query>")
            self.last_error = True
            return
        match = self._resolve_one(query)
        if match is None:
            return
        if match.entry is None:
            print(f"{match.designation} has no database entry.")
            return
        self.ctx.db.remove(match.footprint, match.designation)
        self.ctx.db.save()
        print(f"Forgot: {match.label()}")

    def do_new(self, arg):
        """Scaffold a new part: new fab|harness|board [name]
        Creates the folder(s) with a README and pre-registers part numbers."""
        if not self._need_chassis():
            return
        args = shlex.split(arg)
        if not args:
            print("Usage: new fab|harness|board [name]")
            self.last_error = True
            return
        kind = args[0].lower()
        name = " ".join(args[1:])
        if not name and kind != "board":
            try:
                name = input(f"{kind.capitalize()} name: ").strip()
            except EOFError:
                name = ""
        if not name:
            print("A name is required.")
            self.last_error = True
            return
        if kind == "fab":
            scaffold.new_fab_part(self.ctx, self.chassis, name)
        elif kind == "harness":
            scaffold.new_harness(self.ctx, self.chassis, name)
        elif kind == "board":
            scaffold.new_board(self.ctx, self.chassis, name)
        else:
            print(f"Unknown kind {kind!r}. Choose from: fab, harness, board")
            self.last_error = True

    # --- fabrication & revisions --------------------------------------------

    def do_fab(self, arg):
        """Fabrication package status.
        Usage: fab                             readiness view
               fab pcb-price <board> <usd>     set a board's fab price"""
        args = shlex.split(arg)
        if args and args[0] == "pcb-price":
            if len(args) != 3:
                print("Usage: fab pcb-price <board> <usd>")
                self.last_error = True
                return
            board = args[1]
            try:
                amount = float(args[2].replace("$", ""))
                if amount < 0:
                    raise ValueError
            except ValueError:
                print(f"Invalid price: {args[2]!r}")
                self.last_error = True
                return
            if not self._need_chassis():
                return
            if not (self.ctx.hardware_root / self.chassis / "Boards" / board).is_dir():
                print(f"No board named {board!r} under {self.chassis}/Boards.")
                self.last_error = True
                return
            entry = self.ctx.db.lookup("PCB", board)
            if entry is None:
                entry = {
                    # Description must stay exactly the board name — descriptor
                    # and IPN registry keys derive from it.
                    "description": board,
                    "customer_part": "",
                    "type": "pcb",
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
                self.ctx.db.add("PCB", board, entry)
            entry["manual_price"] = amount
            entry["price_updated"] = datetime.utcnow().date().isoformat()
            self.ctx.db.save()
            self.ctx.cache.set(PriceInfo(vendor="pcb", part_number=board, unit_price=amount, source="manual"))
            self.ctx.cache.save()
            print(f"Set {board} fab price: ${amount:.2f}")
            return
        if not self._need_chassis():
            return
        print(fab.render(fab.collect(self.ctx, self.chassis)))

    def do_rev(self, arg):
        """Revision management.
        Usage: rev list
               rev bump <query> [--rev X] [--note "what changed"]
        Bumping never renames folders or files; fabricated parts get Rev=
        written into their info.txt."""
        args = shlex.split(arg)
        if not args or args[0] == "list":
            entries = sorted(self.ctx.pn_registry.list_all().values(), key=lambda e: e["part_number"])
            print(f"{len(entries)} internal part number(s)\n")
            for e in entries:
                hist = e.get("history") or []
                last = f"{hist[-1]['date']} {hist[-1].get('note', '')}".strip() if hist else ""
                print(f"  {e['part_number']:<30} rev {e['revision']:<3} {last}")
            return
        if args[0] != "bump":
            print("Usage: rev list | rev bump <query> [--rev X] [--note \"...\"]")
            self.last_error = True
            return
        rest = args[1:]
        new_rev = None
        note = ""
        positional = []
        i = 0
        while i < len(rest):
            if rest[i] == "--rev" and i + 1 < len(rest):
                new_rev = rest[i + 1]
                i += 2
            elif rest[i] == "--note" and i + 1 < len(rest):
                note = rest[i + 1]
                i += 2
            else:
                positional.append(rest[i])
                i += 1
        query = " ".join(positional)
        if not query:
            print("Usage: rev bump <query> [--rev X] [--note \"...\"]")
            self.last_error = True
            return
        matches = self.ctx.pn_registry.find(query)
        if not matches:
            print(f"No part numbers match {query!r}. See 'rev list'.")
            self.last_error = True
            return
        entry = matches[0]
        if len(matches) > 1:
            print("Multiple part numbers match:")
            for j, e in enumerate(matches, 1):
                print(f"  {j}. {e['part_number']} — {e.get('description', '')}")
            try:
                ans = input(f"Select 1-{len(matches)} (blank to cancel): ").strip()
            except EOFError:
                ans = ""
            if not ans.isdigit() or not 1 <= int(ans) <= len(matches):
                print("Cancelled.")
                return
            entry = matches[int(ans) - 1]
        new_pn = self.ctx.pn_registry.bump_revision(entry["key"], new_rev, note)
        self.ctx.pn_registry.save()
        print(f"{entry.get('description', query)}: now {new_pn}")
        synced = fab.sync_info_rev(self.ctx, entry["key"], new_pn.rsplit("-", 1)[1])
        if synced:
            print(f"  Wrote Rev= to {synced}")

    def do_status(self, arg):
        """Dashboard: BOM line counts, pricing coverage, attention items, fab readiness."""
        lines = addpart.collect_lines(self.ctx, self.chassis)
        priced_count = 0
        total = 0.0
        for _, line in lines:
            price, _ = addpart.known_price(self.ctx, line)
            if price is not None:
                priced_count += 1
                total += line_total(line, price)
        pending = addpart.needs_attention(self.ctx, self.chassis)
        stock = self.ctx.inventory.all_entries()
        print(f"Chassis: {self.chassis or '(none — use: chassis <name>)'}")
        print(f"BOM lines: {len(lines)} ({priced_count} with prices, ~${total:.2f} known cost)")
        print(f"Needs attention: {len(pending)} (run: add)")
        reasons = {}
        for _, _, reason in pending:
            reasons[reason] = reasons.get(reason, 0) + 1
        for reason, n in sorted(reasons.items()):
            print(f"  {n:>3} x {reason}")
        print(f"Stock on hand: {len(stock)} part(s) (run: stock)")
        if self.chassis:
            status = fab.collect(self.ctx, self.chassis)
            problems = status.problems
            print(f"Fabrication: {len(status.boards)} board(s), {len(status.parts)} fab part(s), "
                  f"{len(status.harnesses)} harness(es) — {len(problems)} not ready (run: fab)")

    def do_release(self, arg):
        """Build the full release package: all vendor BOMs + order files, price
        report scaled at 1/2/3/5/10 units, PCB zips, and one concatenated
        Schematics.pdf of every board and harness."""
        from . import release
        if release.run(self.ctx, self.chassis, shlex.split(arg)) != 0:
            self.last_error = True

    # --- shell housekeeping -------------------------------------------------

    def do_quit(self, arg):
        """Exit the shell."""
        return True

    def do_exit(self, arg):
        """Exit the shell."""
        return True

    def do_EOF(self, arg):
        print()
        return True


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    ctx = build_context()
    shell = BomShell(ctx)
    if not argv:
        try:
            shell.cmdloop()
        except KeyboardInterrupt:
            print()
        return 0
    shell.onecmd(" ".join(shlex.quote(a) for a in argv))
    return 1 if shell.last_error else 0
