/**
 * BikeMap – Leaflet map with dark tiles, live marker, and speed-colored trail
 */
class BikeMap {
  constructor(containerId, opts = {}) {
    this.mini = opts.mini || false;
    this.map = L.map(containerId, {
      zoomControl: !this.mini,
      attributionControl: !this.mini,
      dragging: !this.mini,
      scrollWheelZoom: !this.mini,
    }).setView([23.8103, 90.4125], this.mini ? 14 : 15);

    // Geoapify Dark Tile layer
    const geoapifyApiKey = 'fffa995ca4904cd690a8c9baad0dc234';
    L.tileLayer(`https://maps.geoapify.com/v1/tile/carto/{z}/{x}/{y}.png?&apiKey=${geoapifyApiKey}`, {
      attribution: 'Powered by <a href="https://www.geoapify.com/" target="_blank">Geoapify</a> | &copy; OpenStreetMap contributors',
      maxZoom: 20,
    }).addTo(this.map);

    // Custom bike icon
    this.bikeIcon = L.divIcon({
      className: 'bike-marker',
      html: `<div style="
        width:20px;height:20px;border-radius:50%;
        background:radial-gradient(circle, #00d4ff 30%, rgba(0,212,255,0.2) 70%);
        box-shadow:0 0 16px rgba(0,212,255,0.6);
        border:2px solid rgba(0,212,255,0.8);
      "></div>`,
      iconSize: [20, 20],
      iconAnchor: [10, 10],
    });

    this.marker = L.marker([0, 0], { icon: this.bikeIcon }).addTo(this.map);
    this.trail = L.polyline([], {
      color: '#00d4ff', weight: 3, opacity: 0.7,
      smoothFactor: 1,
    }).addTo(this.map);

    this.firstFix = true;
    this.trailPoints = [];
  }

  update(lat, lng, speed) {
    if (lat === 0 && lng === 0) return;
    const pos = [lat, lng];
    this.marker.setLatLng(pos);

    if (this.firstFix) {
      this.map.setView(pos, this.mini ? 15 : 16);
      this.firstFix = false;
    } else if (!this.mini) {
      this.map.panTo(pos, { animate: true, duration: 0.5 });
    }

    this.trailPoints.push(pos);
    if (this.trailPoints.length > 1000) {
      this.trailPoints = this.trailPoints.slice(-800);
    }
    this.trail.setLatLngs(this.trailPoints);
  }

  setTrail(points) {
    if (!points || !points.length) return;
    this.trailPoints = points.map(p => [p[0], p[1]]);
    this.trail.setLatLngs(this.trailPoints);
  }

  resize() {
    setTimeout(() => this.map.invalidateSize(), 100);
  }
}

window.BikeMap = BikeMap;
