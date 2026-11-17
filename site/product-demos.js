(function () {
  'use strict';
  const motion = window.matchMedia('(prefers-reduced-motion: reduce)');
  const narrow = window.matchMedia('(max-width: 760px)');
  const tools = [...document.querySelectorAll('.tool-details')];
  const sizeTools = () => tools.forEach((details) => { details.open = !narrow.matches; });
  sizeTools();
  narrow.addEventListener('change', sizeTools);
  const stories = [...document.querySelectorAll('[data-demo]')];
  if (!stories.length) return;
  const states = stories.map((story) => ({
    story, page: story.closest('[data-page]'), reels: [...story.querySelectorAll('[data-reel]')],
    choices: [...story.querySelectorAll('[data-demo-choice]')], index: 0, paused: false,
    play: story.querySelector('[data-demo-play]'), progress: story.querySelector('[data-demo-progress]'),
    time: story.querySelector('[data-demo-time]'), open: story.querySelector('[data-demo-open]')
  }));
  const clock = (seconds) => `00:${String(Math.floor(seconds || 0)).padStart(2, '0')}`;
  const active = (state) => state.page.classList.contains('is-current') && !document.hidden;
  const currentVideo = (state) => state.reels[state.index].querySelector('video');

  function updateControls(state) {
    const video = currentVideo(state);
    const playing = video ? !video.paused : state.comparing;
    state.play.textContent = playing ? 'Pause demo  Ⅱ' : 'Play demo  ▶';
    state.play.setAttribute('aria-label', playing ? 'Pause demonstration' : 'Play demonstration');
    state.progress.value = video?.duration ? video.currentTime / video.duration : 0;
    state.time.textContent = video ? clock(video.currentTime) : 'DRAG ↔';
    state.open.hidden = !video;
    if (video) state.open.href = video.dataset.src;
  }

  function stopComparison(state) {
    window.cancelAnimationFrame(state.compareFrame || 0);
    state.comparing = false;
  }

  function compare(state, animate) {
    const stage = state.reels[state.index].querySelector('[data-comparison]');
    if (!stage) return;
    const range = stage.querySelector('[data-compare-range]');
    const paint = (value) => {
      range.value = value;
      stage.style.setProperty('--split', `${value}%`);
    };
    if (!stage.dataset.ready) {
      stage.dataset.ready = 'true';
      range.addEventListener('input', () => {
        stopComparison(state); state.paused = true; paint(range.value); updateControls(state);
      });
      range.addEventListener('pointerdown', () => { stopComparison(state); state.paused = true; updateControls(state); });
    }
    if (!animate) { updateControls(state); return; }
    stopComparison(state);
    state.comparing = true;
    const start = performance.now();
    function frame(now) {
      const t = Math.min((now - start) / 2800, 1);
      // Reveal genuine before/after pixels; the site never simulates the edit.
      paint(50 + Math.sin(t * Math.PI * 2) * 44 * Math.sin(t * Math.PI));
      if (t < 1 && active(state) && state.comparing) state.compareFrame = window.requestAnimationFrame(frame);
      else { state.comparing = false; updateControls(state); }
    }
    state.compareFrame = window.requestAnimationFrame(frame);
    updateControls(state);
  }

  function sync(state, explicit = false) {
    const visible = active(state);
    state.reels.forEach((reel, index) => {
      const video = reel.querySelector('video');
      if (video && (!visible || index !== state.index)) video.pause();
    });
    if (!visible) { stopComparison(state); updateControls(state); return; }
    const video = currentVideo(state);
    if (!video) {
      compare(state, !state.paused && (explicit || !motion.matches));
      return;
    }
    if (!video.getAttribute('src')) { video.src = video.dataset.src; video.load(); }
    if (!state.paused && (explicit || !motion.matches)) {
      video.muted = true;
      video.play().catch(() => updateControls(state));
    } else video.pause();
    updateControls(state);
  }

  function choose(state, index, explicit = false) {
    stopComparison(state);
    state.index = index;
    state.play.disabled = false;
    state.reels.forEach((reel, i) => { reel.hidden = i !== index; });
    state.choices.forEach((button, i) => button.setAttribute('aria-pressed', String(i === index)));
    sync(state, explicit);
  }

  states.forEach((state) => {
    state.choices.forEach((button, index) => button.addEventListener('click', () => choose(state, index)));
    state.play.addEventListener('click', () => {
      const video = currentVideo(state);
      state.paused = video ? !video.paused : !!state.comparing;
      if (!video && state.paused) stopComparison(state);
      sync(state, !state.paused);
    });
    state.reels.forEach((reel) => {
      const video = reel.querySelector('video');
      if (!video) return;
      ['play', 'pause', 'timeupdate', 'loadedmetadata'].forEach((event) => video.addEventListener(event, () => updateControls(state)));
      video.addEventListener('ended', () => {
        if (state.story.hasAttribute('data-rotate') && active(state) && !state.paused) choose(state, (state.index + 1) % state.reels.length);
        else updateControls(state);
      });
      video.addEventListener('error', () => {
        state.time.textContent = 'PREVIEW';
        state.play.textContent = 'Preview unavailable';
        state.play.disabled = true;
        state.open.hidden = false;
      });
    });
    new MutationObserver(() => sync(state)).observe(state.page, { attributes: true, attributeFilter: ['class'] });
    sync(state);
  });
  document.addEventListener('visibilitychange', () => states.forEach((state) => sync(state)));
  motion.addEventListener('change', () => states.forEach((state) => {
    stopComparison(state);
    sync(state);
  }));
})();
