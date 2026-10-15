#!/usr/bin/env python3
"""Prüft PBXS-Aufnahmen und schreibt die empfangenen Busbytes unverändert.

START steuert nur den TCP-Aufnahmedienst. START sendet keine Busdaten.
TCP-Blöcke und UART-Blöcke sind keine ProBusX-Telegrammgrenzen.
Benötigt Python ab Version 3.10 und nur die Standardbibliothek.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import socket
import struct
import sys
import time
import zlib

HEADER = struct.Struct("<4sBBHIQQII")
VERSION = 2
HELLO, DATA, STATUS, END = 1, 2, 3, 4
KINDS = {HELLO: "hello", DATA: "data", STATUS: "status", END: "end"}
ERROR_COUNTERS = (
    "queue_dropped_bytes",
    "queue_dropped_records",
    "fifo_overflows",
    "uart_buffer_full",
    "frame_errors",
    "parity_errors",
    "breaks",
    "event_queue_pressure",
)
COUNTERS = ("rx_bytes", "data_records") + ERROR_COUNTERS


class ProtocolError(ValueError):
    pass


class FileSource:
    def __init__(self, stream):
        self.stream = stream

    def read(self, size):
        return self.stream.read(size)


class SocketSource:
    def __init__(self, sock, wire_file, deadline):
        self.sock, self.wire_file, self.deadline = sock, wire_file, deadline

    def read(self, size):
        while time.monotonic() < self.deadline:
            try:
                block = self.sock.recv(size)
            except socket.timeout:
                continue
            self.wire_file.write(block)
            return block
        raise TimeoutError("Zeitlimit ohne vollständigen END-Datensatz erreicht")


def exact(source, size):
    data = bytearray()
    while len(data) < size:
        block = source.read(size - len(data))
        if not block:
            raise ProtocolError(f"Verbindung/Datei endet vorzeitig ({len(data)}/{size} Bytes)")
        data.extend(block)
    return bytes(data)


# Einen vollständigen PBXS-Datensatz lesen. Zuerst Transportkopf und Transport-CRC prüfen.
def read_record(source):
    header = exact(source, HEADER.size)
    magic, version, kind, flags, seq, timestamp, offset, size, crc = HEADER.unpack(header)
    if magic != b"PBXS" or version != VERSION:
        raise ProtocolError(
            "Kein PBXS-v2-Datenstrom. Alter transparenter TCP-Adapter ist nicht kompatibel."
        )
    if kind not in KINDS or flags != 0 or size > 1024:
        raise ProtocolError("Ungültiger Record-Typ, Flags oder Länge")
    payload = exact(source, size)
    actual_crc = zlib.crc32(payload, zlib.crc32(header[:32])) & 0xFFFFFFFF
    if actual_crc != crc:
        raise ProtocolError(f"Transport-CRC falsch bei Record {seq}")
    return kind, seq, timestamp, offset, payload


# Vergleicht Sequenzen, Offsets und Gerätestatistiken. ProBusX-Busframes werden hier nicht dekodiert.
class Decoder:
    def __init__(self, binary, events, include_hex=False, progress=False):
        self.binary, self.events = binary, events
        self.include_hex, self.progress = include_hex, progress
        self.bytes_written = 0
        self.data_records = 0
        self.next_seq = 0
        self.next_offset = 0
        self.sequence_gaps = 0
        self.byte_offset_gaps = 0
        self.last_time = 0
        self.started_us = None
        self.hello = None
        self.latest_stats = None
        self.end = None
        self.problems = []
        self.hash = hashlib.sha256()
        self.header_candidates = Counter()
        self.tail = b""
        self.progress_time = 0.0

    def problem(self, value):
        if value not in self.problems:
            self.problems.append(value)

    # Die Nutzbytes unverändert schreiben. Verlustkennungen bleiben zusätzlich in JSONL erhalten.
    def consume(self, source):
        while self.end is None:
            kind, seq, timestamp, offset, payload = read_record(source)
            if self.hello is None and (kind != HELLO or seq != 0 or offset != 0):
                raise ProtocolError("Datenstrom beginnt nicht mit HELLO, seq=0, offset=0")
            if seq < self.next_seq:
                raise ProtocolError("Doppelte oder rückwärts laufende Sequenznummer")
            self.sequence_gaps += seq - self.next_seq
            self.next_seq = seq + 1
            if timestamp < self.last_time:
                raise ProtocolError("Rückwärts laufender ESP32-Zeitstempel")
            self.last_time = timestamp
            event = {
                "type": KINDS[kind],
                "sequence": seq,
                "esp_time_us": timestamp,
                "stream_offset": offset,
                "file_offset": self.bytes_written,
                "transport_crc_ok": True,
            }
            if kind == DATA:
                if not payload:
                    raise ProtocolError("Leerer DATA-Datensatz")
                if offset < self.next_offset:
                    raise ProtocolError("Überlappende Daten-Offsets")
                gap = offset - self.next_offset
                if gap:
                    self.tail = b""
                self.byte_offset_gaps += gap
                self.next_offset = offset + len(payload)
                scan = self.tail + payload
                for i in range(max(0, len(scan) - 4)):
                    if (
                        scan[i : i + 2] == b"\xa5\x13"
                        and scan[i + 2] in (0x35, 0x36)
                        and scan[i + 3] == 1
                    ):
                        self.header_candidates[f"{scan[i+2]:02x}:{scan[i+4]:02x}"] += 1
                self.tail = scan[-4:]
                event.update(length=len(payload), missing_read_bytes_before=gap)
                if self.include_hex:
                    event["hex"] = payload.hex()
                self.binary.write(payload)
                self.hash.update(payload)
                self.bytes_written += len(payload)
                self.data_records += 1
            else:
                try:
                    metadata = json.loads(payload)
                except (ValueError, UnicodeDecodeError) as exc:
                    raise ProtocolError("Ungültige JSON-Metadaten") from exc
                if not isinstance(metadata, dict):
                    raise ProtocolError("JSON-Metadaten sind kein Objekt")
                event["metadata"] = metadata
                if kind == HELLO:
                    if self.hello is not None:
                        raise ProtocolError("Zweites HELLO innerhalb einer Aufnahme")
                    if (
                        metadata.get("sender_enabled") is not False
                        or metadata.get("format") != "8N1"
                    ):
                        raise ProtocolError("Unerwarteter Senderzustand oder UART-Format")
                    for key in ("baud_requested", "baud_configured", "duration_ms"):
                        if type(metadata.get(key)) is not int or metadata[key] <= 0:
                            raise ProtocolError(f"HELLO-Feld fehlt oder ist ungültig: {key}")
                    if metadata["baud_configured"] != metadata["baud_requested"]:
                        self.problem("configured_baud_differs_from_requested")
                    self.started_us = timestamp
                    self.hello = metadata
                else:
                    for key in COUNTERS + ("elapsed_us", "baud_configured"):
                        value = metadata.get(key)
                        if type(value) is not int or value < 0:
                            raise ProtocolError(f"Statistikfeld fehlt oder ist ungültig: {key}")
                        if self.latest_stats and key in COUNTERS and value < self.latest_stats[key]:
                            raise ProtocolError(f"Rückwärts laufender Fehlerzähler: {key}")
                    if metadata["rx_bytes"] != offset or offset < self.next_offset:
                        raise ProtocolError("Statistik-/Stream-Offset widersprüchlich")
                    if metadata["elapsed_us"] != timestamp - self.started_us:
                        raise ProtocolError("Widersprüchliche Aufnahmedauer")
                    if metadata["baud_configured"] != self.hello["baud_configured"]:
                        raise ProtocolError("Baudrate hat sich während der Aufnahme geändert")
                    self.latest_stats = metadata
                    if kind == END:
                        self.end = metadata
                        trailing_gap = offset - self.next_offset
                        self.byte_offset_gaps += trailing_gap
                        event["missing_read_bytes_at_end"] = trailing_gap
            self.events.write(json.dumps(event, ensure_ascii=False) + "\n")
            if self.progress and time.monotonic() - self.progress_time >= 1:
                self.progress_time = time.monotonic()
                print(
                    f"\r{self.bytes_written:,} Bytes gespeichert",
                    end="",
                    file=sys.stderr,
                    flush=True,
                )

    # Eine fehlerfreie Transportprüfung beweist keine fehlerfreien elektrischen Busbits.
    def summary(self, error=None):
        if error:
            self.problem(error)
        if self.end is None:
            self.problem("missing_end_record")
        if self.sequence_gaps:
            self.problem("record_sequence_gaps")
        if self.byte_offset_gaps:
            self.problem("read_byte_offset_gaps")
        if self.latest_stats:
            for key in ERROR_COUNTERS:
                if self.latest_stats[key]:
                    self.problem(key)
        if self.end:
            if self.bytes_written + self.end["queue_dropped_bytes"] != self.end["rx_bytes"]:
                self.problem("byte_accounting_mismatch")
            if self.byte_offset_gaps != self.end["queue_dropped_bytes"]:
                self.problem("drop_accounting_mismatch")
            if self.sequence_gaps != self.end["queue_dropped_records"]:
                self.problem("record_accounting_mismatch")
            if self.data_records > self.end["data_records"]:
                self.problem("data_record_count_mismatch")
        quality = (
            "incomplete"
            if self.end is None
            else (
                "suspect"
                if self.problems
                else "no_data" if not self.bytes_written else "no_reported_errors"
            )
        )
        return {
            "capture_format": "PBXS-v2",
            "quality": quality,
            "complete_end_received": self.end is not None,
            "problems": self.problems,
            "saved_bytes": self.bytes_written,
            "data_records_saved": self.data_records,
            "sequence_gaps": self.sequence_gaps,
            "known_read_byte_gaps": self.byte_offset_gaps,
            "sha256_bin": self.hash.hexdigest(),
            "hello": self.hello,
            "last_device_statistics": self.latest_stats,
            "header_candidates_not_validated_frames": dict(self.header_candidates),
            "limitations": [
                "Keine Prüfung einer ProBusX-Prüfsumme.",
                "Keine exakten Bit-/Bytezeitstempel oder Senderzuordnung.",
                "UART-Verluste vor dem Auslesen sind nicht exakt zählbar.",
                "Keine gemeldeten Fehler beweisen keine fehlerfreie physische Übertragung.",
            ],
        }


def run_capture(args):
    prefix = Path(args.prefix)
    paths = {
        key: Path(str(prefix) + suffix)
        for key, suffix in [("bin", ".bin"), ("events", ".jsonl"), ("summary", ".summary.json")]
    }
    if args.command == "capture":
        paths["wire"] = Path(str(prefix) + ".pbxs")
    # Eine vorhandene Aufnahme nicht überschreiben. Das gilt auch für unvollständige Dateien.
    for path in paths.values():
        if path.exists():
            raise FileExistsError(f"Datei existiert bereits: {path}")
    for path in paths.values():
        path.parent.mkdir(parents=True, exist_ok=True)
    decoder = None
    error = None
    interrupted = False
    created_at = datetime.now(timezone.utc).isoformat()
    from contextlib import ExitStack

    with ExitStack() as stack:
        # Jeden Dateinamen exklusiv reservieren. Bei einem Fehler die neuen leeren Dateien entfernen.
        files = {}
        try:
            for key, path in paths.items():
                files[key] = stack.enter_context(path.open("xb"))
        except Exception:
            stack.close()
            for key in files:
                paths[key].unlink()
            raise
        import io

        events = stack.enter_context(io.TextIOWrapper(files["events"], encoding="utf-8"))
        decoder = Decoder(files["bin"], events, args.hex, progress=True)
        try:
            if args.command == "capture":
                sock = stack.enter_context(
                    socket.create_connection((args.host, args.port), timeout=10)
                )
                sock.settimeout(1)
                milliseconds = round(args.seconds * 1000)
                sock.sendall(f"START {milliseconds}\n".encode("ascii"))
                source = SocketSource(sock, files["wire"], time.monotonic() + args.seconds + 60)
            else:
                source = FileSource(stack.enter_context(Path(args.input).open("rb")))
            decoder.consume(source)
            if args.command == "decode" and source.read(1):
                raise ProtocolError("Zusätzliche Bytes nach END; keine einzelne gültige Session")
        except KeyboardInterrupt:
            interrupted = True
            error = "interrupted_by_user"
        except (OSError, ProtocolError) as exc:
            error = f"{type(exc).__name__}: {exc}"
        summary = decoder.summary(error)
        summary["created_at_utc"] = created_at
        summary["files"] = {key: str(value) for key, value in paths.items()}
        if args.command == "capture":
            summary["host"] = args.host
            summary["port"] = args.port
        files["summary"].write(
            (json.dumps(summary, indent=2, ensure_ascii=False) + "\n").encode("utf8")
        )
    print(file=sys.stderr)
    print(f"{summary['saved_bytes']:,} Bytes: {paths['bin']}")
    print(f"Bewertung: {summary['quality']} | Diagnose: {paths['summary']}")
    if error:
        print(error, file=sys.stderr)
    if interrupted:
        return 130
    return 0 if summary["quality"] == "no_reported_errors" else 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    capture = sub.add_parser("capture", help="Aufnahme vom Waveshare-Bridge")
    capture.add_argument("host")
    capture.add_argument("prefix", help="Dateipräfix ohne Endung, z.B. aufnahme_01")
    capture.add_argument("--port", type=int, default=4196)
    capture.add_argument("--seconds", type=float, default=300)
    capture.add_argument("--hex", action="store_true", help="Rohbytes zusätzlich als Hex in JSONL")
    decode = sub.add_parser("decode", help="PBXS-Datei offline erneut auswerten")
    decode.add_argument("input")
    decode.add_argument("prefix")
    decode.add_argument("--hex", action="store_true")
    args = parser.parse_args()
    if args.command == "capture" and not (1 <= args.seconds <= 3600):
        parser.error("--seconds muss zwischen 1 und 3600 liegen")
    try:
        return run_capture(args)
    except (OSError, ValueError) as exc:
        print(f"Fehler: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
