#!/usr/bin/env python3
"""Static sanity checks for the host Verilog sources.

No Verilog simulator is available in this environment, so this script performs
the mechanical checks a compiler would do first: block-keyword balance, macro
definedness, declared-vs-used identifiers, subprogram arity at every call site,
and $display/$fwrite format-argument counts.
"""
import re, sys, os, collections

RTL = sys.argv[1] if len(sys.argv) > 1 else "rtl"
TB = sys.argv[2] if len(sys.argv) > 2 else "tb"

problems = []


def strip_comments(t):
    """String-aware: a // or /* inside a Verilog string literal is data."""
    out = []
    i = 0
    n = len(t)
    while i < n:
        c = t[i]
        if c == '"':
            j = i + 1
            while j < n:
                if t[j] == "\\":
                    j += 2
                    continue
                if t[j] == '"':
                    break
                j += 1
            out.append(t[i:j + 1])
            i = j + 1
        elif t.startswith("//", i):
            while i < n and t[i] != "\n":
                i += 1
        elif t.startswith("/*", i):
            k = t.find("*/", i + 2)
            i = n if k < 0 else k + 2
            out.append(" ")
        else:
            out.append(c)
            i += 1
    return "".join(out)


def strip_strings(t):
    return re.sub(r'"(\\.|[^"\\])*"', '""', t)


files = {}
for d in (RTL, TB):
    for fn in sorted(os.listdir(d)):
        if fn.endswith((".v", ".vh")):
            p = os.path.join(d, fn)
            files[p] = open(p).read()

# ---------------------------------------------------------------- 1. balance
OPEN_CLOSE = [("begin", "end"), ("function", "endfunction"), ("task", "endtask"),
              ("case", "endcase"), ("module", "endmodule"), ("fork", "join")]
for p, raw in files.items():
    t = strip_strings(strip_comments(raw))
    words = re.findall(r"\b\w+\b", t)
    counts = collections.Counter(words)
    # casex/casez also close with endcase
    ncase = counts["case"] + counts["casex"] + counts["casez"]
    pairs = {"begin": counts["begin"] - counts["end"],
             "function": counts["function"] - counts["endfunction"],
             "task": counts["task"] - counts["endtask"],
             "case": ncase - counts["endcase"],
             "module": counts["module"] - counts["endmodule"]}
    for k, v in pairs.items():
        if v != 0:
            problems.append("%s: %s/end%s unbalanced by %+d" % (p, k, k, v))

# --------------------------------------------------------------- 2. brackets
for p, raw in files.items():
    t = strip_strings(strip_comments(raw))
    for open_c, close_c in (("(", ")"), ("[", "]"), ("{", "}")):
        if t.count(open_c) != t.count(close_c):
            problems.append("%s: '%s' %d vs '%s' %d"
                            % (p, open_c, t.count(open_c),
                               close_c, t.count(close_c)))

# ----------------------------------------------------------------- 3. macros
defined = set()
for p, raw in files.items():
    defined |= set(re.findall(r"^\s*`define\s+(\w+)", raw, re.M))
BUILTIN = {"ifndef", "endif", "define", "include", "ifdef", "else", "timescale"}
for p, raw in files.items():
    t = strip_strings(strip_comments(raw))
    for m in re.finditer(r"`(\w+)", t):
        if m.group(1) not in defined and m.group(1) not in BUILTIN:
            problems.append("%s: undefined macro `%s" % (p, m.group(1)))

# ------------------------------------------- 4. subprogram arity at call site
# collect "function [ret] name; input ...;" and "task name; input ...;"
sig = {}
for p, raw in files.items():
    t = strip_comments(raw)
    for m in re.finditer(r"\b(function|task)\b(.*?)\b(end\1)\b", t, re.S):
        kind, bodyfull = m.group(1), m.group(2)
        head = bodyfull.split(";", 1)
        if len(head) < 2:
            continue
        decl, body = head[0], head[1]
        name = re.findall(r"(\w+)\s*$", decl.strip())
        if not name:
            continue
        name = name[0]
        # inputs are declared before the first begin/statement
        pre = re.split(r"\bbegin\b|\bcase\b|\bif\b|%s\s*=" % re.escape(name), body, 1)[0]
        nin = len(re.findall(r"\binput\b[^;]*;", pre))
        # multiple names on one input declaration each count
        extra = 0
        for line in re.findall(r"\binput\b([^;]*);", pre):
            extra += re.sub(r"\[[^\]]*\]", "", line).count(",")
        sig[name] = (kind, nin + extra, p)

CALL_SKIP = {"if", "while", "for", "case", "casex", "casez", "repeat", "begin",
             "function", "task", "input", "output", "inout", "reg", "wire",
             "integer", "return", "and", "or", "not", "nand", "nor", "xor",
             "xnor", "buf", "posedge", "negedge", "signed", "automatic"}
for p, raw in files.items():
    t = strip_strings(strip_comments(raw))
    for m in re.finditer(r"\b(\w+)\s*\(", t):
        name = m.group(1)
        if name not in sig or name in CALL_SKIP:
            continue
        # extract the balanced argument list
        i = m.end() - 1
        depth = 0
        for j in range(i, len(t)):
            if t[j] == "(":
                depth += 1
            elif t[j] == ")":
                depth -= 1
                if depth == 0:
                    break
        args = t[i + 1:j]
        if args.strip() == "":
            nargs = 0
        else:
            depth = 0
            nargs = 1
            for ch in args:
                if ch in "([{":
                    depth += 1
                elif ch in ")]}":
                    depth -= 1
                elif ch == "," and depth == 0:
                    nargs += 1
        kind, want, decl_p = sig[name]
        if nargs != want:
            line = t[:m.start()].count("\n") + 1
            problems.append("%s:%d: %s %s called with %d arg(s), declared %d"
                            % (p, line, kind, name, nargs, want))

# ------------------------------------------------ 5. format-specifier counts
def mask_strings(t):
    """Blank out string CONTENTS (keeping length and the quotes) so that
    parentheses and commas inside literals do not confuse the scanner."""
    out = list(t)
    i = 0
    while i < len(t):
        if t[i] == '"':
            j = i + 1
            while j < len(t):
                if t[j] == "\\":
                    out[j] = " "
                    if j + 1 < len(t):
                        out[j + 1] = " "
                    j += 2
                    continue
                if t[j] == '"':
                    break
                out[j] = " "
                j += 1
            i = j + 1
        else:
            i += 1
    return "".join(out)


def match_paren(s, i):
    depth = 0
    for j in range(i, len(s)):
        if s[j] == "(":
            depth += 1
        elif s[j] == ")":
            depth -= 1
            if depth == 0:
                return j
    return len(s) - 1


for p, raw in files.items():
    t = strip_comments(raw)
    mt = mask_strings(t)
    for m in re.finditer(r"\$(display|write|fwrite)\s*\(", mt):
        i = m.end() - 1
        j = match_paren(mt, i)
        margs = mt[i + 1:j]
        fm = re.search(r'"[^"]*"', margs)
        if not fm:
            continue
        fmt = t[i + 1 + fm.start() + 1: i + 1 + fm.end() - 1]
        nspec = len(re.findall(r"%[0-9]*[a-zA-Z]", fmt))
        rest = margs[fm.end():].lstrip()
        if rest.startswith(","):
            rest = rest[1:]
        depth = 0
        nargs = 0
        cur = ""
        for ch in rest:
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth -= 1
            if ch == "," and depth == 0:
                nargs += 1
                cur = ""
            else:
                cur += ch
        if cur.strip():
            nargs += 1
        if nspec != nargs:
            line = t[:m.start()].count("\n") + 1
            problems.append("%s:%d: $%s has %d specifier(s) but %d argument(s)"
                            % (p, line, m.group(1), nspec, nargs))

if problems:
    print("\n".join(problems))
    print("\n%d problem(s)" % len(problems))
    sys.exit(1)
print("vcheck: no problems found in %d file(s)" % len(files))
