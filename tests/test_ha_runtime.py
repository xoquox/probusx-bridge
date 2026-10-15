"""Prüft die echte HA-Laufzeit mit einem lokalen HTTP-Simulator."""

import asyncio
import copy
import json
from pathlib import Path
import shutil
import tempfile
from unittest.mock import patch
import aiohttp
from aiohttp import web
from homeassistant.core import HomeAssistant
from homeassistant.setup import async_setup_component
from homeassistant import auth, loader
from homeassistant.config_entries import ConfigEntries
from homeassistant.helpers import (
    entity_registry,
    device_registry,
    area_registry,
    floor_registry,
    label_registry,
)

ROOT = Path(__file__).resolve().parents[1]


async def main():
    state = {
        "schema": 1,
        "device_id": "aa:bb:cc:dd:ee:ff",
        "version": "3.2.0",
        "token": "test",
        "tx_enabled": True,
        "busy": False,
        "key_pending": False,
        "main_age_ms": 10,
        "ext_age_ms": 10,
        "display_age_ms": 10,
        "poll_age_ms": 10,
        "main_frames": 1,
        "ext_frames": 1,
        "display_frames": 1,
        "tx_frames": 0,
        "uart_errors": 0,
        "temperatures_raw": [
            823,
            788,
            813,
            193,
            441,
            2500,
            2500,
            801,
            339,
            184,
            322,
            2500,
            2500,
            2500,
        ],
        "outputs_raw": [200, 200, 0, 0, 0, 0, 200, 0, 1500, 0, 0, 0, 0, 0, 0],
    }
    state["diagnostics"] = {
        "crc_bad": 0,
        "crc_ok": 200,
        "crc_error_percent_60s": 0.0,
        "uart_errors": 0,
        "rx_bytes_per_s": 10770,
        "partial_timeouts": 0,
        "main_gaps": 0,
        "display_gaps": 0,
        "main_crc_verified": True,
    }
    state["settings"] = {
        "display_ms": 250,
        "temperature_ms": 1000,
        "output_ms": 1000,
        "analysis_enabled": True,
        "transmit_enabled": True,
    }
    image_requests = []
    failures = {"state": False, "display": False}
    raw = (ROOT / "tests/display.raw").read_bytes()
    pressed = []

    async def live(req):
        return web.json_response(state, status=503 if failures["state"] else 200)

    async def image(req):
        image_requests.append(asyncio.get_running_loop().time())
        return web.Response(
            body=raw,
            content_type="application/octet-stream",
            status=503 if failures["display"] else 200,
        )

    async def diagnostics(req):
        return web.json_response(
            {"device_id": state["device_id"], "crc_bad": 0, "token": "must-redact"}
        )

    async def key(req):
        data = await req.post()
        assert data["token"] == "test"
        pressed.append(data["key"])
        return web.json_response({"accepted": True})

    app = web.Application()
    app.router.add_get("/api/live", live)
    app.router.add_get("/api/display", image)
    app.router.add_post("/api/key", key)
    app.router.add_get("/api/diagnostics", diagnostics)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, "127.0.0.1", 0)
    await site.start()
    port = site._server.sockets[0].getsockname()[1]
    with tempfile.TemporaryDirectory() as config:
        shutil.copytree(
            ROOT / "home_assistant/custom_components", Path(config) / "custom_components"
        )
        hass = HomeAssistant(config)
        hass.config.skip_pip = True
        hass.auth = await auth.auth_manager_from_config(hass, [], [])
        loader.async_setup(hass)
        hass.config_entries = ConfigEntries(hass, {})
        await hass.config_entries.async_initialize()
        for registry in (
            floor_registry,
            label_registry,
            area_registry,
            device_registry,
            entity_registry,
        ):
            await registry.async_get(hass).async_load()
        try:
            assert await async_setup_component(hass, "network", {})
            assert await async_setup_component(hass, "probusx", {})
            result = await hass.config_entries.flow.async_init(
                "probusx", context={"source": "user"}, data={"host": "127.0.0.1", "port": port}
            )
            assert result["type"] == "create_entry", result
            await hass.async_block_till_done()
            entry = result["result"]
            if str(entry.state.value) != "loaded":
                raise AssertionError(f"Integration wurde nicht geladen: {entry.state}")
            entities = {
                s.entity_id: s
                for s in hass.states.async_all()
                if s.entity_id.split(".")[0] in ("sensor", "binary_sensor", "camera", "button")
            }
            assert len(entities) == 61, (len(entities), list(entities))
            assert entities["sensor.probusx_s1"].state == "82.3"
            assert entities["sensor.probusx_s6"].state == "unknown"
            assert entities["sensor.probusx_r1"].state == "100.0"
            assert entities["sensor.probusx_he1"].state == "15.0"
            assert entities["binary_sensor.probusx_r1_aktiv"].state == "on"
            assert entities["binary_sensor.probusx_r3_aktiv"].state == "off"
            from homeassistant.components.camera import async_get_image

            im = await async_get_image(hass, "camera.probusx_display")
            assert im.content_type == "image/png", im.content_type
            assert im.content.startswith(b"\x89PNG")
            count = len(image_requests)
            for _ in range(5):
                await asyncio.sleep(0.26)
                await async_get_image(hass, "camera.probusx_display")
            assert (
                len(image_requests) - count >= 4
            ), "Die HA-Kamera muss schneller als einmal je Sekunde abrufen"
            await hass.services.async_call(
                "button", "press", {"entity_id": "button.probusx_dreitastendruck"}, blocking=True
            )
            assert pressed == ["special"], pressed
            assert entities["sensor.probusx_crc_fehler"].state == "0"
            assert entities["sensor.probusx_s1"].attributes["validation"] == "crc16"
            from custom_components.probusx.diagnostics import async_get_config_entry_diagnostics

            report = await async_get_config_entry_diagnostics(hass, entry)
            assert "must-redact" not in json.dumps(report)
            assert report["last_state"]["token"] == "**REDACTED**"
            coordinator = entry.runtime_data
            assert (
                hass.states.get("camera.probusx_display").attributes["refresh_interval_ms"] == 250
            )
            count = len(image_requests)
            await asyncio.gather(*[coordinator.async_refresh_image() for _ in range(8)])
            assert len(image_requests) - count <= 1
            await asyncio.sleep(0.27)
            count = len(image_requests)
            await asyncio.gather(*[coordinator.async_refresh_image() for _ in range(8)])
            assert len(image_requests) == count + 1
            state["settings"]["display_ms"] = 500
            state["settings"]["temperature_ms"] = 10000
            state["settings"]["output_ms"] = 250
            state["temperatures_raw"][0] = 900
            state["outputs_raw"][0] = 100
            coordinator.last_temperature_update = hass.loop.time()
            coordinator.last_output_update = 0
            await coordinator.async_refresh()
            await hass.async_block_till_done()
            assert coordinator.display_ms == 500
            assert coordinator.update_interval.total_seconds() == 0.25
            assert hass.states.get("sensor.probusx_s1").state == "82.3"
            assert hass.states.get("sensor.probusx_r1").state == "50.0"
            state["diagnostics"]["enabled"] = False
            await coordinator.async_refresh()
            await hass.async_block_till_done()
            assert hass.states.get("sensor.probusx_crc_fehler").state == "unavailable"
            state["diagnostics"]["enabled"] = True
            state["temperatures_raw"][0] = 823
            state["settings"]["display_ms"] = 250
            state["settings"]["temperature_ms"] = 1000
            state["settings"]["output_ms"] = 1000
            coordinator.last_image_attempt = 0
            failures["display"] = True
            await entry.runtime_data.async_refresh()
            await hass.async_block_till_done()
            assert entry.runtime_data.last_update_success
            assert entry.runtime_data.display_errors == 1
            assert hass.states.get("sensor.probusx_s1").state == "82.3"
            assert hass.states.get("camera.probusx_display").state == "unavailable"
            failures["display"] = False
            failures["state"] = True
            await entry.runtime_data.async_refresh()
            assert (
                not entry.runtime_data.last_update_success and entry.runtime_data.state_errors == 1
            )
            failures["state"] = False
            entry.runtime_data.last_image_attempt = 0
            await entry.runtime_data.async_refresh()
            await hass.async_block_till_done()
            assert hass.states.get("sensor.probusx_ha_datenabruf_fehler").state == "1"
            assert hass.states.get("sensor.probusx_crc_fehler").state == "0"
            state["main_age_ms"] = 5000
            state["ext_age_ms"] = 5000
            state["display_age_ms"] = 5000
            state["tx_enabled"] = False
            await entry.runtime_data.async_refresh()
            await hass.async_block_till_done()
            assert hass.states.get("sensor.probusx_s1").state == "unavailable"
            assert hass.states.get("camera.probusx_display").state == "unavailable"
            assert hass.states.get("button.probusx_hoch").state == "unavailable"
            # Bei falschem API-Schema muss der Abruf fehlschlagen. Keine Nullwerte erzeugen.
            state["schema"] = 99
            await entry.runtime_data.async_refresh()
            assert not entry.runtime_data.last_update_success
            assert await hass.config_entries.async_unload(entry.entry_id)
            print(
                json.dumps(
                    {
                        "ha_version": "2025.8.3",
                        "entities": len(entities),
                        "passed": [
                            "config_flow",
                            "image_request_coalescing",
                            "image_interval_change",
                            "independent_value_intervals",
                            "analysis_unavailable_when_disabled",
                            "all_platforms",
                            "diagnostic_entities",
                            "redaction",
                            "http_failure_separate_from_crc",
                            "display_failure_preserves_sensors",
                            "temperatures",
                            "unused_sensor",
                            "outputs",
                            "binary_states",
                            "camera_png",
                            "special_key",
                            "stale_bus",
                            "tx_disabled",
                            "invalid_schema",
                            "unload",
                        ],
                    },
                    indent=2,
                )
            )
        finally:
            await hass.async_stop(force=True)
    await runner.cleanup()


class LocalResolver(aiohttp.ThreadedResolver):
    async def real_close(self):
        await super().close()


if __name__ == "__main__":
    # Im Test keine realen Netzwerkadapter suchen. Der HTTP-Simulator läuft lokal.
    with (
        patch("ifaddr.get_adapters", return_value=[]),
        patch(
            "homeassistant.helpers.aiohttp_client._async_make_resolver",
            side_effect=lambda hass: LocalResolver(),
        ),
    ):
        asyncio.run(main())
