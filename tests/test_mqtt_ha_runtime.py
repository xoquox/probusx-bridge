"""Prüft echte HA-MQTT-Entities gegen einen lokalen Testbroker.

Vorher mqtt_test.cpp ausführen. Dessen Ausgabeordner als Argument angeben.
Der Broker muss unter 127.0.0.1:18883 erreichbar sein. Kein reales Heizgerät erforderlich.
"""

import asyncio
import json
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch
import paho.mqtt.client as mqtt
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


async def main():
    fixture = Path(sys.argv[1])
    root = "probusx/aabbccddeeff"
    publisher = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="probusx-pruefung")
    commands = []
    publisher.on_message = lambda client, userdata, msg: commands.append(json.loads(msg.payload))
    publisher.connect("127.0.0.1", 18883)
    publisher.loop_start()
    publisher.subscribe(root + "/key")

    async def send(topic, payload, retain=True):
        info = publisher.publish(topic, payload, qos=1, retain=retain)
        await asyncio.to_thread(info.wait_for_publish, 5)

    async def wait(predicate, label):
        async with asyncio.timeout(30):
            while not predicate():
                await asyncio.sleep(0.1)
        print("Bestanden:", label, flush=True)

    with tempfile.TemporaryDirectory() as config:
        (Path(config) / "configuration.yaml").write_text("{}\n")
        hass = HomeAssistant(config)
        hass.config.skip_pip = True
        if hasattr(device_registry, "async_setup"):
            device_registry.async_setup(hass)
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
            await registry.async_load(hass)
        hass.auth = await auth.auth_manager_from_config(hass, [], [])
        try:
            # Die Prüfumgebung erlaubt keine Betriebssystem-Abfrage der Netzwerkadapter.
            # MQTT selbst verwendet den echten TCP-Broker auf der Loopback-Adresse.
            with patch(
                "homeassistant.components.network.util.ifaddr.get_adapters", return_value=[]
            ):
                assert await async_setup_component(hass, "network", {})
            assert await async_setup_component(hass, "mqtt", {})
            flow = await hass.config_entries.flow.async_init("mqtt", context={"source": "user"})
            assert flow["step_id"] == "broker", flow
            result = await hass.config_entries.flow.async_configure(
                flow["flow_id"],
                {
                    "broker": "127.0.0.1",
                    "port": 18883,
                    "protocol": "3.1.1",
                    "username": "",
                    "password": "",
                    "other_settings": {
                        "set_client_cert": False,
                        "set_ca_cert": "off",
                        "transport": "tcp",
                    },
                },
            )
            assert result["type"] == "create_entry", result
            await hass.async_block_till_done()
            await hass.async_start()
            records = json.loads((fixture / "discovery.json").read_text())
            for record in records:
                await send(record["topic"], json.dumps(record["payload"]))
            await send(root + "/availability", "online")
            for group in ["main", "ext", "display", "controls", "analysis", "bridge"]:
                await send(root + "/available/" + group, "online")
            temps = {"s" + str(i): 82.3 for i in range(1, 11)}
            temps.update({"f1s" + str(i): 32.2 for i in range(1, 5)})
            temps["s6"] = None
            temps.update(
                {
                    key + "_raw": 2500 if value is None else round(value * 10)
                    for key, value in list(temps.items())
                }
            )
            outputs = {
                key: 0
                for key in [
                    "r1",
                    "r2",
                    "r3",
                    "r4",
                    "r5",
                    "r6",
                    "r7",
                    "r0",
                    "he1",
                    "he2",
                    "he3",
                    "f1r1",
                    "f1r2",
                    "f1r3",
                    "f1he",
                ]
            }
            outputs["r1"] = 100.0
            outputs["he1"] = 15.0
            outputs.update(
                {
                    key + "_raw": round(value * (100 if key.startswith("he") else 2))
                    for key, value in list(outputs.items())
                }
            )
            outputs.update(
                {
                    key + "_aktiv": "ON" if value else "OFF"
                    for key, value in list(outputs.items())
                    if not key.endswith("_raw")
                }
            )
            await send(root + "/temperatures", json.dumps(temps))
            await send(root + "/outputs", json.dumps(outputs))
            await send(
                root + "/diagnostics",
                json.dumps(
                    {
                        key: 0
                        for key in [
                            "wifi_signal",
                            "uptime",
                            "free_heap",
                            "mqtt_connections",
                            "mqtt_errors",
                            "mqtt_rejected",
                            "crc_ok",
                            "crc_bad",
                            "uart_errors",
                            "tx_errors",
                            "key_expired",
                        ]
                    }
                ),
            )
            image = (fixture / "display.png").read_bytes()
            await send(root + "/display", image)
            await send(
                root + "/display_meta",
                json.dumps({"refresh_interval_ms": 250, "width": 160, "height": 128}),
            )
            await wait(
                lambda: len(
                    [
                        s
                        for s in hass.states.async_all()
                        if s.entity_id.startswith(
                            (
                                "sensor.probusx_",
                                "binary_sensor.probusx_",
                                "camera.probusx_",
                                "button.probusx_",
                            )
                        )
                    ]
                )
                == 61,
                "61 Entities aus Produktions-Discovery",
            )

            def state(entity):
                value = hass.states.get(entity)
                return value.state if value else None

            await wait(lambda: state("sensor.probusx_s1") == "82.3", "Temperatur")
            assert state("sensor.probusx_s6") == "unknown", state("sensor.probusx_s6")
            assert state("sensor.probusx_r1") in ("100.0", "100")
            assert state("sensor.probusx_he1") in ("15.0", "15")
            assert state("binary_sensor.probusx_r1_aktiv") == "on"
            assert state("binary_sensor.probusx_r3_aktiv") == "off"
            from homeassistant.components.camera import async_get_image

            im = await async_get_image(hass, "camera.probusx_display")
            assert im.content == image
            await wait(
                lambda: hass.states.get("camera.probusx_display").attributes.get(
                    "refresh_interval_ms"
                )
                == 250,
                "Kamera-Intervall als MQTT-Attribut",
            )
            print("Bestanden: Kamera empfängt PNG über MQTT; Typ", im.content_type, flush=True)
            for key, suffix in zip(
                ["up", "right", "down", "left", "special"],
                ["hoch", "rechts", "runter", "links", "dreitastendruck"],
                strict=True,
            ):
                await hass.services.async_call(
                    "button", "press", {"entity_id": "button.probusx_" + suffix}, blocking=True
                )
                await wait(lambda: len(commands) > 0 and commands[-1]["key"] == key, "Taste " + key)
                assert commands[-1]["session"] == "fresh"
            assert state("camera.probusx_display") != "unavailable"
            await send(root + "/available/main", "offline")
            await wait(lambda: state("sensor.probusx_s1") == "unavailable", "Busausfall")
            assert state("sensor.probusx_f1s1") == "32.2"
            await send(root + "/availability", "offline")
            await wait(lambda: state("sensor.probusx_f1s1") == "unavailable", "Bridge offline")
            assert state("camera.probusx_display") == "unavailable"
            await send(root + "/availability", "online")
            await send(root + "/available/main", "online")
            await wait(lambda: state("sensor.probusx_s1") == "82.3", "Wiederverbindung")
            await hass.config_entries.async_reload(result["result"].entry_id)
            await wait(
                lambda: state("sensor.probusx_s1") == "82.3",
                "Werte nach Neuladen aus Broker-Speicher",
            )
            im = await async_get_image(hass, "camera.probusx_display")
            assert im.content == image
            assert not hass.config_entries.async_entries("probusx")
            print("Bestanden: kein eigener ProBusX-HTTP-Eintrag vorhanden", flush=True)
        finally:
            await hass.async_stop(force=True)
            publisher.disconnect()
            publisher.loop_stop()


asyncio.run(main())
