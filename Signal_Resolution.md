# Phase 2 — Elaboration, Task 5.2: Signal & Port Resolution

CSE215/CSE312 Electronic Design Automation, Ain Shams University (iCHEP).

This document specifies the deliverable implemented in `rtl/vsim_sig.v`. It follows the
conventions of `Specification.md` and `Elaboration.md`: **host** = the Verilog source of the tool
itself, **target** = the Verilog file the tool reads. It assumes Task 5.1 (`Elaboration.md`) has
already run: `mt_*` (module table) and `in_*` (instance table) exist and are valid.

---

## 1. Why this task exists

Task 5.1 answers "what instances exist, and how do they nest." It deliberately stops short of the
next question: *what is each instance actually wired to?* The AST records a connection like
`.a(x)` as a name and an expression, both still text-shaped. Nothing yet says

* how wide `x` is,
* whether `x` even exists in the scope that wrote it,
* what net inside the child instance `a` refers to,
* or what happens to a module's own internal `wire`/`reg` declarations once a parameter override
  changes a range like `[WIDTH-1:0]`.

Task 5.2 answers those questions. It is the second half of elaboration (`Elaboration.md` §6): given
a validated instance tree, build a per-instance signal table, resolve parameter overrides, resolve
widths, and flatten every connection down to a `(child port, parent net)` pair.

| # | Responsibility (from the 5.2 brief) | Implemented by |
|---|---|---|
| 1 | Resolve input/output port mappings | `build_scope`, `flatten_connections` |
| 2 | Connect wires and registers | `build_scope` (locally declared `wire`/`reg`) |
| 3 | Link module ports to parent signals | `flatten_connections`, `resolve_conn_expr` |
| 4 | Validate signal names | `find_sig_local`, `resolve_conn_expr` |
| 5 | Report undefined signals or invalid connections | `add_diag_id` calls throughout |

---

## 2. Data representation

Three tables of parallel arrays, arena-style, same convention as `mt_*`/`in_*` in `Elaboration.md`
§2. `-1` is the null handle, matching Task 5.1 (module 0 / instance 0 are legitimate; `-1` is free).

### 2.1 Signal table — one row per port or locally declared net, per instance

| Field | Meaning |
|---|---|
| `sg_name[i]` | local name (not a path) |
| `sg_inst[i]` | owning instance — an `in_*` index, i.e. the *scope* this signal lives in |
| `sg_node[i]` | its `ND_PORT` / `ND_NET` / `ND_REG` handle in the AST |
| `sg_isport[i]` | 1 if this row came from the module header, 0 if a body declaration |
| `sg_dir[i]` | `DIR_*` (meaningless — `DIR_NONE` — for a plain net) |
| `sg_width[i]` | resolved bit width, filled in by `resolve_widths` |

A scope is a *contiguous* range: `in_sig0[inst] .. in_sig0[inst]+in_nsig[inst]-1`, ports first (in
header order), then locally declared nets. Positional connections can therefore index straight into
a scope by port position, without a name lookup — the same reasoning Task 5.1 used to keep the
instance tree as parallel arrays rather than a linked object graph.

### 2.2 Parameter table — one row per parameter, per instance

| Field | Meaning |
|---|---|
| `pm_name[i]` | parameter name |
| `pm_inst[i]` | owning instance |
| `pm_value[i]` | resolved (post-override) integer value |

Populated in two steps per instance: copy the module's own defaults (header `#( )` list, then body
`parameter` declarations, in that order), then walk *that instance's* `#( )` override list and
overwrite by name or by position. Each instance gets its **own** copy — two instances of the same
`counter` module with different `WIDTH` overrides do not share rows.

### 2.3 Connection table — one row per resolved port

| Field | Meaning |
|---|---|
| `cn_inst[i]` | the child instance being connected |
| `cn_port[i]` | `sg_*` index of the port, in the child's own scope |
| `cn_net[i]` | `sg_*` index of the net in the **parent's** scope, or `-1` |

Every port of every non-root instance gets exactly one row, whether or not the target Verilog
mentioned it. A trailing port from a short positional list, or a name simply left out of a named
list, is still a legal unconnected port (`Elaboration.md` §3.3) — it gets a row with `cn_net == -1`
rather than silently having no row at all. This makes the table a complete answer to "what is
connected to what," not a partial one that requires knowing module arity out-of-band to interpret.

---

## 3. The algorithm

```
resolve_signals():
    for each instance i (Task 5.1's preorder):
        build_scope(i)     # ports, then local wire/reg declarations
        build_params(i)    # module defaults, then this instance's #( ) overrides
    for each instance i:
        resolve_widths(i)  # now that every instance's parameter table is final
    for each instance i:
        flatten_connections(i)   # parent-scope lookups are now safe
```

Three separate passes, not one fused pass, for the same reason `Elaboration.md` §3.2 makes
`resolve_instances` walk the whole module set before `pick_top` runs: `flatten_connections` on
instance *i* needs to look up a name in `in_parent[i]`'s scope, which may not have been built yet if
instances were processed in an order that let a later part of pass 1 run interleaved with pass 3.
Finishing all of pass 1 before starting pass 3 removes that ordering hazard entirely, rather than
requiring `elab_body`'s preorder guarantee to also hold across passes.

### 3.1 Building a scope

`build_scope` walks the header (`nd_a[modnode]`) for ports, then the body (`nd_b[modnode]`) for
`ND_NET`/`ND_REG`, descending into `ND_GENERATE` the same way `count_items` (`Elaboration.md`
equivalent) does. A name already present in the scope is a duplicate-declaration error — the same
policy `build_module_table` applies to a repeated module name.

### 3.2 Resolving parameters

Two sources of defaults (header `#( )` list *and* body `parameter` declarations) are folded into one
ordered list per module, because Verilog allows either or both. Overrides are then applied per
*instance*, not per module: `#(.WIDTH(4))` on one instantiation must not affect a sibling
instantiation of the same module with no override. Constant folding (`eval_const`) supports the
literal/parameter/unary/binary/ternary subset the expression grammar (`Specification.md` §4.2)
defines — anything else in a range position is not a constant expression in this subset, and is
reported as one rather than silently evaluating to a wrong number.

### 3.3 Resolving widths

A width is `1` for an unranged signal, or `msb - lsb + 1` (direction-agnostic, since `[0:7]` is
legal though unusual) once both ends of the range fold to constants via `eval_const` in that
instance's own parameter scope. This is why parameters must be fully resolved (§3.2) before any
width is computed — `[WIDTH-1:0]` is meaningless until `WIDTH` has a number.

### 3.4 Flattening connections

Task 5.1's `check_conns` already confirmed a connection list is well-formed: named-XOR-positional,
and arity that fits the target module. `flatten_connections` does not re-check that — it maps each
already-valid connection onto a `(child port, parent net)` pair, and fills in `-1` for whatever
Task 5.1 left legally unconnected.

**Scope of this subset.** A connection expression is resolved only when it is a bare identifier.
This is not a shortcut of convenience — it is the boundary consistent with the rest of the project's
declared subset (`Specification.md` §0.B chooses the DUT-only test-vector model for the same reason:
keep every recognised construct traceable to something the lectures or this subset actually cover).
None of the ten `golden/` designs connect a port with anything richer than an identifier. An
expression connection (`.a(x[3:0])`, `.a({x,y})`) is recognised and reported as a warning —
"connection expression too complex to flatten in this subset" — rather than mis-resolved or
silently dropped. State this explicitly in the report; it is the same anticipate-the-challenge move
`Specification.md` §0.A makes for non-synthesizability.

---

## 4. Output

`dump_sig` (mirrors `dump_hier`'s `+dump_hier` convention) writes two deterministic S-expression
blocks, no line/column information:

```
(signals
  (inst "ripple_adder4.fa0.h0"
    (sig "a" input width=1)
    (sig "b" input width=1)
    (sig "y" output width=1)))
(connections
  (bind "ripple_adder4.fa0.h0.a" -> "ripple_adder4.fa0.n1")
  (bind "ripple_adder4.fa0.h0.b" -> "ripple_adder4.fa0.n2")
  (bind "ripple_adder4.fa0.h0.y" -> <unconnected>))
```

Diagnostics go through the same collector Task 5.1 uses (`Specification.md` §7, `add_diag_id`), so
signal errors interleave with elaboration errors in one sorted, positioned list.

---

## 5. Acceptance

| # | Criterion | Where verified |
|---|---|---|
| 1 | every port becomes a signal-table row, in header order | `tb_sig`, cases 1–2 |
| 2 | locally declared wires/regs are added after the ports | `tb_sig`, case 2 |
| 3 | an unranged signal resolves to width 1 | `tb_sig`, case 3 |
| 4 | a ranged port resolves using the module's own default parameter | `tb_sig`, case 4 |
| 5 | a named parameter override changes the resolved width | `tb_sig`, case 5 |
| 6 | a positional parameter override changes the resolved width | `tb_sig`, case 6 |
| 7 | a named connection flattens to the right net in the parent's scope | `tb_sig`, case 7 |
| 8 | a positional connection flattens in port order | `tb_sig`, case 8 |
| 9 | a connection to an undefined signal is a positioned error | `tb_sig`, case 9 |
| 10 | an unconnected port still gets a row, with `cn_net == -1` | `tb_sig`, case 10 |
| 11 | duplicate signal declaration in one scope is an error | `tb_sig`, case 11 |
| 12 | two instances of the same module with different overrides resolve independently | `tb_sig`, case 12 |

## 6. Known gaps to close before calling this done

1. **`vsim_parser.v` position bug.** `parse_net_decl`/`parse_reg_decl` never set `nd_line`/`nd_col`
   on `ND_NET`/`ND_REG`. Every diagnostic this file raises against a locally-declared signal (not a
   port) will report line 0 until that two-line fix lands.
2. **No golden `.sig` files yet.** Unlike Task 5.1 (`golden/*.hier`, generated by
   `tools/gen_hier_golden.py`), there is no reference-model generator or golden output for signal
   resolution. `tools/gen_hier_golden.py` cannot be extended as-is — it never modeled net
   declarations or parameter values, only instance/module statistics — so this needs a new
   generator, not a patch.
3. **64-port cap.** `flatten_connections` uses a fixed-size local array (`seen[0:63]`) to track which
   ports of an instance were explicitly connected. Fine for every module in `golden/`, but it is a
   real ceiling, not a soft one — a module with more than 64 ports silently stops tracking coverage
   past port 63.
