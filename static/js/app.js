/**
 * Bike Computer – Main Application Logic
 * Handles Socket.IO, navigation, and data binding
 */
(function () {
  'use strict';

  // ── Socket.IO ──
  const socket = io({ transports: ['websocket', 'polling'] });

  // ── DOM refs ──
  const $ = (sel) => document.querySelector(sel);
  const $$ = (sel) => document.querySelectorAll(sel);

  const pulseDot = $('.pulse-dot');
  const connText = $('#connection-text');
  const safetyBadge = $('#safety-badge');
  const safetyIcon = $('.safety-icon');
  const safetyText = $('#safety-text');
  const accidentModal = $('#accident-modal');

  // ── Initialize Components ──
  let hallGauge, gpsGauge, miniMap, fullMap, displayCtrl;

  function initComponents() {
    hallGauge = new SpeedGauge('hall-gauge', {
      id: 'hall', max: 60, unit: 'km/h',
      colorStart: '#00d4ff', colorEnd: '#00b8d4',
    });
    gpsGauge = new SpeedGauge('gps-gauge', {
      id: 'gps', max: 60, unit: 'km/h',
      colorStart: '#00e676', colorEnd: '#69f0ae',
    });
    miniMap = new BikeMap('mini-map', { mini: true });
    displayCtrl = new DisplayControl('oled-canvas');
  }

  // Full map lazy-init (only when tab is opened)
  function initFullMap() {
    if (fullMap) return;
    fullMap = new BikeMap('full-map', { mini: false });
  }

  // ── Navigation ──
  $$('.nav-item').forEach(btn => {
    btn.addEventListener('click', () => {
      const target = btn.dataset.screen;
      $$('.nav-item').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      $$('.screen').forEach(s => s.classList.remove('active'));
      $(`#screen-${target}`).classList.add('active');

      if (target === 'map') {
        initFullMap();
        if (fullMap) fullMap.resize();
      }
      if (target === 'dashboard') {
        miniMap.resize();
      }
    });
  });

  // ── Speed Source Toggle ──
  const btnGps = $('#btn-gps-source');
  const btnHall = $('#btn-hall-source');
  const gpsCard = $('#gps-gauge-card');
  const hallCard = $('#hall-gauge-card');

  if (btnGps && btnHall) {
    btnGps.addEventListener('click', () => {
      btnGps.classList.add('active');
      btnHall.classList.remove('active');
      gpsCard.style.display = 'flex';
      hallCard.style.display = 'none';
    });
    btnHall.addEventListener('click', () => {
      btnHall.classList.add('active');
      btnGps.classList.remove('active');
      hallCard.style.display = 'flex';
      gpsCard.style.display = 'none';
    });
  }

  // ── Safety Badge ──
  function updateSafety(status) {
    safetyBadge.className = 'safety-badge';
    switch (status) {
      case 'safe':
        safetyIcon.textContent = '🟢';
        safetyText.textContent = 'SAFE';
        break;
      case 'warning':
        safetyBadge.classList.add('warning');
        safetyIcon.textContent = '🟡';
        safetyText.textContent = 'WARNING';
        break;
      case 'danger':
        safetyBadge.classList.add('danger');
        safetyIcon.textContent = '🔴';
        safetyText.textContent = 'DANGER';
        break;
      case 'accident':
        safetyBadge.classList.add('accident');
        safetyIcon.textContent = '🚨';
        safetyText.textContent = 'ACCIDENT';
        break;
    }
  }

  // ── Format duration ──
  function fmtDuration(secs) {
    const h = Math.floor(secs / 3600);
    const m = Math.floor((secs % 3600) / 60);
    const s = secs % 60;
    return [h, m, s].map(v => String(v).padStart(2, '0')).join(':');
  }

  // ── Log state ──
  const logTbody = $('#log-tbody');
  let logCount = 0;

  // ── Update UI from telemetry ──
  function updateUI(data) {
    // Gauges
    if (hallGauge) hallGauge.set(data.hall_speed || 0);
    if (gpsGauge) gpsGauge.set(data.gps_speed || 0);

    // Info cards
    const tilt = data.tilt_angle || 0;
    const accel = data.total_accel || 0;
    $('#tilt-value').textContent = tilt.toFixed(0) + '°';
    $('#accel-value').textContent = accel.toFixed(2) + 'g';
    $('#rpm-value').textContent = (data.rpm || 0).toFixed(0);
    $('#sats-value').textContent = data.satellites || 0;

    // Coordinates
    if (data.lat && data.lng && !(data.lat === 0 && data.lng === 0)) {
      $('#coords-text').textContent =
        `${data.lat.toFixed(4)}°N, ${data.lng.toFixed(4)}°E`;
    }

    // Maps
    miniMap.update(data.lat, data.lng, data.gps_speed);
    if (fullMap) fullMap.update(data.lat, data.lng, data.gps_speed);

    // Map overlay
    $('#map-speed').textContent = (data.gps_speed || 0).toFixed(1);
    $('#map-sats').textContent = data.satellites || 0;

    // Safety
    updateSafety(data.status || 'safe');

    // Connection
    if (data.esp32_connected) {
      pulseDot.classList.add('connected');
      connText.textContent = 'ESP32 Connected';
    } else {
      pulseDot.classList.remove('connected');
      connText.textContent = 'ESP32 Offline';
    }

    // Display control preview
    if (displayCtrl) displayCtrl.setData(data);

    // Trip stats
    if (data.trip) {
      const t = data.trip;
      $('#trip-duration').textContent = fmtDuration(t.elapsed_seconds || 0);
      $('#trip-distance').textContent = (t.distance_km || 0).toFixed(2) + ' km';
      $('#max-speed').textContent = (t.max_gps_speed || 0).toFixed(1) + ' km/h';
      $('#avg-speed').textContent = (t.avg_speed || 0).toFixed(1) + ' km/h';
      $('#max-hall-speed').textContent = (t.max_hall_speed || 0).toFixed(1) + ' km/h';
      $('#accident-count').textContent = t.accident_count || 0;
      // Set trail on full map
      if (fullMap && t.trail && t.trail.length > 0) {
        fullMap.setTrail(t.trail);
      }
    }

    // Update 5-min Log
    if (logTbody && data.esp32_connected) {
      const tr = document.createElement('tr');
      tr.innerHTML = `
        <td>${data.timestamp}</td>
        <td>${(data.gps_speed || 0).toFixed(1)}</td>
        <td>${(data.hall_speed || 0).toFixed(1)}</td>
        <td>${(data.rpm || 0).toFixed(0)}</td>
        <td>${(data.tilt_angle || 0).toFixed(0)}</td>
        <td>${(data.total_accel || 0).toFixed(2)}</td>
        <td>${data.satellites || 0}</td>
        <td style="font-size: 0.65rem;">
          <a href="https://maps.google.com/?q=${data.lat},${data.lng}" target="_blank" style="color: var(--accent-cyan); text-decoration: none;">
            ${(data.lat || 0).toFixed(5)}, ${(data.lng || 0).toFixed(5)}
          </a>
        </td>
      `;
      logTbody.prepend(tr);
      logCount++;
      if (logCount > 300) {
        logTbody.lastElementChild.remove();
        logCount--;
      }
    }
  }

  // ── Socket Events ──
  socket.on('connect', () => {
    console.log('Socket.IO connected');
  });

  socket.on('disconnect', () => {
    pulseDot.classList.remove('connected');
    connText.textContent = 'Server Disconnected';
  });

  socket.on('telemetry_update', (data) => {
    updateUI(data);
  });

  socket.on('status_change', (data) => {
    updateSafety(data.status);
  });

  socket.on('accident_alert', (data) => {
    // Show accident modal
    accidentModal.classList.add('active');
    const locEl = $('#accident-location');
    if (locEl) {
      locEl.innerHTML = `
        📍 ${data.lat.toFixed(6)}, ${data.lng.toFixed(6)}<br>
        🕐 ${data.timestamp}<br>
        <a href="${data.maps_url}" target="_blank" style="color:var(--accent-cyan)">Open in Google Maps</a>
      `;
    }
    // Vibrate on mobile
    if (navigator.vibrate) {
      navigator.vibrate([200, 100, 200, 100, 400]);
    }
  });

  socket.on('display_changed', (cfg) => {
    if (displayCtrl) displayCtrl.setConfig(cfg);
  });

  // ── Accident dismiss ──
  const dismissBtn = $('#dismiss-accident');
  if (dismissBtn) {
    dismissBtn.addEventListener('click', () => {
      accidentModal.classList.remove('active');
    });
  }

  // ── Trip reset ──
  const resetBtn = $('#reset-trip');
  if (resetBtn) {
    resetBtn.addEventListener('click', () => {
      fetch('/trip/reset', { method: 'POST' })
        .then(() => {
          if (fullMap) {
            fullMap.trailPoints = [];
            fullMap.trail.setLatLngs([]);
          }
        })
        .catch(e => console.error('Trip reset failed:', e));
    });
  }

  // ── History Logic ──
  const historyTbody = $('#history-tbody');
  const historyFilterDate = $('#history-filter-date');
  const btnFilterHistory = $('#btn-filter-history');
  let fullHistoryData = [];

  function renderHistory(data) {
    if (!historyTbody) return;
    historyTbody.innerHTML = '';
    if (data.length === 0) {
      historyTbody.innerHTML = `<tr><td colspan="4" style="text-align: center; color: var(--text-muted);">No history data found.</td></tr>`;
      return;
    }
    // Reverse to show newest first
    data.slice().reverse().forEach(row => {
      const tr = document.createElement('tr');
      tr.innerHTML = `
        <td>${row.timestamp.split(' ')[1]}</td>
        <td>${row.gps_speed.toFixed(1)}</td>
        <td>${row.hall_speed.toFixed(1)}</td>
        <td style="font-size: 0.65rem;">
          <a href="https://maps.google.com/?q=${row.lat},${row.lng}" target="_blank" style="color: var(--accent-cyan); text-decoration: none;">
            ${row.lat.toFixed(4)}, ${row.lng.toFixed(4)}
          </a>
        </td>
      `;
      historyTbody.appendChild(tr);
    });
  }

  function fetchHistory() {
    fetch('/api/history')
      .then(res => res.json())
      .then(data => {
        fullHistoryData = data;
        if (historyFilterDate && historyFilterDate.value) {
           const filtered = fullHistoryData.filter(row => row.timestamp.startsWith(historyFilterDate.value));
           renderHistory(filtered);
        } else {
           renderHistory(fullHistoryData);
        }
      })
      .catch(err => console.error("Error fetching history:", err));
  }

  if (btnFilterHistory && historyFilterDate) {
    // Default filter to today
    const today = new Date().toISOString().split('T')[0];
    historyFilterDate.value = today;
    
    btnFilterHistory.addEventListener('click', () => {
      const filterVal = historyFilterDate.value;
      if (!filterVal) {
        renderHistory(fullHistoryData);
        return;
      }
      const filtered = fullHistoryData.filter(row => row.timestamp.startsWith(filterVal));
      renderHistory(filtered);
    });
  }

  // Initial fetch
  setTimeout(fetchHistory, 500);

  // ── Init ──
  document.addEventListener('DOMContentLoaded', initComponents);
  if (document.readyState !== 'loading') initComponents();
})();
