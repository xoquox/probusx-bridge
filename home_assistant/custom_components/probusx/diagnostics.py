"""Erstellt den Diagnosebericht. Entfernt den Sitzungstoken vor der Ausgabe."""

import asyncio
import aiohttp
from homeassistant.components.diagnostics import async_redact_data


async def async_get_config_entry_diagnostics(hass, entry):
    c = entry.runtime_data
    result = {
        "last_update_success": c.last_update_success,
        "ha_state_errors": c.state_errors,
        "ha_display_errors": c.display_errors,
        "last_state_error": c.last_state_error,
        "last_display_error": c.last_display_error,
        "last_state": async_redact_data(c.data, {"token", "device_id"}),
    }
    try:
        result["esp_diagnostics"] = async_redact_data(
            await c.client.diagnostics(), {"token", "device_id"}
        )
    except (aiohttp.ClientError, asyncio.TimeoutError, ValueError) as err:
        result["esp_diagnostics_error"] = str(err)
    return result
