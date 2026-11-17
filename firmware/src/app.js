'use strict';
const $ = (id) => document.getElementById(id);
// Nur echte Bedienung verlängert die Anzeigezeit. Abrufe zählen nicht als Bedienung.
const idleLimitMs = 60 * 60 * 1000;
let lastInteraction = Date.now(),
  browserPaused = false;
let restartPhase = false,
  restartToken = '',
  restartUntil = 0;
const automaticRequests = new Set();
function automaticAllowed() {
  if (
    !browserPaused &&
    Date.now() - lastInteraction >= idleLimitMs &&
    updatePhase === 'idle' &&
    !operationPending &&
    !restartPhase &&
    !state?.capture?.busy &&
    Date.now() >= pendingUntil
  ) {
    browserPaused = true;
    $('idle-banner').hidden = false;
    for (const controller of automaticRequests) controller.abort();
  }
  return !browserPaused;
}
function resumeFromInteraction(event) {
  if (!event.isTrusted) return;
  // Den Fortsetzen-Knopf nicht schon beim Anfahren mit der Maus verschieben.
  if (browserPaused && event.type === 'pointermove') return;
  lastInteraction = Date.now();
  browserPaused = false;
  $('idle-banner').hidden = true;
}
for (const name of ['pointerdown', 'click', 'pointermove', 'keydown', 'wheel', 'input'])
  document.addEventListener(name, resumeFromInteraction, { capture: true, passive: true });
// Laufende Abrufe beim Pausieren abbrechen. Auch das Lesen der Antwort gehört zum Abruf.
async function automaticRead(path, timeout, binary = false) {
  const controller = new AbortController();
  automaticRequests.add(controller);
  try {
    const response = await fetch(path, {
      cache: 'no-store',
      signal: AbortSignal.any([controller.signal, AbortSignal.timeout(timeout)]),
    });
    if (!response.ok) throw new Error('HTTP ' + response.status);
    return binary ? await response.arrayBuffer() : await response.json();
  } finally {
    automaticRequests.delete(controller);
  }
}
setInterval(automaticAllowed, 1000);
let updatePhase = 'idle',
  updateToken = '',
  updateUntil = 0,
  updateVersion = '';
let state = null,
  token = '',
  pendingUntil = 0,
  operationPending = false;
let networksSignature = '',
  formTouched = false,
  previousResult = 'none';
function notice(text) {
  $('notice').textContent = text;
  $('notice').hidden = !text;
}
function humanBytes(n) {
  return n >= 1e6
    ? (n / 1e6).toFixed(2) + ' MB'
    : n >= 1e3
      ? (n / 1e3).toFixed(1) + ' kB'
      : n + ' B';
}
function updateApMode() {
  const fixed = $('ap-mode').value === 'fixed';
  $('bssid').disabled = !fixed;
  $('bssid').required = fixed;
}
// Asynchroner Oberflächenaufruf. Die Anzeige meldet Fehler und blockiert den Empfang nicht.
async function post(path, values = {}) {
  const response = await fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ token, ...values }),
    cache: 'no-store',
    signal: AbortSignal.timeout(8000),
  });
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || 'Aktion fehlgeschlagen.');
  return result;
}
let mqttTouched = false;
function renderMqtt(m, locked) {
  $('mqtt-fields').disabled = locked || !m;
  if (!m) {
    $('mqtt-state').textContent = 'Firmware ohne MQTT';
    return;
  }
  $('mqtt-state').textContent = !m.enabled
    ? 'Ausgeschaltet'
    : m.connected
      ? 'Verbunden'
      : 'Verbindet …';
  $('mqtt-message').textContent = m.message;
  $('mqtt-detail').textContent =
    'Topic: ' +
    m.root +
    ' · Verbindungen: ' +
    m.connections +
    ' · Sendefehler: ' +
    m.publish_errors +
    ' · Abgelehnte Tasten: ' +
    m.rejected_keys;
  if (!mqttTouched) {
    $('mqtt-enabled').checked = m.enabled;
    $('mqtt-host').value = m.host;
    $('mqtt-port').value = m.port;
    $('mqtt-tls').checked = m.tls;
    $('mqtt-username').value = m.username;
  }
  $('mqtt-password-hint').textContent = m.password_set
    ? 'Passwort gespeichert. Leer lassen, um es zu behalten.'
    : 'Noch kein Passwort gespeichert.';
}
$('mqtt-form').addEventListener('input', () => {
  mqttTouched = true;
});
$('mqtt-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  if ($('mqtt-fields').disabled) return;
  const fields = {
    enabled: $('mqtt-enabled').checked ? '1' : '0',
    host: $('mqtt-host').value.trim(),
    port: $('mqtt-port').value,
    tls: $('mqtt-tls').checked ? '1' : '0',
    username: $('mqtt-username').value,
    password: $('mqtt-password').value,
    clear_password: $('mqtt-clear-password').checked ? '1' : '0',
  };
  if (fields.enabled === '1' && !fields.host) {
    $('mqtt-save-message').textContent = 'Bitte den Broker angeben.';
    return;
  }
  if (await action('/api/mqtt', fields)) {
    $('mqtt-password').value = '';
    $('mqtt-clear-password').checked = false;
    pendingUntil = Date.now() + 1500;
    mqttTouched = false;
    $('mqtt-save-message').textContent =
      'Einstellung angenommen. Speicher- und Verbindungsstatus oben prüfen.';
  }
});
function render(s) {
  state = s;
  token = s.token;
  $('capture-token').value = token;
  $('version').textContent = s.version;
  const c = s.capture,
    w = s.wifi,
    stats = c.statistics || {};
  const waiting = Date.now() < pendingUntil;
  const updating = updatePhase !== 'idle';
  const locked =
    c.busy ||
    waiting ||
    operationPending ||
    updating ||
    restartPhase ||
    !!s.restart_pending ||
    !!s.ota?.pending_verify;
  $('restart').disabled = locked;
  if (restartPhase && s.token !== restartToken && !s.ota?.pending_verify) {
    restartPhase = false;
    $('restart-message').textContent = 'Neustart abgeschlossen. Die Bridge ist wieder erreichbar.';
  } else if (restartPhase && Date.now() > restartUntil) {
    restartPhase = false;
    $('restart-message').textContent =
      'Neustart nicht bestätigt. Geräteadresse oder Ersatzzugangspunkt prüfen.';
  }
  $('update-fields').disabled = locked;
  $('update-state').textContent = s.ota?.pending_verify
    ? 'Startprüfung'
    : s.ota?.updating
      ? 'Wird installiert'
      : updating
        ? 'Bitte warten'
        : 'Bereit';
  if (updatePhase === 'reboot' && s.token !== updateToken) {
    if (s.ota?.pending_verify)
      $('update-message').textContent =
        'Firmware ' + s.version + ' gestartet. Startprüfung läuft …';
    else {
      const same = s.version === updateVersion;
      updatePhase = 'idle';
      $('update-progress').hidden = true;
      $('update-message').textContent = same
        ? 'Update abgeschlossen. Firmware ' + s.version + ' ist aktiv.'
        : 'Das Gerät läuft mit Firmware ' +
          s.version +
          '. Die gewünschte Version ' +
          updateVersion +
          ' wurde nicht bestätigt; möglicherweise wurde zur vorherigen Firmware zurückgekehrt.';
    }
  }
  if (updatePhase === 'reboot' && Date.now() > updateUntil) {
    updatePhase = 'idle';
    $('update-progress').hidden = true;
    $('update-message').textContent =
      'Update-Ergebnis nicht bestätigt. Aktuelle Version: ' +
      s.version +
      '. Falls nötig Seite neu laden oder den Ersatzzugangspunkt öffnen.';
  }
  $('connection').textContent = w.connected
    ? 'Verbunden mit ' + w.ssid
    : 'Über den Zugangspunkt erreichbar';
  $('capture-state').textContent = c.active
    ? 'Aufnahme läuft'
    : waiting
      ? 'Download startet …'
      : c.result === 'interrupted'
        ? 'Unterbrochen'
        : c.result === 'complete' || c.result === 'stopped'
          ? 'Beendet'
          : 'Bereit';
  $('start').disabled = locked;
  $('seconds').disabled = c.active;
  $('stop').disabled = !c.active;
  $('wifi-fields').disabled = locked;
  renderMqtt(s.mqtt, locked);
  window.probusxHistoryOptions?.update(s.history, locked);
  $('bytes').textContent = humanBytes(stats.rx_bytes || 0);
  $('elapsed').textContent = Math.floor((stats.elapsed_us || 0) / 1e6) + ' s';
  const errors = [
    'queue_dropped_records',
    'fifo_overflows',
    'uart_buffer_full',
    'frame_errors',
    'parity_errors',
    'breaks',
    'event_queue_pressure',
  ].reduce((n, k) => n + (stats[k] || 0), 0);
  $('errors').textContent = errors.toLocaleString('de-DE');
  if (
    c.result !== previousResult &&
    (c.result === 'complete' || c.result === 'stopped' || c.result === 'interrupted')
  ) {
    $('capture-hint').textContent =
      c.result === 'interrupted'
        ? 'Aufnahme unterbrochen. Ein unvollständiger Download enthält möglicherweise nur einen Teil der Daten.'
        : errors
          ? 'Aufnahme beendet, dabei wurden Fehler gemeldet. Download vollständig abwarten und die Datei zur Analyse bereitstellen.'
          : 'Aufnahme beendet. Download vollständig abwarten; die .pbxs-Datei kann direkt zur Auswertung hochgeladen werden.';
  }
  previousResult = c.result;
  $('wifi-state').textContent = w.connected
    ? 'Verbunden'
    : w.scanning
      ? 'Suche läuft'
      : 'Zugangspunkt / Verbindungssuche';
  $('wifi-message').textContent = w.message;
  $('wifi-current-ap').textContent =
    w.connected && w.connected_bssid
      ? 'Verbunden: ' +
        w.ssid +
        ' · BSSID ' +
        w.connected_bssid +
        ' · Kanal ' +
        w.connected_channel +
        ' · ' +
        w.connected_rssi +
        ' dBm'
      : w.connected
        ? 'Verbunden. Zugangspunktdaten werden abgefragt …'
        : 'Zurzeit keine Verbindung mit dem Heim-WLAN.';
  $('wifi-saved-binding').textContent =
    w.ap_mode === 'fixed'
      ? 'Gespeicherte Wahl: feste Bindung an ' + w.selected_bssid
      : 'Gespeicherte Wahl: automatisch';
  $('scan').textContent = w.scanning ? 'Suche läuft …' : 'Zugangspunkte suchen';
  $('address-box').hidden = !w.connected || !w.ip;
  if (w.connected && w.ip) {
    $('address').textContent = 'http://' + w.ip;
    $('address').href = 'http://' + w.ip + '/';
    $('handover').textContent = w.ap
      ? c.busy
        ? 'Zugangspunkt bleibt während der aktuellen Aktion aktiv.'
        : w.ap_clients
          ? 'Zugangspunkt bleibt aktiv, solange ein Gerät damit verbunden ist. Du kannst jetzt in dein normales WLAN wechseln.'
          : 'Zugangspunkt wird nach der Übergangszeit abgeschaltet' +
            (w.ap_seconds ? ' (noch etwa ' + w.ap_seconds + ' s).' : '.')
      : 'Zugangspunkt ist aus. Bei WLAN-Ausfall wird er automatisch wieder aktiviert.';
  }
  $('fallback').textContent =
    'Ersatzzugang: ProBusX-Bridge · http://192.168.4.1 · Passwort: probusx12345 · ' +
    (w.ap ? 'zurzeit aktiv' : 'zurzeit aus');
  if (!formTouched) {
    $('ssid').value = w.ssid || '';
    $('ap-mode').value = w.ap_mode === 'fixed' ? 'fixed' : 'auto';
    $('bssid').value = w.selected_bssid || '';
  }
  updateApMode();
  const networks = Array.isArray(w.networks) ? w.networks : [];
  const signature = JSON.stringify(networks);
  if (signature !== networksSignature) {
    networksSignature = signature;
    const selected = $('networks').value;
    $('networks').replaceChildren(new Option('Zugangspunkt wählen oder Daten unten eingeben', ''));
    // BSSID als Schlüssel verwenden. Gleiche SSIDs bleiben verschiedene, wählbare Einträge.
    for (const n of networks) {
      const option = new Option(
        (n.ssid || '(SSID verborgen)') +
          ' · ' +
          n.bssid +
          ' · Kanal ' +
          n.channel +
          ' · ' +
          n.rssi +
          ' dBm' +
          (n.open ? ' · offen' : ''),
        n.bssid,
      );
      $('networks').add(option);
    }
    $('networks').value = $('bssid').value || selected;
  }
  $('wifi-scan-count').textContent = networks.length + ' Zugangspunkte';
}
async function poll() {
  if (!automaticAllowed()) {
    setTimeout(poll, 1000);
    return;
  }
  try {
    render(await automaticRead('/api/status', 4000));
  } catch (error) {
    if (browserPaused) return;
    $('restart').disabled = true;
    $('connection').textContent =
      'Verbindung zum Gerät unterbrochen. Nach WLAN-Wechsel die neue IP-Adresse öffnen.';
    $('start').disabled = true;
    $('stop').disabled = true;
    $('wifi-fields').disabled = true;
    $('mqtt-fields').disabled = true;
    $('history-options-fields').disabled = true;
    $('update-fields').disabled = true;
    if (updatePhase === 'reboot')
      $('update-message').textContent =
        Date.now() > updateUntil
          ? 'Gerät noch nicht erreichbar. Mit dem normalen WLAN oder dem Zugangspunkt ProBusX-Bridge verbinden und die Geräteadresse öffnen.'
          : 'Gerät startet neu. Die Verbindung wird automatisch wiederhergestellt …';
  } finally {
    setTimeout(poll, 1000);
  }
}
$('ssid').addEventListener('input', () => {
  formTouched = true;
});
$('ap-mode').addEventListener('change', () => {
  formTouched = true;
  updateApMode();
});
$('bssid').addEventListener('input', () => {
  formTouched = true;
});
$('networks').addEventListener('change', () => {
  const selected = state?.wifi.networks?.find((n) => n.bssid === $('networks').value);
  if (!selected) return;
  $('ssid').value = selected.ssid || '';
  $('bssid').value = selected.bssid;
  $('ap-mode').value = 'fixed';
  formTouched = true;
  updateApMode();
  if (!selected.ssid)
    notice('Der WLAN-Name ist verborgen. Den bekannten WLAN-Namen unten eingeben.');
});
$('capture-form').addEventListener('submit', (event) => {
  if (
    !state ||
    state.capture.busy ||
    operationPending ||
    updatePhase !== 'idle' ||
    state.ota?.pending_verify ||
    Date.now() < pendingUntil
  ) {
    event.preventDefault();
    notice('Bitte warten, bis die aktuelle Aktion beendet ist.');
    return;
  }
  pendingUntil = Date.now() + 5000;
  notice(
    'Der Browser startet den Download. Bitte den vorgeschlagenen Dateinamen speichern und die Verbindung beibehalten.',
  );
  $('capture-hint').textContent =
    'Download läuft während der Aufnahme. Bei „Aufnahme beenden“ wird die Datei mit einer abschließenden Statistik fertiggestellt.';
  $('start').disabled = true;
  $('wifi-fields').disabled = true;
  // Datei direkt im Browser herunterladen. Die Aufnahme nicht im Browser-RAM oder Flash sammeln.
});
$('download').addEventListener('load', () => {
  try {
    const text = $('download').contentDocument.body.textContent.trim();
    if (text) {
      let message = text;
      try {
        message = JSON.parse(text).error || text;
      } catch (_) {}
      notice(message);
      pendingUntil = 0;
    }
  } catch (_) {}
});
async function action(path, values) {
  operationPending = true;
  if (state) render(state);
  try {
    await post(path, values);
    return true;
  } catch (error) {
    notice(error.message);
    return false;
  } finally {
    operationPending = false;
  }
}
$('restart').addEventListener('click', async () => {
  if ($('restart').disabled || !confirm('Bridge jetzt neu starten?')) return;
  restartPhase = true;
  restartToken = token;
  restartUntil = Date.now() + 90000;
  if (state) render(state);
  $('restart-message').textContent = 'Neustart angefordert. Bitte warten …';
  try {
    await post('/api/restart');
  } catch (error) {
    restartPhase = false;
    $('restart-message').textContent = error.message;
    if (state) render(state);
  }
});
$('scan').addEventListener('click', async () => {
  notice('Netzwerksuche läuft …');
  await action('/api/scan');
});
$('stop').addEventListener('click', async () => {
  if (await action('/api/stop'))
    notice('Aufnahme wird beendet. Bitte das Ende des Downloads abwarten.');
});
$('wifi-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  if (new TextEncoder().encode($('ssid').value).length > 32) {
    notice('Der WLAN-Name ist zu lang (höchstens 32 UTF-8-Bytes).');
    return;
  }
  notice('WLAN wird gespeichert. Die Verbindung kann kurz unterbrochen werden.');
  const fixed = $('ap-mode').value === 'fixed';
  if (
    await action('/api/wifi', {
      ssid: $('ssid').value,
      password: $('password').value,
      ap_mode: fixed ? 'fixed' : 'auto',
      bssid: fixed ? $('bssid').value.trim() : '',
    })
  )
    $('password').value = '';
});
$('forget').addEventListener('click', async () => {
  if (!confirm('Gespeichertes WLAN vergessen und den Zugangspunkt verwenden?')) return;
  if (await action('/api/forget')) {
    $('ssid').value = '';
    $('password').value = '';
    $('ap-mode').value = 'auto';
    $('bssid').value = '';
    $('networks').value = '';
    updateApMode();
    formTouched = false;
    notice(
      'WLAN wird vergessen. Bei Verbindungsabbruch mit dem Zugangspunkt ProBusX-Bridge verbinden.',
    );
  }
});
$('update-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  if (
    !state ||
    state.capture.busy ||
    operationPending ||
    updatePhase !== 'idle' ||
    state.ota?.pending_verify ||
    Date.now() < pendingUntil
  )
    return;
  const file = $('firmware').files[0];
  if (!file) return;
  if (file.size < 320 || file.size > (state.ota?.max_size || 3145728)) {
    $('update-message').textContent =
      'Ungültige Dateigröße. Bitte die Datei mit -ota.bin auswählen.';
    return;
  }
  updatePhase = 'checking';
  render(state);
  let prefix;
  try {
    prefix = new Uint8Array(await file.slice(0, 288).arrayBuffer());
  } catch (_) {
    updatePhase = 'idle';
    $('update-message').textContent = 'Datei konnte nicht gelesen werden.';
    render(state);
    return;
  }
  const str = (start, n) => new TextDecoder().decode(prefix.slice(start, start + n)).split('\0')[0];
  if (
    prefix[0] !== 0xe9 ||
    prefix[12] !== 9 ||
    prefix[13] !== 0 ||
    prefix[23] !== 1 ||
    prefix[32] !== 0x32 ||
    prefix[33] !== 0x54 ||
    prefix[34] !== 0xcd ||
    prefix[35] !== 0xab ||
    str(80, 32) !== 'pbx_waveshare_ota_v1'
  ) {
    updatePhase = 'idle';
    $('update-message').textContent =
      'Diese Datei passt nicht. Bitte die ProBusX-Waveshare-Datei mit -ota.bin auswählen.';
    render(state);
    return;
  }
  updateVersion = str(48, 32);
  updateToken = token;
  updatePhase = 'upload';
  $('update-progress').hidden = false;
  $('update-progress').value = 0;
  $('update-message').textContent = 'Firmware ' + updateVersion + ' wird übertragen …';
  render(state);
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/update');
  xhr.setRequestHeader('Content-Type', 'application/octet-stream');
  xhr.setRequestHeader('X-CSRF-Token', token);
  xhr.timeout = 180000;
  xhr.upload.onprogress = (e) => {
    if (e.lengthComputable) {
      const percent = Math.floor((e.loaded / e.total) * 100);
      $('update-progress').value = percent;
      $('update-message').textContent =
        percent < 100
          ? 'Firmware wird übertragen: ' + percent + ' %'
          : 'Übertragung abgeschlossen. Firmware wird geschrieben und geprüft …';
    }
  };
  const reconnect = (message) => {
    updatePhase = 'reboot';
    updateUntil = Date.now() + 120000;
    $('update-message').textContent = message;
  };
  xhr.onload = () => {
    let result = {};
    try {
      result = JSON.parse(xhr.responseText);
    } catch (_) {}
    if (xhr.status === 200 && result.accepted) reconnect('Firmware geprüft. Gerät startet neu …');
    else {
      updatePhase = 'idle';
      $('update-progress').hidden = true;
      $('update-message').textContent =
        result.error || 'Update abgelehnt. Bisherige Firmware bleibt aktiv.';
      if (state) render(state);
    }
  };
  xhr.onerror = xhr.ontimeout = () =>
    reconnect('Verbindung unterbrochen. Der Status des Geräts wird geprüft …');
  xhr.send(file);
});
poll();

let liveState = null;
let settings = {
  display_ms: 250,
  temperature_ms: 1000,
  output_ms: 1000,
  analysis_enabled: true,
  transmit_enabled: true,
};
let optionsTouched = false,
  lastTemperatures = 0,
  lastOutputs = 0;
const activePage =
  location.pathname === '/history'
    ? 'history'
    : location.pathname === '/schema'
      ? 'schema'
      : location.pathname === '/analysis'
        ? 'analysis'
        : location.pathname === '/options'
          ? 'options'
          : 'live';
for (const name of ['live', 'schema', 'history', 'analysis', 'options'])
  $('page-' + name).hidden = name !== activePage;
for (const link of document.querySelectorAll('nav a'))
  if (link.dataset.page === activePage) link.setAttribute('aria-current', 'page');
$('options-form').addEventListener('input', () => (optionsTouched = true));
function applySettings(s) {
  if (s) settings = { ...settings, ...s };
  if (!optionsTouched) {
    $('display-ms').value = settings.display_ms;
    $('temperature-ms').value = settings.temperature_ms;
    $('output-ms').value = settings.output_ms;
    $('transmit-enabled').checked = settings.transmit_enabled;
    $('analysis-enabled').checked = settings.analysis_enabled;
  }
  $('analysis-toggle').textContent = settings.analysis_enabled
    ? 'Analyse ausschalten'
    : 'Analyse einschalten';
  $('analysis-toggle').disabled = !!liveState?.busy;
  $('options-fields').disabled = !!liveState?.busy;
}
async function saveSettings(next) {
  await liveCommand('/api/settings', {
    display_ms: next.display_ms,
    temperature_ms: next.temperature_ms,
    output_ms: next.output_ms,
    analysis_enabled: next.analysis_enabled ? '1' : '0',
    transmit_enabled: next.transmit_enabled ? '1' : '0',
  });
  optionsTouched = false;
  applySettings(next);
}
$('options-form').onsubmit = async (e) => {
  e.preventDefault();
  try {
    await saveSettings({
      ...settings,
      display_ms: Number($('display-ms').value),
      temperature_ms: Number($('temperature-ms').value),
      output_ms: Number($('output-ms').value),
      analysis_enabled: $('analysis-enabled').checked,
      transmit_enabled: $('transmit-enabled').checked,
    });
    $('options-message').textContent =
      'Gespeichert. Die Intervalle werden auch von Home Assistant übernommen.';
  } catch (e) {
    $('options-message').textContent = e.message;
  }
};
$('analysis-toggle').onclick = async () => {
  try {
    await saveSettings({ ...settings, analysis_enabled: !settings.analysis_enabled });
    diagSamples = [];
    diagnosis = null;
    $('quality-download').disabled = true;
    $('analysis-message').textContent = settings.analysis_enabled
      ? 'Analyse läuft mit neuen Statistiken.'
      : 'Analyse ausgeschaltet. CRC-Prüfung und Rohdatenaufnahme bleiben verfügbar.';
  } catch (e) {
    $('analysis-message').textContent = e.message;
  }
};

async function liveCommand(path, fields) {
  const response = await fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ token: liveState.token, ...fields }),
    signal: AbortSignal.timeout(8000),
  });
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || 'Anfrage fehlgeschlagen');
  return result;
}
for (const b of document.querySelectorAll('[data-key]'))
  b.onclick = async () => {
    try {
      await liveCommand('/api/key', { key: b.dataset.key });
      $('live-message').textContent = 'Taste zur Übertragung vorgemerkt.';
    } catch (e) {
      $('live-message').textContent = e.message;
    }
  };
$('transmit-toggle').onclick = async () => {
  try {
    await liveCommand('/api/transmit', { enable: liveState.tx_enabled ? '0' : '1' });
    $('live-message').textContent = 'Freigabe geändert.';
  } catch (e) {
    $('live-message').textContent = e.message;
  }
};

// Tabellen bleiben getrennt nach Gerät. Nur die Wertzellen werden aktualisiert.
const outputNames = [
  'R1',
  'R2',
  'R3',
  'R4',
  'R5',
  'R6',
  'R7',
  'R0',
  'HE1',
  'HE2',
  'HE3',
  'F1R1',
  'F1R2',
  'F1R3',
  'F1HE',
];
function valueTable(id, labels, group, offset) {
  const table = document.createElement('table');
  table.className = 'value-table';
  const head = table.createTHead().insertRow();
  for (const text of [
    group === 't' ? 'Eingang' : 'Ausgang',
    group === 't' ? 'Temperatur' : 'Stellwert',
  ]) {
    const th = document.createElement('th');
    th.textContent = text;
    head.append(th);
  }
  const body = table.createTBody();
  labels.forEach((label, i) => {
    const row = body.insertRow();
    const name = document.createElement('th');
    name.scope = 'row';
    name.textContent = label;
    row.append(name);
    const td = row.insertCell();
    td.dataset.value = group + (offset + i);
    td.textContent = '–';
  });
  $(id).replaceChildren(table);
}
valueTable(
  'live-values',
  Array.from({ length: 10 }, (_, i) => 'S' + (i + 1)),
  't',
  0,
);
valueTable('live-outputs', outputNames.slice(0, 11), 'o', 0);
valueTable('flex-inputs', ['S1', 'S2', 'S3', 'S4'], 't', 10);
valueTable('flex-outputs', ['R1', 'R2', 'R3', 'HE'], 'o', 11);
function flexVisible(s) {
  const on = s.flex1_enabled ?? state?.history?.flex1 ?? true;
  $('flex-modules').hidden = !on;
  return on;
}
function renderTemperatures(s, fresh) {
  const ext = flexVisible(s) && s.ext_age_ms >= 0 && s.ext_age_ms < 3000;
  for (let i = 0; i < 14; i++) {
    const v = s.temperatures_raw?.[i],
      cell = document.querySelector('[data-value="t' + i + '"]');
    const valid = (i < 10 ? fresh : ext) && Number.isFinite(v) && v !== 2500;
    cell.textContent = valid ? (v / 10).toFixed(1) + ' °C' : '–';
    cell.classList.toggle('value-stale', !valid);
  }
}
function renderOutputs(s, fresh) {
  const ext = flexVisible(s) && s.ext_age_ms >= 0 && s.ext_age_ms < 3000;
  for (let i = 0; i < 15; i++) {
    const raw = s.outputs_raw?.[i],
      v = raw / (i >= 8 && i <= 10 ? 100 : 2),
      cell = document.querySelector('[data-value="o' + i + '"]');
    const valid = (i < 11 ? fresh : ext) && Number.isFinite(v) && v >= 0 && v <= 100;
    cell.textContent = valid ? v.toLocaleString('de-DE', { maximumFractionDigits: 2 }) + ' %' : '–';
    cell.classList.toggle('value-on', valid && v > 0);
    cell.classList.toggle('value-stale', !valid);
  }
}
async function livePoll() {
  if (!automaticAllowed()) {
    setTimeout(livePoll, 1000);
    return;
  }
  try {
    liveState = await automaticRead('/api/live', 4000);
    applySettings(liveState.settings);
    window.probusxSchema?.update(liveState, settings);
    const fresh = liveState.main_age_ms >= 0 && liveState.main_age_ms < 3000;
    $('live-state').textContent = fresh ? 'Empfang aktiv' : 'Warte auf Bus';
    $('transmit-toggle').textContent = liveState.tx_enabled
      ? 'Bedienung sperren'
      : 'Bedienung freigeben';
    $('tx-footer').textContent = liveState.tx_enabled
      ? 'Bedienung freigegeben'
      : 'Bedienung gesperrt';
    for (const b of document.querySelectorAll('[data-key]'))
      b.disabled =
        !liveState.tx_enabled ||
        liveState.busy ||
        liveState.poll_age_ms < 0 ||
        liveState.poll_age_ms >= 2000;
    const t = performance.now();
    if (
      activePage === 'live' &&
      (t - lastTemperatures >= settings.temperature_ms || !lastTemperatures)
    ) {
      lastTemperatures = t;
      renderTemperatures(liveState, fresh);
    }
    if (activePage === 'live' && (t - lastOutputs >= settings.output_ms || !lastOutputs)) {
      lastOutputs = t;
      renderOutputs(liveState, fresh);
    }
  } catch (e) {
    if (!browserPaused) {
      $('live-state').textContent = 'Live-Verbindung unterbrochen';
      for (const b of document.querySelectorAll('[data-key]')) b.disabled = true;
      $('options-fields').disabled = true;
      $('analysis-toggle').disabled = true;
    }
  }

  setTimeout(
    livePoll,
    ['live', 'schema'].includes(activePage)
      ? Math.min(1000, settings.temperature_ms, settings.output_ms)
      : 1000,
  );
}
livePoll();
async function displayPoll() {
  if (!automaticAllowed()) {
    setTimeout(displayPoll, 1000);
    return;
  }
  const began = performance.now();
  if (activePage === 'live' && !document.hidden) {
    const ctx = $('lcd').getContext('2d');
    try {
      const data = new Uint8Array(await automaticRead('/api/display', 2000, true));
      if (data.length !== 5120) throw new Error('Unvollständiges Display');
      const im = ctx.createImageData(160, 128);
      for (let y = 0; y < 128; y++)
        for (let x = 0; x < 160; x++) {
          let g = 255 - 85 * ((data[(159 - x) * 32 + (y >> 2)] >> (2 * (y % 4))) & 3),
            j = (y * 160 + x) * 4;
          im.data[j] = im.data[j + 1] = im.data[j + 2] = g;
          im.data[j + 3] = 255;
        }
      ctx.putImageData(im, 0, 0);
    } catch (e) {
      if (!browserPaused) {
        ctx.fillStyle = '#eee';
        ctx.fillRect(0, 0, 160, 128);
        ctx.fillStyle = '#333';
        ctx.font = '10px sans-serif';
        ctx.fillText('Kein aktuelles Display', 20, 64);
      }
    }
  }
  setTimeout(displayPoll, Math.max(20, settings.display_ms - (performance.now() - began)));
}
displayPoll();

let diagnosis = null,
  diagSamples = [],
  diagBrowserEvents = [],
  diagFailures = 0,
  diagRtt = 0,
  diagLastUptime = null;
const diagNames = {
  main: 'Hauptgerät',
  ext: 'FlexBox F1',
  display: 'Display · ohne CRC',
  short: 'Kurzstatus',
  poll: 'Abfragen · ohne CRC',
  key: 'Empfangene Tasten',
  aux: 'Weitere · ohne CRC',
};
const diagEvents = {
  1: 'CRC fehlerhaft',
  2: 'Unvollständiges Telegramm (>100 ms Pause)',
  3: 'Ungültige/ unbekannte Kopfbytes',
  4: 'UART-FIFO übergelaufen',
  5: 'UART-Puffer voll',
  6: 'UART-Framingfehler',
  7: 'UART-Paritätsfehler',
  8: 'UART-Break',
  9: 'UART-Ereignisqueue fast voll',
  10: 'Telegramm-Unterbrechung erkannt',
  11: 'Empfang wieder da',
  12: 'Parser für Aufnahme neu begonnen',
  13: 'Nicht abgeholter Tastendruck verfallen',
  14: 'Sendefehler',
  15: 'Tastenantwort empfangen (anderer Sender/Echo)',
  16: 'Hauptdaten-Endemarkierung ungültig',
};
function diagAge(v) {
  return v < 0 ? 'noch nie' : v < 1000 ? Math.round(v) + ' ms' : (v / 1000).toFixed(2) + ' s';
}
function renderDiagnosis(d) {
  const q = d.window_60s,
    u = d.uart,
    f = d.families;
  const uartTotal = u.fifo_overflows + u.buffer_full + u.frame_errors + u.parity_errors + u.breaks;
  $('quality-rate').textContent = Math.round(q.bytes_per_s).toLocaleString('de-DE');
  $('quality-crc').textContent =
    q.crc_bad +
    ' / ' +
    (q.crc_ok + q.crc_bad) +
    (q.crc_error_percent === null ? '' : ' (' + q.crc_error_percent.toFixed(3) + ' %)');
  $('quality-uart').textContent = uartTotal;
  const recentError = d.events.some(
    (e) => d.uptime_s - e.uptime_s < 60 && [1, 2, 4, 5, 6, 7, 8, 9, 14, 16].includes(e.code),
  );
  let level = 'ok',
    status = 'Empfang unauffällig';
  if (d.rx_age_ms < 0) {
    level = 'bad';
    status = 'Noch keine Busdaten';
  } else if (d.rx_age_ms > 1500) {
    level = 'bad';
    status = 'Bus-Empfang unterbrochen';
  } else if (['main', 'ext', 'display', 'poll'].some((k) => f[k].gap_open)) {
    level = 'bad';
    status = 'Telegramm-Unterbrechung';
  } else if (recentError || q.crc_bad) {
    level = 'bad';
    status = 'Fehler in den letzten 60 s';
  } else if (!f.main.valid) {
    level = 'bad';
    status = 'Noch keine gültigen Hauptdaten';
  }
  $('quality-state').textContent = status;
  $('quality-state').dataset.level = level;
  $('quality-detail').textContent =
    'Empfangen: ' +
    d.rx_bytes.toLocaleString('de-DE') +
    ' Bytes · letzte Bytes vor ' +
    diagAge(d.rx_age_ms) +
    ' · CRC gesamt: ' +
    d.crc_ok +
    ' gültig / ' +
    d.crc_bad +
    ' fehlerhaft · ' +
    d.unchecked +
    ' Telegramme ohne bekannte CRC';
  $('quality-network').textContent =
    'Browser ↔ ESP: ' +
    Math.round(diagRtt) +
    ' ms · fehlgeschlagene Diagnoseabrufe: ' +
    diagFailures +
    ' · WLAN: ' +
    (d.wifi_connected ? d.wifi_rssi_dbm + ' dBm' : 'Zugangspunkt / nicht mit Heim-WLAN verbunden') +
    ' · Laufzeit: ' +
    Math.floor(d.uptime_s) +
    ' s · Neustartursache: ' +
    d.reset_reason;
  const tbody = $('quality-families');
  tbody.replaceChildren();
  for (const [key, name] of Object.entries(diagNames)) {
    const a = f[key],
      tr = document.createElement('tr');
    for (const text of [
      name,
      a.valid,
      a.unchecked ? 'nicht geprüft' : a.crc_ok + ' / ' + a.crc_bad,
      diagAge(a.age_ms),
      diagAge(a.max_gap_ms),
      a.gaps,
    ]) {
      const td = document.createElement('td');
      td.textContent = text;
      tr.append(td);
    }
    if (a.gap_open) tr.className = 'quality-bad';
    tbody.append(tr);
  }
  $('quality-errors').textContent =
    'FIFO-Überläufe: ' +
    u.fifo_overflows +
    ' | Puffer voll: ' +
    u.buffer_full +
    ' | Framing: ' +
    u.frame_errors +
    ' | Parität: ' +
    u.parity_errors +
    ' | Break: ' +
    u.breaks +
    '\n' +
    'Max. UART-Puffer: ' +
    d.uart_buffer_high_water +
    ' / ' +
    d.uart_buffer_capacity +
    ' Bytes | Ereignisqueue-Warnungen: ' +
    u.event_queue_pressure +
    '\n' +
    'Ungültige Köpfe: ' +
    d.header_errors +
    ' | Endemarkierung: ' +
    d.trailer_errors +
    ' | Teiltelegramm-Timeouts: ' +
    d.partial_timeouts +
    ' | Übersprungene Nicht-Null-Bytes: ' +
    d.skipped_bytes +
    '\n' +
    'Gesendete Antworten: ' +
    (d.tx_frames || 0) +
    ' | Letzte Antwort vor: ' +
    diagAge(d.last_tx_age_ms ?? -1) +
    ' | Taste wartet: ' +
    (d.key_pending ? diagAge(d.key_pending_age_ms) : 'nein') +
    '\n' +
    'Sendefehler: ' +
    d.tx_errors +
    ' | Abgelaufene Tasten: ' +
    d.key_expired +
    ' | Empfangene Tastenantworten: ' +
    d.foreign_key_frames +
    '\n' +
    'Freier interner Heap: ' +
    d.free_heap +
    ' Bytes (Minimum ' +
    d.minimum_free_heap +
    ')' +
    (d.last_bad_crc
      ? '\nLetzter CRC-Fehler: erwartet 0x' +
        d.last_bad_crc.expected.toString(16) +
        ', empfangen 0x' +
        d.last_bad_crc.received.toString(16) +
        ' | Kopf ' +
        d.last_bad_crc.head_hex
      : '');
  const list = $('quality-events');
  list.replaceChildren();
  for (const e of [...d.events].reverse()) {
    const li = document.createElement('li');
    li.textContent =
      e.uptime_s.toFixed(2) +
      ' s: ' +
      (diagEvents[e.code] || 'Ereignis ' + e.code) +
      (e.code === 1 || e.code === 10 || e.code === 11 ? ' · ' + diagNames[e.family] : '') +
      (e.value ? ' · Wert ' + e.value : '');
    list.append(li);
  }
  if (!d.events.length) {
    const li = document.createElement('li');
    li.textContent = 'Keine Ereignisse seit Start';
    list.append(li);
  }
  $('quality-download').disabled = false;
}
async function pollDiagnosis() {
  if (!automaticAllowed()) {
    setTimeout(pollDiagnosis, 1000);
    return;
  }
  if (activePage !== 'analysis' || document.hidden || !settings.analysis_enabled) {
    if (!settings.analysis_enabled) {
      $('quality-state').textContent = 'Analyse ausgeschaltet';
      $('quality-detail').textContent =
        'Keine Statistikaufzeichnung. CRC-Validierung bleibt aktiv.';
      $('quality-rate').textContent =
        $('quality-crc').textContent =
        $('quality-uart').textContent =
          '–';
      $('quality-families').replaceChildren();
      $('quality-events').replaceChildren();
      $('quality-errors').textContent = '';
    }
    setTimeout(pollDiagnosis, 2000);
    return;
  }
  const started = performance.now();
  try {
    const data = await automaticRead('/api/diagnostics', 3500);
    diagRtt = performance.now() - started;
    if (diagLastUptime !== null && data.uptime_s < diagLastUptime) {
      diagBrowserEvents.push({ time: new Date().toISOString(), event: 'esp_reboot_detected' });
    }
    diagLastUptime = data.uptime_s;
    diagnosis = data;
    if (data.analysis_enabled === false) {
      applySettings({ ...settings, analysis_enabled: false });
      setTimeout(pollDiagnosis, 2000);
      return;
    }
    renderDiagnosis(data);
    diagSamples.push({
      time: new Date().toISOString(),
      uptime_s: data.uptime_s,
      http_ms: Math.round(diagRtt),
      rx_age_ms: data.rx_age_ms,
      crc_bad: data.crc_bad,
      crc_ok: data.crc_ok,
      bytes_per_s: data.window_60s.bytes_per_s,
      uart: data.uart,
      families: data.families,
    });
    if (diagSamples.length > 300) diagSamples.shift();
  } catch (e) {
    if (!browserPaused) {
      diagFailures++;
      diagBrowserEvents.push({
        time: new Date().toISOString(),
        event: 'diagnostics_http_failed',
        error: e.message,
      });
      $('quality-state').textContent = 'Browser-Verbindung zum ESP gestört';
      $('quality-state').dataset.level = 'bad';
      $('quality-network').textContent =
        'Letzter Abruf fehlgeschlagen. Aus dieser Störung lässt sich kein Busfehler ableiten.';
    }
  }
  if (diagBrowserEvents.length > 100) diagBrowserEvents.shift();
  setTimeout(pollDiagnosis, 2000);
}
$('quality-download').onclick = () => {
  if (!diagnosis) return;
  const report = {
    exported_at: new Date().toISOString(),
    note: 'Zähler seit Analysestart. Browsermesspunkte entstehen nur bei geöffneter Seite. Bei HTTP-Fehlern kann der Gerätestand älter sein.',
    browser_failed_requests: diagFailures,
    snapshot: diagnosis,
    browser_samples: diagSamples,
    browser_events: diagBrowserEvents,
  };
  const url = URL.createObjectURL(
    new Blob([JSON.stringify(report, null, 2)], { type: 'application/json' }),
  );
  const a = document.createElement('a');
  a.href = url;
  a.download = 'probusx-diagnose-' + new Date().toISOString().replace(/[:.]/g, '-') + '.json';
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
};
pollDiagnosis();
