#!/usr/bin/env python3
"""Reference model of rtl/vsim_elab.v -- generates the golden .hier files.

This is a line-for-line transliteration of build_module_table / resolve_instances
/ pick_top / elab_body / dump_hier so that the expected output can be produced
(and the algorithm cross-checked) without a Verilog simulator.  The Verilog
implementation is authoritative; this only writes down what it must print.
"""
import os, sys

# (module, ports, params, [(target, instname, nconns), ...], gates)
DESIGNS = {
    "half_adder_struct":  [("half_adder_structural", 4, 0, [], 2)],
    "half_adder_rtl":     [("half_adder_rtl", 4, 0, [], 0)],
    "blocking_nonblocking": [("blocking_nonblocking", 3, 0, [], 0)],
    "counter":            [("counter", 3, 1, [], 0)],
    "fsm":                [("fsm", 4, 0, [], 0)],
    "op_battery":         [("op_battery", 6, 0, [], 0)],
    "hier_ripple4": [
        ("half_adder",   4, 0, [], 2),
        ("full_adder",   5, 0, [("half_adder", "h0", 4),
                                ("half_adder", "h1", 4)], 1),
        ("ripple_adder4", 5, 0, [("full_adder", "fa0", 5),
                                 ("full_adder", "fa1", 5),
                                 ("full_adder", "fa2", 5),
                                 ("full_adder", "fa3", 5)], 0),
    ],
}


def elaborate(mods):
    names = [m[0] for m in mods]
    refs = {n: 0 for n in names}
    for _, _, _, insts, _ in mods:
        for tgt, _, _ in insts:
            refs[tgt] += 1
    cands = [n for n in names if refs[n] == 0]
    assert len(cands) == 1, cands
    top = cands[0]

    by_name = {m[0]: m for m in mods}
    inst = []          # name, mod, parent, child, sib, nconn

    def new_inst(nm, mod, parent, nconn):
        i = len(inst)
        inst.append(dict(name=nm, mod=mod, parent=parent,
                         child=-1, sib=-1, nconn=nconn))
        if parent >= 0:
            if inst[parent]["child"] < 0:
                inst[parent]["child"] = i
            else:
                t = inst[parent]["child"]
                while inst[t]["sib"] >= 0:
                    t = inst[t]["sib"]
                inst[t]["sib"] = i
        return i

    def body(self_i, modname):
        for tgt, iname, nconn in by_name[modname][3]:
            kid = new_inst(iname, tgt, self_i, nconn)
            body(kid, tgt)

    root = new_inst(top, top, -1, 0)
    body(root, top)
    return top, refs, inst


def path(inst, i):
    return (path(inst, inst[i]["parent"]) + "."
            if inst[i]["parent"] >= 0 else "") + inst[i]["name"]


def dump(mods, top, refs, inst):
    o = []
    o.append('(design "%s"' % top)
    o.append("  (modules")
    rows = []
    for name, ports, params, insts, gates in mods:
        rows.append('    (ND_MODULE "%s" ports=%d params=%d insts=%d '
                    'gates=%d refs=%d)' %
                    (name, ports, params, len(insts), gates, refs[name]))
    o.append("\n".join(rows) + ")")
    o.append("  (hierarchy")

    def d(i, ind):
        s = " " * ind + '(inst "%s" of "%s" conns=%d' % (
            inst[i]["name"], inst[i]["mod"], inst[i]["nconn"])
        c = inst[i]["child"]
        while c >= 0:
            s += "\n" + d(c, ind + 2)
            c = inst[c]["sib"]
        return s + ")"

    o.append(d(0, 4) + ")")
    o.append("  (paths")
    o.append("\n".join('    "%s"' % path(inst, i) for i in range(len(inst)))
             + "))")
    return "\n".join(o) + "\n"


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "golden"
    for key, mods in DESIGNS.items():
        top, refs, inst = elaborate(mods)
        text = dump(mods, top, refs, inst)
        with open(os.path.join(out, key + ".hier"), "w") as fh:
            fh.write(text)
        print("wrote %s.hier (%d modules, %d instances)"
              % (key, len(mods), len(inst)))
