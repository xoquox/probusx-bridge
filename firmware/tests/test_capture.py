import io
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import capture as c


def record(kind, seq, stamp, offset, payload):
    if not isinstance(payload, bytes):
        payload = json.dumps(payload, separators=(",", ":")).encode()
    prefix = struct.pack("<4sBBHIQQI", b"PBXS", 2, kind, 0, seq, stamp, offset, len(payload))
    crc = zlib.crc32(payload, zlib.crc32(prefix)) & 0xFFFFFFFF
    return prefix + struct.pack("<I", crc) + payload


def hello():
    return {
        "sender_enabled": False,
        "format": "8N1",
        "baud_requested": 1000000,
        "baud_configured": 1000000,
        "duration_ms": 1000,
    }


def stats(n, chunks=1, elapsed=100, **kwargs):
    result = {key: 0 for key in c.COUNTERS}
    result.update(rx_bytes=n, data_records=chunks, elapsed_us=elapsed, baud_configured=1000000)
    result.update(kwargs)
    return result


def session(data=b"\x00\xff\xa5\x13\x35\x01\x55\n\r\x00", errors=None):
    return (
        record(c.HELLO, 0, 1000, 0, hello())
        + record(c.DATA, 1, 1050, 0, data)
        + record(c.END, 2, 1100, len(data), stats(len(data), **(errors or {})))
    )


class Fragmented:
    def __init__(self, data, n=1):
        self.stream = io.BytesIO(data)
        self.n = n

    def read(self, n):
        return self.stream.read(min(n, self.n))


class CaptureTest(unittest.TestCase):
    def decode(self, data, fragment=1):
        binary, events = io.BytesIO(), io.StringIO()
        decoder = c.Decoder(binary, events)
        decoder.consume(Fragmented(data, fragment))
        return decoder, binary.getvalue(), [json.loads(x) for x in events.getvalue().splitlines()]

    def test_fragmented_tcp_preserves_all_byte_values(self):
        payload = bytes(range(256)) * 4
        decoder, binary, events = self.decode(session(payload))
        self.assertEqual(binary, payload)
        self.assertEqual(decoder.summary()["quality"], "no_reported_errors")
        self.assertEqual(len(events), 3)

    def test_coalesced_records(self):
        self.assertEqual(
            self.decode(session(), 65536)[0].summary()["quality"], "no_reported_errors"
        )

    def test_crc_covers_header_and_payload(self):
        original = session()
        for offset in (12, len(original) - 1):
            bad = bytearray(original)
            bad[offset] ^= 1
            with self.assertRaisesRegex(c.ProtocolError, "CRC"):
                self.decode(bytes(bad))

    def test_missing_end_is_never_silent(self):
        data = session()[:-10]
        decoder = c.Decoder(io.BytesIO(), io.StringIO())
        with self.assertRaises(c.ProtocolError):
            decoder.consume(Fragmented(data))
        self.assertEqual(decoder.summary()["quality"], "incomplete")

    def test_uart_error_in_final_record(self):
        decoder, _, _ = self.decode(session(errors={"fifo_overflows": 1, "frame_errors": 3}))
        self.assertEqual(decoder.summary()["quality"], "suspect")
        self.assertIn("fifo_overflows", decoder.summary()["problems"])

    def test_known_queue_gap_without_fabricated_bytes(self):
        data = (
            record(c.HELLO, 0, 1000, 0, hello())
            + record(c.DATA, 1, 1010, 0, b"abc")
            + record(c.DATA, 3, 1030, 6, b"ghi")
            + record(
                c.END,
                4,
                1100,
                9,
                stats(9, chunks=3, queue_dropped_bytes=3, queue_dropped_records=1),
            )
        )
        decoder, binary, events = self.decode(data)
        self.assertEqual(binary, b"abcghi")
        self.assertEqual(events[2]["missing_read_bytes_before"], 3)
        summary = decoder.summary()
        self.assertEqual(summary["quality"], "suspect")
        self.assertNotIn("byte_accounting_mismatch", summary["problems"])
        self.assertEqual(summary["known_read_byte_gaps"], 3)

    def test_dropped_final_data(self):
        data = record(c.HELLO, 0, 1000, 0, hello()) + record(
            c.END, 2, 1100, 10, stats(10, queue_dropped_bytes=10, queue_dropped_records=1)
        )
        decoder, _, _ = self.decode(data)
        self.assertEqual(decoder.summary()["known_read_byte_gaps"], 10)
        self.assertNotIn("drop_accounting_mismatch", decoder.summary()["problems"])

    def test_duplicate_sequence_rejected(self):
        data = record(c.HELLO, 0, 1000, 0, hello()) + record(c.DATA, 0, 1050, 0, b"x")
        with self.assertRaisesRegex(c.ProtocolError, "Sequenz"):
            self.decode(data)

    def test_missing_diagnostics_rejected(self):
        data = record(c.HELLO, 0, 1000, 0, hello()) + record(c.END, 1, 1100, 0, {"rx_bytes": 0})
        with self.assertRaisesRegex(c.ProtocolError, "Statistikfeld"):
            self.decode(data)

    def test_header_candidate_across_records(self):
        data = (
            record(c.HELLO, 0, 1000, 0, hello())
            + record(c.DATA, 1, 1010, 0, b"\xa5\x13")
            + record(c.DATA, 2, 1020, 2, b"\x35\x01\x60")
            + record(c.END, 3, 1100, 5, stats(5, chunks=2))
        )
        decoder, _, _ = self.decode(data)
        self.assertEqual(decoder.summary()["header_candidates_not_validated_frames"], {"35:60": 1})

    def test_empty_capture(self):
        data = record(c.HELLO, 0, 1000, 0, hello()) + record(c.END, 1, 1100, 0, stats(0, chunks=0))
        self.assertEqual(self.decode(data)[0].summary()["quality"], "no_data")

    def test_actual_socket_and_cli_replay(self):
        payload = bytes(range(256)) * 4
        contents = record(c.HELLO, 0, 1000, 0, hello())
        for i in range(200):
            contents += record(c.DATA, i + 1, 1010 + i, i * len(payload), payload)
        contents += record(
            c.END, 201, 1300, 200 * len(payload), stats(200 * len(payload), chunks=200, elapsed=300)
        )
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        port = listener.getsockname()[1]
        request = []

        def server():
            with listener:
                with listener.accept()[0] as conn:
                    command = b""
                    while not command.endswith(b"\n"):
                        command += conn.recv(1)
                    request.append(command)
                    # Variable fragmentation, splitting record headers and payloads.
                    pos = 0
                    for size in [1, 7, 31, 500, 4097] * 200:
                        if pos >= len(contents):
                            break
                        conn.sendall(contents[pos : pos + size])
                        pos += size
                    if pos < len(contents):
                        conn.sendall(contents[pos:])

        thread = threading.Thread(target=server, daemon=True)
        thread.start()
        with tempfile.TemporaryDirectory() as td:
            prefix = Path(td) / "capture"
            cli = str(Path(c.__file__))
            result = subprocess.run(
                [
                    sys.executable,
                    cli,
                    "capture",
                    "127.0.0.1",
                    str(prefix),
                    "--port",
                    str(port),
                    "--seconds",
                    "1",
                ],
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(prefix.with_suffix(".bin").read_bytes(), payload * 200)
            self.assertEqual(prefix.with_suffix(".pbxs").read_bytes(), contents)
            replay = Path(td) / "replay"
            result = subprocess.run(
                [sys.executable, cli, "decode", str(prefix.with_suffix(".pbxs")), str(replay)],
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(replay.with_suffix(".bin").read_bytes(), payload * 200)
            # Vorhandene Dateien müssen nach einem zweiten Versuch unverändert bleiben.
            result = subprocess.run(
                [sys.executable, cli, "decode", str(prefix.with_suffix(".pbxs")), str(replay)],
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertEqual(result.returncode, 2)
            self.assertEqual(replay.with_suffix(".bin").read_bytes(), payload * 200)
        thread.join(timeout=2)
        self.assertEqual(request, [b"START 1000\n"])

    def test_truncated_cli_keeps_raw_evidence(self):
        with tempfile.TemporaryDirectory() as td:
            source = Path(td) / "bad.pbxs"
            source.write_bytes(session()[:-3])
            prefix = Path(td) / "out"
            result = subprocess.run(
                [sys.executable, str(Path(c.__file__)), "decode", str(source), str(prefix)],
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertEqual(result.returncode, 2)
            self.assertEqual(
                json.loads(prefix.with_suffix(".summary.json").read_text())["quality"], "incomplete"
            )
            self.assertGreater(prefix.with_suffix(".bin").stat().st_size, 0)

    def test_cpp_wire_matches_python(self):
        root = Path(c.__file__).parent
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / "wire_test.cpp"
            exe = Path(td) / "test"
            src.write_text(
                '#include "wire.h"\n#include <cstdio>\nint main(){uint8_t h[36];'
                "uint8_t p[]={0,255,165,19};wire::header(h,2,123,987654321012ULL,123456789012ULL,p,4);"
                "fwrite(h,1,36,stdout);fwrite(p,1,4,stdout);}"
            )
            subprocess.run(
                ["g++", "-std=c++17", "-I", str(root / "src"), str(src), "-o", str(exe)], check=True
            )
            actual = subprocess.check_output([str(exe)])
            self.assertEqual(
                actual, record(c.DATA, 123, 987654321012, 123456789012, bytes([0, 255, 165, 19]))
            )


if __name__ == "__main__":
    unittest.main()
