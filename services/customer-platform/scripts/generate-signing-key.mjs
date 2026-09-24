// Writes a private signing key once, outside the checkout; never prints it.
import { generateKeyPairSync } from 'node:crypto';
import { mkdir, writeFile, realpath } from 'node:fs/promises';
import { resolve, relative, isAbsolute, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
const [directory, kid='looksy-1'] = process.argv.slice(2);
if (!directory || !/^[A-Za-z0-9_-]+$/.test(kid)) throw new Error('Usage: node scripts/generate-signing-key.mjs /private/key-directory [key-id]');
const output=resolve(directory), checkout=await realpath(fileURLToPath(new URL('../../../',import.meta.url)));
await mkdir(output,{recursive:true,mode:0o700});
const canonical=await realpath(output), pathFromCheckout=relative(checkout,canonical);
if (!isAbsolute(pathFromCheckout) && pathFromCheckout !== '..' && !pathFromCheckout.startsWith('..'+sep))
  throw new Error('Keep private keys outside the checkout');
const {privateKey,publicKey}=generateKeyPairSync('ed25519');
const publicJwk=publicKey.export({format:'jwk'});
await writeFile(resolve(canonical,kid+'.pem'),privateKey.export({format:'pem',type:'pkcs8'}),{flag:'wx',mode:0o600});
await writeFile(resolve(canonical,kid+'.public.json'),JSON.stringify({kid,
  publicKeyBase64:Buffer.from(publicJwk.x,'base64url').toString('base64')},null,2)+'\n',{flag:'wx',mode:0o600});
console.log('Signing key and desktop public configuration written to the requested directory.');
