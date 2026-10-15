"""Stellt fünf Tasten bereit. Sendet nur bei freigegebener und aktueller Bridge."""

import asyncio
import aiohttp
from homeassistant.components.button import ButtonEntity
from homeassistant.exceptions import HomeAssistantError
from .const import KEYS
from .entity import ProBusXEntity


async def async_setup_entry(hass, entry, async_add_entities):
    async_add_entities([Key(entry.runtime_data, key, name) for key, name in KEYS.items()])


class Key(ProBusXEntity, ButtonEntity):
    def __init__(self, c, key, name):
        super().__init__(c, f"key_{key}", name)
        self.key = key
        self._attr_icon = "mdi:tune" if key == "special" else f"mdi:arrow-{key}"

    @property
    def available(self):
        d = self.coordinator.data
        return (
            super().available
            and d.get("tx_enabled", False)
            and not d.get("busy", True)
            and 0 <= d.get("poll_age_ms", -1) < 2000
        )

    async def async_press(self):
        try:
            await self.coordinator.client.press(self.key)
        except (aiohttp.ClientError, asyncio.TimeoutError, ValueError) as err:
            raise HomeAssistantError(f"ProBusX: {err}") from err
        await self.coordinator.async_request_refresh()
