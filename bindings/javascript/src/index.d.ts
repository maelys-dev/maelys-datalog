/* SPDX-License-Identifier: MPL-2.0 */
/// <reference lib="esnext.disposable" />
/** A symbol, an exact signed int64, or a boolean. Unsafe numbers are rejected. */
export type InputValue = string | bigint | number | boolean;
/** Integers returned by either runtime always use bigint. */
export type Value = string | bigint | boolean;
/** One input fact, staged atomically with the rest of its batch. */
export interface Fact { readonly predicate: string; readonly terms: readonly InputValue[] }
/** The public methods needed from an Emscripten module factory. */
export interface WasmModule {
  ccall(name: string, result: string | null, types: string[], args: number[]): number | string;
  _malloc(bytes: number): number;
  _free(pointer: number): void;
  HEAPU8: Uint8Array;
}
/** Both entry points accept the same options; WASM uses its optional asset resolver. */
export interface EngineOptions {
  profile?: 'small' | 'large';
  wasmFactory?: (options: { locateFile?: (file: string) => string }) => Promise<WasmModule>;
  wasmUrl?: string | URL;
}
/** Native loads paths; both runtimes accept an in-memory manifest and source bundle. */
export type ManifestInput = { path: string; text?: never; policies?: never } |
  { text: string; policies: readonly { policyId: string; source: string }[]; path?: never };
/** Independent manifest permissions; SHA-256 verification remains enabled. */
export interface ManifestOptions { allowTestOnly?: boolean; allowUndeclaredPolicyAtoms?: boolean }
/** Fixed session quotas. Omitted fields retain profile defaults; zero is explicit. */
export interface CapacityOptions { inputFacts?: number; derivedFacts?: number; symbols?: number; textBytes?: number }
/** Select a policy, required capabilities, and opt-in explanation storage. */
export interface SessionOptions {
  policyIndex?: number;
  requiredCapabilities?: bigint | number;
  workLimit?: bigint | number;
  explanations?: number;
  capacities?: CapacityOptions;
}
/** Bounds for the reusable input buffer; independent of session quotas. */
export interface EdbOptions { factCapacity?: number; textCapacity?: number }
/** Fixed retained-input storage, opt-in before a session's first solve.
 * Omitted factCapacity defaults to E; omitted batch capacities to factCapacity.
 * symbols extends the immutable program vocabulary. Zero capacities are legal. */
export interface InputOptions {
  factCapacity?: number; additionCapacity?: number; removalCapacity?: number;
  symbols?: Iterable<string>;
}
/** Exact unsigned 64-bit token. Old or foreign bases are rejected by the engine. */
export class InputBase {
  constructor(incarnation: bigint | number, generation: bigint | number);
  readonly incarnation: bigint; readonly generation: bigint;
}
/** Retained dynamic input; compiled policy facts remain separate. */
export class SessionInputs implements Disposable {
  private constructor();
  readonly base: InputBase;
  /** Replace the whole input. Failure preserves its committed facts and base. */
  replace(base: InputBase, facts: Iterable<Fact>): SolveResult;
  /** Independent raw batch bounds apply before deduplication. Additions win.
   * Absent removals (including unknown symbols) are no-ops after validation.
   * Every success, including a set no-op, advances base and leases a result. */
  apply(base: InputBase, changes?: { added?: Iterable<Fact>; removed?: Iterable<Fact> }): SolveResult;
  /** Refused while a result is live. Closing the parent closes result first. */
  close(): void;
  [Symbol.dispose](): void;
}
/** Current EDB occupancy, including interned text terminators. */
export interface EdbUsage { readonly facts: number; readonly textBytes: number; readonly textCapacity: number }
export interface Limits {
  maxSymbols: number; stringPoolBytes: number; maxPredicates: number;
  maxRules: number; maxArity: number; maxBodyLiterals: number; maxDepth: number;
  maxEdbFacts: number; maxIdbFacts: number; maxFactsPerPred: number;
  maxStringBytes: number; inputEdbTextBytes: number;
  maxPolicyAtoms: number; maxPolicyAtomBytes: number;
}
/** Compiled registry entries (including unused declarations), policy facts,
 * and normalized rules (including separate OR alternatives). Not session quotas. */
export interface ProgramCounts { readonly predicates: number; readonly facts: number; readonly rules: number }
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
  readonly aggregate?: Readonly<{ operator: string; value: string; valueKind: number; termIndex: number; limit: number }>;
  readonly context?: Readonly<{ token: string; field: string; domain: string }>;
}
export const Status: Readonly<{ OK: 0; INVALID_ARGUMENT: -1; INVALID_FIELD: -2;
  NOT_FOUND: -3; NOT_IMPLEMENTED: -4; UNSUPPORTED: -5; TIMEOUT: -6; IO: -7;
  INTERNAL: -8; UNAUTHORIZED: -9; FORBIDDEN: -10; RATE_LIMITED: -11;
  PAYLOAD_TOO_LARGE: -12; INVALID_STATE: -13; STORAGE_TOO_SMALL: -14 }>;

export const PRED_EDB: 1;
export const PRED_IDB: 2;
export const PRED_QUERY: 4;
export const PRED_POLICY_FACT: 8;
/** Backend feature bits; independent of the explanation workspace mask. */
export const Capability: Readonly<{
  POSITIVE: bigint; NEGATION: bigint; COMPARISONS: bigint; ARITHMETIC: bigint;
  FILTERS: bigint; EXPLAIN_TRUE: bigint; WORK_LIMIT: bigint; EXPLAIN_FALSE: bigint;
  AGGREGATES: bigint; MIN: bigint; MAX: bigint; SUM: bigint;
}>;
/** Workspace kinds may be ORed together; no workspace is reserved by default. */
export const ExplanationKind: Readonly<{ TRUE: 1; FALSE: 2 }>;
/** A failed engine operation and its copied, structured diagnostic. */
export class MaelysDatalogError extends Error {
  constructor(operation: string, status: number, diagnostic: Diagnostic);
  readonly code: number;
  readonly status: number;
  readonly diagnostic: Diagnostic;
}
/** A domain predicate declaration. Construction does not register it. */
export class Predicate {
  constructor(name: string, arity: number, flags: number);
  readonly name: string;
  readonly arity: number;
  readonly flags: number;
  static edb(name: string, arity: number): Predicate;
  static edbQuery(name: string, arity: number): Predicate;
  static idb(name: string, arity: number): Predicate;
  static idbQuery(name: string, arity: number): Predicate;
  static policyFact(name: string, arity: number): Predicate;
  static policyFactQuery(name: string, arity: number): Predicate;
}
/** Validated portable quotas, each between zero and UINT32_MAX. */
export class SessionCapacities implements CapacityOptions {
  constructor(options?: CapacityOptions);
  readonly inputFacts?: number;
  readonly derivedFacts?: number;
  readonly symbols?: number;
  readonly textBytes?: number;
}
/** Owns independent rulesets; close cascades to all descendants. */
export class Engine implements Disposable {
  private constructor();
  /** Initialize the selected runtime and size profile. No backend fallback. */
  static create(options?: EngineOptions): Promise<Engine>;
  readonly limits: Readonly<Limits>;
  /** Register predicates and permitted policy atoms in the engine registry. */
  registerDomain(name: string, predicates: readonly Predicate[], atoms?: readonly string[]): void;
  /** Compile a policy against an already registered domain. */
  loadInlineRuleset(domain: string, id: string, source: string): Ruleset;
  /** Validate a manifest and load its enabled, hash-verified policies. */
  loadManifest(input: ManifestInput, options?: ManifestOptions): Ruleset;
  /** Release the engine and all its descendants. Idempotent. */
  close(): void;
  [Symbol.dispose](): void;
}
/** An immutable loaded policy set and the owner of its inputs and sessions. */
export class Ruleset implements Disposable {
  private constructor();
  readonly policyCount: number;
  /** Read the selected compiled program without preparing a session. */
  programCounts(policyIndex?: number): Readonly<ProgramCounts>;
  readonly fingerprint: string;
  /** Reserve a reusable input EDB with explicit or profile-default bounds. */
  edb(options?: EdbOptions): Edb;
  /** Prepare reusable fixed session storage for one selected policy. */
  prepare(options?: SessionOptions): Session;
  /** Solve once; closing the result also closes its private session. */
  solve(edb: Edb, options?: SessionOptions): SolveResult;
  close(): void;
  [Symbol.dispose](): void;
}
/** Reusable facts tied to one ruleset; a successful solve freezes append/clear. */
export class Edb implements Disposable {
  private constructor();
  readonly count: number;
  readonly usage: Readonly<EdbUsage>;
  addFact(predicate: string, terms: readonly InputValue[]): void;
  /** Append a staged batch atomically; failure preserves the existing input. */
  addFacts(facts: Iterable<Fact>): void;
  /** Clear mutable input without releasing its storage. */
  clear(): void;
  /** Clear and unfreeze input. Existing results retain their snapshots. */
  reset(): void;
  close(): void;
  [Symbol.dispose](): void;
}
/** A prepared execution context with at most one live result. */
export class Session implements Disposable {
  private constructor();
  readonly capacities: Readonly<Required<CapacityOptions>>;
  readonly fingerprint: string;
  readonly executionFingerprint: string;
  /** Attach retained input before the first successful solve. Ordinary solve
   * is unavailable while attached. Storage is allocated by the binding. */
  inputs(options?: InputOptions): SessionInputs;
  /** Solve an EDB from the same ruleset; release the previous result first. */
  solve(edb: Edb): SolveResult;
  close(): void;
  [Symbol.dispose](): void;
}
/** An immutable term whose symbol identity belongs to exactly one live result. */
export class ResultTerm {
  private constructor();
  readonly kind: 'symbol' | 'integer' | 'boolean';
  readonly value: number | bigint | boolean;
  resolve(): Value;
}
/** A leased snapshot with query, enumeration and canonical explanations. */
export class SolveResult implements Disposable {
  private constructor();
  readonly fingerprint: string;
  readonly executionFingerprint: string;
  containsFact(predicate: string, terms: readonly InputValue[]): boolean;
  derivedFactCount(): number;
  /** Copy resolved values; these remain usable after close. */
  enumeratePredicateFacts(predicate: string, arity: number): Value[][];
  /** Return result-bound terms whose resolve() requires this result to stay open. */
  enumerateRaw(predicate: string, arity: number): ResultTerm[][];
  resolveTerm(term: ResultTerm): Value;
  explainTrue(predicate: string, terms: readonly InputValue[]): string;
  explainFalse(predicate: string, terms: readonly InputValue[]): string;
  close(): void;
  [Symbol.dispose](): void;
}
