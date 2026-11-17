// Prüft Schema-Bedienung mit einem HTTP-Gerätemodell und einem echten Browser.
const { chromium } = require('playwright');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
let revision = 0,
  imageRevision = 0,
  storedImage = null,
  items = [],
  uploads = 0,
  reject = false;
let raw = 823,
  age = 10,
  currentToken = 'schema-test',
  busy = false;
const settings = {
  temperature_ms: 1000,
  output_ms: 1000,
  display_ms: 250,
  analysis_enabled: true,
  transmit_enabled: true,
};
const live = () => ({
  settings,
  token: currentToken,
  busy,
  tx_enabled: true,
  main_age_ms: age,
  ext_age_ms: age,
  poll_age_ms: 10,
  temperatures_raw: [raw, 789, 2500, ...Array(11).fill(321)],
  outputs_raw: [200, 0, 0, 0, 0, 0, 0, 0, 1500, 0, 0, 0, 0, 200, 0],
});
const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://local');
  if (['/logo.png', '/favicon.png', '/favicon.ico'].includes(url.pathname)) {
    res.writeHead(200, {
      'Content-Type': url.pathname.endsWith('.ico') ? 'image/x-icon' : 'image/png',
    });
    return res.end(fs.readFileSync(path.join(root, 'src/assets', url.pathname.slice(1))));
  }

  function json(data, status = 200) {
    res.writeHead(status, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(data));
  }
  if (url.pathname === '/api/status')
    return json({
      token: currentToken,
      version: '3.4.0',
      capture: { busy, active: false, statistics: {} },
      ota: {},
      wifi: { networks: [] },
    });
  if (url.pathname === '/api/live') return json(live());
  if (url.pathname === '/api/schema' && req.method === 'GET')
    return json({
      storage_ready: true,
      revision,
      image_revision: imageRevision,
      image_width: 1000,
      image_height: 600,
      max_image_bytes: 1044480,
      items,
    });
  if (url.pathname === '/api/schema/image' && req.method === 'GET') {
    if (!storedImage) return json({ error: 'Kein Bild' }, 404);
    res.writeHead(200, { 'Content-Type': 'image/png' });
    return res.end(storedImage);
  }
  if (req.method === 'POST') {
    const chunks = [];
    for await (const chunk of req) chunks.push(chunk);
    assert.equal(req.headers['x-csrf-token'], currentToken);
    const buffer = Buffer.concat(chunks);
    if (busy) return json({ error: 'Vorgang läuft' }, 409);
    if (reject)
      return json({ error: 'Speichern fehlgeschlagen. Bisheriger Stand bleibt erhalten.' }, 500);
    if (url.pathname === '/api/schema/image') {
      assert.equal(req.headers['content-type'], 'application/octet-stream');
      assert(buffer.length <= 1044480);
      assert.equal(buffer.subarray(1, 4).toString(), 'PNG');
      storedImage = buffer;
      imageRevision++;
      uploads++;
      return json({ accepted: true, revision: imageRevision });
    }
    assert.equal(req.headers['content-type'], 'application/json');
    const body = JSON.parse(buffer);
    if (body.revision !== revision)
      return json(
        { error: 'Die Anordnung wurde inzwischen geändert. Neu laden und erneut bearbeiten.' },
        409,
      );
    items = body.items;
    revision++;
    return json({ accepted: true, revision });
  }
  const assets = {
    '/schema': 'index.html',
    '/app.js': 'app.js',
    '/schema.js': 'schema.js',
    '/history.js': 'history.js',
    '/style.css': 'style.css',
  };
  if (assets[url.pathname]) {
    res.writeHead(200, {
      'Content-Type': url.pathname.endsWith('.js')
        ? 'text/javascript'
        : url.pathname.endsWith('.css')
          ? 'text/css'
          : 'text/html',
      'Content-Security-Policy':
        "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' blob:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'",
    });
    return res.end(fs.readFileSync(path.join(root, 'src', assets[url.pathname])));
  }
  res.writeHead(404);
  res.end();
});
async function until(check) {
  const deadline = Date.now() + 12000;
  while (Date.now() < deadline) {
    if (await check()) return;
    await new Promise((r) => setTimeout(r, 80));
  }
  throw new Error('Erwarteter Schemazustand fehlt');
}
(async () => {
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  let browser;
  try {
    browser = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
    const context = await browser.newContext({ viewport: { width: 1600, height: 1200 } });
    const errors = [];
    context.on('page', (p) => p.on('pageerror', (error) => errors.push(error.message)));
    const page = await context.newPage();
    const url = 'http://127.0.0.1:' + server.address().port + '/schema';
    await page.goto(url);
    await until(async () => !(await page.locator('#schema-edit').isDisabled()));
    assert(await page.locator('#page-schema').isVisible());
    assert(!(await page.locator('#page-live').isVisible()));
    await page.click('#schema-edit');
    // Im Browser ein einfaches Heizungsbild erzeugen. Keine privaten Benutzerdaten im Test.
    const png = await page.evaluate(() => {
      const c = document.createElement('canvas');
      c.width = 1000;
      c.height = 600;
      const x = c.getContext('2d');
      x.fillStyle = '#fff';
      x.fillRect(0, 0, 1000, 600);
      x.strokeStyle = '#b74b38';
      x.lineWidth = 8;
      x.strokeRect(120, 120, 170, 300);
      x.strokeRect(650, 90, 200, 390);
      x.beginPath();
      x.moveTo(290, 180);
      x.lineTo(650, 180);
      x.stroke();
      x.strokeStyle = '#287ead';
      x.beginPath();
      x.moveTo(290, 360);
      x.lineTo(650, 360);
      x.stroke();
      x.fillStyle = '#333';
      x.font = '28px sans-serif';
      x.fillText('Kessel', 160, 470);
      x.fillText('Speicher', 690, 530);
      return c.toDataURL('image/png').split(',')[1];
    });
    await page.setInputFiles('#schema-file', {
      name: 'schema.png',
      mimeType: 'image/png',
      buffer: Buffer.from(png, 'base64'),
    });
    await page.click('#schema-upload');
    await until(
      async () => imageRevision === 1 && !(await page.locator('#schema-add').isDisabled()),
    );
    assert.equal(uploads, 1);
    await page.selectOption('#schema-channel', 'S1');
    await page.click('#schema-add');
    await page.fill('#schema-label', 'Speicher <oben>');
    const marker = page.locator('.schema-marker[data-id="S1"]');
    const stage = await page.locator('#schema-stage').boundingBox();
    const origin = await marker.boundingBox();
    await page.mouse.move(origin.x + origin.width / 2, origin.y + origin.height / 2);
    await page.mouse.down();
    await page.mouse.move(stage.x + stage.width * 0.7, stage.y + stage.height * 0.3, { steps: 8 });
    await page.mouse.up();
    assert(Math.abs(Number(await page.inputValue('#schema-x')) - 70) < 1);
    assert(Math.abs(Number(await page.inputValue('#schema-y')) - 30) < 1);
    assert.equal(await marker.locator('.schema-name').innerText(), 'Speicher <oben>');
    assert.equal(await marker.locator('oben').count(), 0);
    await until(async () => (await marker.innerText()).includes('82,3 °C'));
    await page.selectOption('#schema-channel', 'R1');
    await page.click('#schema-add');
    await page.fill('#schema-x', '25');
    await page.fill('#schema-y', '60');
    await until(async () =>
      (await page.locator('.schema-marker[data-id="R1"]').innerText()).includes('100 %'),
    );
    await page.selectOption('#schema-channel', 'HE1');
    await page.click('#schema-add');
    await until(async () =>
      (await page.locator('.schema-marker[data-id="HE1"]').innerText()).includes('15 %'),
    );
    await page.selectOption('#schema-channel', 'S3');
    await page.click('#schema-add');
    assert.equal(await page.locator('.schema-marker[data-id="S3"] .schema-value').innerText(), '–');
    await page.selectOption('#schema-channel', 'F3S1');
    await page.click('#schema-add');
    assert.equal(
      await page.locator('.schema-marker[data-id="F3S1"] .schema-value').innerText(),
      '–',
    );
    await page.getByRole('checkbox', { name: 'R1 anzeigen', exact: true }).uncheck();
    assert(!(await page.locator('.schema-marker[data-id="R1"]').isVisible()));
    await page.click('#schema-save');
    await until(() => revision === 1);
    await until(async () =>
      (await page.locator('#schema-message').innerText()).includes('Anordnung gespeichert'),
    );
    currentToken = 'after-restart'; // Gespeicherter Stand bei neuem Starttoken bleibt.
    await page.reload();
    await until(async () =>
      (await page.locator('.schema-marker[data-id="S1"]').innerText()).includes('Speicher <oben>'),
    );
    assert(!(await page.locator('.schema-marker[data-id="R1"]').isVisible()));
    const second = await context.newPage();
    await second.goto(url);
    await until(async () =>
      (await second.locator('.schema-marker[data-id="S1"]').innerText()).includes(
        'Speicher <oben>',
      ),
    );
    raw = 850;
    await until(async () =>
      (await second.locator('.schema-marker[data-id="S1"]').innerText()).includes('85,0 °C'),
    );
    age = 5000;
    await until(async () => (await marker.locator('.schema-value').innerText()) === '–');
    age = 10;
    await page.click('#schema-edit');
    await page.getByRole('checkbox', { name: 'R1 anzeigen', exact: true }).check();
    await page.locator('#schema-list button[data-id="S1"]').click();
    await page.fill('#schema-label', 'Nicht speichern');
    reject = true;
    await page.click('#schema-save');
    await until(async () =>
      (await page.locator('#schema-message').innerText()).includes('Speichern fehlgeschlagen'),
    );
    assert.equal(items.find((i) => i.id === 'S1').label, 'Speicher <oben>');
    reject = false;
    // Ein anderer Client hat die Anordnung geändert. Der alte Editor darf sie nicht überschreiben.
    revision++;
    await page.click('#schema-save');
    await until(async () =>
      (await page.locator('#schema-message').innerText()).includes('inzwischen geändert'),
    );
    page.once('dialog', (d) => d.accept());
    await page.click('#schema-cancel');
    await until(async () => !(await page.locator('#schema-editor').isVisible()));
    assert.equal(await marker.locator('.schema-name').innerText(), 'Speicher <oben>');
    // Bildersatz abbrechen: der gespeicherte Hintergrund bleibt.
    await page.click('#schema-edit');
    await page.setInputFiles('#schema-file', {
      name: 'anderes.png',
      mimeType: 'image/png',
      buffer: Buffer.from(png, 'base64'),
    });
    page.once('dialog', (d) => d.dismiss());
    await page.click('#schema-upload');
    assert.equal(uploads, 1);
    await page.waitForTimeout(100);
    busy = true;
    await page.waitForTimeout(1500);
    await until(async () => await page.locator('#schema-file').isDisabled());
    busy = false;
    await until(async () => !(await page.locator('#schema-file').isDisabled()));
    await page.getByRole('checkbox', { name: 'R1 anzeigen', exact: true }).check();
    await page.click('#schema-save');
    await until(() => revision === 3);
    await page.click('#schema-cancel');
    // Relative Koordinaten bleiben bei veränderter Fenstergröße gleich.
    await page.setViewportSize({ width: 1250, height: 1000 });
    assert.equal(await marker.evaluate((node) => node.style.left), '70%');
    // Das komplette Schema steht bei üblichen Desktopgrößen ohne Seitenscrollen im Bild.
    for (const viewport of [
      { width: 1366, height: 768 },
      { width: 1920, height: 1080 },
      { width: 1250, height: 1000 },
    ]) {
      await page.setViewportSize(viewport);
      await page.evaluate(() => window.scrollTo(0, 0));
      const bounds = await page.locator('#schema-stage').boundingBox();
      const imageBounds = await page.locator('#schema-image').boundingBox();
      const sidebarBounds = await page.locator('.sidebar').boundingBox();
      assert(bounds.y < 120, 'Das Schema beginnt zu weit unten');
      assert(bounds.y + bounds.height < viewport.height, 'Schema reicht unter die Bildschirmkante');
      assert(sidebarBounds.x + sidebarBounds.width < bounds.x, 'Navigation steht nicht links');
      assert(
        Math.abs(bounds.width - imageBounds.width - 2) < 1,
        'Markerfläche passt nicht zum Bild',
      );
      assert(
        Math.abs(imageBounds.width / imageBounds.height - 1000 / 600) < 0.01,
        'Bild ist verzerrt',
      );
      assert.equal(
        await page.evaluate(() => document.documentElement.scrollWidth > innerWidth),
        false,
      );
      assert.equal(await page.locator('#schema-message').isVisible(), false);
      assert.equal(await marker.evaluate((node) => node.style.left), '70%');
    }
    assert.equal(
      await page.getByText('Live-Werte direkt im eigenen Schema.', { exact: true }).count(),
      0,
    );
    assert.equal(await page.locator('.eyebrow').count(), 0);
    const screenshot = process.env.PROBUSX_SCHEMA_SCREENSHOT;
    if (screenshot) await page.screenshot({ path: screenshot, fullPage: true });
    await second.close();
    let automatic = 0;
    page.on('request', (req) => {
      if (req.url().includes('/api/')) automatic++;
    });
    await page.clock.install();
    await page.clock.fastForward(3601000);
    await until(async () => await page.locator('#idle-banner').isVisible());
    await new Promise((r) => setTimeout(r, 200));
    const before = automatic;
    await page.clock.fastForward(10000);
    await new Promise((r) => setTimeout(r, 200));
    assert.equal(automatic, before);
    await page.click('#idle-resume');
    await page.clock.fastForward(6000);
    await until(() => automatic > before);
    assert.deepEqual(errors, []);
    console.log(
      'Bestanden: linke Navigation, volle Bildsicht bei 1366×768, 1920×1080 und 1250×1000, unverzerrte Markerfläche; Schema-Bild gespeichert, freie Positionen, Bezeichnungen, Sichtbarkeit, Sensor-/Ausgangswerte, ungültige/alte Werte, Neustart und zweiter Browser, Speicherfehler, Änderungskonflikt, Uploadabbruch, Vorgangssperre, Skalierung und Bedienpause.',
    );
  } finally {
    if (browser) await browser.close();
    server.closeAllConnections();
    await new Promise((resolve) => server.close(resolve));
  }
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
