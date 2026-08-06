# tools/ — development aids

Neither script is part of the tool. They exist because the goldens and the host
sources have to be checked by something other than eyesight.

## `gen_hier_golden.py`

A reference model of `rtl/vsim_elab.v`: a transliteration of
`build_module_table` / `resolve_instances` / `pick_top` / `elab_body` /
`dump_hier` into Python, driven by a hand-written description of each test
design. It writes the `golden/*.hier` files.

The Verilog implementation is authoritative — this only writes down what it
must print, so that the expected output is derived from the specified algorithm
rather than from a run of the code it is supposed to test.

```sh
python3 tools/gen_hier_golden.py golden
```

## `vcheck.py`

Static checks over `rtl/` and `tb/` that a compiler would do first, useful when
editing the host sources away from a simulator:

* `begin`/`end`, `function`/`endfunction`, `task`/`endtask`, `case`/`endcase`,
  `module`/`endmodule` balance
* bracket balance
* every `` `MACRO `` used is `` `define ``d somewhere
* every call to a host function or task passes the declared number of arguments
* every `$display`/`$write`/`$fwrite` format string has as many specifiers as
  arguments

```sh
python3 tools/vcheck.py rtl tb
```
