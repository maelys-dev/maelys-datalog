/* SPDX-License-Identifier: MPL-2.0 */
import { binding } from './core.mjs';
import { wasmTransport } from './wasm-transport.mjs';
const api = binding(async ({ profile = 'small', wasmFactory, wasmUrl } = {}) => {
  if (profile !== 'small' && profile !== 'large') throw new TypeError('Unknown engine profile');
  const factory = wasmFactory || (await import(`../wasm/${profile}/engine.mjs`)).default;
  return wasmTransport(await factory(wasmUrl ? { locateFile: file => file.endsWith('.wasm') ? String(wasmUrl) : file } : {}));
});
export const { Engine, Predicate, SessionCapacities, Ruleset, Edb, Session, SessionInputs, InputBase, SolveResult, ResultTerm,
  Capability, ExplanationKind, Status, MaelysDatalogError, PRED_EDB, PRED_IDB, PRED_QUERY, PRED_POLICY_FACT } = api;
