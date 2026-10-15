"""Prüft die ESP32-S3-Abbilder einschließlich Prüfsummen und Partitionen."""

import hashlib, pathlib, struct

ROOT = pathlib.Path(__file__).resolve().parents[1]


def verify_image(data, app=False):
    assert data[0] == 0xE9 and 1 <= data[1] <= 16, "Ungültiger ESP-Abbildkopf"
    assert struct.unpack_from("<H", data, 12)[0] == 9, "Ungültige ESP32-S3-Kennung"
    assert data[23] == 1, "Angehängte SHA-256-Prüfsumme erforderlich"
    pos = 24
    checksum = 0xEF
    for _ in range(data[1]):
        _, size = struct.unpack_from("<II", data, pos)
        pos += 8
        assert pos + size <= len(data), "Unvollständiges Segment"
        for b in data[pos : pos + size]:
            checksum ^= b
        pos += size
    pos += 15 - pos % 16
    assert data[pos] == checksum, "Segment-Prüfsumme stimmt nicht überein"
    digest = hashlib.sha256(data[: pos + 1]).digest()
    assert data[pos + 1 : pos + 33] == digest, "SHA-256-Prüfsumme stimmt nicht überein"
    if app:
        assert len(data) == pos + 33, "Unerwartete Daten am Ende"
        assert (
            struct.unpack_from("<I", data, 32)[0] == 0xABCD5432
        ), "Ungültige Anwendungsbeschreibung"
        assert (
            data[80:112].split(b"\0")[0] == b"pbx_waveshare_ota_v1"
        ), "Ungültige OTA-Projektkennung"
        assert (
            data[48:80].split(b"\0")[0].decode() == (ROOT / "version.txt").read_text().strip()
        ), "Version stimmt nicht überein"
    return pos + 33


def main():
    out = ROOT / "abbilder"
    app = (out / "probusx-bridge-waveshare-ota.bin").read_bytes()
    usb = (out / "probusx-bridge-waveshare-usb.bin").read_bytes()
    verify_image(app, True)
    verify_image(usb)
    assert len(app) <= 0x300000
    assert usb[0x10000 : 0x10000 + len(app)] == app, "USB-Abbild enthält eine andere Anwendung"
    assert (
        usb[0x9000:0x10000] == b"\xff" * 0x7000
    ), "USB-Abbild setzt Zugangsdaten nicht wie erwartet zurück"
    assert usb[0x610000:0x612000] == b"\xff" * 0x2000, "OTA-Startauswahl muss zurückgesetzt sein"
    expected = [
        (1, 2, 0x9000, 0x6000, b"nvs"),
        (1, 1, 0xF000, 0x1000, b"phy_init"),
        (0, 0x10, 0x10000, 0x300000, b"ota_0"),
        (0, 0x11, 0x310000, 0x300000, b"ota_1"),
        (1, 0, 0x610000, 0x2000, b"otadata"),
    ]
    for i, entry in enumerate(expected):
        magic, typ, sub, offset, size, label, flags = struct.unpack_from(
            "<HBBII16sI", usb, 0x8000 + 32 * i
        )
        assert magic == 0x50AA and flags == 0
        assert (
            typ,
            sub,
            offset,
            size,
            label.rstrip(b"\0"),
        ) == entry, "Partition stimmt nicht überein"
    print(
        f"Bestanden: Abbildprüfsummen, SHA-256, Projektkennung, Version, Partitionen, identische Anwendung und OTA-Startdaten. Anwendung {len(app)} Byte, USB {len(usb)} bytes"
    )


if __name__ == "__main__":
    main()
