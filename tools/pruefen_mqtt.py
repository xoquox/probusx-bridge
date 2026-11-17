"""Prüft MQTT-Hilfen und optional die echte Home-Assistant-Laufzeit mit lokalem Mosquitto."""

import argparse
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from mqtt_fixture import build_fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--ha", action="store_true", help="Zusätzlich Home Assistant mit lokalem Testbroker prüfen"
    )
    args = parser.parse_args()
    subprocess.run([sys.executable, str(ROOT / "tests/test_mqtt.py")], check=True)
    if not args.ha:
        return
    with tempfile.TemporaryDirectory() as directory:
        directory = Path(directory)
        build_fixture(directory)
        config = directory / "mosquitto.conf"
        config.write_text("listener 18883 127.0.0.1\nallow_anonymous true\npersistence false\n")
        with (directory / "broker.log").open("w") as log:
            broker = subprocess.Popen(
                ["mosquitto", "-c", str(config)], stdout=log, stderr=subprocess.STDOUT
            )
            try:
                for _ in range(100):
                    if broker.poll() is not None:
                        raise RuntimeError(
                            "Testbroker beendet. Prüfen, ob Port 18883 schon verwendet wird."
                        )
                    try:
                        with socket.create_connection(("127.0.0.1", 18883), timeout=0.1):
                            break
                    except OSError:
                        time.sleep(0.1)
                else:
                    raise RuntimeError("Testbroker antwortet nicht")
                subprocess.run(
                    [sys.executable, str(ROOT / "tests/test_mqtt_ha_runtime.py"), str(directory)],
                    check=True,
                )
            finally:
                broker.terminate()
                broker.wait(timeout=5)


if __name__ == "__main__":
    main()
