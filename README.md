# EDA Verilog Simulator — Phase 1 (Lexer + Parser) and Phase 2 Task 5.1 (Elaboration)

CSE215/CSE312 Electronic Design Automation, Ain Shams University (iCHEP).

A simple EDA tool — a Verilog simulator — **written in Verilog**. This repository
implements **Phase 1**: a hand-written scanner and recursive-descent parser that
read a *target* Verilog file and build an arena-allocated AST; and **Phase 2,
Task 5.1**: elaboration of that AST into a module table and a design hierarchy.
The full design specification is in [`Specification.md`](Specification.md), and
the elaboration deliverable is specified in [`Elaboration.md`](Elaboration.md).

> **host** = the Verilog source of the tool itself (this repo), simulation-only
> and non-synthesizable by design (spec §0.A). **target** = the Verilog file the
> tool reads and parses.

## Why non-synthesizable is correct
The tool uses `$fopen`/`$fgetc`/`$fdisplay`, unbounded loops and recursive
`automatic` subprograms — none synthesizable. It is a *program that runs inside a
simulator*, not a circuit (spec §0.A).

## Layout
```
rtl/
  vsim_defs.vh     capacities, token kinds, keyword codes, node kinds  (§2.1, §3.2, §5.1)
  vsim_arena.v     arenas + new_node + identifier/char helpers          (§2)
  vsim_diag.v      diagnostic collection, sorting, printing             (§7)
  vsim_lexer.v     source load, comments, keywords, four-state numbers, ops (§3)
  vsim_parser.v    recursive descent + precedence climbing              (§4)
  vsim_dump.v      deterministic token + AST S-expression dumps         (§6, §8)
  vsim_elab.v      module table, top discovery, instance tree   (Elaboration.md)
  vsim_top.v       plusarg handling and orchestration
tb/
  tb_fourvalue.v   asserts the §2.2 truth table against native operators
  tb_lexer.v       maximal munch, divide-vs-comment, four-state literals
  tb_parser.v      COUT precedence tree (§5.2) and dangling-else binding
  tb_elab.v        module/instance tables, top discovery, cycles, bad ports
golden/            six §10 target circuits + their .tokens / .ast / .hier goldens
                   hier_ripple4.v  three-module two-level hierarchy + .hier
                   bad_*.v         Task-5.1 negative cases (message-checked)
run/
  run_modelsim.sh  build + test with ModelSim / Questa   (used here)
  run_xsim.sh      build + test with Vivado XSim         (course toolchain)
  run_iverilog.sh  build + test with Icarus Verilog
```

### Single-module design
Verilog functions may only touch variables in their own module scope, so the
arenas and every subprogram that walks them must share one module. Each top
module (`vsim_top`, and each testbench) `` `include ``s the fragments, giving it
its own private copy of the whole machine.

## Build & run
Any one of the run scripts drives the full flow (compile → four testbenches →
regenerate every golden `.tokens`/`.ast`/`.hier` and diff → Task-5.1 negative
cases → arena- and instance-table-overflow checks):
```sh
bash run/run_modelsim.sh     # ModelSim / Questa (verified)
bash run/run_xsim.sh         # Vivado XSim
bash run/run_iverilog.sh     # Icarus Verilog
```

Run the tool directly on a target file:
```sh
# ModelSim
vlog +incdir+rtl rtl/vsim_top.v
vsim -c -do "run -all; quit -f" vsim_top +src=golden/counter.v +dump_ast
```
Plusargs: `+src=<file.v>` (required), `+dump_tokens [+tokout=<f>]`,
`+dump_ast [+astout=<f>]`, `+nodecap=<N>` (artificially cap the node arena to
demonstrate overflow). Elaboration adds `+elab`, `+dump_hier [+hierout=<f>]`,
`+top=<name>` (force the top module) and `+instcap=<N>` (cap the instance
table). XSim uses `-testplusarg name[=value]` instead of `+name`.

```sh
# elaborate the ripple-carry adder and print its hierarchy
vsim -c -do "run -all; quit -f" vsim_top +src=golden/hier_ripple4.v +dump_hier
```

## Phase-1 acceptance (spec §9) — all passing
| # | Criterion | Where verified |
|---|---|---|
| 1 | §10 circuits scan & parse with zero diagnostics | run script golden pass |
| 2 | Each AST dump matches its golden | run script `.ast` diff |
| 3 | `COUT=(A&B)|(A&CIN)|(B&CIN)` builds the §5.2 tree | `tb_parser` |
| 4 | §2.2 four-value table matches native operators | `tb_fourvalue` |
| 5 | `===` is one token; `= = =` is three (maximal munch) | `tb_lexer` |
| 6 | `a / b` is three tokens; `a // b` is one + comment | `tb_lexer` |
| 7 | dangling `else` binds to the inner `if` | `tb_parser` |
| 8 | `4'b1x0z` reads back `1x0z`; `8'hFF` extends per §3.5 | `tb_lexer` |
| 9 | a missing `;` yields a positioned diagnostic + recovery | run on a bad file |
| 10 | exhausting an arena is a loud error, not corruption | run with `+nodecap` |

## Notes on host-language choices (spec §12 risks)
- **Recursion**: recursive `automatic` functions are used (verified on ModelSim
  2020.1); the explicit-stack fallback was not needed.
- **Verilog-2001 functions require ≥1 input**: the cursor-driven parse helpers
  therefore carry a dummy input and are called with `0`.
- **Four-value logic is free**: a host `reg` is natively 0/1/x/z, so a target
  literal and every operator result are stored/compared directly — no aval/bval
  bit-planes (spec §2.2).

## Phase 2 acceptance — Task 5.1, Module Hierarchy Resolution
Specified in [`Elaboration.md`](Elaboration.md); all passing.

| # | Criterion | Where verified |
|---|---|---|
| 1 | the module table holds every definition, in declaration order | `tb_elab` |
| 2 | the top module is the one nothing instantiates | `tb_elab`, every `.hier` golden |
| 3 | instances are created at every level of the tree | `tb_elab`, `hier_ripple4.hier` |
| 4 | parent-child links, source order and depths are correct | `tb_elab` |
| 5 | one module instantiated twice gives two distinct instances | `tb_elab`, `hier_ripple4.hier` |
| 6 | `+top=` overrides the deduced top and prunes the tree | `tb_elab`, run scripts |
| 7 | an undefined instantiated module is a positioned error | `bad_undef_inst.v` |
| 8 | an instantiation cycle is an error, and elaboration terminates | `bad_recursive.v` |
| 9 | several candidate tops → warning, first declared is elaborated | `tb_elab` |
| 10 | a named connection to a non-existent port is an error | `bad_conn.v` |
| 11 | more connections than ports is an error | `tb_elab` |
| 12 | exhausting the instance table is a loud error, not corruption | run with `+instcap` |

Task 5.2 (signal/net tables and connection flattening) and Phases 3–6
(evaluation, vectors, verification, report) are out of scope for this
deliverable; see `Specification.md` §11 and `Elaboration.md` §6.
