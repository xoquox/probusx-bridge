"""Bindet die Waveshare-Bridge mit Firmware 3.x in Home Assistant ein."""

from datetime import timedelta
import asyncio
import logging
import time
import aiohttp
from homeassistant.const import Platform
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed
from .client import Client
from .codec import display_png, fresh
from .const import DOMAIN

PLATFORMS = [Platform.SENSOR, Platform.BINARY_SENSOR, Platform.BUTTON, Platform.CAMERA]
_LOGGER = logging.getLogger(__name__)


# Koordiniert Status und Bild. Mehrere Entities teilen denselben Statusabruf.
class Coordinator(DataUpdateCoordinator):
    def __init__(self, hass, entry):
        super().__init__(
            hass, _LOGGER, name=DOMAIN, config_entry=entry, update_interval=timedelta(seconds=1)
        )
        self.client = Client(
            async_get_clientsession(hass), entry.data["host"], entry.data.get("port", 80)
        )
        self.state_errors = 0
        self.display_errors = 0
        self.last_state_error = None
        self.last_display_error = None
        self.last_http_ms = 0.0
        self.png = None
        self.last_pixels = None
        self.last_image_success = 0.0
        self.identity = entry.unique_id
        self.url = str(self.client.base)
        self.image_lock = asyncio.Lock()
        self.last_image_attempt = 0.0
        self.display_ms = 250
        self.last_temperature_update = 0.0
        self.last_output_update = 0.0

    # Die Bildsperre fasst gleichzeitige Kameraabrufe zusammen. Unveränderte Pixel nutzen das vorhandene PNG.
    async def async_refresh_image(self):
        async with self.image_lock:
            now = self.hass.loop.time()
            # Die Zeittoleranz verhindert, dass zwei gleich eingestellte Zeitgeber jedes zweite Bild auslassen.
            if now - self.last_image_attempt < self.display_ms / 1000 - min(
                0.02, self.display_ms / 10000
            ):
                return
            self.last_image_attempt = now
            try:
                pixels = await self.client.display()
                if pixels != self.last_pixels or self.png is None:
                    self.png = await self.hass.async_add_executor_job(display_png, pixels)
                    self.last_pixels = pixels
                self.last_image_success = self.hass.loop.time()
            except (
                aiohttp.ClientError,
                asyncio.TimeoutError,
                asyncio.IncompleteReadError,
                ValueError,
            ) as err:
                self.display_errors += 1
                self.last_display_error = str(err)
                self.png = None

    # Zuerst Gerätekennung prüfen. Danach Temperatur- und Ausgangswerte getrennt nach ihrem Intervall übernehmen.
    async def _async_update_data(self):
        started = time.monotonic()
        try:
            state = await self.client.state()
            if state["device_id"] != self.identity:
                raise ValueError("Unter dieser Adresse antwortet ein anderes ProBusX-Gerät")
        except (aiohttp.ClientError, asyncio.TimeoutError, ValueError) as err:
            self.state_errors += 1
            self.last_state_error = str(err)
            self.png = None
            raise UpdateFailed(str(err)) from err
        self.last_http_ms = (time.monotonic() - started) * 1000
        settings = state.get("settings", {})
        self.display_ms = settings.get("display_ms", 250)
        temperature_ms = settings.get("temperature_ms", 1000)
        output_ms = settings.get("output_ms", 1000)
        # Status mindestens einmal je Sekunde abrufen. So bleiben Einstellungen und Verfügbarkeit aktuell.
        self.update_interval = timedelta(milliseconds=min(1000, temperature_ms, output_ms))
        now = self.hass.loop.time()
        for key, interval, attr in (
            ("temperatures_raw", temperature_ms, "last_temperature_update"),
            ("outputs_raw", output_ms, "last_output_update"),
        ):
            if self.data and now - getattr(self, attr) < interval / 1000:
                state[key] = self.data[key]
            else:
                setattr(self, attr, now)
        if fresh(state, "display"):
            await self.async_refresh_image()
        else:
            self.png = None
        state["ha_transport"] = {
            "state_errors": self.state_errors,
            "display_errors": self.display_errors,
            "http_ms": round(self.last_http_ms, 1),
        }
        return state


async def async_setup_entry(hass, entry):
    coordinator = Coordinator(hass, entry)
    await coordinator.async_config_entry_first_refresh()
    entry.runtime_data = coordinator
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass, entry):
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
