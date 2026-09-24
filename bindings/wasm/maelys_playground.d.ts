/* SPDX-License-Identifier: MPL-2.0 */
export type InputValue = string | bigint | number | boolean;
export type Value = string | bigint | boolean;
export interface Fact { predicate: string; terms: InputValue[] }
export interface Domain {
  name: string;
  predicates: { name: string; arity: number; flags: number }[];
  atoms?: string[];
}
export interface Limits {
  maxSymbols: number; stringPoolBytes: number; maxPredicates: number;
  maxRules: number; maxArity: number; maxBodyLiterals: number; maxDepth: number;
  maxEdbFacts: number; maxIdbFacts: number; maxFactsPerPred: number;
  maxStringBytes: number; inputEdbTextBytes: number;
}
export interface Diagnostic {
  readonly status: number; readonly source: number; readonly code: number;
  readonly present: bigint; readonly phase: string; readonly message: string;
  readonly hint: string; readonly codeName: string; readonly statusName: string;
  readonly location?: Readonly<{ line: number; column: number; file: string }>;
  readonly predicate?: Readonly<{ name: string; arity: number }>;
  readonly capacity?: Readonly<{ observed: number; limit: number; kind: number }>;
  readonly depth?: Readonly<{ observed: number; limit: number }>;
  readonly comparison?: Readonly<{ result: number; expectedKind: number; lhsKind: number; rhsKind: number; op: number }>;
  readonly arity?: Readonly<{ expected: number; observed: number; termIndex: number }>;
  readonly rule?: Readonly<{ id: number }>;
  readonly context?: Readonly<{ token: string; field: string; domain: string }>;
}
export const PredKind: Readonly<{ EDB: 1; IDB: 2; QUERY: 4; POLICY_FACT: 8 }>;
export const Status: Readonly<{ OK: 0; INVALID_ARGUMENT: -1; INVALID_FIELD: -2;
  NOT_FOUND: -3; NOT_IMPLEMENTED: -4; UNSUPPORTED: -5; TIMEOUT: -6; IO: -7;
  INTERNAL: -8; UNAUTHORIZED: -9; FORBIDDEN: -10; RATE_LIMITED: -11;
  PAYLOAD_TOO_LARGE: -12; INVALID_STATE: -13; STORAGE_TOO_SMALL: -14 }>;
export class DatalogError extends Error {
  constructor(operation: string, status: number, diagnostic: Diagnostic);
  readonly status: number;
  readonly diagnostic: Diagnostic;
}
export interface WasmModule {
  ccall(name: string, result: string, types: string[], args: number[]): number | string;
  _malloc(bytes: number): number;
  _free(pointer: number): void;
  HEAPU8: Uint8Array;
  UTF8ToString(pointer: number): string;
}
export class MaelysPlayground {
  constructor(module: WasmModule);
  static create(factory: (options: object) => Promise<WasmModule>, wasmUrl?: string): Promise<MaelysPlayground>;
  buildLimits(): Readonly<Limits>;
  registerDomain(domain: Domain): this;
  loadPolicy(domain: string, id: string, source: string): this;
  clearFacts(): this;
  addFacts(facts: Fact[]): this;
  inputUsage(): { facts: number; textBytes: number; textCapacity: number };
  solve(): this;
  freeResult(): this;
  query(predicate: string, terms: InputValue[]): boolean;
  enumerate(predicate: string, arity: number): Value[][];
  explainTrue(predicate: string, terms: InputValue[]): string;
  explainFalse(predicate: string, terms: InputValue[]): string;
  derivedFactCount(): number;
  fingerprints(): { policy: string; session: string; execution: string };
  close(): void;
}
