"""McMaster-Carr lookup (official API, web scrape fallback, order-paste export)."""

import re
from pathlib import Path
from typing import Any, Dict, Optional

import requests
from bs4 import BeautifulSoup


class McMasterAPIClient:
    """Official McMaster-Carr Product Information API client.

    Requires a client certificate, cert password, username, and password from
    McMaster-Carr e-procurement (eprocurement@mcmaster.com).
    """

    BASE_URL = "https://api.mcmaster.com"

    def __init__(
        self,
        username: Optional[str] = None,
        password: Optional[str] = None,
        cert_path: Optional[Path] = None,
        cert_password: Optional[str] = None,
        timeout: int = 30,
    ):
        self.username = username
        self.password = password
        self.cert_path = cert_path
        self.cert_password = cert_password
        self.timeout = timeout
        self._token: Optional[str] = None
        self._session = requests.Session()

    def enabled(self) -> bool:
        return bool(self.username and self.password and self.cert_path and self.cert_path.exists())

    def _cert(self) -> Any:
        if not self.cert_path:
            return None
        if self.cert_password:
            return (str(self.cert_path), str(self.cert_path.with_suffix(".key")))
        return str(self.cert_path)

    def login(self) -> Optional[str]:
        if self._token:
            return self._token
        if not self.enabled():
            return None
        try:
            resp = self._session.post(
                f"{self.BASE_URL}/v1/login",
                json={"UserName": self.username, "Password": self.password},
                cert=self._cert(),
                timeout=self.timeout,
            )
            resp.raise_for_status()
            data = resp.json()
            self._token = data.get("AuthToken")
            return self._token
        except Exception as e:
            print(f"McMaster API login error: {e}")
            return None

    def _headers(self) -> Dict[str, str]:
        return {"Authorization": f"Bearer {self._token}"}

    def add_product(self, part_number: str) -> bool:
        token = self.login()
        if not token:
            return False
        try:
            resp = self._session.put(
                f"{self.BASE_URL}/v1/products",
                headers=self._headers(),
                json={"URL": f"https://mcmaster.com/{part_number}"},
                cert=self._cert(),
                timeout=self.timeout,
            )
            return resp.status_code in (200, 201)
        except Exception as e:
            print(f"McMaster API add_product error for {part_number}: {e}")
            return False

    def get_price(self, part_number: str) -> Optional[Dict[str, Any]]:
        token = self.login()
        if not token:
            return None
        if not self.add_product(part_number):
            return None
        try:
            resp = self._session.get(
                f"{self.BASE_URL}/v1/products/{part_number}/price",
                headers=self._headers(),
                cert=self._cert(),
                timeout=self.timeout,
            )
            resp.raise_for_status()
            data = resp.json()
            if data and isinstance(data, list):
                pb = data[0]
                return {
                    "mcmaster_part": part_number,
                    "unit_price": float(pb.get("Amount", 0)),
                    "currency": "USD",
                    "min_qty": pb.get("MinimumQuantity", 1),
                    "uom": pb.get("UnitOfMeasure", "Each"),
                    "source": "mcmaster_api",
                    "url": f"https://www.mcmaster.com/{part_number}/",
                }
        except Exception as e:
            print(f"McMaster API price error for {part_number}: {e}")
        return None


class McMasterScrapeClient:
    """Best-effort McMaster-Carr web scraper (no API credentials needed)."""

    def __init__(self, timeout: int = 15):
        self.timeout = timeout
        self.session = requests.Session()
        self.session.headers.update({
            "User-Agent": (
                "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
            )
        })

    def search(self, part_number: str) -> Optional[Dict[str, Any]]:
        url = f"https://www.mcmaster.com/{part_number}/"
        try:
            resp = self.session.get(url, timeout=self.timeout)
            if resp.status_code != 200:
                return None
            soup = BeautifulSoup(resp.text, "html.parser")
            title = soup.find("title")
            description = title.get_text(strip=True) if title else ""
            text = soup.get_text(" ", strip=True)
            price = None
            for match in re.finditer(r"\$([0-9,]+\.\d{2})", text):
                price = float(match.group(1).replace(",", ""))
                break
            return {
                "mcmaster_part": part_number,
                "description": description,
                "unit_price": price,
                "currency": "USD",
                "url": url,
                "source": "mcmaster_scrape",
            }
        except Exception as e:
            print(f"McMaster scrape error for {part_number}: {e}")
            return None


class McMasterClient:
    """Combined McMaster client: official API if creds are available, else scrape."""

    def __init__(
        self,
        username: Optional[str] = None,
        password: Optional[str] = None,
        cert_path: Optional[Path] = None,
        cert_password: Optional[str] = None,
        use_scrape: bool = True,
        timeout: int = 30,
    ):
        self.api = McMasterAPIClient(username, password, cert_path, cert_password, timeout)
        self.scraper = McMasterScrapeClient(timeout)
        self.use_scrape = use_scrape

    def enabled(self) -> bool:
        return self.api.enabled() or self.use_scrape

    def search(self, part_number: str) -> Optional[Dict[str, Any]]:
        if self.api.enabled():
            result = self.api.get_price(part_number)
            if result:
                return result
        if self.use_scrape:
            return self.scraper.search(part_number)
        return None
