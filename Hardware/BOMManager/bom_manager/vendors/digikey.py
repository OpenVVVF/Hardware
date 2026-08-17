"""DigiKey API client."""

from typing import Any, Dict, Optional

import requests


class DigiKeyClient:
    AUTH_URL = "https://api.digikey.com/v1/oauth2/token"
    SEARCH_URL = "https://api.digikey.com/products/v4/search/keyword"
    SEARCH_URL_SANDBOX = "https://sandbox-api.digikey.com/products/v4/search/keyword"

    def __init__(self, client_id: Optional[str] = None,
                 client_secret: Optional[str] = None,
                 sandbox: bool = False):
        self.client_id = client_id
        self.client_secret = client_secret
        self.sandbox = sandbox
        self._token: Optional[str] = None

    def enabled(self) -> bool:
        return bool(self.client_id) and bool(self.client_secret)

    def _authenticate(self) -> Optional[str]:
        if self._token:
            return self._token
        if not self.enabled():
            return None
        try:
            resp = requests.post(
                self.AUTH_URL,
                data={"grant_type": "client_credentials"},
                auth=(self.client_id, self.client_secret),
                timeout=20,
            )
            resp.raise_for_status()
            self._token = resp.json().get("access_token")
            return self._token
        except Exception as e:
            print(f"DigiKey auth error: {e}")
            return None

    def search(self, part_number: str) -> Optional[Dict[str, Any]]:
        token = self._authenticate()
        if not token:
            return None
        url = self.SEARCH_URL_SANDBOX if self.sandbox else self.SEARCH_URL
        headers = {
            "Authorization": f"Bearer {token}",
            "X-DIGIKEY-Client-Id": self.client_id,
            "Content-Type": "application/json",
        }
        payload = {"Keywords": part_number, "Limit": 5, "Offset": 0}
        try:
            resp = requests.post(url, headers=headers, json=payload, timeout=20)
            resp.raise_for_status()
            data = resp.json()
            products = data.get("Products", [])
            if not products:
                return None
            part = products[0]
            pkg = part.get("ProductVariations", [{}])[0]
            unit_price = None
            for pb in pkg.get("Pricing", []):
                if pb.get("BreakQuantity", 999999) <= 1:
                    unit_price = pb.get("UnitPrice", 0)
                    break
            return {
                "digikey_part": part.get("DigiKeyPartNumber", part_number),
                "description": part.get("ProductDescription", ""),
                "manufacturer": part.get("Manufacturer", {}).get("Name", ""),
                "manufacturer_pn": part.get("ManufacturerPartNumber", ""),
                "stock": part.get("QuantityAvailable", ""),
                "unit_price": unit_price,
                "currency": "USD",
                "url": f"https://www.digikey.com/en/products/detail/{part.get('DigiKeyPartNumber', '')}",
                "source": "digikey_api",
            }
        except Exception as e:
            print(f"DigiKey API error for {part_number}: {e}")
            return None
