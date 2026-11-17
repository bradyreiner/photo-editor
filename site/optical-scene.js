const stage = document.getElementById('optical-stage');
const canvas = document.getElementById('optical-canvas');
const motionButton = document.getElementById('motion-control');
const channels = [...document.querySelectorAll('[data-channel]')];
const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
let paused = reducedMotion.matches;
let channelMeshes = [];
let draw = () => {};

// The CSS sculpture and controls remain usable if WebGL or the module is unavailable.
channels.forEach((button, index) => {
  button.addEventListener('click', () => {
    const enabled = button.getAttribute('aria-pressed') !== 'true';
    button.setAttribute('aria-pressed', String(enabled));
    stage.querySelectorAll('.optical-fallback i')[index].style.opacity = enabled ? '1' : '0';
    if (channelMeshes[index]) channelMeshes[index].visible = enabled;
    draw();
  });
});

async function initialize() {
  const THREE = await import('./assets/vendor/three-r180/three.module.min.js');
  const renderer = new THREE.WebGLRenderer({ canvas, alpha: true, antialias: true, powerPreference: 'low-power' });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.75));
  renderer.setClearColor(0x101110, 0);
  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(36, 1, .1, 50);
  camera.position.set(0, 0, 10.5);
  const sculpture = new THREE.Group();
  scene.add(sculpture);
  const geometry = new THREE.TorusGeometry(1.62, .47, 40, 160);
  const colors = [new THREE.Vector3(1, .025, .012), new THREE.Vector3(.015, 1, .045), new THREE.Vector3(.025, .13, 1)];
  const positions = [[-.73, .4, 0], [.73, .4, .05], [0, -.75, .35]];
  channelMeshes = colors.map((color, index) => {
    const material = new THREE.ShaderMaterial({
      uniforms: { lightColor: { value: color } },
      vertexShader: `
        varying vec3 vNormal;
        varying vec3 vView;
        varying vec2 vUv;
        void main() {
          vec4 viewPosition = modelViewMatrix * vec4(position, 1.0);
          vNormal = normalize(normalMatrix * normal);
          vView = -viewPosition.xyz;
          vUv = uv;
          gl_Position = projectionMatrix * viewPosition;
        }`,
      fragmentShader: `
        uniform vec3 lightColor;
        varying vec3 vNormal;
        varying vec3 vView;
        varying vec2 vUv;
        void main() {
          vec3 n = normalize(vNormal);
          vec3 view = normalize(vView);
          float facing = max(dot(n, view), 0.0);
          float edge = pow(1.0 - facing, 3.0);
          vec3 light = normalize(vec3(-2.0, 3.0, 5.0));
          float diffuse = max(dot(n, light), 0.0);
          float specular = pow(max(dot(n, normalize(light + view)), 0.0), 36.0);
          float ribs = .94 + .06 * sin(vUv.y * 280.0);
          float brightness = (.13 + .75 * diffuse + .55 * edge) * ribs;
          vec3 color = lightColor * brightness + lightColor * specular * .7;
          gl_FragColor = vec4(color, 1.0);
        }`,
      transparent: true,
      blending: THREE.AdditiveBlending,
      depthWrite: false
    });
    const mesh = new THREE.Mesh(geometry, material);
    mesh.position.set(...positions[index]);
    mesh.rotation.set(.25, index === 0 ? -.46 : index === 1 ? .46 : .12, index === 2 ? -.2 : .12);
    mesh.visible = channels[index].getAttribute('aria-pressed') === 'true';
    sculpture.add(mesh);
    return mesh;
  });

  let inView = true;
  let elapsed = 0;
  let previousTime = null;
  let targetX = 0;
  let targetY = 0;
  draw = () => renderer.render(scene, camera);
  function resize() {
    const { width, height } = stage.getBoundingClientRect();
    renderer.setSize(width, height, false);
    camera.aspect = width / height;
    camera.position.z = camera.aspect < 1 ? 11.3 : 10.5;
    camera.updateProjectionMatrix();
    draw();
  }
  function frame(time) {
    if (previousTime !== null) elapsed += Math.min((time - previousTime) / 1000, .05);
    previousTime = time;
    sculpture.rotation.y += (targetX + Math.sin(elapsed * .2) * .14 - sculpture.rotation.y) * .035;
    sculpture.rotation.x += (targetY + Math.cos(elapsed * .17) * .10 - sculpture.rotation.x) * .035;
    sculpture.rotation.z = Math.sin(elapsed * .15) * .09;
    sculpture.position.y = Math.sin(elapsed * .5) * .10;
    channelMeshes.forEach((mesh, index) => {
      mesh.rotation.z = Math.sin(elapsed * .25 + index * 2) * .16 + (index === 2 ? -.2 : .12);
    });
    draw();
  }
  function syncMotion() {
    previousTime = null;
    renderer.setAnimationLoop(!paused && inView && !document.hidden ? frame : null);
    motionButton.setAttribute('aria-pressed', String(paused));
    motionButton.setAttribute('aria-label', paused ? 'Play sculpture motion' : 'Pause sculpture motion');
    motionButton.textContent = paused ? '▷' : 'Ⅱ';
    draw();
  }
  motionButton.hidden = false;
  motionButton.addEventListener('click', () => { paused = !paused; syncMotion(); });
  reducedMotion.addEventListener('change', (event) => { paused = event.matches; syncMotion(); });
  stage.addEventListener('pointermove', (event) => {
    if (paused) return;
    const bounds = stage.getBoundingClientRect();
    targetX = ((event.clientX - bounds.left) / bounds.width - .5) * .65;
    targetY = ((event.clientY - bounds.top) / bounds.height - .5) * .45;
  });
  stage.addEventListener('pointerleave', () => { targetX = 0; targetY = 0; });
  new ResizeObserver(resize).observe(stage);
  const observer = new IntersectionObserver(([entry]) => { inView = entry.isIntersecting; syncMotion(); });
  observer.observe(stage);
  document.addEventListener('visibilitychange', syncMotion);
  canvas.addEventListener('webglcontextlost', (event) => {
    event.preventDefault();
    renderer.setAnimationLoop(null);
    stage.classList.remove('scene-ready');
    motionButton.hidden = true;
  });
  canvas.addEventListener('webglcontextrestored', () => {
    stage.classList.add('scene-ready');
    motionButton.hidden = false;
    resize();
    syncMotion();
  });
  resize();
  syncMotion();
  stage.classList.add('scene-ready');
}

initialize().catch(() => {
  // Decorative enhancement only: downloads and the static RGB artwork still work.
  stage.classList.remove('scene-ready');
});
