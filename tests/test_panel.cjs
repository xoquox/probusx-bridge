const { chromium } = require('playwright');
const fs = require('fs'),
  path = require('path');
(async () => {
  const browser = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
  const page = await browser.newPage({
    viewport: { width: 1280, height: 800 },
    deviceScaleFactor: 1,
  });
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  let images = 0;
  await page.route('http://probusx.test/**', async (route) => {
    if (route.request().url().includes('/api/camera_proxy/')) {
      images++;
      return route.fulfill({
        contentType: 'image/png',
        body: fs.readFileSync(path.join(__dirname, 'display.png')),
      });
    }
    return route.fulfill({
      contentType: 'text/html',
      body: '<html><head><meta charset="utf-8"></head><body style="margin:0;background:#edf1f4;font:16px Arial;color:#243b44"><header style="padding:24px 38px;background:#fff;border-bottom:1px solid #dbe3e7">Home Assistant <span style="margin-left:38px;color:#78909b">Heizung</span></header><main style="max-width:1080px;margin:32px auto"><probusx-panel></probusx-panel><p style="font-size:13px;color:#70818a">Vorschau mit Displaybild aus deiner Busaufzeichnung · keine Live-Verbindung</p></main></body></html>',
    });
  });
  await page.goto('http://probusx.test/');
  await page.addScriptTag({
    path: path.join(__dirname, '../home_assistant/www/probusx/probusx-panel.js'),
  });
  await page.evaluate(() => {
    const p = document.querySelector('probusx-panel');
    p.setConfig({
      camera: 'camera.probusx_display',
      up: 'button.up',
      right: 'button.right',
      down: 'button.down',
      left: 'button.left',
      special: 'button.special',
    });
    window.presses = [];
    const states = {
      'camera.probusx_display': {
        state: 'idle',
        attributes: { entity_picture: '/api/camera_proxy/camera.probusx_display?token=test' },
      },
    };
    for (const k of ['up', 'right', 'down', 'left', 'special'])
      states['button.' + k] = { state: 'unknown', attributes: {} };
    p.hass = {
      states,
      callService: async (domain, action, args) => window.presses.push({ domain, action, args }),
    };
  });
  await page.waitForTimeout(2300);
  if (images < 6 || images > 11) throw Error('Aktualisierung alle 250 ms erwartet: ' + images);
  await page.evaluate(() => {
    const p = document.querySelector('probusx-panel');
    p._hass.states[p.config.camera].attributes.refresh_interval_ms = 1000;
  });
  const before = images;
  await page.waitForTimeout(2200);
  if (images - before < 1 || images - before > 3)
    throw Error('Geändertes Intervall wurde nicht übernommen');
  await page.locator('[data-key="special"]').click();
  const presses = await page.evaluate(() => window.presses);
  if (presses.length !== 1 || presses[0].args.entity_id !== 'button.special')
    throw Error('Falscher Dienst für den Dreitastendruck');
  await page.screenshot({
    path: path.join(__dirname, '../Bedienfeld_Vorschau.png'),
    fullPage: true,
  });
  await page.evaluate(() => {
    const p = document.querySelector('probusx-panel');
    p.hass = {
      states: { 'camera.probusx_display': { state: 'unavailable', attributes: {} } },
      callService: async () => {},
    };
  });
  if (!(await page.locator('[data-key="up"]').isDisabled()))
    throw Error('Taste ist trotz fehlender Verbindung aktiv');
  if (!(await page.locator('.offline').isVisible()))
    throw Error('Veraltetes Kamerabild wird nicht ausgeblendet');
  await page.evaluate(() => document.querySelector('probusx-panel').remove());
  const count = images;
  await page.waitForTimeout(1200);
  if (images !== count) throw Error('Abruf läuft nach Entfernen der Karte weiter');
  if (errors.length) throw Error(errors.join('\n'));
  console.log(
    'Bestanden: Bedienfeld, Bildaktualisierung, Sondertaste, Verbindungsausfall, Tastensperre und Zeitgeberabbau',
  );
  await browser.close();
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
