"""Baut die Prüfdateien aus dem tatsächlichen C++-Discovery- und PNG-Code."""

import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def build_fixture(destination):
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    executable = destination / "mqtt_test"
    flags = []
    objects = []
    if sdk := os.environ.get("PROBUSX_IDF_PATH"):
        cjson = Path(sdk) / "components/json/cJSON"
        obj = destination / "cjson.o"
        subprocess.run(
            ["gcc", "-c", str(cjson / "cJSON.c"), "-I", str(cjson), "-o", str(obj)], check=True
        )
        flags = ["-I", str(cjson)]
        objects = [str(obj)]
    else:
        flags = shlex.split(
            subprocess.check_output(["pkg-config", "--cflags", "libcjson"], text=True)
        )
        objects = shlex.split(
            subprocess.check_output(["pkg-config", "--libs", "libcjson"], text=True)
        )
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "firmware/src"),
            *flags,
            str(ROOT / "tests/mqtt_test.cpp"),
            *objects,
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable), str(ROOT / "tests/display.raw"), str(destination)], check=True)
