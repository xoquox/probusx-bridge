"""Prüft die echte Sitzungsbereinigung mit einer kleinen SDK-Testumgebung."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUB = r'''
#pragma once
#include <cstddef>
#include <cstdint>
#include <cassert>
using esp_err_t = int;
using httpd_handle_t = void *;
constexpr int ESP_OK = 0, ESP_ERR_NO_MEM = -1;
struct httpd_req_t { void *sess_ctx; };
struct httpd_config_t {
  unsigned max_open_sockets = 0;
  esp_err_t (*open_fn)(httpd_handle_t, int) = nullptr;
};
void httpd_sess_set_ctx(httpd_handle_t, int, void *, void (*)(void *));
void *httpd_sess_get_ctx(httpd_handle_t, int);
esp_err_t httpd_get_client_list(httpd_handle_t, size_t *, int *);
esp_err_t httpd_queue_work(httpd_handle_t, void (*)(void *), void *);
int64_t esp_timer_get_time();
void vTaskDelay(unsigned);
int xTaskCreatePinnedToCore(void (*)(void *), const char *, unsigned, void *, unsigned,
                           void *, unsigned);
int shutdown(int, int);
constexpr int SHUT_RDWR = 2;
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(n) (n)
#define configASSERT(n) assert(n)
#define pdPASS 1
'''
TEST = r'''
#include "sdk_stub.h"
#include <map>
#include <set>
#include <iostream>
struct Context { void *value; void (*free)(void *); };
static std::map<int, Context> sessions;
static std::set<int> closed;
static int64_t clock_us = 0;
static bool busy = false;
static bool queue_fails = false;
void httpd_sess_set_ctx(httpd_handle_t, int fd, void *ctx, void (*free)(void *)) {
  assert(!sessions.count(fd)); sessions[fd] = {ctx, free};
}
void *httpd_sess_get_ctx(httpd_handle_t, int fd) { return sessions.at(fd).value; }
esp_err_t httpd_get_client_list(httpd_handle_t, size_t *count, int *fds) {
  assert(*count >= sessions.size()); *count = 0;
  for (auto [fd, _] : sessions) fds[(*count)++] = fd;
  return ESP_OK;
}
esp_err_t httpd_queue_work(httpd_handle_t, void (*fn)(void *), void *arg) {
  if (queue_fails) return -1;
  fn(arg);
  return ESP_OK;
}
int64_t esp_timer_get_time() { return clock_us; }
void vTaskDelay(unsigned) {}
int xTaskCreatePinnedToCore(void (*)(void *), const char *, unsigned, void *, unsigned,
                           void *, unsigned) { return pdPASS; }
int shutdown(int fd, int how) { assert(how == SHUT_RDWR); closed.insert(fd); return 0; }
bool operation_busy() { return busy; }
#include "http_connections.cpp"
static void remove(int fd) {
  auto ctx = sessions.at(fd); sessions.erase(fd); ctx.free(ctx.value);
}
int main() {
  httpd_config_t config;
  http_connections::configure(config);
  assert(config.max_open_sockets == 6 && config.open_fn);
  http_connections::start(nullptr);
  assert(config.open_fn(nullptr, 10) == ESP_OK);
  assert(config.open_fn(nullptr, 11) == ESP_OK);
  clock_us = 29000LL * 1000;
  http_connections::clean_idle(nullptr);
  assert(closed.empty());
  httpd_req_t req{sessions.at(10).value};
  http_connections::touch(&req);
  clock_us = 31000LL * 1000;
  http_connections::clean_idle(nullptr);
  assert(closed == std::set<int>{11});
  remove(11); closed.clear();
  // Auch sehr alte Sitzungen während eines asynchronen Vorgangs nicht anfassen.
  busy = true; clock_us = 3600000LL * 1000;
  http_connections::clean_idle(nullptr);
  assert(closed.empty());
  busy = false;
  // Ein asynchroner Verlaufsabruf besitzt nur seine Sitzung und blockiert keine Steuerung.
  http_connections::protect_async(&req, true);
  http_connections::clean_idle(nullptr);
  assert(closed.empty());
  http_connections::protect_async(&req, false);
  http_connections::clean_idle(nullptr);
  assert(closed == std::set<int>{10});
  remove(10); closed.clear();
  // Eine neue Sitzung mit derselben FD erhält einen neuen Zeitstempel.
  assert(config.open_fn(nullptr, 10) == ESP_OK);
  http_connections::clean_idle(nullptr);
  assert(closed.empty());
  remove(10);
  std::cout << "Bestanden: freie HTTP-Sitzungen bereinigt, aktive Sitzung behalten, "
               "asynchroner Vorgang geschützt und wiederverwendete FD neu erfasst.\n";
}
'''

class HttpConnectionsTest(unittest.TestCase):
    def test_real_cleanup_callbacks(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            (folder / "sdk_stub.h").write_text(STUB)
            for name in ["esp_http_server.h", "esp_log.h", "esp_timer.h", "lwip/sockets.h",
                         "freertos/FreeRTOS.h", "freertos/task.h"]:
                p = folder / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text('#include "sdk_stub.h"\n')
            (folder / "test.cpp").write_text(TEST)
            binary = folder / "test"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(folder),
                            "-I", str(ROOT / "src"), str(folder / "test.cpp"), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)
