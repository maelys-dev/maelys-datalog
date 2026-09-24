/* SPDX-License-Identifier: MPL-2.0 */
'use strict';

const PredKind = Object.freeze({ EDB: 1, IDB: 2, QUERY: 4, POLICY_FACT: 8 });
const Status = Object.freeze({ OK: 0, INVALID_ARGUMENT: -1, INVALID_FIELD: -2,
  NOT_FOUND: -3, NOT_IMPLEMENTED: -4, UNSUPPORTED: -5, TIMEOUT: -6, IO: -7,
  INTERNAL: -8, UNAUTHORIZED: -9, FORBIDDEN: -10, RATE_LIMITED: -11,
  PAYLOAD_TOO_LARGE: -12, INVALID_STATE: -13, STORAGE_TOO_SMALL: -14 });
const claimedModules = new WeakSet();
const encoder = new TextEncoder();
const INT64_MIN = -(1n << 63n), INT64_MAX = (1n << 63n) - 1n;
const UINT32_MAX = 0xffffffff, FACT_WORDS = 15;
const LIMITS = ['maxSymbols', 'stringPoolBytes', 'maxPredicates', 'maxRules',
  'maxArity', 'maxBodyLiterals', 'maxDepth', 'maxEdbFacts', 'maxIdbFacts',
  'maxFactsPerPred', 'maxStringBytes', 'inputEdbTextBytes'];

function utf8(value) {
  if (typeof value !== 'string') throw new TypeError('Expected a string');
  for (let i = 0; i < value.length; ++i) {
    const c = value.charCodeAt(i);
    if (c === 0) throw new TypeError('Embedded NUL is not supported');
    if (c >= 0xd800 && c <= 0xdbff) {
      const next = value.charCodeAt(++i);
      if (!(next >= 0xdc00 && next <= 0xdfff)) throw new TypeError('Unpaired UTF-16 surrogate');
    } else if (c >= 0xdc00 && c <= 0xdfff) throw new TypeError('Unpaired UTF-16 surrogate');
  }
  return encoder.encode(value);
}
function u32(value, name) {
  if (!Number.isInteger(value) || value < 0 || value > UINT32_MAX) throw new RangeError(`${name}: expected uint32`);
  return value;
}
function integer(value) {
  if (typeof value === 'number') {
    if (!Number.isSafeInteger(value)) throw new RangeError('Use bigint for integers outside the exact number range');
    value = BigInt(value);
  }
  if (value < INT64_MIN || value > INT64_MAX) throw new RangeError('Integer outside signed int64 range');
  const bits = BigInt.asUintN(64, value);
  return [Number(bits & 0xffffffffn), Number(bits >> 32n)];
}
function strings() {
  const index = new Map(), chunks = [];
  let bytes = 0;
  return {
    add(value) {
      if (index.has(value)) return index.get(value);
      const encoded = utf8(value), entry = [bytes, encoded.length];
      bytes = u32(bytes + encoded.length + 1, 'Text size');
      index.set(value, entry); chunks.push(encoded);
      return entry;
    },
    finish() {
      const out = new Uint8Array(bytes); let offset = 0;
      for (const chunk of chunks) { out.set(chunk, offset); offset += chunk.length + 1; }
      return out;
    },
  };
}

class DatalogError extends Error {
  constructor(operation, status, diagnostic) {
    super(`${operation}: ${diagnostic.statusName}${diagnostic.message ? ': ' + diagnostic.message : ''}`);
    this.name = 'DatalogError'; this.status = status; this.diagnostic = diagnostic;
  }
}

class MaelysPlayground {
  constructor(module) {
    if (claimedModules.has(module)) throw new Error('A Wasm module belongs to exactly one wrapper');
    this._mod = module; this._closed = false;
    if (this._call('transport_version') !== 1) throw new Error('Incompatible Wasm binding transport');
    if (this._call('diagnostic_scalar', [23]) !== 2) throw new Error('Incompatible engine consumer API');
    this._check(this._call('open'), 'open');
    claimedModules.add(module);
  }
  static async create(factory, wasmUrl) {
    return new MaelysPlayground(await factory(wasmUrl ? {
      locateFile: file => file.endsWith('.wasm') ? wasmUrl : file,
    } : {}));
  }
  _call(name, args = [], type = 'number') {
    return this._mod.ccall(`maelys_datalog_wasm_${name}`, type, args.map(() => 'number'), args);
  }
  _live() {
    if (this._closed) throw new DatalogError('closed', Status.INVALID_STATE,
      Object.freeze({ status: Status.INVALID_STATE, statusName: 'invalid_state', source: 0,
        code: 0, codeName: 'none', present: 0n, phase: '', message: 'Wasm wrapper is closed.', hint: '' }));
  }
  _diagnostic() {
    const n = id => this._call('diagnostic_scalar', [id]) >>> 0;
    const text = id => this._call('diagnostic_text', [id], 'string');
    const present = BigInt(n(3)) | (BigInt(n(4)) << 32n);
    const d = { status: this._call('diagnostic_scalar', [0]), source: n(1), code: n(2),
      present, phase: text(0), message: text(1), hint: text(2), codeName: text(8), statusName: text(9) };
    if (present & 1n) d.location = Object.freeze({ line: n(5), column: n(6), file: text(3) });
    if (present & 2n) d.predicate = Object.freeze({ name: text(4), arity: n(7) });
    if (present & 4n) d.capacity = Object.freeze({ observed: n(8), limit: n(9), kind: n(18) });
    if (present & 8n) d.depth = Object.freeze({ observed: n(10), limit: n(11) });
    if (present & 16n) d.comparison = Object.freeze({ result: n(13), expectedKind: n(14), lhsKind: n(15), rhsKind: n(16), op: n(17) });
    if (present & 32n) d.arity = Object.freeze({ expected: n(20), observed: n(21), termIndex: n(19) });
    if (present & 64n) d.rule = Object.freeze({ id: n(12) });
    if (present & 128n) d.context = Object.freeze({ token: text(5), field: text(6), domain: text(7) });
    if (present & 256n) d.aggregate = Object.freeze({ operator: text(6), value: text(5),
      valueKind: n(15), termIndex: n(19), limit: n(9) });
    return Object.freeze(d);
  }
  _check(rc, operation) { if (rc !== 0) throw new DatalogError(operation, rc, this._diagnostic()); }
  _memory(bytes, action) {
    u32(bytes, 'Allocation size');
    const ptr = this._mod._malloc(Math.max(bytes, 1));
    if (!ptr) throw new Error('Wasm transport allocation failed');
    try { return action(ptr); } finally { this._mod._free(ptr); }
  }
  _view() { return new DataView(this._mod.HEAPU8.buffer); }
  _frame(words, text, action) {
    const prefix = u32(words.length * 4, 'Frame size');
    return this._memory(u32(prefix + text.length, 'Frame size'), ptr => {
      const view = this._view();
      words.forEach((word, i) => view.setUint32(ptr + i * 4, word, true));
      this._mod.HEAPU8.set(text, ptr + prefix);
      return action(ptr, words.length, ptr + prefix, text.length);
    });
  }
  _facts(facts, action) {
    if (!Array.isArray(facts)) throw new TypeError('Facts must be an array');
    if (facts.length > this.buildLimits().maxEdbFacts) throw new RangeError('Batch exceeds maxEdbFacts');
    const pool = strings(), words = [];
    for (const fact of facts) {
      if (!fact || !Array.isArray(fact.terms)) throw new TypeError('Expected {predicate, terms}');
      if (fact.terms.length > 4) throw new RangeError('Arity exceeds four');
      words.push(...pool.add(fact.predicate), fact.terms.length);
      for (let j = 0; j < 4; ++j) {
        if (j >= fact.terms.length) { words.push(0, 0, 0); continue; }
        const v = fact.terms[j];
        if (typeof v === 'string') words.push(1, ...pool.add(v));
        else if (typeof v === 'number' || typeof v === 'bigint') words.push(2, ...integer(v));
        else if (typeof v === 'boolean') words.push(3, v ? 1 : 0, 0);
        else throw new TypeError('Terms must be string, bigint, exact integer number, or boolean');
      }
    }
    return this._frame(words, pool.finish(), action);
  }
  buildLimits() {
    this._live();
    if (!this._limits) this._limits = this._memory(4, ptr => {
      const limits = {};
      LIMITS.forEach((name, i) => {
        this._check(this._call('limit', [i + 1, ptr]), 'buildLimits');
        limits[name] = this._view().getUint32(ptr, true);
      });
      return Object.freeze(limits);
    });
    return this._limits;
  }
  registerDomain({ name, predicates, atoms = [] }) {
    this._live();
    if (!Array.isArray(predicates) || !Array.isArray(atoms)) throw new TypeError('Expected predicate and atom arrays');
    if (predicates.length > this.buildLimits().maxPredicates) throw new RangeError('Too many predicates');
    const pool = strings(), nameRef = pool.add(name), words = [];
    for (const p of predicates) words.push(...pool.add(p.name), u32(p.arity, 'Arity'), u32(p.flags, 'Predicate flags'));
    for (const atom of atoms) words.push(...pool.add(atom));
    this._frame(words, pool.finish(), (w, n, text, bytes) => this._check(
      this._call('register_domain', [text + nameRef[0], w, n, predicates.length, atoms.length, text, bytes]), 'registerDomain'));
    return this;
  }
  loadPolicy(domain, id, source) {
    this._live();
    const pool = strings(), d = pool.add(domain), i = pool.add(id), s = pool.add(source);
    this._frame([], pool.finish(), (_w, _n, text) => this._check(
      this._call('load_policy', [text + d[0], text + i[0], text + s[0], s[1]]), 'loadPolicy'));
    return this;
  }
  clearFacts() { this._live(); this._check(this._call('clear_facts'), 'clearFacts'); return this; }
  addFacts(facts) {
    this._live();
    this._facts(facts, (w, n, text, bytes) => this._check(
      this._call('add_facts', [w, n, facts.length, text, bytes]), 'addFacts'));
    return this;
  }
  inputUsage() {
    this._live();
    return this._memory(12, ptr => {
      this._check(this._call('input_usage', [ptr]), 'inputUsage');
      const v = this._view();
      return { facts: v.getUint32(ptr, true), textBytes: v.getUint32(ptr + 4, true), textCapacity: v.getUint32(ptr + 8, true) };
    });
  }
  solve() { this._live(); this._check(this._call('solve'), 'solve'); return this; }
  freeResult() { this._live(); this._check(this._call('free_result'), 'freeResult'); return this; }
  query(predicate, terms) {
    this._live();
    return this._facts([{ predicate, terms }], (w, n, text, bytes) => this._memory(4, out => {
      this._check(this._call('query', [w, n, text, bytes, out]), 'query');
      return this._view().getUint32(out, true) !== 0;
    }));
  }
  enumerate(predicate, arity) {
    this._live(); u32(arity, 'Arity');
    if (arity > 4) throw new RangeError('Arity exceeds four');
    const pool = strings(), name = pool.add(predicate);
    return this._frame([], pool.finish(), (_w, _n, text) => this._memory(4, countPtr => {
      const call = (out, capacity) => this._call('enumerate', [text + name[0], arity, out, capacity, countPtr]);
      this._check(call(0, 0), 'enumerate');
      const count = this._view().getUint32(countPtr, true);
      if (!count || !arity) return Array.from({ length: count }, () => []);
      return this._memory(u32(count * arity * 12, 'Result size'), out => {
        this._check(call(out, count), 'enumerate');
        if (this._view().getUint32(countPtr, true) !== count) throw new Error('Enumeration count changed');
        return Array.from({ length: count }, (_, i) => Array.from({ length: arity }, (_, j) => {
          const v = this._view(), p = out + (i * arity + j) * 12;
          const kind = v.getUint32(p, true), lo = v.getUint32(p + 4, true), hi = v.getUint32(p + 8, true);
          if (kind === 1) {
            const ptr = this._call('symbol_text', [lo]);
            if (!ptr) this._check(this._call('diagnostic_scalar', [0]), 'enumerate symbol');
            if (!ptr) throw new Error('Missing symbol text');
            return this._mod.UTF8ToString(ptr);
          }
          if (kind === 2) return BigInt.asIntN(64, BigInt(lo) | (BigInt(hi) << 32n));
          if (kind === 3) return lo !== 0;
          throw new Error(`Invalid output term kind ${kind}`);
        }));
      });
    }));
  }
  _explain(kind, predicate, terms) {
    this._live();
    return this._facts([{ predicate, terms }], (w, n, text, bytes) => this._memory(4, size => {
      const call = (out, cap) => this._call('explain', [kind, w, n, text, bytes, out, cap, size]);
      this._check(call(0, 0), 'explain');
      const needed = this._view().getUint32(size, true);
      return this._memory(u32(needed + 1, 'Explanation size'), out => {
        this._check(call(out, needed + 1), 'explain');
        if (this._view().getUint32(size, true) !== needed) throw new Error('Explanation size changed');
        const value = this._mod.UTF8ToString(out);
        if (encoder.encode(value).length !== needed) throw new Error('Explanation length mismatch');
        return value;
      });
    }));
  }
  explainTrue(predicate, terms) { return this._explain(1, predicate, terms); }
  explainFalse(predicate, terms) { return this._explain(2, predicate, terms); }
  derivedFactCount() {
    this._live(); return this._memory(4, ptr => {
      this._check(this._call('derived_count', [ptr]), 'derivedFactCount');
      return this._view().getUint32(ptr, true);
    });
  }
  fingerprints() {
    this._live(); return this._memory(65, ptr => {
      const result = {};
      ['policy', 'session', 'execution'].forEach((name, i) => {
        this._check(this._call('fingerprint', [i + 1, ptr, 65]), 'fingerprints');
        result[name] = this._mod.UTF8ToString(ptr);
      });
      return result;
    });
  }
  close() { this._live(); this._check(this._call('close'), 'close'); this._closed = true; }
}
const api = { MaelysPlayground, PredKind, Status, DatalogError };
if (typeof module !== 'undefined' && module.exports) module.exports = api;
if (typeof globalThis !== 'undefined') Object.assign(globalThis, api);
