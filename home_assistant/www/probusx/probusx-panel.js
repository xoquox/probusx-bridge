/* Bedienfeld für Home Assistant. Home Assistant stellt die Kamerabilder bereit. */
class ProBusXPanel extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({ mode: 'open' });
    this.pending = false;
  }
  setConfig(config) {
    if (!config.camera) throw new Error('camera: Kamera-Entity angeben');
    this.config = { title: 'Heizungsregler', ...config };
    this.shadowRoot.innerHTML = `<style>
      :host{display:block}ha-card{display:block;padding:28px;background:var(--ha-card-background,var(--card-background-color,#fff));color:var(--primary-text-color,#20343b);border-radius:16px;box-shadow:var(--ha-card-box-shadow,0 2px 12px #0001)}
      header{display:flex;align-items:center;justify-content:space-between;margin-bottom:26px}h2{font-size:23px;margin:0;font-weight:600}small{color:var(--secondary-text-color,#617078)}
      .console{display:flex;align-items:center;justify-content:center;gap:52px;padding:32px;background:#69b7c5;border:1px solid #509baa;border-radius:9px}
      .lcd{position:relative;background:#fff;border:6px inset #8ccad3;width:480px;max-width:65%;aspect-ratio:5/4;flex-shrink:1}img{display:block;width:100%;height:100%;image-rendering:pixelated} .offline{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;background:#e8edef;color:#3b454a;text-align:center;padding:15px}.offline[hidden]{display:none}
      .pad{display:grid;grid-template:repeat(3,66px)/repeat(3,66px);gap:3px;flex-shrink:0;background:#dce4e5;border-radius:50%;padding:12px}
      button{border:0;background:transparent;color:#285964;font-size:32px;cursor:pointer;border-radius:12px}button:hover:enabled{background:#b6d7dd}button:active:enabled{background:#99c5ce}button:focus-visible{outline:3px solid #164f60}button:disabled{opacity:.35;cursor:default}
      [data-key=up]{grid-area:1/2}[data-key=left]{grid-area:2/1}[data-key=right]{grid-area:2/3}[data-key=down]{grid-area:3/2}[data-key=special]{grid-area:2/2;background:#fff8;font-size:12px;font-weight:700;border-radius:50%}
      footer{display:flex;justify-content:space-between;gap:16px;margin-top:18px;font-size:13px;color:var(--secondary-text-color,#617078)}#message{color:var(--error-color,#b94b37)}
    </style><ha-card><header><h2></h2><small>ProBusX · Live-Display</small></header><div class="console"><div class="lcd"><img alt="Live-Display des Heizungsreglers"><div class="offline">Warte auf Display …</div></div><div class="pad">
    <button data-key="up" title="Hoch" aria-label="Hoch">▲</button><button data-key="left" title="Links" aria-label="Links">◀</button><button data-key="special" title="Dreitastendruck: Grundeinstellungen" aria-label="Dreitastendruck">3 Tasten</button><button data-key="right" title="Rechts" aria-label="Rechts">▶</button><button data-key="down" title="Runter" aria-label="Runter">▼</button></div></div><footer><span>Mitteltaste: Grundeinstellungen</span><span id="message" role="status"></span></footer></ha-card>`;
    this.shadowRoot.querySelector('h2').textContent = this.config.title;
    for (const b of this.shadowRoot.querySelectorAll('button'))
      b.onclick = () => this.press(b.dataset.key);
    const img = this.shadowRoot.querySelector('img');
    img.onload = () => {
      this.loading = false;
      this.shadowRoot.querySelector('.offline').hidden = true;
    };
    img.onerror = () => {
      this.loading = false;
      this.offline('Display momentan nicht erreichbar');
    };
    this.update();
  }
  set hass(value) {
    this._hass = value;
    this.update();
  }
  connectedCallback() {
    if (!this.timer) this.timer = setInterval(() => this.refresh(), 50);
    this.refresh();
  }
  // Beim Entfernen der Karte alle Zeitgeber beenden.
  disconnectedCallback() {
    clearInterval(this.timer);
    this.timer = null;
    this.loading = false;
  }
  getCardSize() {
    return 7;
  }
  offline(message) {
    const el = this.shadowRoot.querySelector('.offline');
    if (el) {
      el.textContent = message;
      el.hidden = false;
    }
  }
  update() {
    if (!this.config || !this._hass) return;
    for (const b of this.shadowRoot.querySelectorAll('button')) {
      const entity = this._hass.states[this.config[b.dataset.key]];
      b.disabled =
        this.pending || !entity || entity.state === 'unavailable' || entity.state === 'unknown';
      // Eine noch nicht verwendete Taste kann trotz Verfügbarkeit den Zustand unknown haben.
      if (entity && entity.state === 'unknown') b.disabled = this.pending;
    }
    if (!this.loading) this.refresh();
  }
  refresh() {
    if (!this.isConnected || !this.config || !this._hass || document.hidden) return;
    const state = this._hass.states[this.config.camera];
    if (!state || state.state === 'unavailable') {
      this.offline('Kein aktuelles Display');
      return;
    }
    const source = state.attributes.entity_picture;
    if (!source || !source.startsWith('/api/camera_proxy/')) {
      this.offline('Kamerabild wird vorbereitet …');
      return;
    }
    const interval = Number(state.attributes.refresh_interval_ms ?? 250);
    const ms = Number.isFinite(interval) ? Math.max(100, Math.min(5000, interval)) : 250;
    this.shadowRoot.querySelector('small').textContent = 'ProBusX · Live-Display · ' + ms + ' ms';
    const now = Date.now();
    if (now - (this.lastRequest || 0) < ms) return;
    if (this.loading && now - this.lastRequest < 4000) return;
    this.loading = true;
    this.lastRequest = now;
    this.shadowRoot.querySelector('img').src =
      source + (source.includes('?') ? '&' : '?') + 'pbx_ts=' + now;
  }
  async press(key) {
    if (this.pending || !this.config[key]) return;
    this.pending = true;
    this.update();
    const message = this.shadowRoot.querySelector('#message');
    message.textContent = 'Taste wird übergeben …';
    try {
      await this._hass.callService('button', 'press', { entity_id: this.config[key] });
      message.textContent = '';
    } catch (error) {
      message.textContent = error.message || 'Taste konnte nicht gesendet werden';
    } finally {
      this.pending = false;
      this.update();
    }
  }
}
customElements.define('probusx-panel', ProBusXPanel);
window.customCards = window.customCards || [];
window.customCards.push({
  type: 'probusx-panel',
  name: 'ProBusX Bedienfeld',
  description: 'Reglerdisplay und fünf Bedientasten',
});
