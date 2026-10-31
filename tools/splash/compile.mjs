#!/usr/bin/env node
// compile.mjs in.json out.rc — RemoteCompose JSON -> .rc with the converter from
// hoford/RemoteCompose-Examples (assets/json2rc.js, vendored beside this file). Patched here:
// a canvas `variable` may be exported (search json2rc.js for "esp32-rc patch").
import { readFileSync, writeFileSync } from 'node:fs';
import { createContext, runInContext } from 'node:vm';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const ctx = createContext({ TextEncoder, TextDecoder, console });
runInContext(readFileSync(resolve(here, 'json2rc.js'), 'utf8'), ctx);

const [src, dest] = process.argv.slice(2);
if (!src || !dest) { console.error('usage: compile.mjs in.json out.rc'); process.exit(2); }
let bytes;
try {
    bytes = ctx.J2RC.convert(readFileSync(src, 'utf8'));
} catch (e) {
    console.error(`${src}: ${e && e.message ? e.message : e}`);
    process.exit(1);
}
writeFileSync(dest, Buffer.from(bytes));
console.log(`${dest}: ${bytes.length} bytes`);
