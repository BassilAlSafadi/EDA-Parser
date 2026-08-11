# EDA Verilog Simulator — Phase 1 (Lexer + Parser)

CSE215/CSE312 Electronic Design Automation, Ain Shams University (iCHEP).

A simple EDA tool — a Verilog simulator — **written in C++**. This repository
implements **Phase 1**: a hand-written scanner and recursive-descent parser that
read a *target* Verilog file and build an arena-allocated AST. The full design
specification is in [`Specification.md`](Specification.md).

> **host** = the C++ source of the tool itself (this repo). **target** = the
> Verilog file the tool reads and parses.

The tool was originally implemented in Verilog itself (simulation-only,
non-synthesizable), as a deliberate demonstration that the host language need
not be the target language (spec §0.A). It has since been migrated to C++
with the exact same algorithms and output format — every golden
`.tokens`/`.ast` fixture is unchanged and still matches byte-for-byte.

## Layout
```
src/
  defs.hpp       capacities, token kinds, keyword codes, node kinds  (§2.1, §3.2, §5.1)
  four_state.hpp four-state (0/1/x/z) literal value type              (§2.2)
  vsim.hpp       the Vsim class: arenas + every method declaration
  arena.cpp      arenas + new_node + identifier/char helpers          (§2)
  diag.cpp       diagnostic collection, sorting, printing             (§7)
  lexer.cpp      source load, comments, keywords, four-state numbers, ops (§3)
  parser.cpp     recursive descent + precedence climbing              (§4)
  dump.cpp       deterministic token + AST S-expression dumps         (§6, §8)
  main.cpp       CLI argument handling and orchestration
tests/
  test_lexer.cpp   maximal munch, divide-vs-comment, four-state literals
  test_parser.cpp  COUT precedence tree (§5.2) and dangling-else binding
golden/            six §10 target circuits + their .tokens / .ast goldens
                   (plus Task-5.1 elaboration fixtures kept for later
                   reference — see "About Phase 2" below)
run/
  run_cpp.sh     configure + build + test + golden-diff + overflow check
CMakeLists.txt
```

## Build & run
Requires a C++17 compiler and CMake. On this machine there is no `g++` on
PATH but MSVC (Visual Studio 2022 Community) is installed, and CMake was
installed user-locally via `pip install --user cmake` (no admin rights
needed). Any CMake + any C++17 compiler works elsewhere.

```sh
bash run/run_cpp.sh      # configure, build, run both unit tests, diff every
                          # golden fixture, and exercise the overflow path
```

Run the tool directly on a target file:
```sh
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
build/Release/vsim.exe +src=golden/counter.v +dump_ast
```
Flags (kept identical to the original Verilog host's plusarg syntax):
`+src=<file.v>` (required), `+dump_tokens [+tokout=<f>]`,
`+dump_ast [+astout=<f>]`, `+nodecap=<N>` (artificially cap the node arena to
demonstrate overflow).

## Phase-1 acceptance (spec §9) — all passing
| # | Criterion | Where verified |
|---|---|---|
| 1 | §10 circuits scan & parse with zero diagnostics | run script golden pass |
| 2 | Each AST dump matches its golden | run script `.ast` diff |
| 3 | `COUT=(A&B)|(A&CIN)|(B&CIN)` builds the §5.2 tree | `test_parser` |
| 4 | §2.2 four-value table matches native operators | N/A in C++ (see note below) |
| 5 | `===` is one token; `= = =` is three (maximal munch) | `test_lexer` |
| 6 | `a / b` is three tokens; `a // b` is one + comment | `test_lexer` |
| 7 | dangling `else` binds to the inner `if` | `test_parser` |
| 8 | `4'b1x0z` reads back `1x0z`; `8'hFF` extends per §3.5 | `test_lexer` |
| 9 | a missing `;` yields a positioned diagnostic + recovery | run on a bad file |
| 10 | exhausting an arena is a loud error, not corruption | run with `+nodecap` |

### On criterion 4 and the four-state type
The original Verilog host's `tb_fourvalue.v` verified that a *host* `reg` bit
is natively four-state, so the host's own `==`, `===`, `!=`, `!==`, `&`, `&&`,
`|`, `||`, `^` already produced the §2.2 truth table for free. C++ has no
native four-state type, but the tool was never asked to *evaluate* those
operators on target values in the first place — it only lexes and prints
target literal bits (§3.5, §9.8). `src/four_state.hpp` hand-implements exactly
the operations the scanner and dumper actually perform (shift-and-insert,
uniform x/z fill, per-bit read), verified instead by `test_lexer`'s literal
round-trip checks (criterion 8). There is nothing here for a truth-table test
to exercise, so `tb_fourvalue.v` was not ported.

## Notes on host-language choices
- **Recursion**: the parser is ordinary C++ recursion (the Verilog host
  needed `automatic` functions specifically because Verilog-2001 functions
  take only inputs, spec §2.6 — that constraint doesn't exist here).
- **Identifiers**: stored as `std::string` rather than the original's
  fixed-width packed bit vector. The one behavioral detail preserved
  deliberately: an identifier longer than `MAX_IDENT` (32) characters still
  keeps only its *last* 32 characters, matching the original's
  `acc = (acc << 8) | ch` accumulator, which silently drops the earliest
  bytes once it saturates — the "identifier exceeds MAX_IDENT chars"
  diagnostic still fires either way.
- **Four-value logic** is no longer free (see above); `FourState` provides
  exactly what §2.2/§3.5 require and nothing more.

## About Phase 2 (Task 5.1, elaboration)
A Verilog implementation of Task 5.1 (module hierarchy resolution —
`rtl/vsim_elab.v`, `tb/tb_elab.v`, and the `tools/` Python goldens/checker)
existed on `main` before this C++ migration, specified in
[`Elaboration.md`](Elaboration.md) with [`Signal_Resolution.md`](Signal_Resolution.md)
describing the next task. It depended entirely on the Verilog host fragments
removed in this migration, so it was removed along with them rather than left
in a non-building state. Both design docs and the `golden/*.hier`,
`golden/bad_*.v`, and `golden/hier_ripple4.*` fixtures were kept, since they
still describe the elaboration algorithm and its expected output precisely —
useful as a reference if/when Task 5.1 is ported to C++ on top of this Phase 1
host. The original Verilog implementation remains recoverable from git history
(commit `ba841f9`).

Phases 3–6 (evaluation, vectors, verification, report) are out of scope for
this deliverable; see `Specification.md` §11.
