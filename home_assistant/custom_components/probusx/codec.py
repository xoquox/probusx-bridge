"""Dekodiert Messwerte und Displaydaten. Benötigt kein Home Assistant."""

import struct
import zlib


# Wandelt jede Gruppe aus vier senkrechten 2-Bit-Pixeln in ein verlustfreies PNG um.
def display_png(payload: bytes) -> bytes:
    if len(payload) != 5120:
        raise ValueError("Displaydaten müssen genau 5120 Byte enthalten")
    rows = bytearray()
    for y in range(128):
        rows.append(0)  # PNG-Zeilenfilter 0: keine Filterung.
        for x in range(160):
            rows.append(255 - 85 * ((payload[(159 - x) * 32 + y // 4] >> (2 * (y % 4))) & 3))

    def chunk(kind, value):
        return (
            struct.pack(">I", len(value))
            + kind
            + value
            + struct.pack(">I", zlib.crc32(kind + value))
        )

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", 160, 128, 8, 0, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows))
        + chunk(b"IEND", b"")
    )


# Nur Familien mit einem Datenalter unter drei Sekunden gelten als aktuell.
def fresh(data, family):
    age = data.get(f"{family}_age_ms", -1)
    return isinstance(age, (int, float)) and 0 <= age < 3000


def temperature(raw):
    # Der Regler zeigt bei unbenutzten Eingängen mit Rohwert 2500 keinen Wert an.
    return None if raw == 2500 else raw / 10.0


# HE1 bis HE3 nutzen /100. Alle anderen bekannten Ausgangswörter nutzen /2.
def output(raw, index):
    scale = 100 if 8 <= index <= 10 else 2
    value = raw / scale
    return value if 0 <= value <= 100 else None


# Falsche Schemata, Arraylängen und Rohwerttypen ablehnen. Fehlende Werte nicht durch Null ersetzen.
def validate(data):
    if not isinstance(data, dict) or data.get("schema") != 1:
        raise ValueError("ProBusX-Bridge mit Firmware 3.x erforderlich")
    if not isinstance(data.get("device_id"), str) or len(data["device_id"]) != 17:
        raise ValueError("Ungültige Gerätekennung")
    for key, length in (("temperatures_raw", 14), ("outputs_raw", 15)):
        values = data.get(key)
        if (
            not isinstance(values, list)
            or len(values) != length
            or any(type(v) is not int or not -32768 <= v <= 32767 for v in values)
        ):
            raise ValueError(f"Ungültiges Feld {key}")
    settings = data.get("settings", {})
    if not isinstance(settings, dict):
        raise ValueError("Ungültige Einstellungen")
    for key, low, high in (
        ("display_ms", 100, 5000),
        ("temperature_ms", 250, 60000),
        ("output_ms", 250, 60000),
    ):
        if key in settings and (type(settings[key]) is not int or not low <= settings[key] <= high):
            raise ValueError(f"Ungültige Einstellung: {key}")
    return data
