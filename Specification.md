# EDA Verilog Simulator — Implementation Specification

CSE215/CSE312 Electronic Design Automation, Ain Shams University (iCHEP).
Major task: a simple EDA tool (Verilog simulator), **implemented in Verilog**.

**This document is self-contained.** It is the complete input for implementing Phase 1 (lexer and
parser) and defines the data representations Phases 2 and 3 depend on.

---

## Terminology — read this first

Two different Verilogs appear throughout. They are never interchangeable.

| Term | Meaning |
|---|---|
| **host** | The Verilog source code *of the tool itself*. Simulation-only, non-synthesizable. |
| **target** | The Verilog source code the tool *reads and parses*. The DUT. |

"The host lexer reads target characters" = the tool (written in Verilog) reads the file being
analysed (also Verilog). Where ambiguity is possible, the words *host* and *target* are used
explicitly.

---

> **Sourcing rule.** Everything the tool *recognises* in the target — tokens, operators,
> precedence, module structure, statement forms, four-value semantics — comes from the course
> lecture slides and is cited inline. The *techniques* used to recognise them (subset construction,
> maximal munch, recursive descent, precedence climbing, arena allocation) are compiler-construction
> methods that no CSE215/312 slide covers; they are authorised by the project brief's requirement to
> apply "concepts from digital design, compiler construction, data structures, and software
> engineering." Host-language constructs not taught in the lectures (`$fopen`, `automatic`
> functions, `$fdisplay`) cite the IEEE 1364-2001 Verilog LRM.

---

## 0. Decisions

| # | Decision | Value |
|---|---|---|
| 0.1 | Host language | **Verilog (IEEE 1364-2001)**, simulation-only, non-synthesizable |
| 0.2 | Host simulator | Vivado XSim (course toolchain). Keep Icarus Verilog compatible if possible |
| 0.3 | Parser generator | None — hand-written scanner and recursive-descent parser |
| 0.4 | Memory model | Arena allocation: fixed-capacity arrays, integer indices as node handles |
| 0.5 | Recursion | `automatic` functions (LRM §10.4.2). Fall back to an explicit stack array if the simulator misbehaves |
| 0.6 | Test-vector source | Separate vector file; the tool parses the target DUT only |
| 0.7 | Error policy | Collect all diagnostics in an array, print sorted, set a failure flag |

### 0.A Why non-synthesizable is correct here, and must be stated

The tool uses `$fopen`, `$fgetc`, `$fdisplay`, unbounded loops, and `automatic` recursion. None of
these are synthesizable — they exist only in simulation. This is not a defect. The tool is a
*program that runs inside a simulator*, not a circuit.

Say this explicitly in the report. It anticipates the obvious challenge ("your EDA tool can't be
synthesized") and shows you understand the distinction between the synthesizable subset
(05 *HDL Coding Techniques*, which is about writing hardware) and full Verilog.

### 0.B Why the DUT-only default (0.6)

The brief requires results "for a set of test vectors" but never says the vectors arrive as a
target testbench. The lectures contain no `` `timescale ``, no `$display` as a taught construct, no
`$monitor`, no `$finish`, and no testbench slide. Reading vectors from a plain text file keeps every
*recognised* target construct citable to a lecture, and makes Phase 5 comparison easier.

---

## 1. Build order

| Phase | Deliverable | Lecture source |
|---|---|---|
| 0 | Frozen list of supported target constructs (§3–§4) | 04 structural modelling; 05 behavioural features |
| 1 | Host lexer + parser producing an arena AST | not in lectures; project brief |
| 2 | Elaboration: module table, signal table, connectivity | 06 *ASIC Design Flow*, Design step |
| 3 | Evaluation engine and event queue | 05 *Data Types*; 04 *Blocking vs Nonblocking* |
| 4 | Vector application and output logging | 06, Simulation stage |
| 5 | Verification against reference waveforms | project brief, rubric |
| 6 | Report | project brief, Evaluation Criteria |

Phase 1 is done when §9 passes.

---

*(This file is the task specification as provided. The full data-representation, lexer, parser,
AST-encoding, dump, diagnostics, testing and acceptance sections — §2 through §12 — are the
authoritative reference the `rtl/`, `tb/`, `golden/` and `run/` implementations follow, and are
cited by section number throughout the source. See `README.md` for the Phase-1 acceptance matrix.)*
