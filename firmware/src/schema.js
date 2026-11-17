'use strict';
// Das Schema gehört zur Bridge. Keine Speicherung nur im Browser oder in Home Assistant.
(() => {
  if (location.pathname !== '/schema') return;
  document.body.classList.add('schema-wide');
  const byId = (id) => document.getElementById(id);
  const channels = [];
  for (let i = 1; i <= 10; i++) channels.push({ id: 'S' + i, kind: 'temperature', index: i - 1 });
  ['R1', 'R2', 'R3', 'R4', 'R5', 'R6', 'R7', 'R0', 'HE1', 'HE2', 'HE3'].forEach((id, index) =>
    channels.push({ id, kind: 'output', index }),
  );
  for (let slot = 1; slot <= 3; slot++) {
    for (let i = 1; i <= 4; i++)
      channels.push({ id: `F${slot}S${i}`, kind: 'temperature', index: 9 + i, slot });
    ['R1', 'R2', 'R3', 'HE'].forEach((suffix, index) =>
      channels.push({ id: `F${slot}${suffix}`, kind: 'output', index: 11 + index, slot }),
    );
  }
  const catalog = new Map(channels.map((c) => [c.id, c]));
  let model = { revision: 0, image_revision: 0, items: [], storage_ready: false };
  let items = [],
    selected = '',
    editing = false,
    dirty = false,
    busy = false;
  let snapshot = null,
    receivedAt = 0,
    lastTemperature = 0,
    lastOutput = 0;
  let options = { temperature_ms: 1000, output_ms: 1000 };
  let imageLoaded = false,
    imageRevision = -1,
    initialized = false;
  const markers = new Map();
  const message = (text) => (byId('schema-message').textContent = text);
  function changed() {
    dirty = true;
    byId('schema-dirty').textContent = 'Änderungen noch nicht gespeichert.';
  }
  function controls() {
    const locked =
      busy || !!snapshot?.busy || !!state?.ota?.pending_verify || !!state?.restart_pending;
    byId('schema-edit').disabled = !model.storage_ready || locked;
    byId('schema-fields').disabled = locked;
    byId('schema-add').disabled = !imageLoaded || !byId('schema-channel').value;
    byId('schema-save').disabled = !dirty;
    byId('schema-editor').hidden = !editing;
    byId('schema-edit').textContent = editing ? 'Bearbeitung aktiv' : 'Schema bearbeiten';
    byId('schema-stage').classList.toggle('editing', editing);
  }
  function field(item) {
    byId('schema-selected').hidden = !item;
    if (!item) return;
    byId('schema-label').value = item.label;
    byId('schema-x').value = item.x.toFixed(1);
    byId('schema-y').value = item.y.toFixed(1);
  }
  function select(id) {
    selected = id;
    for (const [key, node] of markers) node.classList.toggle('selected', editing && key === id);
    for (const node of byId('schema-list').querySelectorAll('button[data-id]'))
      node.setAttribute('aria-pressed', String(node.dataset.id === id));
    field(items.find((item) => item.id === id));
  }
  function value(id) {
    const channel = catalog.get(id);
    if (!snapshot || !channel) return { text: '–', fresh: false };
    const elapsed = performance.now() - receivedAt;
    let age = snapshot.main_age_ms;
    if (channel.slot) {
      const module = snapshot.extensions?.find((entry) => entry.slot === channel.slot);
      if (channel.slot !== 1 || module?.supported === false) return { text: '–', fresh: false };
      age = module ? module.age_ms : snapshot.ext_age_ms;
    }
    const fresh = Number.isFinite(age) && age >= 0 && age + elapsed < 3000;
    const raw = (
      channel.kind === 'temperature' ? snapshot.temperatures_raw : snapshot.outputs_raw
    )?.[channel.index];
    if (!fresh || !Number.isFinite(raw) || (channel.kind === 'temperature' && raw === 2500))
      return { text: '–', fresh: false };
    const outputValue = raw / (channel.index >= 8 && channel.index <= 10 ? 100 : 2);
    if (channel.kind === 'output' && (outputValue < 0 || outputValue > 100))
      return { text: '–', fresh: false };
    return {
      text:
        channel.kind === 'temperature'
          ? (raw / 10).toLocaleString('de-DE', {
              minimumFractionDigits: 1,
              maximumFractionDigits: 1,
            }) + ' °C'
          : (raw / (channel.index >= 8 && channel.index <= 10 ? 100 : 2)).toFixed(0) + ' %',
      fresh: true,
      on: raw > 0,
    };
  }
  function drawValues(force = false) {
    const now = performance.now();
    for (const item of items) {
      const node = markers.get(item.id);
      if (!node) continue;
      const channel = catalog.get(item.id),
        output = channel.kind === 'output';
      const due =
        force ||
        (output
          ? now - lastOutput >= options.output_ms
          : now - lastTemperature >= options.temperature_ms);
      const current = value(item.id);
      if (!due && current.fresh) continue;
      node.querySelector('.schema-value').textContent = current.text;
      node.classList.toggle('stale', !current.fresh);
      node.classList.toggle('is-on', output && current.fresh && current.on);
      node.title = item.id + (current.fresh ? '' : ' · kein aktueller Wert');
    }
    if (force || now - lastTemperature >= options.temperature_ms) lastTemperature = now;
    if (force || now - lastOutput >= options.output_ms) lastOutput = now;
    byId('schema-freshness').textContent =
      snapshot && performance.now() - receivedAt < 3000
        ? 'Live-Werte · – bedeutet: kein aktueller Messwert oder Eingang nicht belegt.'
        : 'Keine aktuellen Live-Daten. Angezeigte Werte werden als veraltet markiert.';
  }
  function position(item, node) {
    node.style.left = item.x + '%';
    node.style.top = item.y + '%';
    node.hidden = !item.visible;
    node.querySelector('.schema-name').textContent = item.label || item.id;
  }
  function move(item, node, event) {
    const bounds = byId('schema-stage').getBoundingClientRect();
    if (!bounds.width || !bounds.height) return;
    const insetX = Math.min(49, (node.offsetWidth / bounds.width) * 50);
    const insetY = Math.min(49, (node.offsetHeight / bounds.height) * 50);
    item.x = Math.max(
      insetX,
      Math.min(100 - insetX, ((event.clientX - bounds.left) / bounds.width) * 100),
    );
    item.y = Math.max(
      insetY,
      Math.min(100 - insetY, ((event.clientY - bounds.top) / bounds.height) * 100),
    );
    position(item, node);
    field(item);
    changed();
    controls();
  }
  function rebuild() {
    byId('schema-markers').replaceChildren();
    byId('schema-list').replaceChildren();
    markers.clear();
    for (const item of items) {
      const node = document.createElement('div');
      node.className = 'schema-marker ' + catalog.get(item.id).kind;
      node.dataset.id = item.id;
      const name = document.createElement('span'),
        reading = document.createElement('strong');
      name.className = 'schema-name';
      reading.className = 'schema-value';
      node.append(name, reading);
      position(item, node);
      node.addEventListener('pointerdown', (event) => {
        if (!editing || busy || snapshot?.busy || event.button !== 0) return;
        event.preventDefault();
        select(item.id);
        node.setPointerCapture(event.pointerId);
      });
      node.addEventListener('pointermove', (event) => {
        if (node.hasPointerCapture(event.pointerId)) move(item, node, event);
      });
      node.addEventListener('pointerup', (event) => {
        if (node.hasPointerCapture(event.pointerId)) node.releasePointerCapture(event.pointerId);
      });
      markers.set(item.id, node);
      byId('schema-markers').append(node);
      const row = document.createElement('div'),
        check = document.createElement('input'),
        choose = document.createElement('button');
      row.className = 'schema-list-row';
      check.type = 'checkbox';
      check.checked = item.visible;
      check.setAttribute('aria-label', item.id + ' anzeigen');
      check.onchange = () => {
        item.visible = check.checked;
        position(item, node);
        changed();
        controls();
      };
      choose.type = 'button';
      choose.dataset.id = item.id;
      choose.textContent = item.id + (item.label ? ' · ' + item.label : '');
      choose.onclick = () => select(item.id);
      row.append(check, choose);
      byId('schema-list').append(row);
    }
    const selectBox = byId('schema-channel');
    const previous = selectBox.value;
    selectBox.replaceChildren();
    for (const channel of channels) {
      if (items.some((item) => item.id === channel.id)) continue;
      selectBox.add(
        new Option(channel.id + (channel.slot > 1 ? ' · noch nicht dekodiert' : ''), channel.id),
      );
    }
    if ([...selectBox.options].some((option) => option.value === previous))
      selectBox.value = previous;
    select(selected);
    drawValues(true);
    controls();
  }
  function imageStatus(next) {
    // Die Bildfläche passt in die verfügbare Bildschirmhöhe. Marker bleiben relativ zum Bild.
    if (next.image_width > 0 && next.image_height > 0)
      byId('schema-stage').style.setProperty(
        '--schema-ratio',
        next.image_width / next.image_height,
      );
    byId('schema-empty').hidden = !!next.image_revision;
    byId('schema-stage').hidden = !next.image_revision;
    if (imageRevision === next.image_revision) return;
    imageRevision = next.image_revision;
    imageLoaded = false;
    if (next.image_revision)
      byId('schema-image').src = '/api/schema/image?v=' + next.image_revision;
  }
  async function load(force = false) {
    const next = await automaticRead('/api/schema', 6000);
    if (!Array.isArray(next.items) || next.items.some((item) => !catalog.has(item.id)))
      throw new Error('Gespeichertes Schema ist ungültig.');
    imageStatus(next);
    if (force || (!editing && (!initialized || next.revision !== model.revision))) {
      model = next;
      initialized = true;
      items = next.items.map((item) => ({ ...item }));
      dirty = false;
      byId('schema-dirty').textContent = 'Keine ungespeicherten Änderungen.';
      rebuild();
    } else {
      model.storage_ready = next.storage_ready;
      model.image_revision = next.image_revision;
      model.max_image_bytes = next.max_image_bytes;
    }
    if (!next.storage_ready) message('Der Schema-Speicher ist auf diesem Gerät nicht verfügbar.');
    controls();
  }
  byId('schema-image').onload = () => {
    imageLoaded = true;
    controls();
  };
  byId('schema-image').onerror = () => {
    imageLoaded = false;
    imageRevision = -1;
    message('Schema-Bild konnte nicht geladen werden. Erneuter Versuch folgt.');
    controls();
  };
  byId('schema-edit').onclick = () => {
    editing = true;
    message('Bearbeiten: Werte im Bild ziehen oder über X und Y positionieren.');
    controls();
  };
  byId('schema-add').onclick = () => {
    const id = byId('schema-channel').value;
    if (!catalog.has(id) || items.some((item) => item.id === id)) return;
    items.push({
      id,
      x: 15 + (items.length % 5) * 17.5,
      y: 12 + Math.floor(items.length / 5) * 9,
      visible: true,
      label: '',
    });
    selected = id;
    changed();
    rebuild();
  };
  byId('schema-label').oninput = () => {
    const item = items.find((entry) => entry.id === selected);
    if (!item) return;
    item.label = byId('schema-label').value;
    position(item, markers.get(item.id));
    const button = [...byId('schema-list').querySelectorAll('button')].find(
      (entry) => entry.dataset.id === item.id,
    );
    button.textContent = item.id + (item.label ? ' · ' + item.label : '');
    changed();
    controls();
  };
  for (const axis of ['x', 'y'])
    byId('schema-' + axis).oninput = () => {
      const item = items.find((entry) => entry.id === selected),
        number = Number(byId('schema-' + axis).value);
      if (!item || !Number.isFinite(number) || number < 0 || number > 100) return;
      item[axis] = number;
      position(item, markers.get(item.id));
      changed();
      controls();
    };
  byId('schema-remove').onclick = () => {
    items = items.filter((item) => item.id !== selected);
    selected = '';
    changed();
    rebuild();
  };
  byId('schema-cancel').onclick = async () => {
    if (dirty && !confirm('Ungespeicherte Änderungen verwerfen?')) return;
    try {
      await load(true);
      editing = false;
      controls();
      message('');
    } catch (error) {
      message(error.message);
    }
  };
  async function writing(work) {
    busy = true;
    operationPending = true;
    controls();
    try {
      await work();
    } catch (error) {
      message(error.message);
    } finally {
      busy = false;
      operationPending = false;
      controls();
    }
  }
  byId('schema-save').onclick = () =>
    writing(async () => {
      if (items.some((item) => new TextEncoder().encode(item.label).length > 80))
        throw new Error('Bezeichnung zu lang. Höchstens 80 UTF-8-Bytes verwenden.');
      const response = await fetch('/api/schema', {
        method: 'POST',
        cache: 'no-store',
        headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': token },
        body: JSON.stringify({ version: 1, revision: model.revision, items }),
        signal: AbortSignal.timeout(30000),
      });
      const result = await response.json();
      if (!response.ok)
        throw new Error(result.error || 'Anordnung konnte nicht gespeichert werden.');
      model.revision = result.revision;
      dirty = false;
      byId('schema-dirty').textContent = 'Anordnung auf der Bridge gespeichert.';
      message('Anordnung gespeichert. Sie bleibt nach Neustarts erhalten.');
    });
  function blob(canvas, type, quality) {
    return new Promise((resolve, reject) =>
      canvas.toBlob(
        (data) =>
          data ? resolve(data) : reject(new Error('Bild konnte nicht umgewandelt werden.')),
        type,
        quality,
      ),
    );
  }
  byId('schema-upload').onclick = () =>
    writing(async () => {
      const file = byId('schema-file').files[0];
      if (!file) throw new Error('Zuerst ein Bild auswählen.');
      if (
        file.size > 20 * 1024 * 1024 ||
        !['image/png', 'image/jpeg', 'image/webp', 'image/bmp', 'image/x-ms-bmp'].includes(
          file.type,
        )
      )
        throw new Error('PNG, JPEG, WebP oder BMP mit höchstens 20 MiB auswählen.');
      if (
        model.image_revision &&
        !confirm('Das gespeicherte Hintergrundbild ersetzen? Die Positionen bleiben erhalten.')
      )
        return;
      message('Bild wird vorbereitet …');
      const bitmap = await createImageBitmap(file);
      let data;
      try {
        const ratio = Math.min(1, 2048 / bitmap.width, 2048 / bitmap.height);
        const canvas = document.createElement('canvas');
        canvas.width = Math.max(1, Math.round(bitmap.width * ratio));
        canvas.height = Math.max(1, Math.round(bitmap.height * ratio));
        const context = canvas.getContext('2d');
        context.fillStyle = '#fff';
        context.fillRect(0, 0, canvas.width, canvas.height);
        context.drawImage(bitmap, 0, 0, canvas.width, canvas.height);
        data = await blob(canvas, 'image/png');
        const limit = model.max_image_bytes || 1044480;
        for (const quality of [0.92, 0.8, 0.65]) {
          if (data.size <= limit) break;
          data = await blob(canvas, 'image/jpeg', quality);
        }
        if (data.size > limit)
          throw new Error('Bild zu groß. Ein einfacheres oder kleineres Bild auswählen.');
      } finally {
        bitmap.close();
      }
      await new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.open('POST', '/api/schema/image');
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');
        xhr.setRequestHeader('X-CSRF-Token', token);
        xhr.timeout = 120000;
        xhr.upload.onprogress = (event) =>
          message(
            event.lengthComputable && event.loaded < event.total
              ? 'Bild wird übertragen: ' + Math.round((event.loaded / event.total) * 100) + ' %'
              : 'Bild wird geprüft und gespeichert …',
          );
        xhr.onload = () => {
          let result = {};
          try {
            result = JSON.parse(xhr.responseText);
          } catch (_) {}
          if (xhr.status === 200 && result.accepted) resolve();
          else reject(new Error(result.error || 'Bild konnte nicht gespeichert werden.'));
        };
        xhr.onerror = xhr.ontimeout = () =>
          reject(
            new Error('Verbindung unterbrochen. Gespeicherten Stand nach Wiederverbindung prüfen.'),
          );
        xhr.send(data);
      });
      await load(false);
      byId('schema-file').value = '';
      message('Bild auf der Bridge gespeichert. Die Anordnung bei Bedarf separat speichern.');
    });
  window.addEventListener('beforeunload', (event) => {
    if (dirty) {
      event.preventDefault();
      event.returnValue = '';
    }
  });
  window.probusxSchema = {
    update(next, settings) {
      snapshot = next;
      receivedAt = performance.now();
      options = { ...options, ...settings };
      drawValues();
      controls();
    },
  };
  async function pollSchema() {
    if (automaticAllowed() && !busy) {
      try {
        await load();
        if (!editing && model.storage_ready)
          message(
            model.image_revision ? '' : 'Über „Schema bearbeiten“ ein Hintergrundbild hinzufügen.',
          );
      } catch (error) {
        message('Schema nicht erreichbar: ' + error.message);
      }
    }
    setTimeout(pollSchema, 5000);
  }
  setInterval(() => {
    if (automaticAllowed()) drawValues();
  }, 1000);
  pollSchema();
})();
