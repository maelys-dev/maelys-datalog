/* SPDX-License-Identifier: MPL-2.0 */
'use strict';
const { binding } = require('./core.cjs');
const { wasmTransport } = require('./wasm-transport.cjs');
module.exports = binding(async ({ profile = 'small', wasmFactory, wasmUrl } = {}) => {
  if (profile !== 'small' && profile !== 'large') throw new TypeError('Unknown engine profile');
  const factory = wasmFactory || (await import(`../wasm/${profile}/engine.mjs`)).default;
  const module = await factory(wasmUrl ? { locateFile: file => file.endsWith('.wasm') ? String(wasmUrl) : file } : {});
  return wasmTransport(module);
});
