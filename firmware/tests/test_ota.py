import pathlib, subprocess, tempfile, unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class OtaTests(unittest.TestCase):
    def test_actual_transfer_with_flash_failures(self):
        with tempfile.TemporaryDirectory() as d:
            exe = str(pathlib.Path(d) / "ota-test")
            subprocess.run(
                [
                    "g++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I" + str(ROOT / "src"),
                    str(ROOT / "tests/test_ota_transfer.cpp"),
                    "-o",
                    exe,
                ],
                check=True,
            )
            subprocess.run([exe], check=True)


if __name__ == "__main__":
    unittest.main()
