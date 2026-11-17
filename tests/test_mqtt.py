"""Prüft PNG-Pixel und Discovery aus dem C++-Produktionscode."""

import json
from pathlib import Path
import tempfile
import unittest
from PIL import Image
from mqtt_fixture import build_fixture

ROOT = Path(__file__).resolve().parents[1]


class MqttTests(unittest.TestCase):
    def test_production_helpers_and_png(self):
        with tempfile.TemporaryDirectory() as temp:
            build_fixture(temp)
            raw = (ROOT / "tests/display.raw").read_bytes()
            image = Image.open(Path(temp) / "display.png").convert("L")
            self.assertEqual(image.size, (160, 128))
            for y in range(128):
                for x in range(160):
                    self.assertEqual(
                        image.getpixel((x, y)),
                        255 - 85 * ((raw[(159 - x) * 32 + (y >> 2)] >> (2 * (y % 4))) & 3),
                    )
            records = json.loads((Path(temp) / "discovery.json").read_text())
            self.assertEqual(len(records), 61)
            self.assertEqual(len({r["payload"]["unique_id"] for r in records}), 61)
            camera = next(r["payload"] for r in records if "/camera/" in r["topic"])
            self.assertEqual(camera["topic"], "probusx/aabbccddeeff/display")
            self.assertEqual(camera["encoding"], "utf-8")
            self.assertNotIn("image_topic", camera)
            for r in records:
                self.assertEqual(r["payload"]["availability_mode"], "all")
                self.assertTrue(
                    r["payload"]["default_entity_id"].startswith(
                        r["topic"].split("/")[1] + ".probusx_"
                    )
                )


if __name__ == "__main__":
    unittest.main(verbosity=2)
