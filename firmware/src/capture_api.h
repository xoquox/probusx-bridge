#pragma once
#include <cstddef>
#include <cstdint>
// Eine binäre Semaphore übergibt die Vorgangsreservierung an den Hintergrundtask.
// Aufnahme, WLAN-Aktion, Update und Tastenvorgang verwenden dieselbe Reservierung.
bool operation_take();
void operation_give();
bool operation_busy();
bool capture_active();
void capture_stop();
void capture_transport_failed();
using CaptureSink = bool (*)(void *, const uint8_t *, size_t);
// Der Aufrufer hält die Vorgangsreservierung und muss sie anschließend freigeben.
bool capture_run(uint32_t duration_ms, CaptureSink sink, void *context);
void capture_status(char *out, size_t size);
void wifi_prepare_capture();
void web_wifi_start();
