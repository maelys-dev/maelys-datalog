/**
 * maelys_playground.js — Ergonomic wrapper over MaelysDatalogDynamic.
 * Located in js/ (JS distribution), not src/wasm/ (C source).
 *
 * Paths: from js/, build/wasm is one level up (../build/wasm/).
 *        from tests/wasm/, build/wasm is two levels up (../../build/wasm/).
 *
 * Browser (MaelysDatalogDynamic loaded via script tag):
 *   const wasmUrl = new URL('../build/wasm/maelys_datalog_dynamic.wasm', import.meta.url).href
 *   const pg = await MaelysPlayground.create(MaelysDatalogDynamic, wasmUrl)
 *
 * Node.js (from tests/wasm/test_playground.mjs):
 *   import MaelysDatalogDynamic from '../../build/wasm/maelys_datalog_dynamic.js'
 *   import { MaelysPlayground, PredKind } from '../../js/maelys_playground.js'
 *   const wasmUrl = new URL('../../build/wasm/maelys_datalog_dynamic.wasm', import.meta.url).href
 *   const pg = await MaelysPlayground.create(MaelysDatalogDynamic, wasmUrl)
 *
 * EDB design note:
 *   string helpers are ergonomic sugar over the WASM symbol-id path.
 *   Runtime EDB symbols are interned dynamically and are not required to be
 *   pre-declared as policy atoms.
 */

'use strict';

const PredKind = Object.freeze({
  EDB: 1,
  IDB: 2,
  QUERY: 4,
  POLICY_FACT: 8,
});

const MAELYS_OK = 0;
const MAELYS_ERR_INVALID_ARGUMENT = -1;
const MAELYS_ERR_INVALID_FIELD = -2;
const MAELYS_ERR_INVALID_STATE = -13;

function makeError(name, rc, msg) {
  return new Error(`${name} failed (rc=${rc})${msg ? ': ' + msg : ''}`);
}

class MaelysPlayground {
  constructor(mod) {
    this._mod = mod;
  }

  static async create(factory, wasmUrl) {
    const opts = wasmUrl
      ? {
          locateFile: (f) => (f.endsWith('.wasm') ? wasmUrl : f),
        }
      : {};
    return new MaelysPlayground(await factory(opts));
  }

  _call(name, retType, argTypes, args) {
    return this._mod.ccall(name, retType, argTypes, args);
  }

  _diag() {
    return this._call('maelys_datalog_wasm_last_diag_message', 'string', [], []);
  }

  _check(rc, name) {
    if (rc !== MAELYS_OK) throw makeError(name, rc, this._diag());
  }

  domainBegin(name) {
    this._check(
      this._call('maelys_datalog_wasm_domain_begin', 'number', ['string'], [name]),
      'domainBegin',
    );
    return this;
  }

  domainAddPredicate(name, arity, kindFlags) {
    this._check(
      this._call('maelys_datalog_wasm_domain_add_predicate',
                 'number',
                 ['string', 'number', 'number'],
                 [name, arity, kindFlags]),
      'domainAddPredicate',
    );
    return this;
  }

  domainCommit() {
    this._check(this._call('maelys_datalog_wasm_domain_commit', 'number', [], []), 'domainCommit');
    return this;
  }

  domainAbort() {
    this._call('maelys_datalog_wasm_domain_abort', null, [], []);
    return this;
  }

  loadPolicy(domainName, policyId, src) {
    const srcLen = this._mod.lengthBytesUTF8(src);
    this._check(
      this._call('maelys_datalog_wasm_load_policy',
                 'number',
                 ['string', 'string', 'string', 'number'],
                 [domainName, policyId, src, srcLen]),
      'loadPolicy',
    );
    return this;
  }

  edbBegin() {
    this._check(this._call('maelys_datalog_wasm_edb_begin', 'number', [], []), 'edbBegin');
    return this;
  }

  addFact(pred, arg0) {
    this._check(
      this._call('maelys_datalog_wasm_edb_add_symbol', 'number', ['string', 'string'], [pred, arg0]),
      'addFact',
    );
    return this;
  }

  internRuntimeSymbol(text) {
    const ptr = this._mod._malloc(4);
    if (!ptr) throw new Error('internRuntimeSymbol failed: malloc returned 0');
    try {
      this._check(
        this._call('maelys_datalog_wasm_edb_intern_runtime_symbol',
                   'number',
                   ['string', 'number'],
                   [text, ptr]),
        'internRuntimeSymbol',
      );
      return this._mod.getValue(ptr, 'i32');
    } finally {
      this._mod._free(ptr);
    }
  }

  addSymbolIdFact(pred, symbolId) {
    this._check(
      this._call('maelys_datalog_wasm_edb_add_symbol_id_fact',
                 'number',
                 ['string', 'number'],
                 [pred, symbolId]),
      'addSymbolIdFact',
    );
    return this;
  }

  addSymbolIdsFact(pred, left, right) {
    this._check(
      this._call('maelys_datalog_wasm_edb_add_symbol_ids_fact',
                 'number',
                 ['string', 'number', 'number'],
                 [pred, left, right]),
      'addSymbolIdsFact',
    );
    return this;
  }

  addFact2(pred, arg0, arg1) {
    this._check(
      this._call('maelys_datalog_wasm_edb_add_symbol2',
                 'number',
                 ['string', 'string', 'string'],
                 [pred, arg0, arg1]),
      'addFact2',
    );
    return this;
  }

  solve() {
    this._check(this._call('maelys_datalog_wasm_solve', 'number', [], []), 'solve');
    return this;
  }

  querySymbol(pred, arg0) {
    const rc = this._call('maelys_datalog_wasm_query_symbol', 'number', ['string', 'string'], [pred, arg0]);
    if (rc === -1) throw makeError('querySymbol', -1, this._diag());
    return rc === 1;
  }

  querySymbol2(pred, arg0, arg1) {
    const rc = this._call('maelys_datalog_wasm_query_symbol2',
                          'number',
                          ['string', 'string', 'string'],
                          [pred, arg0, arg1]);
    if (rc === -1) throw makeError('querySymbol2', -1, this._diag());
    return rc === 1;
  }

  freeResult() {
    this._call('maelys_datalog_wasm_solve_result_free', null, [], []);
    return this;
  }

  get diagMessage() {
    return this._diag();
  }

  get diagCode() {
    return this._call('maelys_datalog_wasm_last_diag_code', 'number', [], []);
  }
}

const api = {
  PredKind,
  MAELYS_OK,
  MAELYS_ERR_INVALID_ARGUMENT,
  MAELYS_ERR_INVALID_FIELD,
  MAELYS_ERR_INVALID_STATE,
  MaelysPlayground,
};

if (typeof module !== 'undefined' && module.exports) {
  module.exports = api;
}

if (typeof globalThis !== 'undefined') {
  globalThis.PredKind = PredKind;
  globalThis.MAELYS_OK = MAELYS_OK;
  globalThis.MAELYS_ERR_INVALID_ARGUMENT = MAELYS_ERR_INVALID_ARGUMENT;
  globalThis.MAELYS_ERR_INVALID_FIELD = MAELYS_ERR_INVALID_FIELD;
  globalThis.MAELYS_ERR_INVALID_STATE = MAELYS_ERR_INVALID_STATE;
  globalThis.MaelysPlayground = MaelysPlayground;
}
