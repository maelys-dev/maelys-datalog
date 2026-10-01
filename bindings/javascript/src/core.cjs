/* SPDX-License-Identifier: MPL-2.0 */
'use strict';

const PredKind = Object.freeze({ EDB: 1, IDB: 2, QUERY: 4, POLICY_FACT: 8 });
const Status = Object.freeze({ OK: 0, INVALID_ARGUMENT: -1, INVALID_FIELD: -2,
  NOT_FOUND: -3, NOT_IMPLEMENTED: -4, UNSUPPORTED: -5, TIMEOUT: -6, IO: -7,
  INTERNAL: -8, UNAUTHORIZED: -9, FORBIDDEN: -10, RATE_LIMITED: -11,
  PAYLOAD_TOO_LARGE: -12, INVALID_STATE: -13, STORAGE_TOO_SMALL: -14 });
const encoder = new TextEncoder();
const INT64_MIN = -(1n << 63n), INT64_MAX = (1n << 63n) - 1n;
const UINT32_MAX = 0xffffffff, FACT_WORDS = 15;
const LIMITS = ['maxSymbols', 'stringPoolBytes', 'maxPredicates', 'maxRules',
  'maxArity', 'maxBodyLiterals', 'maxDepth', 'maxEdbFacts', 'maxIdbFacts',
  'maxFactsPerPred', 'maxStringBytes', 'inputEdbTextBytes', 'maxPolicyAtoms', 'maxPolicyAtomBytes'];

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

const PRED_EDB = 1, PRED_IDB = 2, PRED_QUERY = 4, PRED_POLICY_FACT = 8;
const ExplanationKind = Object.freeze({ TRUE: 1, FALSE: 2 });
const Capability = Object.freeze(Object.fromEntries(['POSITIVE', 'NEGATION', 'COMPARISONS',
  'ARITHMETIC', 'FILTERS', 'EXPLAIN_TRUE', 'WORK_LIMIT', 'EXPLAIN_FALSE',
  'AGGREGATES', 'MIN', 'MAX', 'SUM'].map((name, i) => [name, 1n << BigInt(i)])));
const states = new WeakMap(), secret = Symbol('binding constructor');
const CAPACITIES = ['inputFacts', 'derivedFacts', 'symbols', 'textBytes'];
function uint64(value, name) {
  if (typeof value === 'number') {
    if (!Number.isSafeInteger(value)) throw new RangeError(`${name}: expected an exact integer`);
    value = BigInt(value);
  }
  if (typeof value !== 'bigint' || value < 0n || value > (1n << 64n) - 1n)
    throw new RangeError(`${name}: expected uint64`);
  return [Number(value & 0xffffffffn), Number(value >> 32n)];
}
function diagnostic(response) {
  const n = id => response.scalars[id] >>> 0, text = id => response.texts[id];
  const present = BigInt(n(3)) | (BigInt(n(4)) << 32n);
  const d = { status: response.status, source: n(1), code: n(2),
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

class MaelysDatalogError extends Error {
  constructor(operation, status, detail) {
    super(`${operation}: ${detail.statusName}${detail.message ? ': ' + detail.message : ''}`);
    this.name = 'MaelysDatalogError'; this.code = this.status = status;
    this.diagnostic = detail;
  }
}
function invalidState(message) {
  return new MaelysDatalogError(message, Status.INVALID_STATE, Object.freeze({
    status: Status.INVALID_STATE, statusName: 'invalid_state', source: 0, code: 0,
    codeName: 'none', present: 0n, phase: '', message, hint: '',
  }));
}
function live(object, type) {
  const s = states.get(object);
  if (!s || (type && s.type !== type)) throw new TypeError(`Expected ${type || 'binding object'}`);
  if (s.closed || s.closing) throw invalidState('Object is closed');
  return s;
}
function invoke(engine, operation, op, words = [], text = new Uint8Array()) {
  const s = states.get(engine);
  if (!s || s.type !== 'Engine' || s.closed) throw invalidState('Engine is closed');
  const response = s.transport.call(op, Uint32Array.from(words), text);
  if (response.status) throw new MaelysDatalogError(operation, response.status, diagnostic(response));
  return response;
}
function initialize(object, token, type, parent, id, kind) {
  if (token !== secret) throw new TypeError(`${type} is created by its owner`);
  const p = live(parent);
  const s = { type, parent, id, kind, engine: p.engine || parent, children: new Set(), closed: false };
  states.set(object, s); p.children.add(object); Object.freeze(object);
  return s;
}
function close(object) {
  const s = states.get(object);
  if (!s) throw new TypeError('Invalid receiver');
  if (s.closed || s.closing) return;
  if (s.busy) throw invalidState('Object is being staged');
  if (s.type === 'SessionInputs' && states.get(s.parent).busy) throw invalidState('Session input is being staged');
  s.closing = true;
  try {
    if (s.type === 'Session' && s.result) close(s.result);
    for (const child of s.children) close(child);
    if (s.type === 'Engine') s.transport.close();
    else invoke(s.engine, 'close', 6, [s.kind, s.id]);
    s.closed = true;
    if (s.parent) states.get(s.parent).children.delete(object);
    if (s.type === 'SessionInputs') states.get(s.parent).inputs = undefined;
    if (s.type === 'SolveResult') {
      const session = states.get(s.parent);
      session.result = undefined;
      if (s.ownSession && !session.closing) close(s.parent);
    }
  } finally { s.closing = false; }
}
// Stage all values before any native append. A conversion failure never commits
// a prefix, and reentrant mutation through getters/iterators is rejected.
function factWords(facts, pool, maximum) {
  const words = []; let count = 0;
  for (const fact of facts) {
    if (++count > maximum) throw new RangeError('Batch exceeds fact capacity');
    if (!fact || !Array.isArray(fact.terms)) throw new TypeError('Expected {predicate, terms}');
    const terms = [...fact.terms];
    if (terms.length > 4) throw new RangeError('Arity exceeds four');
    words.push(...pool.add(fact.predicate), terms.length);
    for (let i = 0; i < 4; ++i) {
      const v = terms[i];
      if (i >= terms.length) words.push(0, 0, 0);
      else if (typeof v === 'string') words.push(1, ...pool.add(v));
      else if (typeof v === 'number' || typeof v === 'bigint') words.push(2, ...integer(v));
      else if (typeof v === 'boolean') words.push(3, v ? 1 : 0, 0);
      else throw new TypeError('Terms must be string, bigint, exact integer number, or boolean');
    }
  }
  return { words, count };
}
function factCall(object, operation, op, prefix, predicate, terms) {
  const s = live(object), pool = strings();
  const frame = factWords([{ predicate, terms }], pool, 1);
  live(object);
  return invoke(s.engine, operation, op, [...prefix, ...frame.words], pool.finish());
}
class Predicate {
  constructor(name, arity, flags) { this.name = name; this.arity = arity; this.flags = flags; Object.freeze(this); }
  static edb(name, arity) { return new Predicate(name, arity, PRED_EDB); }
  static edbQuery(name, arity) { return new Predicate(name, arity, PRED_EDB | PRED_QUERY); }
  static idb(name, arity) { return new Predicate(name, arity, PRED_IDB); }
  static idbQuery(name, arity) { return new Predicate(name, arity, PRED_IDB | PRED_QUERY); }
  static policyFact(name, arity) { return new Predicate(name, arity, PRED_POLICY_FACT); }
  static policyFactQuery(name, arity) { return new Predicate(name, arity, PRED_POLICY_FACT | PRED_QUERY); }
}
class SessionCapacities {
  constructor(options = {}) {
    for (const key of CAPACITIES) if (options[key] !== undefined) this[key] = u32(options[key], key);
    Object.freeze(this);
  }
}
class EngineBase {
  constructor(token, transport) {
    if (token !== secret) throw new TypeError('Use Engine.create()');
    states.set(this, { type: 'Engine', transport, children: new Set(), closed: false });
    const version = invoke(this, 'transport version', 0).words;
    if (version[0] !== 2 || version[1] !== 2) throw new Error('Incompatible engine transport');
    const limits = invoke(this, 'limits', 1).words;
    if (limits.length !== LIMITS.length) throw new Error('Incompatible engine introspection transport');
    states.get(this).limits = Object.freeze(Object.fromEntries(LIMITS.map((name, i) => [name, limits[i]])));
    Object.freeze(this);
  }
  get limits() { return live(this, 'Engine').limits; }
  registerDomain(name, predicates, atoms = []) {
    const s = live(this, 'Engine'), pool = strings();
    if (!Array.isArray(predicates) || !Array.isArray(atoms)) throw new TypeError('Expected predicate and atom arrays');
    if (predicates.length > s.limits.maxPredicates || atoms.length > s.limits.maxPolicyAtoms) throw new RangeError('Domain exceeds build limits');
    const words = [...pool.add(name), predicates.length, atoms.length];
    for (const p of predicates) words.push(...pool.add(p.name), u32(p.arity, 'arity'), u32(p.flags, 'flags'));
    for (const atom of atoms) words.push(...pool.add(atom));
    invoke(this, 'registerDomain', 2, words, pool.finish());
  }
  loadInlineRuleset(domain, id, source) {
    live(this, 'Engine'); const pool = strings();
    const words = [...pool.add(domain), ...pool.add(id), ...pool.add(source)];
    const response = invoke(this, 'loadInlineRuleset', 3, words, pool.finish());
    return new Ruleset(secret, this, response.words[0]);
  }
  loadManifest(input, options = {}) {
    live(this, 'Engine');
    for (const key of ['allowTestOnly', 'allowUndeclaredPolicyAtoms'])
      if (options[key] !== undefined && typeof options[key] !== 'boolean') throw new TypeError(`${key}: expected boolean`);
    const flags = (options.allowTestOnly ? 1 : 0) | (options.allowUndeclaredPolicyAtoms ? 2 : 0);
    if (!input || typeof input !== 'object') throw new TypeError('Expected {path} or {text, policies}');
    const pool = strings(); let op, words;
    if ('path' in input) {
      if ('text' in input || 'policies' in input) throw new TypeError('Manifest input must select one source');
      op = 4; words = [...pool.add(input.path), flags];
    } else {
      if (!Array.isArray(input.policies)) throw new TypeError('Expected policies array');
      op = 5; words = [...pool.add(input.text), flags, u32(input.policies.length, 'bundle length')];
      for (const entry of input.policies) words.push(...pool.add(entry.policyId), ...pool.add(entry.source));
    }
    const response = invoke(this, 'loadManifest', op, words, pool.finish());
    return new Ruleset(secret, this, response.words[0]);
  }
  close() { close(this); }
  [Symbol.dispose]() { this.close(); }
}
class Ruleset {
  programCounts(policyIndex = 0) {
    const s = live(this, 'Ruleset');
    const [predicates, facts, rules] = invoke(s.engine, 'programCounts', 21,
      [s.id, u32(policyIndex, 'policyIndex')]).words;
    return Object.freeze({ predicates, facts, rules });
  }
  constructor(token, engine, id) { initialize(this, token, 'Ruleset', engine, id, 1); }
  get policyCount() { const s = live(this, 'Ruleset'); return invoke(s.engine, 'policyCount', 7, [s.id]).words[0]; }
  get fingerprint() { const s = live(this, 'Ruleset'); return invoke(s.engine, 'fingerprint', 11, [1, s.id]).text; }
  edb({ factCapacity, textCapacity } = {}) {
    const s = live(this, 'Ruleset'), limits = s.engine.limits;
    const capacity = factCapacity === undefined ? limits.maxEdbFacts : u32(factCapacity, 'factCapacity');
    const text = textCapacity === undefined ? limits.inputEdbTextBytes : u32(textCapacity, 'textCapacity');
    const id = invoke(s.engine, 'edb', 8, [s.id, capacity, text]).words[0];
    const edb = new Edb(secret, this, id); states.get(edb).capacity = capacity; return edb;
  }
  prepare({ policyIndex = 0, requiredCapabilities = 0n, workLimit = 0n, explanations = 0, capacities = {} } = {}) {
    const s = live(this, 'Ruleset');
    const requested = new SessionCapacities(capacities);
    const mask = CAPACITIES.reduce((bits, key, i) => bits | (requested[key] === undefined ? 0 : 1 << i), 0);
    const words = [s.id, u32(policyIndex, 'policyIndex'), ...uint64(requiredCapabilities, 'requiredCapabilities'),
      ...uint64(workLimit, 'workLimit'), u32(explanations, 'explanations'), mask,
      ...CAPACITIES.map(key => requested[key] ?? 0)];
    const id = invoke(s.engine, 'prepare', 9, words).words[0];
    return new Session(secret, this, id);
  }
  solve(edb, options) {
    live(this, 'Ruleset');
    if (live(edb, 'Edb').parent !== this) throw new TypeError('EDB belongs to another ruleset');
    const session = this.prepare(options);
    try { const result = session.solve(edb); states.get(result).ownSession = true; return result; }
    catch (error) { session.close(); throw error; }
  }
  close() { close(this); }
  [Symbol.dispose]() { this.close(); }
}
class Edb {
  constructor(token, ruleset, id) { initialize(this, token, 'Edb', ruleset, id, 2); }
  get count() { const s = live(this, 'Edb'); return invoke(s.engine, 'count', 14, [s.id]).words[0]; }
  get usage() {
    const s = live(this, 'Edb'), w = invoke(s.engine, 'usage', 14, [s.id]).words;
    return Object.freeze({ facts: w[0], textBytes: w[1], textCapacity: w[2] });
  }
  addFact(predicate, terms) { this.addFacts([{ predicate, terms }]); }
  addFacts(facts) {
    const s = live(this, 'Edb');
    if (s.busy) throw invalidState('Reentrant EDB mutation');
    if (s.frozen) throw invalidState('EDB is frozen; call reset()');
    s.busy = true;
    try {
      const pool = strings(), frame = factWords(facts, pool, s.capacity - this.count);
      live(this, 'Edb');
      invoke(s.engine, 'addFacts', 13, [s.id, frame.count, ...frame.words], pool.finish());
    } finally { s.busy = false; }
  }
  clear() { this._clear(false); }
  reset() { this._clear(true); }
  _clear(reset) {
    const s = live(this, 'Edb');
    if (s.busy) throw invalidState('Reentrant EDB mutation');
    invoke(s.engine, reset ? 'reset' : 'clear', 12, [s.id, Number(reset)]);
    s.frozen = false;
  }
  close() { close(this); }
  [Symbol.dispose]() { this.close(); }
}
class Session {
  constructor(token, ruleset, id) { initialize(this, token, 'Session', ruleset, id, 3); }
  get capacities() {
    const s = live(this, 'Session'), words = invoke(s.engine, 'capacities', 10, [s.id]).words;
    return new SessionCapacities(Object.fromEntries(CAPACITIES.map((key, i) => [key, words[i]])));
  }
  get fingerprint() { const s = live(this, 'Session'); return invoke(s.engine, 'fingerprint', 11, [2, s.id]).text; }
  get executionFingerprint() { const s = live(this, 'Session'); return invoke(s.engine, 'executionFingerprint', 11, [3, s.id]).text; }
  inputs(options = {}) {
    const s = live(this, 'Session');
    if (s.busy || s.inputs || s.result) throw invalidState('Session is busy or already has retained input');
    s.busy = true;
    try {
      const factCapacity = u32(options.factCapacity ?? this.capacities.inputFacts, 'factCapacity');
      const additionCapacity = u32(options.additionCapacity ?? factCapacity, 'additionCapacity');
      const removalCapacity = u32(options.removalCapacity ?? factCapacity, 'removalCapacity');
      const symbols = options.symbols ?? [], pool = strings(), vocabulary = [];
      if (typeof symbols === 'string') throw new TypeError('symbols must be an iterable of strings');
      let count = 0;
      for (const symbol of symbols) {
        if (++count > s.engine.limits.maxSymbols) throw new RangeError('Too many vocabulary entries');
        vocabulary.push(...pool.add(symbol));
      }
      live(this, 'Session');
      const id = invoke(s.engine, 'attach retained input', 22,
        [s.id, factCapacity, additionCapacity, removalCapacity, count, ...vocabulary], pool.finish()).words[0];
      const inputs = new SessionInputs(secret, this, id);
      states.get(inputs).capacities = [factCapacity, additionCapacity, removalCapacity];
      s.inputs = inputs; return inputs;
    } finally { s.busy = false; }
  }
  solve(edb) {
    const s = live(this, 'Session'), input = live(edb, 'Edb');
    if (s.busy) throw invalidState('Session input is being staged');
    if (input.parent !== s.parent) throw new TypeError('EDB belongs to another ruleset');
    if (input.busy) throw invalidState('EDB is being staged');
    const id = invoke(s.engine, 'solve', 15, [s.id, input.id]).words[0];
    const result = new SolveResult(secret, this, id); s.result = result; input.frozen = true; return result;
  }
  close() { close(this); }
  [Symbol.dispose]() { this.close(); }
}
class InputBase {
  constructor(incarnation, generation) {
    uint64(incarnation, 'incarnation'); uint64(generation, 'generation');
    this.incarnation = BigInt(incarnation); this.generation = BigInt(generation);
    Object.freeze(this);
  }
}
class SessionInputs {
  constructor(token, session, id) { initialize(this, token, 'SessionInputs', session, id, 5); }
  get base() {
    const s = live(this, 'SessionInputs'), w = invoke(s.engine, 'input base', 23, [s.id]).words;
    return new InputBase(BigInt(w[0]) | BigInt(w[1]) << 32n, BigInt(w[2]) | BigInt(w[3]) << 32n);
  }
  replace(base, facts) { return this._transact(base, facts, [], true); }
  apply(base, changes = {}) {
    // Read user getters only while the session's reentrancy guard is held.
    return this._transact(base, changes, undefined, false);
  }
  _transact(base, first, second, replace) {
    const s = live(this, 'SessionInputs'), session = live(s.parent, 'Session');
    if (session.busy || session.result) throw invalidState('Session is busy or has a live result');
    session.busy = true;
    try {
      if (!(base instanceof InputBase)) throw new TypeError('base must be InputBase');
      const prefix = [s.id, ...uint64(base.incarnation, 'incarnation'), ...uint64(base.generation, 'generation')];
      const pool = strings(), added = replace ? first : (first.added ?? []), removed = replace ? second : (first.removed ?? []);
      const a = factWords(added, pool, s.capacities[replace ? 0 : 1]);
      const r = factWords(removed, pool, s.capacities[2]);
      live(this, 'SessionInputs'); live(s.parent, 'Session');
      const id = invoke(s.engine, replace ? 'replace inputs' : 'apply inputs', replace ? 24 : 25,
        [...prefix, a.count, ...(replace ? [] : [r.count]), ...a.words, ...r.words], pool.finish()).words[0];
      const result = new SolveResult(secret, s.parent, id); session.result = result; return result;
    } finally { session.busy = false; }
  }
  close() { close(this); }
  [Symbol.dispose]() { this.close(); }
}
class ResultTerm {
  constructor(token, result, kind, value) {
    if (token !== secret) throw new TypeError('ResultTerm is created by enumerateRaw');
    states.set(this, { type: 'ResultTerm', result, kind, value });
    this.kind = ['invalid', 'symbol', 'integer', 'boolean'][kind]; this.value = value; Object.freeze(this);
  }
  resolve() { const s = states.get(this); return s.result.resolveTerm(this); }
}
class SolveResult {
  constructor(token, session, id) { initialize(this, token, 'SolveResult', session, id, 4); }
  get fingerprint() { return live(this, 'SolveResult').parent.fingerprint; }
  get executionFingerprint() { return live(this, 'SolveResult').parent.executionFingerprint; }
  containsFact(predicate, terms) {
    const s = live(this, 'SolveResult'); return factCall(this, 'containsFact', 16, [s.id], predicate, terms).words[0] !== 0;
  }
  derivedFactCount() { const s = live(this, 'SolveResult'); return invoke(s.engine, 'derivedFactCount', 20, [s.id]).words[0]; }
  enumerateRaw(predicate, arity) {
    const s = live(this, 'SolveResult'), pool = strings();
    u32(arity, 'arity'); if (arity > 4) throw new RangeError('Arity exceeds four');
    const words = [s.id, arity, ...pool.add(predicate)];
    const out = invoke(s.engine, 'enumerateRaw', 17, words, pool.finish()).words;
    return Array.from({ length: out[0] }, (_, i) => Array.from({ length: arity }, (_, j) => {
      const p = 1 + (i * arity + j) * 3, kind = out[p], lo = out[p + 1], hi = out[p + 2];
      const value = kind === 1 ? lo : kind === 2 ? BigInt.asIntN(64, BigInt(lo) | BigInt(hi) << 32n) : Boolean(lo);
      return new ResultTerm(secret, this, kind, value);
    }));
  }
  enumeratePredicateFacts(predicate, arity) { return this.enumerateRaw(predicate, arity).map(row => row.map(term => this.resolveTerm(term))); }
  resolveTerm(term) {
    const s = live(this, 'SolveResult'), t = states.get(term);
    if (!t || t.type !== 'ResultTerm' || t.result !== this) throw new TypeError('Term belongs to another result');
    return t.kind === 1 ? invoke(s.engine, 'resolveTerm', 18, [s.id, t.value]).text : t.value;
  }
  explainTrue(predicate, terms) { const s = live(this, 'SolveResult'); return factCall(this, 'explainTrue', 19, [s.id, 1], predicate, terms).text; }
  explainFalse(predicate, terms) { const s = live(this, 'SolveResult'); return factCall(this, 'explainFalse', 19, [s.id, 2], predicate, terms).text; }
  close() { close(this); }
  [Symbol.dispose]() { this.close(); }
}
const api = { Predicate, SessionCapacities, Ruleset, Edb, Session, SessionInputs, InputBase, SolveResult, ResultTerm,
  Capability, ExplanationKind, Status, MaelysDatalogError, PRED_EDB, PRED_IDB, PRED_QUERY, PRED_POLICY_FACT };
function binding(createTransport) {
  class Engine extends EngineBase {
    static async create(options = {}) {
      const transport = await createTransport(options);
      try { return new Engine(secret, transport); }
      catch (error) { transport.close(); throw error; }
    }
  }
  return Object.freeze({ Engine, ...api });
}
module.exports = { binding };
