#include "history.h"
#include "settings.h"
#include "live.h"
#include "capture_api.h"
#include "ota.h"
#include "esp_app_desc.h"
#include "wire.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

static const char *TAG = "probusx";
static constexpr uart_port_t UART = UART_NUM_1;
static constexpr size_t PAYLOAD_MAX = 1024;
static constexpr size_t RING_BYTES = 2 * 1024 * 1024;
static constexpr size_t UART_BUFFER_BYTES = 32768;
static constexpr unsigned EVENT_QUEUE_LEN = 1024;
static constexpr EventBits_t RX_IDLE = 1, RX_READY = 2;
static RingbufHandle_t ring;
static StaticRingbuffer_t ring_control;
static QueueHandle_t uart_events, requests;
static EventGroupHandle_t state;
static std::atomic<bool> cancelled{false};
static uint32_t configured_baud;
static uint8_t mac[6];
static SemaphoreHandle_t operation;
static std::atomic<bool> busy{false}, active{false}, stop_requested{false};
static portMUX_TYPE status_mux = portMUX_INITIALIZER_UNLOCKED;
static char latest_stats[1024] = "{}";
static char last_result[32] = "none";

// Reserviert genau einen Vorgang. Ein bereits laufender Vorgang wird nicht unterbrochen.
bool operation_take() {
  if (xSemaphoreTake(operation, 0) != pdTRUE)
    return false;
  busy = true;
  return true;
}
void operation_give() {
  busy = false;
  xSemaphoreGive(operation);
}
bool operation_busy() {
  return busy;
}
bool capture_active() {
  return active;
}
void capture_stop() {
  if (active)
    stop_requested = true;
}
void capture_transport_failed() {
  portENTER_CRITICAL(&status_mux);
  strcpy(last_result, "interrupted");
  portEXIT_CRITICAL(&status_mux);
}
void capture_status(char *out, size_t size) {
  char stats[1024], result[32];
  portENTER_CRITICAL(&status_mux);
  memcpy(stats, latest_stats, sizeof(stats));
  memcpy(result, last_result, sizeof(result));
  portEXIT_CRITICAL(&status_mux);
  snprintf(out, size, "{\"active\":%s,\"busy\":%s,\"result\":\"%s\",\"statistics\":%s}",
           active ? "true" : "false", busy ? "true" : "false", result, stats);
}

struct Request {
  uint32_t duration_ms;
  uint32_t id;
};
struct Counters {
  uint64_t rx_bytes = 0;
  uint64_t data_records = 0;
  uint64_t queue_dropped_bytes = 0;
  uint32_t queue_dropped_records = 0;
  uint32_t fifo_overflows = 0;
  uint32_t uart_buffer_full = 0;
  uint32_t frame_errors = 0;
  uint32_t parity_errors = 0;
  uint32_t breaks = 0;
  uint32_t event_queue_pressure = 0;
};
static Counters counts; // Nur der Empfangstask greift zu und erstellt daraus die Statistik.
static uint32_t sequence;

// PBXS-Transportdatensatz bilden und im PSRAM-Ringpuffer einreihen.
// Bei Platzmangel meldet der Aufrufer den Verlust in seinen Zählern.
static bool emit(uint8_t type, const uint8_t *payload, size_t size, uint64_t stamp, uint64_t offset,
                 TickType_t wait = 0) {
  uint8_t record[wire::HEADER_SIZE + PAYLOAD_MAX];
  configASSERT(size <= PAYLOAD_MAX);
  wire::header(record, type, sequence++, stamp, offset, payload, size);
  if (size)
    memcpy(record + wire::HEADER_SIZE, payload, size);
  return xRingbufferSend(ring, record, wire::HEADER_SIZE + size, wait) == pdTRUE;
}

static size_t statistics(char *out, size_t size, uint64_t started, uint64_t now) {
  const int n = snprintf(
      out, size,
      "{\"rx_bytes\":%" PRIu64 ",\"data_records\":%" PRIu64 ",\"queue_dropped_bytes\":%" PRIu64
      ",\"queue_dropped_records\":%" PRIu32 ",\"fifo_overflows\":%" PRIu32
      ",\"uart_buffer_full\":%" PRIu32 ",\"frame_errors\":%" PRIu32 ",\"parity_errors\":%" PRIu32
      ",\"breaks\":%" PRIu32 ",\"event_queue_pressure\":%" PRIu32 ",\"elapsed_us\":%" PRIu64
      ",\"baud_configured\":%" PRIu32 ",\"stopped_by_user\":%s}",
      counts.rx_bytes, counts.data_records, counts.queue_dropped_bytes,
      counts.queue_dropped_records, counts.fifo_overflows, counts.uart_buffer_full,
      counts.frame_errors, counts.parity_errors, counts.breaks, counts.event_queue_pressure,
      now - started, configured_baud, stop_requested ? "true" : "false");
  configASSERT(n > 0 && static_cast<size_t>(n) < size);
  portENTER_CRITICAL(&status_mux);
  memcpy(latest_stats, out, n + 1);
  portEXIT_CRITICAL(&status_mux);
  return n;
}

static void collect_events() {
  size_t buffered = 0;
  uart_get_buffered_data_len(UART, &buffered);
  live_uart_buffer(buffered);
  if (uxQueueMessagesWaiting(uart_events) >= EVENT_QUEUE_LEN - 8) {
    ++counts.event_queue_pressure;
    live_uart_fault(5);
  }
  uart_event_t event;
  // UART-Empfangsblöcke sind keine ProBusX-Telegrammgrenzen.
  while (xQueueReceive(uart_events, &event, 0) == pdTRUE) {
    switch (event.type) {
    case UART_FIFO_OVF:
      ++counts.fifo_overflows;
      live_uart_fault(0);
      break;
    case UART_BUFFER_FULL:
      ++counts.uart_buffer_full;
      live_uart_fault(1);
      break;
    case UART_FRAME_ERR:
      ++counts.frame_errors;
      live_uart_fault(2);
      break;
    case UART_PARITY_ERR:
      ++counts.parity_errors;
      live_uart_fault(3);
      break;
    case UART_BREAK:
      ++counts.breaks;
      live_uart_fault(4);
      break;
    default:
      break;
    }
  }
  // Verfügbare Bytes erhalten. Bei Aufnahmefehlern den Empfangspuffer nicht leeren.
}

// Einziger Leser der UART. Dieser Task speist den Parser auch ohne Rohaufnahme.
// Während einer Aufnahme übergibt er dieselben Bytes zusätzlich an den PSRAM-Ringpuffer.
static void rx_task(void *) {
  uart_config_t config{};
  config.baud_rate = BUS_BAUD;
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  ESP_ERROR_CHECK(uart_param_config(UART, &config));
  // TX ist verbunden. Den Bustreiber nur für eine vorgemerkte Tastenantwort aktivieren.
  ESP_ERROR_CHECK(
      uart_set_pin(UART, BUS_TX_GPIO, BUS_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(
      uart_driver_install(UART, UART_BUFFER_BYTES, 0, EVENT_QUEUE_LEN, &uart_events, 0));
  ESP_ERROR_CHECK(uart_set_mode(UART, UART_MODE_UART));
  ESP_ERROR_CHECK(uart_set_rx_full_threshold(UART, 64));
  ESP_ERROR_CHECK(uart_set_rx_timeout(UART, 2));
  ESP_ERROR_CHECK(uart_get_baudrate(UART, &configured_baud));
  ESP_LOGI(TAG, "Empfang GPIO%d, Freigabe GPIO%d AUS, UART %" PRIu32 " 8N1", BUS_RX_GPIO,
           BUS_ENABLE_GPIO, configured_baud);
  xEventGroupSetBits(state, RX_READY | RX_IDLE);
  Request request;
  uint8_t data[PAYLOAD_MAX];
  char json[PAYLOAD_MAX];
  while (true) {
    if (xQueueReceive(requests, &request, 0) != pdTRUE) {
      collect_events();
      size_t available = 0;
      uart_get_buffered_data_len(UART, &available);
      if (available) {
        int n = uart_read_bytes(UART, data, std::min(available, sizeof(data)), 0);
        if (n > 0)
          live_feed(data, n, true);
      } else
        vTaskDelay(1);
      live_tick();
      continue;
    }
    live_reset();
    xEventGroupClearBits(state, RX_IDLE);
    // Vor START empfangene Daten gehören nicht zur neuen Aufnahme.
    ESP_ERROR_CHECK(uart_flush_input(UART));
    xQueueReset(uart_events);
    counts = {};
    sequence = 0;
    const uint64_t started = esp_timer_get_time();
    const uint64_t deadline = started + uint64_t(request.duration_ms) * 1000;
    const int n =
        snprintf(json, sizeof(json),
                 "{\"firmware\":\"probusx-bridge-%s\",\"board\":\"waveshare-esp32-s3-rs485-can\","
                 "\"session_id\":%" PRIu32 ",\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                 "\"baud_requested\":%d,\"baud_configured\":%" PRIu32 ",\"format\":\"8N1\","
                 "\"rx_gpio\":%d,\"enable_gpio\":%d,\"sender_enabled\":false,"
                 "\"duration_ms\":%" PRIu32 ",\"uart_buffer_bytes\":%u,\"capture_buffer_bytes\":%u,"
                 "\"timestamp_kind\":\"uart_read_completed_us_since_boot\"}",
                 esp_app_get_description()->version, request.id, mac[0], mac[1], mac[2], mac[3],
                 mac[4], mac[5], BUS_BAUD, configured_baud, BUS_RX_GPIO, BUS_ENABLE_GPIO,
                 request.duration_ms, unsigned(UART_BUFFER_BYTES), unsigned(RING_BYTES));
    configASSERT(n > 0 && n < int(sizeof(json)));
    if (!emit(wire::HELLO, reinterpret_cast<uint8_t *>(json), n, started, 0))
      cancelled = true;
    uint64_t next_status = started + 1000000;
    while (!cancelled && !stop_requested && uint64_t(esp_timer_get_time()) < deadline) {
      live_tick();
      collect_events();
      // Empfangstask auf Kern 1. WLAN und Netzwerkübertragung dürfen ihn nicht blockieren.
      const int len = uart_read_bytes(UART, data, sizeof(data), pdMS_TO_TICKS(5));
      const uint64_t now = esp_timer_get_time();
      if (len < 0) {
        cancelled = true;
        break;
      }
      if (len > 0) {
        const uint64_t offset = counts.rx_bytes;
        counts.rx_bytes += len;
        live_feed(data, len, false);
        ++counts.data_records;
        if (!emit(wire::DATA, data, len, now, offset)) {
          counts.queue_dropped_bytes += len;
          ++counts.queue_dropped_records;
        }
      }
      if (now >= next_status) {
        collect_events();
        const size_t size = statistics(json, sizeof(json), started, now);
        if (!emit(wire::STATUS, reinterpret_cast<uint8_t *>(json), size, now, counts.rx_bytes))
          ++counts.queue_dropped_records;
        next_status = now + 1000000;
      }
    }
    collect_events();
    const uint64_t ended = esp_timer_get_time();
    const size_t size = statistics(json, sizeof(json), started, ended);
    if (!cancelled) {
      // END erst nach den gepufferten Daten einreihen. Auf freien Platz warten.
      // Bei Wiederholungen dieselbe Sequenznummer verwenden. Sonst entstehen scheinbare Lücken.
      while (!cancelled) {
        if (emit(wire::END, reinterpret_cast<uint8_t *>(json), size, ended, counts.rx_bytes,
                 pdMS_TO_TICKS(100)))
          break;
        --sequence;
      }
    }
    ESP_LOGI(TAG,
             "Aufnahme beendet: %" PRIu64 " Byte, verworfen %" PRIu64 ", UART-FIFO-Fehler %" PRIu32
             ", Rahmenfehler %" PRIu32,
             counts.rx_bytes, counts.queue_dropped_bytes, counts.fifo_overflows,
             counts.frame_errors);
    xEventGroupSetBits(state, RX_IDLE);
  }
}

static bool send_all(int fd, const uint8_t *p, size_t len) {
  while (len) {
    const int sent = send(fd, p, len, 0);
    if (sent < 0 && errno == EINTR)
      continue;
    if (sent <= 0)
      return false;
    p += sent;
    len -= sent;
  }
  return true;
}

// Erlaubt nur START mit einer Dauer von 1000 bis 3600000 Millisekunden.
static bool read_request(int fd, Request &request) {
  char line[40];
  size_t pos = 0;
  const uint64_t deadline = esp_timer_get_time() + 5000000;
  while (pos + 1 < sizeof(line) && uint64_t(esp_timer_get_time()) < deadline) {
    char c;
    const int n = recv(fd, &c, 1, 0);
    if (n <= 0)
      return false;
    if (c == '\n') {
      line[pos] = '\0';
      if (strncmp(line, "START ", 6) != 0 || pos <= 6)
        return false;
      for (size_t i = 6; i < pos; ++i)
        if (line[i] < '0' || line[i] > '9')
          return false;
      errno = 0;
      const unsigned long ms = strtoul(line + 6, nullptr, 10);
      if (errno || ms < 1000 || ms > 3600000)
        return false;
      request = {static_cast<uint32_t>(ms), esp_random()};
      return true;
    }
    line[pos++] = c;
  }
  return false;
}

// Verbindet Aufnahmeauftrag und Datenempfänger. Der Aufrufer hält die Vorgangsreservierung.
// Ein langsamer Download darf den UART-Empfang nicht anhalten.
bool capture_run(uint32_t duration_ms, CaptureSink sink, void *context) {
  // Den Rohpuffer nur für diese Aufnahme reservieren. Bei Bedarf alte Verlaufsblöcke abgeben.
  void *storage = history_capture_allocate(RING_BYTES);
  if (!storage) {
    portENTER_CRITICAL(&status_mux);
    strcpy(last_result, "memory_error");
    portEXIT_CRITICAL(&status_mux);
    return false;
  }
  ring = xRingbufferCreateStatic(RING_BYTES, RINGBUF_TYPE_NOSPLIT, static_cast<uint8_t *>(storage),
                                 &ring_control);
  if (!ring) {
    history_capture_release(storage);
    return false;
  }
  wifi_prepare_capture();
  cancelled = false;
  stop_requested = false;
  portENTER_CRITICAL(&status_mux);
  strcpy(latest_stats, "{}");
  strcpy(last_result, "running");
  portEXIT_CRITICAL(&status_mux);
  active = true;
  xEventGroupClearBits(state, RX_IDLE);
  Request request{duration_ms, esp_random()};
  xQueueSend(requests, &request, portMAX_DELAY);
  bool complete = false;
  while (true) {
    size_t len = 0;
    auto *packet = static_cast<uint8_t *>(xRingbufferReceive(ring, &len, pdMS_TO_TICKS(200)));
    if (packet) {
      const bool end = packet[5] == wire::END;
      const bool ok = sink(context, packet, len);
      vRingbufferReturnItem(ring, packet);
      if (!ok)
        break;
      if (end) {
        complete = true;
        break;
      }
    } else if (xEventGroupGetBits(state) & RX_IDLE)
      break;
  }
  cancelled = true;
  xEventGroupWaitBits(state, RX_IDLE, pdFALSE, pdTRUE, portMAX_DELAY);
  size_t size;
  while (void *item = xRingbufferReceive(ring, &size, 0))
    vRingbufferReturnItem(ring, item);
  portENTER_CRITICAL(&status_mux);
  strcpy(last_result, complete ? (stop_requested ? "stopped" : "complete") : "interrupted");
  portEXIT_CRITICAL(&status_mux);
  active = false;
  // RX_IDLE ist gesetzt und alle entliehenen Datensätze sind zurückgegeben.
  vRingbufferDelete(ring);
  ring = nullptr;
  history_capture_release(storage);
  return complete;
}
static bool tcp_sink(void *context, const uint8_t *data, size_t size) {
  return send_all(*static_cast<int *>(context), data, size);
}

static void network_task(void *) {
  const int server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  configASSERT(server >= 0);
  int reuse = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(CAPTURE_TCP_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  configASSERT(bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0);
  configASSERT(listen(server, 1) == 0);
  ESP_LOGI(TAG, "Aufnahme auf TCP-Port %d bereit", CAPTURE_TCP_PORT);
  while (true) {
    int fd = accept(server, nullptr, nullptr);
    if (fd < 0) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    timeval timeout{1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    timeout.tv_sec = 5;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    Request request{};
    if (!read_request(fd, request)) {
      close(fd);
      continue;
    }
    if (!operation_take()) {
      close(fd);
      continue;
    }
    capture_run(request.duration_ms, tcp_sink, &fd);
    shutdown(fd, SHUT_RDWR);
    close(fd);
    operation_give();
  }
}

extern "C" void app_main() {
  // Zu Beginn den RS485-Treiber ausschalten. Das gilt erst ab dem Anwendungsstart.
  // Pegel während Einschalten, Rücksetzen und Bootloader sind dadurch nicht festgelegt.
  ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(BUS_ENABLE_GPIO), 0));
  gpio_config_t pins{};
  pins.pin_bit_mask = 1ULL << BUS_ENABLE_GPIO;
  pins.mode = GPIO_MODE_OUTPUT;
  pins.pull_down_en = GPIO_PULLDOWN_ENABLE;
  ESP_ERROR_CHECK(gpio_config(&pins));
  ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(BUS_TX_GPIO), 1));
  pins.pin_bit_mask = 1ULL << BUS_TX_GPIO;
  pins.pull_down_en = GPIO_PULLDOWN_DISABLE;
  pins.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK(gpio_config(&pins));

  ota_boot_start();
  requests = xQueueCreate(1, sizeof(Request));
  state = xEventGroupCreate();
  operation = xSemaphoreCreateBinary();
  configASSERT(requests && state && operation);
  xSemaphoreGive(operation);
  ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
  // Einstellungen vor dem Empfangstask laden. NVS hier nicht löschen.
  ESP_ERROR_CHECK(nvs_flash_init());
  live_init();
  history_init();
  configASSERT(xTaskCreatePinnedToCore(rx_task, "probusx_rx", 8192, nullptr, 18, nullptr, 1) ==
               pdPASS);
  xEventGroupWaitBits(state, RX_READY, pdFALSE, pdTRUE, portMAX_DELAY);

  esp_err_t err = nvs_flash_init();
  // Gespeicherte WLAN-Daten bei einem Firmwarewechsel nicht automatisch löschen.
  ESP_ERROR_CHECK(err);
  web_wifi_start();
  configASSERT(xTaskCreatePinnedToCore(network_task, "probusx_tcp", 6144, nullptr, 5, nullptr, 0) ==
               pdPASS);
  ota_boot_ready();
}
