import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest
from test_capture import session
import capture


class WebLogicTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.tmp.name) / "web_logic"
        root = Path(__file__).resolve().parents[1]
        subprocess.run(
            [
                "g++",
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(root / "src"),
                str(root / "tests/test_web_logic.cpp"),
                "-o",
                str(cls.binary),
            ],
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_case(self, name):
        subprocess.run([str(self.binary), name], check=True)

    def test_form_special_characters(self):
        self.run_case("forms")

    def test_form_ambiguity_and_invalid_bytes(self):
        self.run_case("invalid_forms")

    def test_wifi_credential_boundaries(self):
        self.run_case("credentials")

    def test_capture_duration_limits(self):
        self.run_case("duration")

    def test_fallback_and_busy_radio(self):
        self.run_case("fallback")

    def test_chunked_http_download_decodes_as_existing_pbxs(self):
        data = bytes(range(256)) * 4
        wire = session(data)

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *args):
                pass

            def do_POST(self):
                self.rfile.read(int(self.headers["Content-Length"]))
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Disposition", 'attachment; filename="capture.pbxs"')
                self.send_header("Transfer-Encoding", "chunked")
                self.end_headers()
                for i in range(0, len(wire), 17):
                    block = wire[i : i + 17]
                    self.wfile.write(f"{len(block):x}\r\n".encode() + block + b"\r\n")
                self.wfile.write(b"0\r\n\r\n")

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            connection = http.client.HTTPConnection(*server.server_address, timeout=3)
            connection.request("POST", "/capture", "seconds=1&token=test")
            response = connection.getresponse()
            saved = response.read()
            connection.close()
            self.assertEqual(saved, wire)
            binary = io.BytesIO()
            decoder = capture.Decoder(binary, io.StringIO())
            decoder.consume(capture.FileSource(io.BytesIO(saved)))
            self.assertEqual(binary.getvalue(), data)
            self.assertEqual(decoder.summary()["quality"], "no_reported_errors")
        finally:
            server.shutdown()
            server.server_close()
            thread.join()
