# Phase 2 — Elaboration, Task 5.2: Signal & Port Resolution

CSE215/CSE312 Electronic Design Automation, Ain Shams University (iCHEP).

This document specifies the deliverable implemented in `src/sig.cpp`. It follows the
conventions of `Specification.md` and `Elaboration.md`: **host** = the tool itself (now
C++), **target** = the Verilog file the tool reads.

---

## 1. Why this task exists

Task 5.1 (`src/elab.cpp`, `Elaboration.md`) answers *which instances exist and how they
nest*. It builds the module table and the first-child/next-sibling instance tree, and
confirms every instantiation names a real module with a resolvable port association. What
it does **not** do is say what a port or a wire actually *is*: no widths, no per-instance
parameter values, and no record of which net a port connects to in its parent's scope.

Task 5.2 is the pass that turns names into indices:

| # | Responsibility | Implemented by |
|---|---|---|
| 1 | Resolve input/output port mappings | `flatten_connections` |
| 2 | Connect wires and registers | `build_scope`, `scan_decls` |
| 3 | Link module ports to parent signals | `flatten_connections` (`cn_net`) |
| 4 | Validate signal names | `find_sig_local`, `resolve_conn_expr` |
| 5 | Report undefined signals or invalid connections | `add_diag` calls throughout |

It also resolves the two things that determine a signal's *shape* rather than its
*existence*: bit width (`resolve_widths`) and per-instance parameter values
(`build_params`, `apply_overrides`). Widths cannot be resolved before parameters are
final, because a range like `[WIDTH-1:0]` is itself a parameter-dependent expression —
this is why the pass runs in three ordered sub-passes (§3).

Task 5.1's `check_conns` already validated the *shape* of every instance's connection
list (named XOR positional, arity in range) — that a 3-port module instantiated with 5
positional connections is an error, for instance. Task 5.2 does not re-validate that; it
consumes the already-confirmed shape and resolves each connection's two endpoints to
actual signal-table rows.

---

## 2. Data representation

Three tables of parallel arrays (`sg_*`, `pm_*`, `cn_*`), following the same arena
convention as Task 5.1's `mt_*`/`in_*` tables: integer indices as handles, `-1` as the
null handle.

### 2.1 Signal table — one row per port or per locally declared wire/reg, per instance

| Field | Meaning |
|---|---|
| `sg_name[i]` | local name (not a path) |
| `sg_inst[i]` | owning instance (an `in_*` index) |
| `sg_node[i]` | the `ND_PORT` / `ND_NET` / `ND_REG` AST handle |
| `sg_isport[i]` | true for a port, false for a locally declared net/reg |
| `sg_dir[i]` | `DIR_*` for a port, `DIR_NONE` for a plain net |
| `sg_width[i]` | resolved bit width, always ≥ 1 |

Every instance's own ports occupy a contiguous run at the front of its scope:
`sg_*` indices `[in_sig0[i] .. in_sig0[i] + mt_nports[in_mod[i]] - 1)`, in header
order. `build_scope` adds ports first, specifically so a positional connection can
index straight into that range via `port_local_at` without a name lookup.

### 2.2 Parameter table — one row per parameter, per instance

Defaults are copied from the module definition (header `#( )` list, then body
`parameter` declarations, in declaration order), then overridden by that specific
instance's own `#( )` list. Two instances of the same module can therefore end up
with different resolved widths — this is exercised directly by `test_elab_sig.cpp`'s
"two instances, independent overrides" case.

### 2.3 Connection table — one row per resolved port↔net binding

| Field | Meaning |
|---|---|
| `cn_inst[i]` | the child instance (an `in_*` index) |
| `cn_port[i]` | `sg_*` index in the **child's own** scope |
| `cn_net[i]` | `sg_*` index in the **parent's** scope, or `-1` |

`cn_net == -1` covers two distinct legal cases, not an error by itself: an
explicitly unconnected port (a short positional list — legal Verilog, already
warned about by Task 5.1) and a connection expression that failed to resolve (which
*is* separately diagnosed, at the point of failure). The connection table always has
exactly one row per port per instance — a port never mentioned in the source still
gets a row with `cn_net == -1` — so "report undefined signals or invalid
connections" (project brief) reads as completeness, not best-effort.

---

## 3. The algorithm

```
resolve_signals():
    for each instance i (already in preorder from Task 5.1):
        build_scope(i)     # ports, then local wire/reg decls
        build_params(i)    # defaults, then this instance's #( ) overrides
    for each instance i:
        resolve_widths(i)  # now that every instance's parameters are final
    for each instance i:
        flatten_connections(i)   # parent-scope lookups are now safe
```

Three ordered passes over the instance table, not one combined pass, because each
pass depends on every instance having finished the previous one:

1. **Scope + parameters** — independent per instance, but pass 3 needs to look
   *sideways* into a parent's already-built scope, so every instance's own table
   must exist first.
2. **Widths** — a range expression may reference that instance's own parameter, so
   parameters must be final before any width is resolved.
3. **Connections** — a connection's parent-side expression is resolved against
   `in_parent[i]`'s scope, which by this point is guaranteed complete.

### 3.1 Constant-expression evaluation

Parameter values and range bounds must fold to a plain integer without touching any
signal — `eval_const` walks `ND_LITERAL` / `ND_IDENT` (parameter lookup only) /
`ND_UNARY` / `ND_BINARY` / `ND_TERNARY` and evaluates them directly. Two things are
diagnosed as **not a constant expression**, not silently defaulted to zero:
- an identifier that isn't a parameter in scope,
- a literal containing an `x`/`z` bit (`FourState::hasUnknown`) — a range or
  parameter genuinely cannot be partially unknown, the same way real Verilog
  requires elaboration-time constants to be fully known values.

### 3.2 Connection expression resolution — subset, stated explicitly

Connection expressions are resolved as **bare identifiers only**. A bit-select,
part-select, or concatenation on the connection side is recognised and flagged
(`SEV_WARNING`, "too complex to flatten in this subset") rather than mis-resolved or
silently dropped. None of the ten golden `.v` designs use anything richer on a
connection. If a design needs one, extend `resolve_conn_expr` — do not work around
the limitation elsewhere.

### 3.3 Duplicate declarations

Task 5.1 has no concept of a signal table at all, so a port re-declared as a body
`wire` of the same name is invisible to it. `build_scope`/`scan_decls` is the first
pass that can catch this, because it is the first pass with a per-instance scope to
check against (`tests/golden/top_dup_sig.v`).

---

## 4. Output

`+dump_sig [+sigout=<f>]` writes a deterministic, line/column-free S-expression pair
(same convention as `dump_hier`, §6 of `Specification.md`):

```
(signals
  (inst "ripple_adder4.fa0.h0"
    (sig "a" input width=1)
    (sig "b" input width=1)
    (sig "y" output width=1)
    (sig "cout" output width=1))
  ...)
(connections
  (bind "ripple_adder4.fa0.h0.a" -> "ripple_adder4.fa0.a")
  (bind "ripple_adder4.fa0.h0.b" -> "ripple_adder4.fa0.b")
  ...)
```

An unresolved connection prints as `<unconnected>` rather than a path.

Console summary (from `main.cpp`, only printed once elaboration itself succeeded):

```
RESOLVE OK: 42 signal(s), 30 connection(s)
```

Diagnostics from this pass share the same collector as Task 5.1 and Phase 1 parse
diagnostics — one sorted printout at the end of the run, not a separate stream per
phase.

---

## 5. Acceptance

| # | Criterion | Where verified |
|---|---|---|
| 1 | ports become signal-table rows, in header order, at the front of the scope | `test_elab_sig.cpp` |
| 2 | locally declared wires/regs are added after the ports | `test_elab_sig.cpp` |
| 3 | an unranged signal resolves to width 1 | `test_elab_sig.cpp` |
| 4 | a ranged port whose range references a parameter resolves using that module's own default | `test_elab_sig.cpp` |
| 5 | a named parameter override changes the resolved width | `test_elab_sig.cpp` |
| 6 | a positional parameter override changes the resolved width | `test_elab_sig.cpp` |
| 7 | two instances of the same module resolve independently under different overrides | `test_elab_sig.cpp` |
| 8 | a named connection flattens to the right net in the parent's scope | `test_elab_sig.cpp` |
| 9 | a positional connection flattens in port order | `test_elab_sig.cpp` |
| 10 | a connection to an undefined signal is a positioned error | `test_elab_sig.cpp`, `tests/golden/top_undef_sig.v` |
| 11 | an unconnected (short positional list) port resolves to `cn_net == -1`, not a crash | `test_elab_sig.cpp` |
| 12 | duplicate signal declaration in one scope is an error | `test_elab_sig.cpp`, `tests/golden/top_dup_sig.v` |
| 13 | a literal containing an x/z bit is rejected as a constant expression | `eval_const` (§3.1) — not yet covered by a dedicated fixture; see `TEAM_TASKS.md` |

---

## 6. Not in this task

Signal *values* — actually driving, storing, and updating a bit/word of simulation
state over time — are Phase 3 (evaluation engine and event queue), not Task 5.2.
`sg_width`/`sg_dir` describe the *shape* of a signal; nothing here allocates
storage for its current value, schedules an update, or evaluates an `always`/
`assign` body. See `TEAM_TASKS.md` for what that phase needs from these tables.