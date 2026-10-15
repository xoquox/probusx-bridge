import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "codec", ROOT / "home_assistant/custom_components/probusx/codec.py"
)
codec = importlib.util.module_from_spec(spec)
spec.loader.exec_module(codec)


class CodecTests(unittest.TestCase):
    def test_png_geometry_orientation_and_gray(self):
        raw = bytearray(5120)
        raw[159 * 32] = 0b11100100
        png = codec.display_png(raw)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        at = 8
        packed = b""
        while at < len(png):
            size = int.from_bytes(png[at : at + 4], "big")
            kind = png[at + 4 : at + 8]
            value = png[at + 8 : at + 8 + size]
            crc = int.from_bytes(png[at + 8 + size : at + 12 + size], "big")
            self.assertEqual(zlib.crc32(kind + value), crc)
            if kind == b"IHDR":
                self.assertEqual(struct.unpack(">IIBBBBB", value), (160, 128, 8, 0, 0, 0, 0))
            if kind == b"IDAT":
                packed += value
            at += size + 12
        rows = zlib.decompress(packed)
        self.assertEqual([rows[y * 161 + 1] for y in range(4)], [255, 170, 85, 0])

    def test_values_and_sentinels(self):
        self.assertEqual(codec.temperature(-123), -12.3)
        self.assertIsNone(codec.temperature(2500))
        self.assertEqual(codec.output(200, 0), 100)
        self.assertEqual(codec.output(1500, 8), 15)
        self.assertEqual(codec.output(200, 14), 100)
        self.assertIsNone(codec.output(201, 0))
        self.assertFalse(codec.fresh({"main_age_ms": -1}, "main"))

    def test_wrong_image_size(self):
        with self.assertRaises(ValueError):
            codec.display_png(bytes(5119))


if __name__ == "__main__":
    unittest.main()
