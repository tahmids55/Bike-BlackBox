/**
 * DisplayControl – OLED preview canvas + template management
 */
class DisplayControl {
  constructor(canvasId) {
    this.canvas = document.getElementById(canvasId);
    this.ctx = this.canvas.getContext('2d');
    this.template = 'default';
    this.data = {};
    this.brightness = 255;
    this.customMsg = '';
    this._setupListeners();
    this.render();
  }

  _setupListeners() {
    // Template cards
    document.querySelectorAll('.template-card').forEach(card => {
      card.addEventListener('click', () => {
        document.querySelectorAll('.template-card').forEach(c => c.classList.remove('active'));
        card.classList.add('active');
        this.template = card.dataset.template;
        const customGrp = document.getElementById('custom-message-group');
        if (customGrp) customGrp.style.display = this.template === 'custom' ? 'block' : 'none';
        this._sendConfig();
        this.render();
      });
    });

    // Brightness slider
    const slider = document.getElementById('brightness-slider');
    const valEl = document.getElementById('brightness-value');
    if (slider) {
      slider.addEventListener('input', () => {
        this.brightness = parseInt(slider.value);
        if (valEl) valEl.textContent = Math.round(this.brightness / 255 * 100) + '%';
        this._sendConfig();
        this.render();
      });
    }

    // Custom message send
    const sendBtn = document.getElementById('send-message');
    const msgInput = document.getElementById('custom-message');
    if (sendBtn && msgInput) {
      sendBtn.addEventListener('click', () => {
        this.customMsg = msgInput.value;
        this._sendConfig();
        this.render();
      });
    }
  }

  _sendConfig() {
    fetch('/display', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        template: this.template,
        brightness: this.brightness,
        custom_message: this.customMsg,
      }),
    }).catch(e => console.error('Display config send failed:', e));
  }

  setData(d) {
    this.data = d;
    this.render();
  }

  setConfig(cfg) {
    this.template = cfg.template || 'default';
    this.brightness = cfg.brightness != null ? cfg.brightness : 255;
    this.customMsg = cfg.custom_message || '';
    // Update UI
    document.querySelectorAll('.template-card').forEach(c => {
      c.classList.toggle('active', c.dataset.template === this.template);
    });
    const slider = document.getElementById('brightness-slider');
    if (slider) slider.value = this.brightness;
    const valEl = document.getElementById('brightness-value');
    if (valEl) valEl.textContent = Math.round(this.brightness / 255 * 100) + '%';
    const customGrp = document.getElementById('custom-message-group');
    if (customGrp) customGrp.style.display = this.template === 'custom' ? 'block' : 'none';
    this.render();
  }

  render() {
    const ctx = this.ctx;
    const w = this.canvas.width;
    const h = this.canvas.height;
    const alpha = this.brightness / 255;

    // Clear to black
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, w, h);

    // OLED cyan color matching the hardware
    const textColor = `rgba(50, 210, 255, ${alpha})`;
    ctx.fillStyle = textColor;
    ctx.textBaseline = 'top';

    const scale = w / 128; // canvas is 256px, OLED is 128px
    const font = (size) => `bold ${size * scale}px "JetBrains Mono", monospace`;

    const d = this.data;
    const rpm = d.rpm != null ? d.rpm.toFixed(0) : '0';
    const hall = d.hall_speed != null ? d.hall_speed.toFixed(1) : '0.0';
    const gps = d.gps_speed != null ? d.gps_speed.toFixed(1) : '0.0';
    const sats = d.satellites != null ? d.satellites : '0';
    const tilt = d.tilt_angle != null ? d.tilt_angle.toFixed(0) : '0';
    const acc = d.total_accel != null ? d.total_accel.toFixed(2) : '0.00';

    switch (this.template) {
      case 'default':
        ctx.font = font(8);
        ctx.fillText(`RPM:${rpm}  Hall:${hall}km/h`, 2 * scale, 2 * scale);
        ctx.fillText(`GPS:${gps}km/h  Sats:${sats}`, 2 * scale, 12 * scale);
        ctx.fillText(`Tilt:${tilt} deg`, 2 * scale, 22 * scale);
        ctx.fillText(`Acc: ${acc} g`, 2 * scale, 32 * scale);
        // separator
        ctx.fillStyle = textColor;
        ctx.fillRect(0, 42 * scale, w, 1 * scale);
        ctx.fillText(`WiFi:OK`, 2 * scale, 46 * scale);
        if (d.status === 'accident' || d.status === 'danger') {
          ctx.fillStyle = '#000';
          ctx.fillRect(65 * scale, 44 * scale, 63 * scale, 20 * scale);
          ctx.fillStyle = textColor;
          ctx.fillRect(65 * scale, 44 * scale, 63 * scale, 20 * scale);
          ctx.fillStyle = '#000';
          ctx.font = font(9);
          ctx.fillText('ACCIDENT', 70 * scale, 48 * scale);
        }
        break;

      case 'speed_only':
        ctx.font = font(24);
        ctx.textAlign = 'center';
        ctx.fillText(hall, w / 2, 6 * scale);
        ctx.font = font(9);
        ctx.fillText('km/h  (Hall)', w / 2, 36 * scale);
        ctx.font = font(12);
        ctx.fillText(`GPS: ${gps}`, w / 2, 50 * scale);
        ctx.textAlign = 'left';
        break;

      case 'gps_only':
        ctx.font = font(8);
        const lat = d.lat != null ? d.lat.toFixed(6) : '0.000000';
        const lng = d.lng != null ? d.lng.toFixed(6) : '0.000000';
        ctx.fillText(`Lat: ${lat}`, 2 * scale, 4 * scale);
        ctx.fillText(`Lng: ${lng}`, 2 * scale, 16 * scale);
        ctx.fillText(`Speed: ${gps} km/h`, 2 * scale, 30 * scale);
        ctx.fillText(`Sats: ${sats}`, 2 * scale, 42 * scale);
        break;

      case 'minimal':
        ctx.font = font(32);
        ctx.textAlign = 'center';
        ctx.fillText(parseFloat(hall).toFixed(0), w / 2, 10 * scale);
        ctx.font = font(10);
        ctx.fillText('km/h', w / 2, 48 * scale);
        ctx.textAlign = 'left';
        break;

      case 'custom':
        ctx.font = font(12);
        ctx.textAlign = 'center';
        ctx.fillText(this.customMsg || '...', w / 2, 24 * scale);
        ctx.textAlign = 'left';
        break;

      case 'off':
        // all black – display off
        break;
    }

    // Border for OLED look
    ctx.strokeStyle = `rgba(80, 80, 80, 0.4)`;
    ctx.lineWidth = 1;
    ctx.strokeRect(0, 0, w, h);
  }
}

window.DisplayControl = DisplayControl;
