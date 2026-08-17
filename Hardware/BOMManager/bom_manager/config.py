"""Configuration loading for BOM Manager."""

import os
from pathlib import Path
from typing import Any, Dict, Optional

import yaml


class Config:
    """Loads config.yaml from the BOMManager root and merges environment variables."""

    def __init__(self, config_path: Optional[Path] = None):
        if config_path is None:
            # Package root -> Hardware/BOMManager
            self.root = Path(__file__).resolve().parent.parent
            self.config_path = self.root / "config.yaml"
        else:
            self.config_path = Path(config_path)
            self.root = self.config_path.parent

        self._data: Dict[str, Any] = {}
        self._load()

    def _load(self) -> None:
        if self.config_path.exists():
            with open(self.config_path, "r", encoding="utf-8") as f:
                self._data = yaml.safe_load(f) or {}
        else:
            self._data = {}

    def get(self, key: str, default: Any = None) -> Any:
        """Get a config value. Keys may be dotted, e.g. 'digikey.client_id'."""
        env_var = "BOM_" + key.upper().replace(".", "_")
        env_value = os.environ.get(env_var)
        if env_value is not None:
            return env_value

        value = self._data
        for part in key.split("."):
            if isinstance(value, dict) and part in value:
                value = value[part]
            else:
                return default
        return value

    def has_api_key(self, vendor: str) -> bool:
        """Return True if any API credential for the vendor is present."""
        vendor = vendor.lower()
        if vendor == "mouser":
            return bool(self.get("mouser.api_key"))
        if vendor == "digikey":
            return bool(self.get("digikey.client_id")) and bool(self.get("digikey.client_secret"))
        if vendor == "octopart":
            return bool(self.get("octopart.api_key"))
        return False

    def api_enabled_vendors(self) -> list:
        return [v for v in ["mouser", "digikey", "octopart"] if self.has_api_key(v)]
