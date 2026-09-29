/* SPDX-License-Identifier: MPL-2.0 */
'use strict';
// Module allocations hold transport buffers, separate from the engine's fixed
// prepared storage. Both response words and text are copied before the next call.
function wasmTransport(module) {
  const run = (name, args = [], type = 'number') => module.ccall(`maelys_js_${name}`, type, args.map(() => 'number'), args);
  let context = run('create');
  if (!context) throw new Error('Engine initialization failed');
  const transport = {
    call(op, words, text) {
      if (!context) throw new Error('Engine is closed');
      const prefix = words.length * 4, bytes = prefix + text.length;
      if (bytes > 0xffffffff) throw new RangeError('Transport frame exceeds wasm32');
      const ptr = module._malloc(Math.max(bytes, 1));
      if (!ptr) throw new Error('Wasm transport allocation failed');
      try {
        const input = new DataView(module.HEAPU8.buffer);
        words.forEach((word, i) => input.setUint32(ptr + i * 4, word, true));
        module.HEAPU8.set(text, ptr + prefix);
        const status = run('call', [context, op, ptr, words.length, ptr + prefix, text.length]);
        const count = run('word_count', [context]), out = run('words', [context]);
        const view = new DataView(module.HEAPU8.buffer);
        const response = { status, words: Uint32Array.from({ length: count }, (_, i) => view.getUint32(out + i * 4, true)),
          text: run('text', [context], 'string') };
        if (status) {
          response.scalars = Array.from({ length: 24 }, (_, i) => run('scalar', [context, i]) >>> 0);
          response.texts = Array.from({ length: 10 }, (_, i) => run('diagnostic', [context, i], 'string'));
        }
        return response;
      } finally { module._free(ptr); }
    },
    close() {
      if (context) { run('destroy', [context], null); context = 0; }
      finalizer?.unregister(transport);
    },
  };
  // The held value does not retain transport/Engine. Explicit close remains the
  // deterministic lifecycle; GC only recovers an abandoned module's resources.
  finalizer?.register(transport, { module, context }, transport);
  return transport;
}
const finalizer = typeof FinalizationRegistry === 'function' ? new FinalizationRegistry(({ module, context }) => {
  module.ccall('maelys_js_destroy', null, ['number'], [context]);
}) : undefined;
module.exports = { wasmTransport };
