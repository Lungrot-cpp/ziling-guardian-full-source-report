(() => {
  const owner = parent !== globalThis && new URLSearchParams(location.search).get('embedded') === '1' && parent.vfPrivacy ? parent : globalThis;
  let enabled = Boolean(owner.vfPrivacy?.enabled);
  const small = document.createElement('canvas');
  small.width = 32; small.height = 18;
  const pixels = small.getContext('2d');
  const image = document.getElementById('stream-image'), video = document.getElementById('stream-video');
  const overlay = image ? document.createElement('canvas') : null;
  if (overlay) {
    overlay.width = 960; overlay.height = 540; overlay.hidden = true;
    overlay.setAttribute('aria-label', '当前摄像头视频流');
    Object.assign(overlay.style, { position:'absolute', inset:'0', width:'100%', height:'100%', pointerEvents:'none' });
    image.after(overlay);
  }
  function draw(ctx, source, x, y, width, height) {
    ctx.save();
    try {
      if (enabled) {
        pixels.clearRect(0, 0, 32, 18);
        pixels.drawImage(source, 0, 0, 32, 18);
        ctx.imageSmoothingEnabled = false;
        ctx.drawImage(small, x, y, width, height);
      } else ctx.drawImage(source, x, y, width, height);
    } finally { ctx.restore(); }
  }
  function render() {
    if (!overlay || !enabled) return;
    const ctx = overlay.getContext('2d');
    ctx.fillStyle = '#1b1610'; ctx.fillRect(0, 0, 960, 540);
    const source = image.hidden ? video : image;
    const width = source.naturalWidth || source.videoWidth, height = source.naturalHeight || source.videoHeight;
    if (!width || !height || source.hidden) return;
    const scale = Math.min(960 / width, 540 / height);
    try { draw(ctx, source, (960-width*scale)/2, (540-height*scale)/2, width*scale, height*scale); }
    catch { /* Leave the masked preview opaque while a frame is unavailable. */ }
  }
  function set(value) {
    enabled = Boolean(value); globalThis.vfPrivacy.enabled = enabled;
    if (overlay) {
      image.style.visibility = video.style.visibility = enabled ? 'hidden' : '';
      overlay.hidden = !enabled; render();
    }
    globalThis.dispatchEvent(new Event('vf-privacy-change'));
    if (owner === globalThis) document.querySelectorAll('iframe').forEach(frame => frame.contentWindow.vfPrivacy?.set(enabled));
  }
  function key(event) {
    if (event.key.toLowerCase() !== 'y' || event.repeat || event.isComposing || event.ctrlKey || event.altKey || event.metaKey ||
        event.target.isContentEditable || event.target.closest?.('input,textarea,select')) return;
    event.preventDefault(); set(!enabled);
  }
  globalThis.vfPrivacy = { enabled, draw, set };
  if (owner === globalThis) {
    document.addEventListener('keydown', key);
    document.querySelectorAll('iframe').forEach(frame => frame.addEventListener('load', () => {
      frame.contentDocument.addEventListener('keydown', key);
      frame.contentWindow.vfPrivacy?.set(enabled);
    }));
  }
  if (overlay) setInterval(() => { if (!document.hidden && overlay.getClientRects().length) render(); }, 100);
})();
