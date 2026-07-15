/**
 * Modern Hypercar-style SVG Gauges
 * Sleek track, glowing gradient stroke, big numbers.
 */

class SpeedGauge {
  constructor(containerId, options = {}) {
    this.container = document.getElementById(containerId);
    if (!this.container) return;

    this.options = {
      max: options.max || 60,
      gradientStart: options.gradientStart || '#00F0FF',
      gradientEnd: options.gradientEnd || '#0057FF',
      ...options
    };

    this.value = 0;
    this.init();
  }

  init() {
    this.container.innerHTML = '';
    
    // Create SVG
    this.svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    this.svg.setAttribute('viewBox', '0 0 200 200');
    this.svg.style.width = '100%';
    this.svg.style.height = '100%';
    this.svg.style.overflow = 'visible';

    // Defs for gradient and glow
    const defs = document.createElementNS('http://www.w3.org/2000/svg', 'defs');
    
    const gradId = `grad-${Math.random().toString(36).substr(2, 9)}`;
    const gradient = document.createElementNS('http://www.w3.org/2000/svg', 'linearGradient');
    gradient.setAttribute('id', gradId);
    gradient.setAttribute('x1', '0%');
    gradient.setAttribute('y1', '100%');
    gradient.setAttribute('x2', '100%');
    gradient.setAttribute('y2', '0%');
    
    const stop1 = document.createElementNS('http://www.w3.org/2000/svg', 'stop');
    stop1.setAttribute('offset', '0%');
    stop1.setAttribute('stop-color', this.options.gradientStart);
    
    const stop2 = document.createElementNS('http://www.w3.org/2000/svg', 'stop');
    stop2.setAttribute('offset', '100%');
    stop2.setAttribute('stop-color', this.options.gradientEnd);
    
    gradient.appendChild(stop1);
    gradient.appendChild(stop2);
    
    const filter = document.createElementNS('http://www.w3.org/2000/svg', 'filter');
    filter.setAttribute('id', 'glow');
    const blur = document.createElementNS('http://www.w3.org/2000/svg', 'feGaussianBlur');
    blur.setAttribute('stdDeviation', '6');
    blur.setAttribute('result', 'coloredBlur');
    const merge = document.createElementNS('http://www.w3.org/2000/svg', 'feMerge');
    const mergeNode1 = document.createElementNS('http://www.w3.org/2000/svg', 'feMergeNode');
    mergeNode1.setAttribute('in', 'coloredBlur');
    const mergeNode2 = document.createElementNS('http://www.w3.org/2000/svg', 'feMergeNode');
    mergeNode2.setAttribute('in', 'SourceGraphic');
    merge.appendChild(mergeNode1);
    merge.appendChild(mergeNode2);
    filter.appendChild(blur);
    filter.appendChild(merge);

    defs.appendChild(gradient);
    defs.appendChild(filter);
    this.svg.appendChild(defs);

    // Track arc
    this.track = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    this.track.setAttribute('d', this._describeArc(100, 100, 80, -140, 140));
    this.track.setAttribute('fill', 'none');
    this.track.setAttribute('stroke', 'rgba(255, 255, 255, 0.05)');
    this.track.setAttribute('stroke-width', '16');
    this.track.setAttribute('stroke-linecap', 'round');
    this.svg.appendChild(this.track);

    // Value arc
    this.path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    this.path.setAttribute('fill', 'none');
    this.path.setAttribute('stroke', `url(#${gradId})`);
    this.path.setAttribute('stroke-width', '16');
    this.path.setAttribute('stroke-linecap', 'round');
    this.path.setAttribute('filter', 'url(#glow)');
    
    // Set initial 0 length
    const totalLength = this.track.getTotalLength();
    this.path.setAttribute('stroke-dasharray', totalLength);
    this.path.setAttribute('stroke-dashoffset', totalLength);
    this.path.setAttribute('d', this._describeArc(100, 100, 80, -140, 140));
    this.path.style.transition = 'stroke-dashoffset 0.4s cubic-bezier(0.16, 1, 0.3, 1)';
    this.svg.appendChild(this.path);

    // Center Text Group
    const textGroup = document.createElementNS('http://www.w3.org/2000/svg', 'g');
    textGroup.setAttribute('transform', 'translate(100, 110)');

    this.valText = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    this.valText.setAttribute('text-anchor', 'middle');
    this.valText.setAttribute('font-family', "'Space Grotesk', sans-serif");
    this.valText.setAttribute('font-weight', '700');
    this.valText.setAttribute('font-size', '56px');
    this.valText.setAttribute('fill', '#ffffff');
    this.valText.textContent = '0';
    textGroup.appendChild(this.valText);

    const unitText = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    unitText.setAttribute('y', '25');
    unitText.setAttribute('text-anchor', 'middle');
    unitText.setAttribute('font-family', "'Outfit', sans-serif");
    unitText.setAttribute('font-weight', '600');
    unitText.setAttribute('font-size', '14px');
    unitText.setAttribute('fill', 'rgba(255,255,255,0.5)');
    unitText.setAttribute('letter-spacing', '2px');
    unitText.textContent = 'KM/H';
    textGroup.appendChild(unitText);

    this.svg.appendChild(textGroup);
    this.container.appendChild(this.svg);
  }

  set(val) {
    this.value = Math.max(0, Math.min(val, this.options.max));
    this.valText.textContent = this.value.toFixed(1);
    
    const percentage = this.value / this.options.max;
    const totalLength = this.track.getTotalLength();
    const dashoffset = totalLength - (totalLength * percentage);
    
    this.path.setAttribute('stroke-dashoffset', dashoffset);
  }

  // --- SVG Arc Math Helper ---
  _polarToCartesian(centerX, centerY, radius, angleInDegrees) {
    const angleInRadians = (angleInDegrees - 90) * Math.PI / 180.0;
    return {
      x: centerX + (radius * Math.cos(angleInRadians)),
      y: centerY + (radius * Math.sin(angleInRadians))
    };
  }

  _describeArc(x, y, radius, startAngle, endAngle) {
    const start = this._polarToCartesian(x, y, radius, endAngle);
    const end = this._polarToCartesian(x, y, radius, startAngle);
    const largeArcFlag = endAngle - startAngle <= 180 ? "0" : "1";
    return [
      "M", start.x, start.y, 
      "A", radius, radius, 0, largeArcFlag, 0, end.x, end.y
    ].join(" ");
  }
}

// Global initialization
window.hallGauge = new SpeedGauge('hall-gauge', { max: 60, gradientStart: '#FF3366', gradientEnd: '#FF9933' });
window.gpsGauge = new SpeedGauge('gps-gauge', { max: 60, gradientStart: '#00F0FF', gradientEnd: '#0057FF' });
