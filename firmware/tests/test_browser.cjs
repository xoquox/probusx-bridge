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
let imageRequests = 0;
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
        { ssid: 'Küche & Hof', rssi: -42, open: false },
        { ssid: '<img src=x onerror=alert(1)>', rssi: -60, open: true },
      ];
      return json({ accepted: true }, 202);
    }
    if (url.pathname === '/api/wifi') {
      lastWifi = Object.fromEntries(form);
      state.wifi.ssid = form.get('ssid');
      state.wifi.ip = '192.168.178.123';
      state.wifi.connected = true;
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
  };
  if (assets[url.pathname]) {
    const types = {
      '/analysis': 'text/html',
      '/options': 'text/html',
      '/': 'text/html',
      '/app.js': 'text/javascript',
      '/style.css': 'text/css',
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
    await page.goto('http://127.0.0.1:' + server.address().port);
    await waitFor(async () => !(await page.locator('#start').isDisabled()));
    assert.match(await page.locator('#connection').innerText(), /Zugangspunkt/);
    await waitFor(async () => !(await page.locator('[data-key="up"]').isDisabled()));
    assert(await page.locator('#page-live').isVisible());
    assert(!(await page.locator('#page-options').isVisible()));
    const first = imageRequests;
    await page.waitForTimeout(1300);
    assert(imageRequests - first >= 4);
    await page.click('a[href="/options"]');
    await waitFor(async () => !(await page.locator('#options-fields').isDisabled()));
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
    await waitFor(async () => (await page.locator('#networks option').count()) === 3);
    assert.equal(await page.locator('#networks img').count(), 0);
    await page.selectOption('#networks', 'Küche & Hof');
    assert.equal(await page.inputValue('#ssid'), 'Küche & Hof');
    await page.fill('#password', 'A+B=%&abcdefgh');
    await page.locator('#wifi-form button[type=submit]').click();
    await waitFor(async () => (await page.inputValue('#password')) === '');
    assert.equal(lastWifi.ssid, 'Küche & Hof');
    assert.equal(lastWifi.password, 'A+B=%&abcdefgh');
    await waitFor(() => page.locator('#address-box').isVisible());
    assert.equal(await page.getAttribute('#address', 'href'), 'http://192.168.178.123/');
    await page.click('a[href="/analysis"]');
    await waitFor(async () => !(await page.locator('#start').isDisabled()));
    const downloadPromise = page.waitForEvent('download');
    await page.click('#start');
    const download = await downloadPromise;
    await waitFor(async () => !(await page.locator('#stop').isDisabled()));
    assert.equal(await page.locator('#ssid').isDisabled(), true);
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
    statusDown = true;
    await waitFor(async () =>
      (await page.locator('#connection').innerText()).includes('unterbrochen'),
    );
    assert.equal(await page.locator('#start').isDisabled(), true);
    assert.deepEqual(errors, []);
    console.log(
      'Bestanden: Zugangspunkt, WLAN-Suche und Speicherung, IP-Wechsel, PBXS-Download, Aufnahmesperre, HTTP-Fehler, Display, Diagnose, Optionen, Bildintervall, Analyseumschaltung und Tasten. OTA: falsche Datei, Übertragung, Prüfungsfehler, Vorgangssperre, Neustart und Bestätigung.',
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
