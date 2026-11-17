#include "restart_policy.h"
#include "http_idle_policy.h"
#include <cassert>
#include <iostream>

struct Backend {
  bool ready_value = true, busy = false, pending_value = false, timer_ok = true;
  unsigned timers = 0, releases = 0;
  bool ready() { return ready_value; }
  bool take() {
    if (busy) return false;
    busy = true;
    return true;
  }
  void pending(bool value) { pending_value = value; }
  bool schedule() { ++timers; return timer_ok; }
  void release() { ++releases; busy = false; }
};
int main() {
  using restart_policy::request;
  using restart_policy::Result;
  assert(!http_connections::idle_expired(30099, 100, false));
  assert(http_connections::idle_expired(30100, 100, false));
  assert(!http_connections::idle_expired(3600000, 100, true));
  assert(http_connections::idle_expired(3600000, 100, false));
  assert(!http_connections::idle_expired(99, 100, false));
  Backend startup;
  startup.ready_value = false;
  assert(request(startup) == Result::BUSY);
  assert(!startup.busy && !startup.pending_value && startup.timers == 0);
  Backend capture;
  capture.busy = true;
  assert(request(capture) == Result::BUSY);
  assert(capture.busy && capture.timers == 0 && capture.releases == 0);
  Backend failure;
  failure.timer_ok = false;
  assert(request(failure) == Result::TIMER_FAILED);
  assert(!failure.busy && !failure.pending_value && failure.releases == 1);
  failure.timer_ok = true;
  assert(request(failure) == Result::ACCEPTED);
  assert(failure.busy && failure.pending_value && failure.timers == 2);
  assert(request(failure) == Result::BUSY);
  assert(failure.timers == 2 && failure.releases == 1);
  std::cout << "Bestanden: Startprüfung und laufender Vorgang sperren den Neustart. "
               "Timerfehler gibt die Reservierung frei. Annahme sperrt doppelte Aufträge.\n";
}
