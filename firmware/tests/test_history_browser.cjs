// Prüft echte Webdateien und binäre Antworten aus dem C++-Verlaufsspeicher.
const { chromium } = require('playwright');
const fs = require('fs'),
  path = require('path'),
  http = require('http'),
  os = require('os');
const { execFileSync } = require('child_process');
const assert = require('assert/strict');
const root = path.resolve(__dirname, '../..'),
  src = path.join(root, 'firmware/src');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'pbx-history-')),
  fixture = path.join(tmp, 'fixture');
execFileSync('g++', [
  '-std=c++17',
  '-Wall',
  '-Wextra',
  '-Werror',
  '-I',
  src,
  path.join(root, 'tests/history_fixture.cpp'),
  '-o',
  fixture,
]);
const history = {
  enabled: true,
  flex1: true,
  flex2: false,
  flex3: false,
  interval_s: 10,
  now_ms: 864000000,
  oldest_ms: 860400000,
  newest_ms: 864000000,
  samples: 3601,
  used_bytes: 259272,
  capacity_bytes: 7340032,
  removed_samples: 42,
  capture_reserved: false,
  dropped_events: 0,
};
const settings = {
  display_ms: 250,
  temperature_ms: 1000,
  output_ms: 1000,
  analysis_enabled: false,
  transmit_enabled: true,
};
const state = {
  version: '3.6.1',
  token: 'test-token',
  history,
  capture: { active: false, busy: false, result: 'none', statistics: {} },
  ota: { pending_verify: false, updating: false },
  wifi: { connected: true, ssid: 'Test', ip: '127.0.0.1', networks: [], ap_mode: 'auto' },
  mqtt: { enabled: false, port: 1883 },
  restart_pending: false,
};
let requests = [],
  broken = false,
  delay = 0;
const server = http.createServer(async (req, res) => {
  const u = new URL(req.url, 'http://localhost');
  const json = (v, code = 200) => {
    res.writeHead(code, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(v));
  };
  if (['/logo.png', '/favicon.png', '/favicon.ico'].includes(u.pathname)) {
    res.writeHead(200, {
      'Content-Type': u.pathname.endsWith('.ico') ? 'image/x-icon' : 'image/png',
    });
    return res.end(fs.readFileSync(path.join(src, 'assets', u.pathname.slice(1))));
  }
  if (u.pathname === '/api/status') return json(state);
  if (u.pathname === '/api/history/status') return json({ history });
  if (u.pathname === '/api/history/data') {
    const args = ['from', 'to', 'limit'].map((k) => u.searchParams.get(k));
    requests.push(args.map(Number));
    let bytes = broken ? Buffer.from('bad') : execFileSync(fixture, args);
    assert(bytes.length <= 65568);
    if (!broken && history.samples === 0) {
      bytes = Buffer.from(bytes.subarray(0, 32));
      bytes.writeUInt32LE(0, 8);
    }
    const wait = delay;
    return setTimeout(() => {
      res.writeHead(200, { 'Content-Type': 'application/octet-stream' });
      res.end(bytes);
    }, wait);
  }
  if (u.pathname === '/api/live')
    return json({
      settings,
      token: state.token,
      tx_enabled: true,
      busy: false,
      flex1_enabled: history.flex1,
      main_age_ms: 0,
      ext_age_ms: history.flex1 ? 0 : -1,
      display_age_ms: 0,
      temperatures_raw: Array(14).fill(345),
      outputs_raw: [0, 200, 0, 0, 0, 0, 200, 0, 0, 0, 0, 0, 0, 200, 0],
    });
  if (u.pathname === '/api/display') {
    res.writeHead(200, { 'Content-Type': 'application/octet-stream' });
    return res.end(fs.readFileSync(path.join(root, 'tests/display.raw')));
  }
  if (u.pathname === '/api/history/settings') {
    let b = '';
    for await (const part of req) b += part;
    const f = new URLSearchParams(b);
    assert.equal(f.get('token'), state.token);
    assert.equal(f.get('flex2'), '0');
    assert.equal(f.get('flex3'), '0');
    history.interval_s = Number(f.get('interval_s'));
    history.enabled = f.get('enabled') === '1';
    history.flex1 = f.get('flex1') === '1';
    return json({ accepted: true });
  }
  const names = {
    '/': 'index.html',
    '/history': 'index.html',
    '/options': 'index.html',
    '/app.js': 'app.js',
    '/schema.js': 'schema.js',
    '/history.js': 'history.js',
    '/style.css': 'style.css',
  };
  if (names[u.pathname]) {
    const name = names[u.pathname];
    res.writeHead(200, {
      'Content-Type': name.endsWith('.js')
        ? 'text/javascript'
        : name.endsWith('.css')
          ? 'text/css'
          : 'text/html',
    });
    return res.end(fs.readFileSync(path.join(src, name)));
  }
  res.writeHead(404);
  res.end();
});
(async () => {
  let browser;
  try {
    await new Promise((r) => server.listen(0, '127.0.0.1', r));
    const base = 'http://127.0.0.1:' + server.address().port;
    browser = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
    const page = await browser.newPage({ viewport: { width: 1600, height: 1100 } });
    const errors = [];
    page.on('pageerror', (e) => errors.push(e.message));
    await page.goto(base);
    await page.waitForFunction(() =>
      document.querySelector('[data-value="t0"]')?.textContent.includes('34.5'),
    );
    assert.equal(await page.locator('#live-outputs tbody tr').count(), 11);
    assert.equal(await page.locator('#flex-outputs tbody tr').count(), 4);
    assert(
      !(await page
        .locator('#live-outputs')
        .textContent()
        .then((t) => t.includes('F1'))),
    );
    assert((await page.locator('#flex-outputs').textContent()).includes('100'));
    assert(!(await page.locator('#flex-modules').textContent()).includes('vorbereitet'));
    await page.goto(base + '/history');
    await page.waitForFunction(() =>
      document.querySelector('#history-message').textContent.includes('Min/Max'),
    );
    assert.equal(await page.locator('#temperature-legend input').count(), 14);
    assert.equal(await page.locator('#output-legend input').count(), 15);
    assert.equal(await page.locator('canvas.history-chart:visible').count(), 2);
    await page.locator('[data-history-none="temperature"]').click();
    assert.equal(await page.locator('#temperature-legend input:checked').count(), 0);
    await page.locator('#temperature-legend input').first().check();
    await page.reload();
    await page.waitForFunction(() =>
      document.querySelector('#history-message').textContent.includes('Min/Max'),
    );
    assert.equal(await page.locator('#temperature-legend input:checked').count(), 1);
    await page.selectOption('#history-preset', '604800000');
    await page.waitForFunction(
      () => document.querySelector('#history-preset').value === '604800000',
    );
    await page.waitForTimeout(200);
    assert.equal(requests.at(-1)[1] - requests.at(-1)[0], 604800000);
    await page.selectOption('#history-preset', '3600000');
    await page.waitForTimeout(200);
    const canvas = page.locator('#temperature-chart');
    const b = await canvas.boundingBox();
    await page.mouse.move(b.x + 150, b.y + 120);
    await page.mouse.down();
    await page.mouse.move(b.x + b.width - 150, b.y + 180, { steps: 8 });
    await page.mouse.up();
    await page.waitForTimeout(250);
    assert.equal(await page.locator('#history-preset').inputValue(), 'custom');
    assert(requests.at(-1)[1] - requests.at(-1)[0] < 3600000);
    await page.locator('#history-reset').click();
    await page.waitForTimeout(250);
    assert.equal(requests.at(-1)[1] - requests.at(-1)[0], 3600000);
    // Eine verspätete Antwort darf die neuere Auswahl nicht überschreiben.
    delay = 500;
    await page.selectOption('#history-preset', '604800000');
    delay = 0;
    await page.selectOption('#history-preset', '3600000');
    await page.waitForTimeout(700);
    assert.equal(await page.locator('#history-preset').inputValue(), '3600000');
    // Eigene Datumsgrenzen übernehmen und eine vertauschte Reihenfolge zurückweisen.
    const dates = await page.evaluate(() => {
      const to = new Date(document.querySelector('#history-to').value);
      const local = (d) => new Date(d - d.getTimezoneOffset() * 60000).toISOString().slice(0, 19);
      return { from: local(new Date(to.getTime() - 1800000)), to: local(to) };
    });
    await page.fill('#history-from', dates.from);
    await page.fill('#history-to', dates.to);
    await page.locator('#history-range-form button[type=submit]').click();
    await page.waitForTimeout(200);
    assert.equal(requests.at(-1)[1] - requests.at(-1)[0], 1800000);
    await page.fill('#history-from', dates.to);
    await page.fill('#history-to', dates.from);
    await page.locator('#history-range-form button[type=submit]').click();
    assert((await page.locator('#history-message').textContent()).includes('gültigen Zeitraum'));
    await page.selectOption('#history-preset', '3600000');
    await page.waitForTimeout(200);
    history.samples = 0;
    await page.locator('#history-refresh').click();
    await page.waitForFunction(() =>
      document.querySelector('#history-message').textContent.includes('Noch keine Messwerte'),
    );
    history.samples = 3601;
    broken = true;
    await page.locator('#history-refresh').click();
    await page.waitForFunction(() =>
      document.querySelector('#history-message').textContent.includes('Verlaufsformat ungültig'),
    );
    broken = false;
    await page.locator('#history-refresh').click();
    await page.waitForFunction(() =>
      document.querySelector('#history-message').textContent.includes('Min/Max'),
    );
    await page.screenshot({ path: path.join(tmp, 'verlauf.png'), fullPage: true });
    await page.goto(base + '/options');
    await page.waitForFunction(() => !document.querySelector('#history-options-fields').disabled);
    assert(await page.locator('#flex2-enabled').isDisabled());
    assert(await page.locator('#flex3-enabled').isDisabled());
    await page.fill('#history-interval', '30');
    await page.uncheck('#flex1-enabled');
    await page.locator('#history-options-form button[type=submit]').click();
    await page.waitForFunction(() =>
      document.querySelector('#history-options-message').textContent.includes('Gespeichert'),
    );
    assert.equal(history.interval_s, 30);
    assert.equal(history.flex1, false);
    await page.reload();
    await page.waitForFunction(() => document.querySelector('#history-interval').value === '30');
    assert(!(await page.locator('#flex1-enabled').isChecked()));
    state.capture.busy = true;
    await page.waitForFunction(() => document.querySelector('#history-options-fields').disabled);
    state.capture.busy = false;
    await page.goto(base);
    await page.waitForFunction(() => document.querySelector('#flex-modules').hidden);
    assert.deepEqual(errors, []);
    console.log(
      'Bestanden: Live-Tabellen, zwei Diagramme, echte C++-Aggregation, Kanalwahl, Woche/Stunde, Mauszoom, Zoom-Rücknahme, überholte Antworten, Binärfehler, gespeicherte Optionen, FlexBox-Sperren. Vorschau: ' +
        path.join(tmp, 'verlauf.png'),
    );
  } finally {
    await browser?.close();
    server.close();
    fs.rmSync(fixture, { force: true });
  }
})().catch((e) => {
  console.error(e);
  process.exitCode = 1;
});
