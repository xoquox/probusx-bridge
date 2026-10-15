"""Erstellt das USB-Abbild und das OTA-Abbild aus demselben PlatformIO-Build."""

import argparse, hashlib, os, pathlib, shutil, subprocess, sys, tempfile

root = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument(
    "--python", default=sys.executable, help="Python-Programm, das esptool ausführen kann"
)
p.add_argument("--esptool", help="Optionaler Pfad zu tool-esptoolpy/esptool.py von PlatformIO")
a = p.parse_args()
build = root / ".pio/build/waveshare"
out = root / "abbilder"
out.mkdir(exist_ok=True)
config = root / "sdkconfig.waveshare"
if not config.exists():
    raise SystemExit("Build-Konfiguration fehlt. Zuerst pio run ausführen.")
if "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y" not in config.read_text():
    raise SystemExit("Build ohne automatische Rückkehr zur alten Firmware abgelehnt.")
cmd = [a.python, a.esptool] if a.esptool else [a.python, "-m", "esptool"]
# PlatformIO legt esptool im Paketverzeichnis ab. Ein Python-Import ist dort nicht immer möglich.
if not a.esptool:
    candidate = (
        pathlib.Path(os.environ.get("PLATFORMIO_CORE_DIR", pathlib.Path.home() / ".platformio"))
        / "packages/tool-esptoolpy/esptool.py"
    )
    if candidate.exists():
        cmd = [a.python, str(candidate)]
with tempfile.TemporaryDirectory() as d:
    blank = pathlib.Path(d) / "otadata.bin"
    blank.write_bytes(b"\xff" * 8192)
    subprocess.run(
        cmd
        + [
            "--chip",
            "esp32s3",
            "merge_bin",
            "-o",
            str(out / "probusx-bridge-waveshare-usb.bin"),
            "--flash_mode",
            "dio",
            "--flash_freq",
            "80m",
            "--flash_size",
            "16MB",
            "0x0",
            str(build / "bootloader.bin"),
            "0x8000",
            str(build / "partitions.bin"),
            "0x10000",
            str(build / "firmware.bin"),
            "0x610000",
            str(blank),
        ],
        check=True,
    )
shutil.copyfile(build / "firmware.bin", out / "probusx-bridge-waveshare-ota.bin")
shutil.copyfile(config, out / "sdkconfig.used")
# Prüfsummen erst nach vollständiger Bilderstellung schreiben.
names = ["probusx-bridge-waveshare-ota.bin", "probusx-bridge-waveshare-usb.bin", "sdkconfig.used"]
(out / "SHA256SUMS").write_text(
    "".join(f"{hashlib.sha256((out / name).read_bytes()).hexdigest()}  {name}\n" for name in names),
    encoding="utf-8",
)
print(
    "USB-Abbild ab 0x0 schreiben. OTA-Abbild über die Weboberfläche installieren. Siehe README.md."
)
