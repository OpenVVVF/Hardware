"""SendCutSend custom part manifest and price lookup."""

import csv
import re
from pathlib import Path
from typing import Any, Dict, List, Optional

import requests
from bs4 import BeautifulSoup


class SendCutSendClient:
    def __init__(self, manifest_path: Optional[Path] = None, timeout: int = 15):
        self.timeout = timeout
        self.session = requests.Session()
        self.session.headers.update({
            "User-Agent": (
                "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
            )
        })
        if manifest_path is None:
            self.manifest_path = Path(__file__).resolve().parent.parent / "data" / "sendcutsend_manifest.csv"
        else:
            self.manifest_path = Path(manifest_path)

    def load_manifest(self) -> List[Dict[str, Any]]:
        if not self.manifest_path.exists():
            return []
        with open(self.manifest_path, "r", encoding="utf-8-sig") as f:
            return list(csv.DictReader(f))

    def save_manifest(self, rows: List[Dict[str, Any]]) -> None:
        self.manifest_path.parent.mkdir(parents=True, exist_ok=True)
        if not rows:
            with open(self.manifest_path, "w", encoding="utf-8", newline="") as f:
                writer = csv.writer(f)
                writer.writerow([
                    "PartName", "Material", "Thickness_mm", "Qty",
                    "Dimensions_mm", "Finish", "UnitPrice", "URL", "Notes"
                ])
            return
        fieldnames = list(rows[0].keys())
        with open(self.manifest_path, "w", encoding="utf-8", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)

    def lookup(self, part_name: str) -> Optional[Dict[str, Any]]:
        """Look up a custom part by name in the manifest."""
        for row in self.load_manifest():
            if row.get("PartName", "").strip().lower() == part_name.strip().lower():
                return row
        return None

    def fetch_price(self, url: str) -> Optional[float]:
        """Best-effort scrape of a SendCutSend instant-quote page."""
        if not url:
            return None
        try:
            resp = self.session.get(url, timeout=self.timeout)
            if resp.status_code != 200:
                return None
            soup = BeautifulSoup(resp.text, "html.parser")
            text = soup.get_text(" ", strip=True)
            for match in re.finditer(r"\$([0-9,]+\.\d{2})", text):
                return float(match.group(1).replace(",", ""))
        except Exception:
            pass
        return None

    def add_part(self, part: Dict[str, Any]) -> None:
        rows = self.load_manifest()
        rows.append(part)
        self.save_manifest(rows)
