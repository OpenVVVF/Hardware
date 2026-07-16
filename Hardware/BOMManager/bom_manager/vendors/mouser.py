"""Mouser API client."""

import time
from typing import Any, Dict, Optional

import requests


class MouserClient:
    SEARCH_URL = "https://api.mouser.com/api/v1/search/partnumber"

    def __init__(self, api_key: Optional[str] = None):
        self.api_key = api_key

    def enabled(self) -> bool:
        return bool(self.api_key)

    def search(self, part_number: str) -> Optional[Dict[str, Any]]:
        if not self.enabled():
            return None
        payload = {
            "SearchByPartRequest": {
                "mouserPartNumber": part_number,
                "partSearchOptions": "",
            }
        }
        params = {"apiKey": self.api_key}
        try:
            resp = requests.post(self.SEARCH_URL, params=params, json=payload, timeout=20)
            resp.raise_for_status()
            data = resp.json()
            parts = data.get("SearchResults", {}).get("Parts", [])
            if not parts:
                return None
            part = parts[0]
            price_breaks = part.get("PriceBreaks", [])
            unit_price = None
            if price_breaks:
                unit_price = float(price_breaks[0].get("Price", "0").replace("$", "").replace(",", ""))
            return {
                "mouser_part": part.get("MouserPartNumber", part_number),
                "description": part.get("Description", ""),
                "manufacturer": part.get("Manufacturer", ""),
                "manufacturer_pn": part.get("ManufacturerPartNumber", ""),
                "stock": part.get("AvailabilityInStock", ""),
                "unit_price": unit_price,
                "currency": "USD",
                "url": part.get("ProductDetailUrl", ""),
                "source": "mouser_api",
            }
        except Exception as e:
            print(f"Mouser API error for {part_number}: {e}")
            return None
