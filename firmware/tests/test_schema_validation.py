"""Prüft die produktive JSON-Prüfung mit cJSON aus derselben ESP-IDF-Fassung."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

class SchemaValidationTest(unittest.TestCase):
    def test_validation_with_sdk_cjson(self):
        sdk = os.environ.get("PROBUSX_IDF_PATH")
        if not sdk:
            self.skipTest("PROBUSX_IDF_PATH für die zusätzliche SDK-Prüfung setzen")
        root = Path(__file__).resolve().parents[1]
        cjson = Path(sdk) / "components/json/cJSON"
        with tempfile.TemporaryDirectory() as folder:
            obj, binary = Path(folder)/"cjson.o", Path(folder)/"test"
            subprocess.run(["gcc", "-c", str(cjson/"cJSON.c"), "-o", str(obj)], check=True)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root/"src"),
                            "-I", str(cjson), str(root/"tests/test_schema_validation.cpp"), str(obj),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
