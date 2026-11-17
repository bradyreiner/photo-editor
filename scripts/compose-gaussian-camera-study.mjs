// Reproducible, rigid 3D composition of licensed Gaussian captures. Transforms,
// covariance and view-dependent SH rotation are handled by the pinned official
// PlayCanvas converter; this never makes a background photograph or billboard.
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {spawnSync} from 'node:child_process';

const [converterRoot, recipeFile, output] = process.argv.slice(2);
if (!converterRoot || !recipeFile || !output || fs.existsSync(output)) {
    throw new Error('Usage: node compose-gaussian-camera-study.mjs CONVERTER_PACKAGE_ROOT RECIPE_JSON NEW_OUTPUT');
}
const {Transform} = await import(pathToFileURL(path.join(converterRoot, 'dist/index.mjs')));
const cli = path.join(converterRoot, 'bin/cli.mjs');
const recipe = JSON.parse(fs.readFileSync(recipeFile, 'utf8'));
if (recipe.schema !== 'looksy-gaussian-composition-study-v1' || !Array.isArray(recipe.layers)) throw new Error('Invalid composition recipe');
fs.mkdirSync(output, {recursive: true});
const operations = [];
const flip = [-1, -1, 1];
const vector = (value) => Array.isArray(value) && value.length === 3 && value.every(Number.isFinite);
for (const layer of recipe.layers) {
    if (!/^[a-z][a-z0-9-]*$/.test(layer.name) || !vector(layer.camera.position) || !vector(layer.translation) ||
        !Number.isFinite(layer.scale) || layer.scale <= 0) throw new Error('Invalid layer transform');
    const rows = [layer.camera.right, layer.camera.down, layer.camera.forward];
    if (!rows.every(vector)) throw new Error('Invalid camera basis');
    const matrix = new Float32Array(16); matrix[15] = 1;
    for (let r = 0; r < 3; ++r) for (let c = 0; c < 3; ++c) matrix[c * 4 + r] = flip[r] * rows[r][c] * flip[c];
    const transform = new Transform(); transform.rotation.setFromMat4({data: matrix});
    const euler = transform.rotation.getEulerAngles();
    const translation = rows.map((row, r) => flip[r] * (layer.translation[r] - layer.scale * row.reduce((v, x, c) => v + x * layer.camera.position[c], 0)));
    const input = path.resolve(path.dirname(recipeFile), layer.input);
    const target = path.join(output, layer.name + '.ply');
    const args = [cli, input, '--scale', String(layer.scale), '--rotate', [euler.x, euler.y, euler.z].join(','), '--translate', translation.join(',')];
    if (layer.bounds) {
        if (layer.bounds.length !== 6 || !layer.bounds.every(Number.isFinite)) throw new Error('Invalid clip box');
        // Recipe bounds use native right/down/forward coordinates, whereas
        // converter operations use its canonical Y-up frame (PLY = Z roll 180).
        const lo = layer.bounds.slice(0, 3), hi = layer.bounds.slice(3);
        const minimum = flip.map((sign, i) => Math.min(sign * lo[i], sign * hi[i]));
        const maximum = flip.map((sign, i) => Math.max(sign * lo[i], sign * hi[i]));
        args.push('--filter-box', [...minimum, ...maximum].join(','));
    }
    args.push(target);
    const result = spawnSync(process.execPath, args, {stdio: 'inherit'});
    if (result.status !== 0) throw new Error('Layer conversion failed: ' + layer.name);
    operations.push({name: layer.name, input, output: target, canonicalRotationDegrees: [euler.x, euler.y, euler.z], canonicalTranslation: translation, args});
}
const merged = path.join(output, 'scene.ply');
const result = spawnSync(process.execPath, [cli, ...operations.map((v) => v.output), merged], {stdio: 'inherit'});
if (result.status !== 0) throw new Error('Gaussian merge failed');
fs.writeFileSync(path.join(output, 'composition-receipt.json'), JSON.stringify({recipe, operations, output: merged}, null, 2) + '\n');
