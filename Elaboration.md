# Phase 2 — Elaboration, Task 5.1: Module Hierarchy Resolution

CSE215/CSE312 Electronic Design Automation, Ain Shams University (iCHEP).

This document specifies the deliverable implemented in `rtl/vsim_elab.v`. It follows the
conventions of `Specification.md`: **host** = the Verilog source of the tool itself, **target** =
the Verilog file the tool reads.

---

## 1. Why elaboration exists

Phase 1 produces a **syntactic** object. The AST records that a module named `full_adder` contains
an item that instantiates *something called* `half_adder` under the instance name `h0`. It records
this as text. Nothing in the AST says

* which module is the top of the design,
* whether `half_adder` is defined anywhere,
* how many `half_adder`s actually exist once the design is unfolded,
* or what the design tree looks like.

Elaboration is the step that answers those questions, and it is a real stage of the flow, not an
implementation detail: in 06 *ASIC Design Flow* it is the work done between reading the HDL and
having a design that can be simulated. Every later phase depends on it — the evaluation engine
cannot schedule anything until it knows which instances exist.

Task 5.1 covers exactly the five responsibilities below. Signal and net tables, and the flattening
of connections onto nets, are **Task 5.2** and are deliberately not done here.

| # | Responsibility | Implemented by |
|---|---|---|
| 1 | Identify the top module | `pick_top` |
| 2 | Resolve all module instantiations | `resolve_instances` / `resolve_items` |
| 3 | Verify that instantiated modules exist | `resolve_items` |
| 4 | Create module instance objects | `new_inst` |
| 5 | Build the parent-child hierarchy | `elab_body`, `in_child` / `in_sib` / `in_parent` |

---

## 2. Data representation

Two tables of parallel arrays, in the same arena style as the AST (`Specification.md` §2.5), with
integer indices used as handles. Verilog-2001 has no structs, so a "row" is one index across
several arrays.

One difference from the AST: **-1 is the null handle here, not 0.** The AST reserves handle 0 as
NULL; in these tables module 0 is a legitimate module and instance 0 is the root of the design, so
reserving the first slot would waste the most-used entry.

### 2.1 Module table — one row per module *definition*

| Field | Meaning |
|---|---|
| `mt_name[i]` | module name, packed identifier |
| `mt_node[i]` | its `ND_MODULE` handle in the AST |
| `mt_nports[i]` | ports in the header |
| `mt_nparam[i]` | parameters in `#( )` |
| `mt_ninst[i]` | module instantiations written inside it |
| `mt_ngate[i]` | gate primitives written inside it |
| `mt_refs[i]` | **static** references: how many `ND_MOD_INST` nodes anywhere name it |

Rows are in declaration order, which makes every dump deterministic.

`mt_refs` is the top-module test, and it is a *static* count, not an instance count. In
`golden/hier_ripple4.v`, `half_adder` has `mt_refs == 2` (two instantiation statements are written
inside `full_adder`) but **eight** elaborated instances, because `full_adder` is itself
instantiated four times. Keeping the two numbers distinct is the whole point of the pass.

### 2.2 Instance table — one row per *elaborated instance*

These are the "module instance objects" of Task 5.1.

| Field | Meaning |
|---|---|
| `in_name[i]` | instance name (the leaf, e.g. `h0` — not a path) |
| `in_mod[i]` | module-table index of the module it instantiates |
| `in_parent[i]` | enclosing instance, `-1` at the root |
| `in_node[i]` | its `ND_MOD_INST` handle, `0` at the root |
| `in_child[i]` | first child, `-1` if a leaf |
| `in_sib[i]` | next sibling, `-1` if last |
| `in_depth[i]` | 0 at the root |
| `in_nconn[i]` | port connections written for it |

The hierarchy is a **first-child / next-sibling tree**. That representation was chosen because it
keeps the parent-child relation explicit in both directions with two integers per node and no
per-node child array — which matters when the arena is a fixed-size Verilog memory. Source order
is preserved: `in_child` points at the first instantiation written in the module, `in_sib` walks
the rest in order.

Because `elab_body` allocates an instance and then immediately descends into it, the instance
table comes out in **preorder DFS**, and the `(paths ...)` section of the dump is simply the table
read top to bottom.

---

## 3. The algorithm

```
elaborate(root, want_top):
    build_module_table(root)      # every definition, with its statistics
    resolve_instances()           # bind every instantiation, count references
    top_mod = pick_top(want_top)  # the module nothing instantiates
    top_inst = new_inst(top)      # the root object
    elab_body(top_inst)           # unfold the tree, depth first
```

### 3.1 Identifying the top module

After `resolve_instances`, the top is the module with `mt_refs == 0`. Three cases:

| Candidates | Action |
|---|---|
| exactly one | that is the top |
| none | every module is instantiated ⇒ the reference graph has a cycle ⇒ **error**: the design has no root |
| several | the file holds independent designs: **warning**, and the first declared one is elaborated |

`+top=<name>` overrides the deduction entirely, which is also how a sub-block is elaborated on its
own (`+top=full_adder` on `hier_ripple4.v` builds a 3-instance tree instead of a 13-instance one).

### 3.2 Verifying the modules exist

`resolve_instances` walks **every** module, not only the ones reachable from the top. An undefined
module inside an unused module is still a design error, and the reference counts it produces are
what `pick_top` consumes, so the pass has to be global. Generate blocks hold their items in
`nd_a`, so the walk descends into `ND_GENERATE` rather than skipping it.

### 3.3 Resolving the instantiation

An instantiation is resolved when its target module is known *and* its port association can be
mapped. Verilog allows either style, but not both in the same instantiation:

* **named** `.p(expr)` — every name must be a port of the target module, else an error;
* **positional** `expr` — more expressions than ports cannot be mapped at all (error); fewer is
  legal Verilog, the trailing ports are simply unconnected (warning).

Binding those expressions to nets is Task 5.2; the check here is the part that "resolve" requires.

### 3.4 Terminating on recursive instantiation

Verilog modules may not instantiate themselves, directly or through a cycle. Before expanding an
instance, `mod_on_path` walks from the current instance to the root and asks whether the target
module already appears. Without this test `elab_body` would not terminate on
`golden/bad_recursive.v` — the guard is what makes elaboration a finite unfolding of a possibly
cyclic reference graph.

---

## 4. Output

`+dump_hier [+hierout=<f>]` writes a deterministic S-expression with no line/column information,
so goldens stay stable under target reformatting (same rule as the AST dump, `Specification.md` §6):

```
(design "ripple_adder4"
  (modules
    (ND_MODULE "half_adder" ports=4 params=0 insts=0 gates=2 refs=2)
    (ND_MODULE "full_adder" ports=5 params=0 insts=2 gates=1 refs=4)
    (ND_MODULE "ripple_adder4" ports=5 params=0 insts=4 gates=0 refs=0))
  (hierarchy
    (inst "ripple_adder4" of "ripple_adder4" conns=0
      (inst "fa0" of "full_adder" conns=5
        (inst "h0" of "half_adder" conns=4)
        (inst "h1" of "half_adder" conns=4))
      ...))
  (paths
    "ripple_adder4"
    "ripple_adder4.fa0"
    "ripple_adder4.fa0.h0"
    ...))
```

The console summary is one line:

```
ELABORATE OK: top='ripple_adder4', 3 module(s), 13 instance(s)
```

Elaboration diagnostics go through the same collector as parse diagnostics (`Specification.md`
§7) and print in source order. `add_diag_id` extends `add_diag` with an optional identifier so a
message can quote the offending name — `diag_msg` is a fixed-width literal and cannot carry one:

```
bad_undef_inst.v:6:3: error: instantiation of undefined module 'missing_mod'
```

---

## 5. Acceptance

| # | Criterion | Where verified |
|---|---|---|
| 1 | module table holds every definition, in declaration order | `tb_elab` |
| 2 | the top module is the one nothing instantiates | `tb_elab`, every `.hier` golden |
| 3 | instances are created at every level of the tree | `tb_elab`, `hier_ripple4.hier` |
| 4 | parent-child links, source order and depths are correct | `tb_elab` |
| 5 | the same module instantiated twice gives two distinct instances | `tb_elab`, `hier_ripple4.hier` |
| 6 | `+top=` overrides the deduced top and prunes the tree | `tb_elab`, run scripts |
| 7 | an undefined instantiated module is a positioned error | `tb_elab`, `bad_undef_inst.v` |
| 8 | an instantiation cycle is an error, and elaboration terminates | `tb_elab`, `bad_recursive.v` |
| 9 | several candidate tops → warning, first declared is elaborated | `tb_elab` |
| 10 | a named connection to a non-existent port is an error | `tb_elab`, `bad_conn.v` |
| 11 | more connections than ports is an error | `tb_elab` |
| 12 | exhausting the instance table is a loud error, not corruption | `tb_elab`, `+instcap=4` |

---

## 6. Not in this task

Task 5.2 (signal/net table construction, connection flattening, width and direction resolution,
parameter override evaluation) is the other half of elaboration. `mt_nparam` and the
`ND_PARAM_ASSIGN` chain that `parse_mod_inst` already builds on `nd_b` are recorded here and left
for it: overriding a parameter changes signal *widths*, which is meaningless until there is a
signal table to change.
