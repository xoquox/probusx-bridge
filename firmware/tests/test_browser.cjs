// Prüft die Oberfläche gegen einen Gerätesimulator. Verändert kein reales WLAN.
const { chromium } = require('playwright');
const fs = require('fs'),
  path = require('path'),
  http = require('http'),
  os = require('os');
const { execFileSync } = require('child_process');
const assert = require('assert/strict');
const root = path.resolve(__dirname, '..');
const state = {
  version: '3.1.0',
  ota: { pending_verify: false, updating: false, max_size: 3145728 },
  token: '12345678901234567890123456789012',
  capture: { active: false, busy: false, result: 'none', statistics: {} },
  mqtt: {
    enabled: false,
    tls: false,
    connected: false,
    host: '',
    port: 1883,
    username: '',
    password_set: false,
    root: 'probusx/test',
    message: 'MQTT ausgeschaltet',
    connections: 0,
    publish_errors: 0,
    rejected_keys: 0,
  },
  wifi: {
    ssid: '',
    ip: '',
    message: 'WLAN einrichten oder direkt im Zugangspunkt aufnehmen.',
    connected: false,
    ap: true,
    ap_seconds: 0,
    ap_clients: 1,
    reason: 0,
    scanning: false,
    networks: [],
    ap_mode: 'auto',
    selected_bssid: '',
    connected_bssid: '',
    connected_rssi: null,
    connected_channel: null,
  },
};
const fixture = execFileSync('python3', [
  '-c',
  `import sys;sys.path.insert(0,sys.argv[1]);from test_capture import session;sys.stdout.buffer.write(session(bytes(range(256))*4))`,
  path.join(root, 'tests'),
]);
const diag = JSON.parse(fs.readFileSync(path.resolve(root, '../tests/quality-fixture.json')));
let diagDown = false;
let liveEnabled = true,
  lastKey = null;
let settings = {
  display_ms: 250,
  temperature_ms: 1000,
  output_ms: 1000,
  analysis_enabled: true,
  transmit_enabled: true,
};
let imageRequests = 0,
  automaticCount = 0,
  restartCount = 0;
let lastMqtt,
  mqttPassword = '',
  rejectMqtt = false;
let lastWifi,
  rejectCapture = false,
  statusDown = false,
  rejectUpdate = false,
  updateCount = 0;
const image = Buffer.alloc(4096);
image[0] = 0xe9;
image[1] = 4;
image[12] = 9;
image[23] = 1;
image.writeUInt32LE(0xabcd5432, 32);
image.write('3.1.1', 48);
image.write('pbx_waveshare_ota_v1', 80);
async function waitFor(check) {
  const until = Date.now() + 10000;
  while (Date.now() < until) {
    if (await check()) return;
    await new Promise((r) => setTimeout(r, 100));
  }
  throw new Error('Zeitgrenze für den erwarteten Oberflächenzustand überschritten');
}
const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://local');
  if (['/logo.png', '/favicon.png', '/favicon.ico'].includes(url.pathname)) {
    res.writeHead(200, {
      'Content-Type': url.pathname.endsWith('.ico') ? 'image/x-icon' : 'image/png',
    });
    return res.end(fs.readFileSync(path.join(root, 'src/assets', url.pathname.slice(1))));
  }

  if (['/api/status', '/api/live', '/api/display', '/api/diagnostics'].includes(url.pathname))
    automaticCount++;
  const json = (data, status = 200) => {
    res.writeHead(status, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
    res.end(JSON.stringify(data));
  };
  if (url.pathname === '/api/diagnostics')
    return json({ ...diag, analysis_enabled: settings.analysis_enabled }, diagDown ? 503 : 200);
  if (url.pathname === '/api/status') return json(state, statusDown ? 503 : 200);
  if (url.pathname === '/api/live')
    return json({
      settings,
      outputs_raw: Array(15).fill(0),
      ext_age_ms: 10,
      token: state.token,
      main_age_ms: 10,
      display_age_ms: 10,
      poll_age_ms: 10,
      tx_enabled: liveEnabled,
      busy: state.capture.busy,
      temperatures_raw: Array(14).fill(823),
    });
  if (url.pathname === '/api/display') {
    imageRequests++;
    res.writeHead(200, { 'Content-Type': 'application/octet-stream' });
    return res.end(fs.readFileSync(path.resolve(root, '../tests/display.raw')));
  }

  if (req.method === 'POST' && url.pathname === '/api/update') {
    const chunks = [];
    for await (const part of req) chunks.push(part);
    assert.equal(req.headers['x-csrf-token'], state.token);
    assert.equal(req.headers['content-type'], 'application/octet-stream');
    assert.deepEqual(Buffer.concat(chunks), image);
    updateCount++;
    if (rejectUpdate)
      return json(
        { error: 'Firmware-Prüfung fehlgeschlagen. Bisherige Firmware bleibt aktiv.' },
        400,
      );
    state.capture.busy = true;
    state.ota.updating = true;
    setTimeout(() => {
      json({ accepted: true, reboot: true });
      setTimeout(() => {
        state.token = 'new-boot-token';
        state.version = '3.1.1';
        state.ota.updating = false;
        state.capture.busy = false;
        state.ota.pending_verify = true;
        setTimeout(() => {
          state.ota.pending_verify = false;
        }, 1600);
      }, 800);
    }, 800);
    return;
  }
  if (req.method === 'POST') {
    let body = '';
    for await (const part of req) body += part;
    const form = new URLSearchParams(body);
    if (form.get('token') !== state.token) return json({ error: 'Seite bitte neu laden.' }, 403);
    if (url.pathname === '/api/restart') {
      if (state.capture.busy || state.ota.pending_verify || state.ota.updating)
        return json({ error: 'Vorgang läuft' }, 409);
      restartCount++;
      state.restart_pending = true;
      setTimeout(() => {
        state.token = 'restart-' + restartCount;
        state.restart_pending = false;
      }, 700);
      return json({ accepted: true }, 202);
    }
    if (url.pathname === '/api/mqtt') {
      if (rejectMqtt || state.capture.busy) return json({ error: 'MQTT abgelehnt' }, 409);
      lastMqtt = Object.fromEntries(form.entries());
      if (form.get('clear_password') === '1') mqttPassword = '';
      else if (form.get('password')) mqttPassword = form.get('password');
      Object.assign(state.mqtt, {
        enabled: form.get('enabled') === '1',
        host: form.get('host'),
        port: Number(form.get('port')),
        username: form.get('username'),
        tls: form.get('tls') === '1',
        password_set: !!mqttPassword,
        connected: form.get('enabled') === '1',
        message: 'MQTT verbunden',
      });
      return json({ accepted: true });
    }
    if (url.pathname === '/api/settings') {
      if (state.capture.busy) return json({ error: 'Vorgang läuft' }, 409);
      settings = {
        display_ms: Number(form.get('display_ms')),
        temperature_ms: Number(form.get('temperature_ms')),
        output_ms: Number(form.get('output_ms')),
        analysis_enabled: form.get('analysis_enabled') === '1',
        transmit_enabled: form.get('transmit_enabled') === '1',
      };
      liveEnabled = settings.transmit_enabled;
      return json({ accepted: true });
    }
    if (url.pathname === '/api/transmit') {
      liveEnabled = form.get('enable') === '1';
      settings.transmit_enabled = liveEnabled;
      return json({ accepted: true });
    }
    if (url.pathname === '/api/key') {
      assert(liveEnabled);
      lastKey = form.get('key');
      return json({ accepted: true });
    }
    if (url.pathname === '/api/scan') {
      state.wifi.networks = [
        { ssid: 'Küche & Hof', bssid: '02:11:22:33:44:55', channel: 6, rssi: -42, open: false },
        { ssid: 'Küche & Hof', bssid: '02:11:22:33:44:66', channel: 11, rssi: -71, open: false },
        {
          ssid: '<img src=x onerror=alert(1)>',
          bssid: '02:11:22:33:44:77',
          channel: 1,
          rssi: -60,
          open: true,
        },
        { ssid: '', bssid: '02:11:22:33:44:88', channel: 3, rssi: -65, open: false },
        ...Array.from({ length: 40 }, (_, i) => ({
          ssid: 'Weiterer Zugang ' + i,
          bssid: '02:00:00:00:01:' + (i + 1).toString(16).padStart(2, '0').toUpperCase(),
          channel: 1,
          rssi: -70,
          open: false,
        })),
      ];
      return json({ accepted: true }, 202);
    }
    if (url.pathname === '/api/wifi') {
      lastWifi = Object.fromEntries(form);
      state.wifi.ssid = form.get('ssid');
      state.wifi.ap_mode = form.get('ap_mode') || 'auto';
      state.wifi.selected_bssid = state.wifi.ap_mode === 'fixed' ? form.get('bssid') : '';
      const ap = state.wifi.networks.find(
        (n) =>
          n.ssid === state.wifi.ssid &&
          (state.wifi.ap_mode === 'auto' || n.bssid === state.wifi.selected_bssid),
      );
      state.wifi.ip = ap ? '192.168.178.123' : '';
      state.wifi.connected = !!ap;
      state.wifi.connected_bssid = ap?.bssid || '';
      state.wifi.connected_rssi = ap?.rssi ?? null;
      state.wifi.connected_channel = ap?.channel ?? null;
      state.wifi.ap_seconds = 60;
      state.wifi.message = 'Mit dem Heimnetz verbunden.';
      return json({ accepted: true }, 202);
    }
    if (url.pathname === '/api/stop') {
      state.capture.result = 'stopped';
      return json({ accepted: true });
    }
    if (url.pathname === '/capture') {
      if (rejectCapture) return json({ error: 'Aufnahme oder WLAN-Aktion läuft bereits.' }, 409);
      assert.equal(form.get('seconds'), '300');
      state.capture.active = state.capture.busy = true;
      state.capture.result = 'running';
      state.capture.statistics = { rx_bytes: 1024, elapsed_us: 1000000, frame_errors: 0 };
      res.writeHead(200, {
        'Content-Type': 'application/octet-stream',
        'Content-Disposition': 'attachment; filename="test-capture.pbxs"',
        'Transfer-Encoding': 'chunked',
      });
      res.write(fixture.subarray(0, 60));
      setTimeout(() => {
        res.end(fixture.subarray(60));
        state.capture.active = state.capture.busy = false;
        state.capture.result = 'complete';
      }, 2500);
      return;
    }
    return json({ accepted: true });
  }
  const assets = {
    '/analysis': 'index.html',
    '/options': 'index.html',
    '/': 'index.html',
    '/app.js': 'app.js',
    '/style.css': 'style.css',
    '/schema.js': 'schema.js',
    '/history.js': 'history.js',
  };
  if (assets[url.pathname]) {
    const types = {
      '/analysis': 'text/html',
      '/options': 'text/html',
      '/': 'text/html',
      '/app.js': 'text/javascript',
      '/style.css': 'text/css',
      '/schema.js': 'text/javascript',
      '/history.js': 'text/javascript',
    };
    res.writeHead(200, {
      'Content-Type': types[url.pathname] + '; charset=utf-8',
      'Content-Security-Policy':
        "default-src 'self'; script-src 'self'; style-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'",
    });
    return res.end(fs.readFileSync(path.join(root, 'src', assets[url.pathname])));
  }
  res.writeHead(404);
  res.end();
});
(async () => {
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'probusx-ui-'));
  let browser;
  try {
    browser = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
    const context = await browser.newContext({
      acceptDownloads: true,
      viewport: { width: 1100, height: 950 },
    });
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', (e) => errors.push(e.message));
    context.on('page', (p) => p.on('pageerror', (e) => errors.push(e.message)));
    await page.goto('http://127.0.0.1:' + server.address().port);
    await waitFor(async () => !(await page.locator('#start').isDisabled()));
    assert.match(await page.locator('#connection').innerText(), /Zugangspunkt/);
    await waitFor(async () => !(await page.locator('[data-key="up"]').isDisabled()));
    assert(await page.locator('#page-live').isVisible());
    assert(!(await page.locator('#page-options').isVisible()));
    const first = imageRequests;
    await page.waitForTimeout(1300);
    assert(imageRequests - first >= 4);
    console.log('Prüfung: Optionen öffnen');
    await page.click('a[href="/options"]');
    await waitFor(async () => !(await page.locator('#options-fields').isDisabled()));
    await waitFor(async () => !(await page.locator('#mqtt-form button').isDisabled()));
    await page.check('#mqtt-enabled');
    await page.fill('#mqtt-host', 'broker.example.org');
    await page.fill('#mqtt-port', '8883');
    await page.check('#mqtt-tls');
    await page.fill('#mqtt-username', 'probusx');
    await page.fill('#mqtt-password', 'test-secret');
    await page.waitForTimeout(1200);
    assert.equal(await page.inputValue('#mqtt-host'), 'broker.example.org');
    await page.locator('#mqtt-form button').click();
    await waitFor(async () => state.mqtt.connected);
    assert.equal(lastMqtt.token, state.token);
    assert.equal(mqttPassword, 'test-secret');
    await waitFor(async () => (await page.inputValue('#mqtt-password')) === '');
    await page.reload();
    await waitFor(async () => (await page.inputValue('#mqtt-host')) === 'broker.example.org');
    assert.equal(await page.inputValue('#mqtt-password'), '');
    assert(
      await page
        .locator('#mqtt-password-hint')
        .innerText()
        .then((t) => t.includes('gespeichert')),
    );
    await page.locator('#mqtt-form button').click();
    await waitFor(async () =>
      (await page.locator('#mqtt-save-message').innerText()).includes('angenommen'),
    );
    assert.equal(mqttPassword, 'test-secret');
    await waitFor(async () => !(await page.locator('#mqtt-form button').isDisabled()));
    rejectMqtt = true;
    await page.fill('#mqtt-password', 'retain-on-error');
    await page.locator('#mqtt-form button').click();
    await waitFor(async () =>
      (await page.locator('#notice').innerText()).includes('MQTT abgelehnt'),
    );
    assert.equal(await page.inputValue('#mqtt-password'), 'retain-on-error');
    rejectMqtt = false;
    await page.check('#mqtt-clear-password');
    await page.locator('#mqtt-form button').click();
    await waitFor(async () => mqttPassword === '');
    state.capture.busy = true;
    await waitFor(async () => await page.locator('#mqtt-form button').isDisabled());
    state.capture.busy = false;
    await waitFor(async () => !(await page.locator('#mqtt-form button').isDisabled()));
    await page.fill('#display-ms', '500');
    await page.fill('#temperature-ms', '2000');
    await page.fill('#output-ms', '3000');
    await page.locator('#options-form button').click();
    await waitFor(async () =>
      (await page.locator('#options-message').innerText()).includes('Gespeichert'),
    );
    assert.equal(settings.display_ms, 500);
    assert.equal(settings.temperature_ms, 2000);
    assert.equal(settings.output_ms, 3000);
    await page.reload();
    await waitFor(async () => (await page.inputValue('#display-ms')) === '500');
    await page.click('#scan');
    await waitFor(async () => (await page.locator('#networks option').count()) === 45);
    assert((await page.locator('#wifi-scan-count').innerText()).includes('44'));
    assert.equal(
      await page.locator('#networks option').filter({ hasText: 'Küche & Hof' }).count(),
      2,
    );
    assert.equal(await page.locator('#networks img').count(), 0);
    await page.selectOption('#networks', '02:11:22:33:44:55');
    assert.equal(await page.inputValue('#bssid'), '02:11:22:33:44:55');
    await page.selectOption('#networks', '02:11:22:33:44:66');
    assert.equal(await page.inputValue('#ssid'), 'Küche & Hof');
    assert.equal(await page.inputValue('#ap-mode'), 'fixed');
    assert.equal(await page.inputValue('#bssid'), '02:11:22:33:44:66');
    await page.fill('#password', 'A+B=%&abcdefgh');
    await page.locator('#wifi-form button[type=submit]').click();
    await waitFor(async () => (await page.inputValue('#password')) === '');
    assert.equal(lastWifi.ssid, 'Küche & Hof');
    assert.equal(lastWifi.password, 'A+B=%&abcdefgh');
    assert.equal(lastWifi.ap_mode, 'fixed');
    assert.equal(lastWifi.bssid, '02:11:22:33:44:66');
    assert.equal(await page.inputValue('#ap-mode'), 'fixed');
    await waitFor(() => page.locator('#address-box').isVisible());
    assert.equal(await page.getAttribute('#address', 'href'), 'http://192.168.178.123/');
    await waitFor(async () =>
      (await page.locator('#wifi-current-ap').innerText()).includes('02:11:22:33:44:66'),
    );
    assert((await page.locator('#wifi-current-ap').innerText()).includes('-71 dBm'));
    assert((await page.locator('#wifi-current-ap').innerText()).includes('Kanal 11'));
    await page.reload();
    await waitFor(async () => (await page.inputValue('#ap-mode')) === 'fixed');
    assert.equal(await page.inputValue('#bssid'), '02:11:22:33:44:66');
    // Ziel fehlt, ein anderer Zugangspunkt mit gleicher SSID bleibt sichtbar.
    // Die Oberfläche darf die gespeicherte Bindung nicht auf automatisch zurücksetzen.
    const target = state.wifi.networks.find((n) => n.bssid === '02:11:22:33:44:66');
    state.wifi.networks = state.wifi.networks.filter((n) => n !== target);
    state.wifi.connected = false;
    state.wifi.ip = '';
    state.wifi.connected_bssid = '';
    state.wifi.connected_rssi = null;
    state.wifi.connected_channel = null;
    state.wifi.ap = true;
    await waitFor(async () => !(await page.locator('#address-box').isVisible()));
    assert((await page.locator('#wifi-current-ap').innerText()).includes('keine Verbindung'));
    await page.reload();
    await waitFor(async () => (await page.inputValue('#bssid')) === '02:11:22:33:44:66');
    assert.equal(await page.inputValue('#ap-mode'), 'fixed');
    assert.equal(state.wifi.selected_bssid, target.bssid);
    assert.equal(lastWifi.ap_mode, 'fixed');
    // Die Rückkehr zur automatischen Wahl ist eine bewusste Benutzeraktion.
    await page.selectOption('#ap-mode', 'auto');
    assert(await page.locator('#bssid').isDisabled());
    await page.fill('#password', 'A+B=%&abcdefgh');
    await page.locator('#wifi-form button[type=submit]').click();
    await waitFor(async () => state.wifi.connected && lastWifi.ap_mode === 'auto');
    assert.equal(lastWifi.bssid, '');
    assert.equal(state.wifi.connected_bssid, '02:11:22:33:44:55');
    await page.reload();
    await waitFor(async () => !(await page.locator('#wifi-fields').isDisabled()));
    assert.equal(await page.inputValue('#ap-mode'), 'auto');
    assert.equal(await page.inputValue('#bssid'), '');
    state.wifi.connected_rssi = -58;
    await waitFor(async () =>
      (await page.locator('#wifi-current-ap').innerText()).includes('-58 dBm'),
    );
    await page.click('a[href="/analysis"]');
    await waitFor(async () => !(await page.locator('#start').isDisabled()));
    const downloadPromise = page.waitForEvent('download');
    await page.click('#start');
    const download = await downloadPromise;
    await waitFor(async () => !(await page.locator('#stop').isDisabled()));
    assert.equal(await page.locator('#ssid').isDisabled(), true);
    assert.equal(await page.locator('#ap-mode').isDisabled(), true);
    await download.saveAs(path.join(tmp, 'test.pbxs'));
    assert.deepEqual(fs.readFileSync(path.join(tmp, 'test.pbxs')), fixture);
    execFileSync('python3', [
      path.join(root, 'capture.py'),
      'decode',
      path.join(tmp, 'test.pbxs'),
      path.join(tmp, 'decoded'),
    ]);
    await waitFor(async () => !(await page.locator('#start').isDisabled()));
    rejectCapture = true;
    await page.click('#start');
    await waitFor(async () =>
      (await page.locator('#notice').innerText()).includes('läuft bereits'),
    );
    state.wifi.connected = false;
    state.wifi.ip = '';
    state.wifi.ap = true;
    state.wifi.message = 'WLAN nicht erreichbar. Zugangspunkt aktiv.';
    await waitFor(async () =>
      (await page.locator('#connection').innerText()).includes('Zugangspunkt'),
    );
    console.log('Prüfung: Optionen öffnen');
    await page.click('a[href="/options"]');
    await waitFor(async () => !(await page.locator('#firmware').isDisabled()));
    await page.setInputFiles('#firmware', {
      name: 'wrong.bin',
      mimeType: 'application/octet-stream',
      buffer: Buffer.alloc(4096),
    });
    await page.click('#update-start');
    await waitFor(async () =>
      (await page.locator('#update-message').innerText()).includes('passt nicht'),
    );
    assert.equal(updateCount, 0);
    rejectUpdate = true;
    await page.setInputFiles('#firmware', {
      name: 'probusx-ota.bin',
      mimeType: 'application/octet-stream',
      buffer: image,
    });
    await page.click('#update-start');
    await waitFor(async () =>
      (await page.locator('#update-message').innerText()).includes('Prüfung fehlgeschlagen'),
    );
    await waitFor(async () => !(await page.locator('#update-start').isDisabled()));
    rejectUpdate = false;
    await page.click('#update-start');
    await waitFor(async () => await page.locator('#start').isDisabled());
    assert.equal(await page.locator('#ssid').isDisabled(), true);
    await waitFor(async () =>
      (await page.locator('#update-message').innerText()).includes('Update abgeschlossen'),
    );
    assert.equal(await page.locator('#version').innerText(), '3.1.1');
    assert.equal(updateCount, 2);
    await page.click('a[href="/analysis"]');
    await waitFor(async () =>
      (await page.locator('#quality-state').innerText()).includes('unauffällig'),
    );
    diag.window_60s.crc_bad = 1;
    diag.crc_bad = 1;
    await waitFor(async () =>
      (await page.locator('#quality-state').innerText()).includes('Fehler'),
    );
    const reportDownload = page.waitForEvent('download');
    await page.click('#quality-download');
    const report = await reportDownload;
    const reportPath = path.join(tmp, 'diagnostics.json');
    await report.saveAs(reportPath);
    const exported = JSON.parse(fs.readFileSync(reportPath));
    assert.equal(exported.snapshot.crc_bad, 1);
    assert(exported.browser_samples.length > 0);
    assert(!JSON.stringify(exported).includes(state.token));
    diag.window_60s.crc_bad = 0;
    diag.crc_bad = 0;
    diagDown = true;
    await waitFor(async () =>
      (await page.locator('#quality-state').innerText()).includes('Browser-Verbindung'),
    );
    diagDown = false;
    await waitFor(async () =>
      (await page.locator('#quality-state').innerText()).includes('unauffällig'),
    );
    const screenshots = process.env.PROBUSX_SCREENSHOTS;
    if (screenshots) {
      state.version = '3.2.0';
      settings = {
        display_ms: 250,
        temperature_ms: 1000,
        output_ms: 1000,
        analysis_enabled: true,
        transmit_enabled: true,
      };
      fs.mkdirSync(screenshots, { recursive: true });
      await page.evaluate(() => {
        document.querySelector('.eyebrow').textContent = 'VORSCHAU · SIMULIERTE MESSWERTE';
      });
      await page.screenshot({
        path: path.join(screenshots, 'Analyse-Aufnahme.png'),
        fullPage: true,
      });
      console.log('Prüfung: Optionen öffnen');
      await page.click('a[href="/options"]');
      await page.waitForTimeout(1200);
      await page.evaluate(() => {
        document.querySelector('.eyebrow').textContent = 'VORSCHAU · SIMULIERTE MESSWERTE';
      });
      await page.screenshot({ path: path.join(screenshots, 'Optionen.png'), fullPage: true });
      await page.click('a[href="/"]');
      await page.waitForTimeout(1200);
      await page.evaluate(() => {
        document.querySelector('.eyebrow').textContent = 'VORSCHAU · SIMULIERTE MESSWERTE';
      });
      await page.screenshot({ path: path.join(screenshots, 'Regler.png'), fullPage: true });
      await page.click('a[href="/analysis"]');
      await page.waitForTimeout(1200);
    }
    await page.click('#analysis-toggle');
    await waitFor(async () => !settings.analysis_enabled);
    await waitFor(async () =>
      (await page.locator('#quality-state').innerText()).includes('ausgeschaltet'),
    );
    await page.click('#analysis-toggle');
    await waitFor(async () => settings.analysis_enabled);
    await page.click('a[href="/"]');
    await waitFor(async () => !(await page.locator('[data-key="special"]').isDisabled()));
    await page.click('[data-key="special"]');
    await waitFor(async () => lastKey === 'special');
    await page.click('#transmit-toggle');
    await waitFor(async () => await page.locator('[data-key="up"]').isDisabled());
    await page.reload();
    await waitFor(async () => !!(await page.locator('[data-key="up"]').isDisabled()));
    assert.equal(liveEnabled, false);
    console.log('Prüfung: Neustart');
    // Neustart: Abbruch der Rückfrage sendet nichts. Bestätigung wartet auf neuen Starttoken.
    console.log('Prüfung: Optionen öffnen');
    await page.click('a[href="/options"]');
    await waitFor(async () => !(await page.locator('#restart').isDisabled()));
    page.once('dialog', (d) => d.dismiss());
    await page.click('#restart');
    assert.equal(restartCount, 0);
    page.once('dialog', (d) => d.accept());
    await page.click('#restart');
    await waitFor(async () =>
      (await page.locator('#restart-message').innerText()).includes('Neustart abgeschlossen'),
    );
    assert.equal(restartCount, 1);
    state.capture.busy = true;
    await waitFor(async () => await page.locator('#restart').isDisabled());
    state.capture.busy = false;
    state.ota.pending_verify = true;
    await page.waitForTimeout(1200);
    assert(await page.locator('#restart').isDisabled());
    state.ota.pending_verify = false;
    await waitFor(async () => !(await page.locator('#restart').isDisabled()));
    // Alle drei Seiten müssen ohne Bedienung pausieren. Weiterlaufende Statusabrufe
    // dürfen die Frist nicht verlängern. HA erhält unabhängig davon weiterhin Antworten.
    for (const route of ['/', '/analysis', '/options']) {
      console.log('Prüfung: Bedienpause ' + route);
      const idlePage = await context.newPage();
      await idlePage.clock.install();
      await idlePage.goto('http://127.0.0.1:' + server.address().port + route);
      await waitFor(async () => !(await idlePage.locator('#restart').isDisabled()));
      // Die andere Seite schließen, damit sie die Zählung nicht verändert.
      if (!page.isClosed()) await page.close();
      if (route === '/analysis') {
        state.capture.busy = true;
        await waitFor(async () => await idlePage.locator('#restart').isDisabled());
        await idlePage.clock.fastForward(3601000);
        assert(!(await idlePage.locator('#idle-banner').isVisible()), 'Aufnahme muss weiterlaufen');
        state.capture.busy = false;
        await idlePage.clock.fastForward(2000);
        await waitFor(async () => await idlePage.locator('#idle-banner').isVisible());
        // Echte Bedienung setzt die Frist zurück. Reine Poll-Antworten tun das nicht.
        await idlePage.click('#idle-resume');
        await idlePage.clock.fastForward(2000);
      }
      console.log('Prüfung: Uhr vor Ablauf');
      await idlePage.clock.fastForward(route === '/analysis' ? 3596000 : 3599000);
      assert(!(await idlePage.locator('#idle-banner').isVisible()));
      console.log('Prüfung: Frist überschreiten oder fortsetzen');
      await idlePage.clock.fastForward(route === '/analysis' ? 3000 : 2000);
      assert(await idlePage.locator('#idle-banner').isVisible());
      await new Promise((r) => setTimeout(r, 150));
      const count = automaticCount;
      await idlePage.clock.fastForward(10000);
      await new Promise((r) => setTimeout(r, 300));
      assert.equal(automaticCount, count, 'Pausierte Seite darf keine Abrufe senden');
      const independent = await context.request.get(
        'http://127.0.0.1:' + server.address().port + '/api/live',
      );
      assert.equal(independent.status(), 200);
      await idlePage.click('#idle-resume');
      console.log('Prüfung: Frist überschreiten oder fortsetzen');
      await idlePage.clock.fastForward(2000);
      await waitFor(async () => automaticCount > count + 1);
      assert(!(await idlePage.locator('#idle-banner').isVisible()));
      await idlePage.close();
    }
    // Neue Seite für die bisherige Prüfung des Verbindungsfehlers.
    const finalPage = await context.newPage();
    await finalPage.goto('http://127.0.0.1:' + server.address().port);
    await waitFor(async () => !(await finalPage.locator('#start').isDisabled()));
    statusDown = true;
    await waitFor(async () =>
      (await finalPage.locator('#connection').innerText()).includes('unterbrochen'),
    );
    assert.equal(await finalPage.locator('#start').isDisabled(), true);
    assert.deepEqual(errors, []);
    console.log(
      'Bestanden: MQTT-Einrichtung, TLS-Wahl, Passworterhaltung und -löschung, Neuladen, Fehlerfall und Vorgangssperre; Neustart mit Rückfrage, Sperren und Wiederverbindung; einstündige Bedienpause auf allen drei Seiten und Fortsetzen; 44 einzelne Zugangspunkte, gleiche SSIDs, BSSID-Wahl und Speicherung, Neuladen, Zielausfall, automatische Wahl und aktuelle Signalstärke. Außerdem IP-Wechsel, PBXS-Download, Aufnahmesperre, HTTP-Fehler, Display, Diagnose, Optionen und Tasten. OTA-Fehlerwege und Startbestätigung bestanden.',
    );
  } finally {
    if (browser) await browser.close();
    server.closeAllConnections();
    await new Promise((resolve) => server.close(resolve));
    fs.rmSync(tmp, { recursive: true, force: true });
  }
})().catch((e) => {
  console.error(e);
  process.exitCode = 1;
});
