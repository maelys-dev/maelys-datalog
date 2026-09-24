/* SPDX-License-Identifier: MPL-2.0 */
import pkg from '../../bindings/wasm/maelys_playground.js';
export const { MaelysPlayground, PredKind: P, Status: S, DatalogError } = pkg;
export const profile = process.env.MAELYS_WASM_PROFILE || 'small';
if (!['small', 'large'].includes(profile)) throw new Error('Invalid profile');
const dir = process.env.MAELYS_WASM_BUILD_DIR
  ? new URL(`file://${process.env.MAELYS_WASM_BUILD_DIR}/`)
  : new URL(`../../build/${profile === 'large' ? 'wasm-large' : 'wasm'}/`, import.meta.url);
export const wasmURL = new URL('maelys_datalog_dynamic.wasm', dir);
const { default: factory } = await import(new URL('maelys_datalog_dynamic.js', dir).href);
export async function create() { return MaelysPlayground.create(factory, wasmURL.href); }
export const fact = (predicate, ...terms) => ({ predicate, terms });
export const pred = (name, arity, flags) => ({ name, arity, flags });
export function status(code) { return e => e instanceof DatalogError && e.status === code; }
