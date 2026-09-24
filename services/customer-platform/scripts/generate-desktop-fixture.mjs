// Public, synthetic interoperability fixture; the generated private key is discarded.
import { generateKeyPairSync, sign } from 'node:crypto';
import { writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
const {publicKey,privateKey}=generateKeyPairSync('ed25519');
const encode=value=>Buffer.from(JSON.stringify(value)).toString('base64url');
const base={iss:'https://accounts.looksy.test',aud:'looksy-desktop',sub:'customer:test',schema:1,
  device:createHash('sha256').update('a'.repeat(43)).digest('hex'),iat:2000000000,exp:2000000600,
  mode:'perpetual',major:1,features:['editor','agent'],plan:'studio'};
const token=(changes={})=>{const message=encode({alg:'EdDSA',kid:'looksy-1',typ:'JWT'})+'.'+encode({...base,...changes});return message+'.'+sign(null,Buffer.from(message),privateKey).toString('base64url');};
writeFileSync(new URL('../../../tests/fixtures/customer-lease.json',import.meta.url),JSON.stringify({
  publicKey:publicKey.export({format:'der',type:'spki'}).subarray(-32).toString('base64'),
  tokens:{valid:token(),wrongAudience:token({aud:'other'}),wrongIssuer:token({iss:'https://other.test'}),
    wrongDevice:token({device:'0'.repeat(64)}),wrongMajor:token({major:2}),unknownFeature:token({features:['admin']}),
    expired:token({exp:2000000001}),future:token({iat:2000000400}),trial:token({mode:'trial',major:-1}),
    malformedTime:token({iat:2000000000.5}),missingFeatures:token({features:null})}},null,2)+'\n');
