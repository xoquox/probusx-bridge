#pragma once

namespace restart_policy {
enum class Result { ACCEPTED, BUSY, TIMER_FAILED };
// Die gleiche Reservierung wie Aufnahme, Update und WLAN verwenden. Bei einem
// Planungsfehler freigeben. Nach Annahme bis zum Neustart reserviert lassen.
template <class Backend> Result request(Backend &backend) {
  if (!backend.ready() || !backend.take())
    return Result::BUSY;
  backend.pending(true);
  if (!backend.schedule()) {
    backend.pending(false);
    backend.release();
    return Result::TIMER_FAILED;
  }
  return Result::ACCEPTED;
}
} // Namensraum restart_policy
