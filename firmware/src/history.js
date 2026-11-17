'use strict';
// Zwei lokale Canvas-Graphen. Messwerte kommen als begrenzte Min/Max-Pakete von der Bridge.
(() => {
  const byId = (id) => document.getElementById(id);
  let touched = false;
  window.probusxHistoryOptions = {
    update(value, locked) {
      byId('history-options-fields').disabled = locked || !value;
      if (!value || touched) return;
      byId('history-enabled').checked = value.enabled;
      byId('history-interval').value = value.interval_s;
      byId('flex1-enabled').checked = value.flex1;
    },
  };
  byId('history-options-form').addEventListener('input', () => {
    touched = true;
  });
  byId('history-options-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    if (byId('history-options-fields').disabled) return;
    const saved = await action('/api/history/settings', {
      enabled: byId('history-enabled').checked ? '1' : '0',
      interval_s: byId('history-interval').value,
      flex1: byId('flex1-enabled').checked ? '1' : '0',
      flex2: '0',
      flex3: '0',
    });
    if (saved) {
      touched = false;
      byId('history-options-message').textContent =
        'Gespeichert. Verlauf und FlexBox-Auswahl sind übernommen.';
    }
  });
  if (location.pathname !== '/history') return;
  const tempNames = Array.from({ length: 10 }, (_, i) => 'S' + (i + 1)).concat([
    'F1S1',
    'F1S2',
    'F1S3',
    'F1S4',
  ]);
  const outNames = [
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
  const colors = [
    '#087f8c',
    '#d1495b',
    '#5267b1',
    '#348449',
    '#b35d16',
    '#9751a3',
    '#4e6775',
    '#a17300',
    '#147e65',
    '#c74680',
    '#4558c5',
    '#956837',
    '#0081b4',
    '#996b9a',
    '#344c5d',
  ];
  const missing = -32768;
  let meta = null,
    anchor = 0,
    range = null,
    baseRange = null,
    basePreset = '86400000';
  let packet = null,
    generation = 0,
    controller = null,
    drag = null,
    hover = null;
  let renderQueued = false;
  const charts = [
    {
      kind: 'temperature',
      names: tempNames,
      offset: 0,
      canvas: byId('temperature-chart'),
      legend: byId('temperature-legend'),
    },
    {
      kind: 'output',
      names: outNames,
      offset: 14,
      canvas: byId('output-chart'),
      legend: byId('output-legend'),
    },
  ];
  function remember(chart) {
    try {
      localStorage.setItem('probusx-history-' + chart.kind, JSON.stringify([...chart.selected]));
    } catch (_) {}
  }
  for (const chart of charts) {
    chart.selected = new Set(chart.names.map((_, i) => i));
    try {
      const saved = JSON.parse(localStorage.getItem('probusx-history-' + chart.kind));
      if (Array.isArray(saved))
        chart.selected = new Set(
          saved.filter((i) => Number.isInteger(i) && i >= 0 && i < chart.names.length),
        );
    } catch (_) {}
    chart.names.forEach((name, i) => {
      const label = document.createElement('label');
      label.className = 'chart-channel';
      const input = document.createElement('input');
      input.type = 'checkbox';
      input.checked = chart.selected.has(i);
      input.dataset.channel = name;
      const swatch = document.createElement('span');
      swatch.className = 'chart-swatch';
      swatch.style.backgroundColor = colors[i];
      label.append(input, swatch, document.createTextNode(name));
      chart.legend.append(label);
      input.addEventListener('change', () => {
        if (input.checked) chart.selected.add(i);
        else chart.selected.delete(i);
        remember(chart);
        drawAll();
      });
    });
    const choose = (on) => {
      chart.selected = new Set(on ? chart.names.map((_, i) => i) : []);
      chart.legend.querySelectorAll('input').forEach((input, i) => {
        input.checked = chart.selected.has(i);
      });
      remember(chart);
      drawAll();
    };
    document.querySelector('[data-history-all="' + chart.kind + '"]').onclick = () => choose(true);
    document.querySelector('[data-history-none="' + chart.kind + '"]').onclick = () =>
      choose(false);
  }
  function dateInput(epoch) {
    const date = new Date(epoch);
    return new Date(epoch - date.getTimezoneOffset() * 60000).toISOString().slice(0, 19);
  }
  function timeLabel(ms, date = false) {
    return new Date(anchor + ms).toLocaleString(
      'de-DE',
      date
        ? { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit' }
        : { hour: '2-digit', minute: '2-digit', second: '2-digit' },
    );
  }
  function inputs() {
    if (!range) return;
    byId('history-from').value = dateInput(anchor + range.from);
    byId('history-to').value = dateInput(anchor + range.to);
  }
  function presetRange() {
    if (!meta) return;
    const value = byId('history-preset').value;
    if (value === 'custom') return;
    range = {
      from: value === 'all' ? meta.oldest_ms : Math.max(0, meta.now_ms - Number(value)),
      to: meta.now_ms,
    };
    if (range.from >= range.to) range.from = Math.max(0, range.to - 1);
    baseRange = { ...range };
    basePreset = value;
    inputs();
  }
  async function request(url, binary = false, owner = controller) {
    const signal = AbortSignal.any([owner.signal, AbortSignal.timeout(12000)]);
    const response = await fetch(url, { cache: 'no-store', signal });
    if (!response.ok) {
      const error = await response.json().catch(() => ({}));
      throw new Error(error.error || 'HTTP ' + response.status);
    }
    return binary ? await response.arrayBuffer() : await response.json();
  }
  // Feste Größen und Grenzen prüfen, bevor eine Antwort als Messwerte verwendet wird.
  function decode(buffer) {
    const d = new DataView(buffer);
    if (
      buffer.byteLength < 32 ||
      d.getUint32(0, true) !== 0x31484250 ||
      ![1, 2].includes(d.getUint32(4, true)) ||
      d.getUint32(12, true) !== 256
    )
      throw new Error('Verlaufsformat ungültig');
    const n = d.getUint32(8, true);
    if (n > 1024 || buffer.byteLength !== 32 + n * 256)
      throw new Error('Verlaufsdaten unvollständig');
    const result = {
      from: Number(d.getBigUint64(16, true)),
      to: Number(d.getBigUint64(24, true)),
      buckets: [],
    };
    for (let i = 0; i < n; i++) {
      const at = 32 + i * 256;
      const row = {
        first: Number(d.getBigUint64(at, true)),
        last: Number(d.getBigUint64(at + 8, true)),
        gap: d.getUint32(at + 248, true),
        count: d.getUint32(at + 252, true),
        low: [],
        high: [],
        start: [],
        end: [],
      };
      for (const [name, offset] of [
        ['low', 16],
        ['high', 74],
        ['start', 132],
        ['end', 190],
      ])
        for (let c = 0; c < 29; c++) row[name].push(d.getInt16(at + offset + 2 * c, true));
      result.buckets.push(row);
    }
    return result;
  }
  async function load() {
    if (!automaticAllowed()) return;
    const id = ++generation;
    controller?.abort();
    controller = new AbortController();
    const ownController = controller;
    automaticRequests.add(ownController);
    byId('history-message').textContent = 'Verlauf wird geladen …';
    try {
      const next = (await request('/api/history/status', false, ownController)).history;
      if (id !== generation) return;
      if (!next) throw new Error('Verlaufsstatus fehlt');
      if (meta && next.now_ms < meta.now_ms) {
        range = null;
        packet = null;
      }
      meta = next;
      anchor = Date.now() - meta.now_ms;
      if (!range || byId('history-preset').value !== 'custom') presetRange();
      if (!range) {
        byId('history-preset').value = '86400000';
        presetRange();
      }
      const n = Math.min(256, Math.max(128, Math.round(charts[0].canvas.clientWidth)));
      const buffer = await request(
        '/api/history/data?from=' +
          Math.floor(range.from) +
          '&to=' +
          Math.ceil(range.to) +
          '&limit=' +
          n,
        true,
        ownController,
      );
      if (id !== generation) return;
      packet = decode(buffer);
      const oldest = meta.samples ? timeLabel(meta.oldest_ms, true) : 'noch keine Messwerte';
      byId('history-status').textContent =
        (meta.enabled ? 'Aufzeichnung aktiv' : 'Aufzeichnung pausiert') +
        ' · ' +
        meta.samples.toLocaleString('de-DE') +
        ' Messpunkte · RAM ' +
        humanBytes(meta.used_bytes) +
        ' / ' +
        humanBytes(meta.capacity_bytes) +
        ' · Beginn: ' +
        oldest +
        (meta.capture_reserved ? ' · Rohaufnahme: 2 MiB reserviert' : '') +
        (meta.removed_samples
          ? ' · Älteste Messpunkte ersetzt: ' + meta.removed_samples.toLocaleString('de-DE')
          : '');
      if (meta.dropped_events)
        byId('history-status').textContent += ' · Verworfene Busereignisse: ' + meta.dropped_events;
      byId('history-message').textContent = meta.samples
        ? 'Min/Max-Bereiche erhalten kurze Ausschläge auch bei großen Zeiträumen.'
        : 'Noch keine Messwerte für den Verlauf vorhanden.';
      drawAll();
    } catch (error) {
      if (id === generation && !ownController.signal.aborted)
        byId('history-message').textContent = 'Verlauf nicht erreichbar: ' + error.message;
    } finally {
      automaticRequests.delete(ownController);
    }
  }
  function value(raw, index) {
    return raw === missing
      ? null
      : index < 14
        ? raw / 10
        : raw / (index >= 22 && index <= 24 ? 100 : 2);
  }
  function draw(chart) {
    const rect = chart.canvas.getBoundingClientRect();
    if (!rect.width) return;
    const width = rect.width,
      height = rect.height,
      ratio = window.devicePixelRatio || 1;
    chart.canvas.width = Math.round(width * ratio);
    chart.canvas.height = Math.round(height * ratio);
    const ctx = chart.canvas.getContext('2d');
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
    ctx.clearRect(0, 0, width, height);
    const plot = { left: 58, right: width - 18, top: 20, bottom: height - 38 };
    chart.plot = plot;
    let low = chart.kind === 'output' ? 0 : Infinity,
      high = chart.kind === 'output' ? 100 : -Infinity;
    if (packet && chart.kind === 'temperature')
      for (const row of packet.buckets)
        for (const index of chart.selected) {
          const a = value(row.low[index + chart.offset], index + chart.offset),
            b = value(row.high[index + chart.offset], index + chart.offset);
          if (a !== null) {
            low = Math.min(low, a);
            high = Math.max(high, b);
          }
        }
    if (!Number.isFinite(low)) {
      low = 0;
      high = 100;
    }
    if (chart.kind === 'temperature') {
      const margin = Math.max(2, (high - low) * 0.08);
      low -= margin;
      high += margin;
    }
    if (high <= low) high = low + 1;
    const y = (v) => plot.bottom - ((v - low) / (high - low)) * (plot.bottom - plot.top);
    const x = (ms) =>
      plot.left + ((ms - packet.from) / (packet.to - packet.from)) * (plot.right - plot.left);
    ctx.font = '12px system-ui';
    ctx.textBaseline = 'middle';
    for (let i = 0; i <= 5; i++) {
      const at = plot.top + ((plot.bottom - plot.top) * i) / 5;
      ctx.strokeStyle = '#dce5e8';
      ctx.beginPath();
      ctx.moveTo(plot.left, at);
      ctx.lineTo(plot.right, at);
      ctx.stroke();
      ctx.fillStyle = '#546a74';
      ctx.textAlign = 'right';
      ctx.fillText(
        (high - ((high - low) * i) / 5).toFixed(chart.kind === 'output' ? 0 : 1),
        plot.left - 9,
        at,
      );
    }
    ctx.textAlign = 'left';
    ctx.fillText(chart.kind === 'output' ? '%' : '°C', 8, 12);
    if (!packet) return;
    for (let i = 0; i <= 4; i++) {
      const ms = packet.from + ((packet.to - packet.from) * i) / 4;
      ctx.textAlign = i === 0 ? 'left' : i === 4 ? 'right' : 'center';
      ctx.fillText(timeLabel(ms, packet.to - packet.from > 86400000), x(ms), height - 14);
    }
    ctx.save();
    ctx.beginPath();
    ctx.rect(plot.left, plot.top, plot.right - plot.left, plot.bottom - plot.top);
    ctx.clip();
    for (const index of chart.selected) {
      const channel = index + chart.offset;
      let previous = null;
      ctx.strokeStyle = colors[index];
      ctx.fillStyle = colors[index];
      ctx.lineWidth = 1.7;
      for (const row of packet.buckets) {
        const a = value(row.low[channel], channel),
          b = value(row.high[channel], channel),
          first = value(row.start[channel], channel),
          last = value(row.end[channel], channel);
        if (!row.count) continue; // Leere Zeitkörbe sind keine Messlücke.
        if (a === null) {
          previous = null;
          continue;
        }
        const firstX = x(row.first),
          lastX = x(row.last),
          mid = (firstX + lastX) / 2;
        if (a !== b) {
          ctx.globalAlpha = 0.13;
          ctx.fillRect(firstX, y(b), Math.max(1, lastX - firstX), Math.max(1, y(a) - y(b)));
          ctx.globalAlpha = 0.65;
          ctx.beginPath();
          ctx.moveTo(mid, y(a));
          ctx.lineTo(mid, y(b));
          ctx.stroke();
          ctx.globalAlpha = 1;
        }
        const gap = (row.gap & (1 << channel)) !== 0;
        ctx.beginPath();
        if (previous && !gap) {
          ctx.moveTo(previous.x, y(previous.value));
          if (chart.kind === 'output') ctx.lineTo(firstX, y(previous.value));
          ctx.lineTo(firstX, y(first));
        } else ctx.moveTo(firstX, y(first));
        if (!gap) {
          if (chart.kind === 'output') ctx.lineTo(lastX, y(first));
          ctx.lineTo(lastX, y(last));
          ctx.stroke();
          if (firstX === lastX) {
            ctx.beginPath();
            ctx.arc(lastX, y(last), 1.6, 0, Math.PI * 2);
            ctx.fill();
          }
        } else {
          ctx.arc(lastX, y(last), 1.8, 0, Math.PI * 2);
          ctx.fill();
        }
        previous = gap ? null : { x: lastX, value: last };
      }
    }
    if (drag) {
      ctx.fillStyle = '#1478ab';
      ctx.globalAlpha = 0.17;
      ctx.fillRect(
        x(Math.min(drag.start, drag.end)),
        plot.top,
        Math.max(1, Math.abs(x(drag.end) - x(drag.start))),
        plot.bottom - plot.top,
      );
      ctx.globalAlpha = 1;
    }
    if (hover !== null) {
      ctx.strokeStyle = '#72858e';
      ctx.lineWidth = 1;
      ctx.setLineDash([3, 3]);
      ctx.beginPath();
      ctx.moveTo(x(hover), plot.top);
      ctx.lineTo(x(hover), plot.bottom);
      ctx.stroke();
      ctx.setLineDash([]);
    }
    ctx.restore();
    if (!chart.selected.size) {
      ctx.fillStyle = '#546a74';
      ctx.textAlign = 'center';
      ctx.fillText('Keine Kanäle ausgewählt', width / 2, height / 2);
    } else if (!packet.buckets.some((row) => row.count)) {
      ctx.fillStyle = '#546a74';
      ctx.textAlign = 'center';
      ctx.fillText('Keine Messwerte in diesem Zeitraum', width / 2, height / 2);
    }
  }
  function drawAll() {
    if (renderQueued) return;
    renderQueued = true;
    requestAnimationFrame(() => {
      renderQueued = false;
      for (const chart of charts) draw(chart);
    });
  }
  function pointerTime(chart, event) {
    if (!packet || !chart.plot) return null;
    const rect = chart.canvas.getBoundingClientRect(),
      p = chart.plot;
    return (
      packet.from +
      Math.max(0, Math.min(1, (event.clientX - rect.left - p.left) / (p.right - p.left))) *
        (packet.to - packet.from)
    );
  }
  function tooltip(chart, ms) {
    if (!packet) return;
    // Version 2 lässt leere Zeitkörbe weg. Nach tatsächlicher Messzeit suchen.
    if (!packet.buckets.length) {
      byId(chart.kind + '-tooltip').textContent = timeLabel(ms, true) + ' · Keine Messwerte';
      return;
    }
    let low = 0,
      high = packet.buckets.length - 1;
    while (low < high) {
      const mid = (low + high) >> 1;
      if (packet.buckets[mid].last < ms) low = mid + 1;
      else high = mid;
    }
    let row = packet.buckets[low];
    if (low && ms < row.first && ms - packet.buckets[low - 1].last < row.first - ms)
      row = packet.buckets[low - 1];
    const values = [];
    for (const selected of chart.selected) {
      const channel = selected + chart.offset,
        a = value(row.low[channel], channel),
        b = value(row.high[channel], channel);
      if (a !== null)
        values.push(
          chart.names[selected] +
            ': ' +
            a.toLocaleString('de-DE', { maximumFractionDigits: 2 }) +
            (a !== b ? '–' + b.toLocaleString('de-DE', { maximumFractionDigits: 2 }) : '') +
            (chart.kind === 'output' ? ' %' : ' °C'),
        );
    }
    byId(chart.kind + '-tooltip').textContent =
      timeLabel(ms, true) + ' · ' + (values.join(' · ') || 'Keine Messwerte');
  }
  for (const chart of charts) {
    chart.canvas.addEventListener('pointerdown', (event) => {
      if (event.button !== 0) return;
      const ms = pointerTime(chart, event);
      if (ms === null) return;
      drag = { start: ms, end: ms, clientX: event.clientX };
      chart.canvas.setPointerCapture(event.pointerId);
      drawAll();
    });
    chart.canvas.addEventListener('pointermove', (event) => {
      const ms = pointerTime(chart, event);
      if (ms === null) return;
      hover = ms;
      if (drag) drag.end = ms;
      for (const c of charts) tooltip(c, ms);
      drawAll();
    });
    chart.canvas.addEventListener('pointerup', (event) => {
      if (!drag) return;
      const selection = drag;
      drag = null;
      chart.canvas.releasePointerCapture(event.pointerId);
      if (
        Math.abs(event.clientX - selection.clientX) >= 8 &&
        Math.abs(selection.end - selection.start) >= 1000
      ) {
        range = {
          from: Math.floor(Math.min(selection.start, selection.end)),
          to: Math.ceil(Math.max(selection.start, selection.end)),
        };
        byId('history-preset').value = 'custom';
        inputs();
        load();
      } else drawAll();
    });
    chart.canvas.addEventListener('pointercancel', () => {
      drag = null;
      hover = null;
      drawAll();
    });
    chart.canvas.addEventListener('pointerleave', () => {
      if (!drag) {
        hover = null;
        drawAll();
      }
    });
  }
  byId('history-preset').addEventListener('change', () => {
    if (byId('history-preset').value !== 'custom') {
      presetRange();
      load();
    }
  });
  for (const id of ['history-from', 'history-to'])
    byId(id).addEventListener('input', () => {
      byId('history-preset').value = 'custom';
    });
  byId('history-range-form').addEventListener('submit', (event) => {
    event.preventDefault();
    const from = new Date(byId('history-from').value).getTime() - anchor,
      to = new Date(byId('history-to').value).getTime() - anchor;
    if (
      !Number.isFinite(from) ||
      !Number.isFinite(to) ||
      from < 0 ||
      from >= to ||
      !meta ||
      to > meta.now_ms + 60000
    ) {
      byId('history-message').textContent =
        'Bitte einen gültigen Zeitraum seit dem Gerätestart wählen.';
      return;
    }
    range = { from: Math.round(from), to: Math.round(to) };
    baseRange = { ...range };
    basePreset = 'custom';
    byId('history-preset').value = 'custom';
    load();
  });
  byId('history-reset').onclick = () => {
    byId('history-preset').value = basePreset;
    if (basePreset === 'custom') {
      range = baseRange ? { ...baseRange } : null;
      inputs();
    } else presetRange();
    load();
  };
  byId('history-refresh').onclick = load;
  new ResizeObserver(drawAll).observe(byId('page-history'));
  load();
  setInterval(() => {
    if (!document.hidden && automaticAllowed()) load();
  }, 15000);
})();
