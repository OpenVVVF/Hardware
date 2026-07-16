"""Octopart API client."""

from typing import Any, Dict, Optional

import requests


class OctopartClient:
    SEARCH_URL = "https://api.nexar.com/supply/v1/search"

    def __init__(self, api_key: Optional[str] = None):
        # Nexar/Octopart uses a client_id/client_secret style OAuth2, but for
        # simplicity we accept an api_key / access_token here.
        self.api_key = api_key

    def enabled(self) -> bool:
        return bool(self.api_key)

    def search(self, part_number: str) -> Optional[Dict[str, Any]]:
        if not self.enabled():
            return None
        headers = {"Authorization": f"Bearer {self.api_key}"}
        params = {"q": part_number, "limit": 5}
        try:
            resp = requests.get(self.SEARCH_URL, headers=headers, params=params, timeout=20)
            resp.raise_for_status()
            data = resp.json()
            hits = data.get("results", [])
            if not hits:
                return None
            part = hits[0].get("part", {})
            best_offer = None
            for offer in part.get("offers", []):
                if offer.get("prices", {}).get("USD"):
                    best_offer = offer
                    break
            unit_price = None
            if best_offer:
                for qty, price in best_offer["prices"]["USD"]:
                    if qty <= 1:
                        unit_price = float(price)
                        break
            return {
                "octopart_uid": part.get("uid", ""),
                "description": part.get("short_description", ""),
                "manufacturer": part.get("manufacturer", {}).get("name", ""),
                "manufacturer_pn": part.get("mpn", ""),
                "stock": part.get("total_avail", ""),
                "unit_price": unit_price,
                "currency": "USD",
                "url": part.get("octopart_url", ""),
                "source": "octopart_api",
            }
        except Exception as e:
            print(f"Octopart API error for {part_number}: {e}")
            return None
