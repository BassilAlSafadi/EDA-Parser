//============================================================================
// test_gate_eval.cpp -- exhaustive truth-table check for every gate
// primitive against IEEE 1364-2001 Table 5-1, plus N-ary reduction and
// z-treated-as-x checks.
//============================================================================
#include "gate_eval.h"

#include <cstdio>

using namespace vsim;

static int fails = 0;

static char c(Bit b) {
    switch (b) {
        case Bit::Zero: return '0';
        case Bit::One:  return '1';
        case Bit::X:    return 'x';
        case Bit::Z:    return 'z';
    }
    return '?';
}

static void ck(Bit got, Bit want, const char* msg) {
    if (got != want) {
        fails += 1;
        std::printf("FAIL: %s (got %c, want %c)\n", msg, c(got), c(want));
    } else {
        std::printf("ok   %s\n", msg);
    }
}

int main() {
    const Bit V[4] = {Bit::Zero, Bit::One, Bit::X, Bit::Z};

    //---------------------------------------------------- AND, IEEE Table 5-1
    // rows/cols in 0,1,x,z order; z column/row is identical to x (LRM 5.1.1)
    const Bit AND_TBL[4][4] = {
        {Bit::Zero, Bit::Zero, Bit::Zero, Bit::Zero},   // a=0
        {Bit::Zero, Bit::One,  Bit::X,    Bit::X   },   // a=1
        {Bit::Zero, Bit::X,    Bit::X,    Bit::X   },   // a=x
        {Bit::Zero, Bit::X,    Bit::X,    Bit::X   },   // a=z
    };
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            char msg[64];
            std::snprintf(msg, sizeof msg, "and(%c,%c)", c(V[i]), c(V[j]));
            ck(gate_and2(V[i], V[j]), AND_TBL[i][j], msg);
        }

    //----------------------------------------------------- OR, IEEE Table 5-1
    const Bit OR_TBL[4][4] = {
        {Bit::Zero, Bit::One, Bit::X,   Bit::X  },   // a=0
        {Bit::One,  Bit::One, Bit::One, Bit::One},   // a=1
        {Bit::X,    Bit::One, Bit::X,   Bit::X  },   // a=x
        {Bit::X,    Bit::One, Bit::X,   Bit::X  },   // a=z
    };
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            char msg[64];
            std::snprintf(msg, sizeof msg, "or(%c,%c)", c(V[i]), c(V[j]));
            ck(gate_or2(V[i], V[j]), OR_TBL[i][j], msg);
        }

    //---------------------------------------------------- XOR, IEEE Table 5-1
    const Bit XOR_TBL[4][4] = {
        {Bit::Zero, Bit::One,  Bit::X, Bit::X},   // a=0
        {Bit::One,  Bit::Zero, Bit::X, Bit::X},   // a=1
        {Bit::X,    Bit::X,    Bit::X, Bit::X},   // a=x
        {Bit::X,    Bit::X,    Bit::X, Bit::X},   // a=z
    };
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            char msg[64];
            std::snprintf(msg, sizeof msg, "xor(%c,%c)", c(V[i]), c(V[j]));
            ck(gate_xor2(V[i], V[j]), XOR_TBL[i][j], msg);
        }

    //----------------------------------------------------------------- NOT/BUF
    ck(gate_not1(Bit::Zero), Bit::One,  "not(0)");
    ck(gate_not1(Bit::One),  Bit::Zero, "not(1)");
    ck(gate_not1(Bit::X),    Bit::X,    "not(x)");
    ck(gate_not1(Bit::Z),    Bit::X,    "not(z) -- z treated as x (LRM 5.1.1)");

    ck(eval_gate(K_BUF, {Bit::Zero}), Bit::Zero, "buf(0)");
    ck(eval_gate(K_BUF, {Bit::One}),  Bit::One,  "buf(1)");
    ck(eval_gate(K_BUF, {Bit::X}),    Bit::X,    "buf(x)");
    ck(eval_gate(K_BUF, {Bit::Z}),    Bit::X,    "buf(z) -- z treated as x, buf output is never z");

    //------------------------------------------ NAND/NOR/XNOR = negated forms
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            char msg[64];
            Bit inv;

            inv = gate_not1(AND_TBL[i][j]);
            std::snprintf(msg, sizeof msg, "nand(%c,%c) == not(and(%c,%c))", c(V[i]), c(V[j]), c(V[i]), c(V[j]));
            ck(eval_gate(K_NAND, {V[i], V[j]}), inv, msg);

            inv = gate_not1(OR_TBL[i][j]);
            std::snprintf(msg, sizeof msg, "nor(%c,%c) == not(or(%c,%c))", c(V[i]), c(V[j]), c(V[i]), c(V[j]));
            ck(eval_gate(K_NOR, {V[i], V[j]}), inv, msg);

            inv = gate_not1(XOR_TBL[i][j]);
            std::snprintf(msg, sizeof msg, "xnor(%c,%c) == not(xor(%c,%c))", c(V[i]), c(V[j]), c(V[i]), c(V[j]));
            ck(eval_gate(K_XNOR, {V[i], V[j]}), inv, msg);
        }

    //------------------------------------------------- eval_gate dispatch, 2-input
    ck(eval_gate(K_AND, {Bit::One, Bit::One}), Bit::One,  "eval_gate(AND,1,1)");
    ck(eval_gate(K_OR,  {Bit::Zero, Bit::Zero}), Bit::Zero, "eval_gate(OR,0,0)");
    ck(eval_gate(K_XOR, {Bit::One, Bit::Zero}), Bit::One,  "eval_gate(XOR,1,0)");

    //---------------------------------------- N-ary gates (Verilog primitives
    //---------------------------------------- allow more than 2 inputs)
    ck(eval_gate(K_AND, {Bit::One, Bit::One, Bit::One}), Bit::One,
       "and(1,1,1) = 1");
    ck(eval_gate(K_AND, {Bit::One, Bit::Zero, Bit::One}), Bit::Zero,
       "and(1,0,1) = 0 -- any 0 dominates regardless of position");
    ck(eval_gate(K_AND, {Bit::One, Bit::X, Bit::One}), Bit::X,
       "and(1,x,1) = x -- no 0 present, x survives");
    ck(eval_gate(K_OR, {Bit::Zero, Bit::Zero, Bit::One}), Bit::One,
       "or(0,0,1) = 1 -- any 1 dominates regardless of position");
    ck(eval_gate(K_OR, {Bit::Zero, Bit::Zero, Bit::Zero}), Bit::Zero,
       "or(0,0,0) = 0");
    ck(eval_gate(K_XOR, {Bit::One, Bit::One, Bit::One}), Bit::One,
       "xor(1,1,1) = 1 -- odd parity");
    ck(eval_gate(K_XOR, {Bit::One, Bit::One, Bit::Zero, Bit::One}), Bit::One,
       "xor(1,1,0,1) = 1 -- three 1s is odd parity");
    ck(eval_gate(K_XOR, {Bit::One, Bit::Zero, Bit::X}), Bit::X,
       "xor(1,0,x) = x -- any x makes parity unknown, regardless of position");
    ck(eval_gate(K_NAND, {Bit::One, Bit::One, Bit::One}), Bit::Zero,
       "nand(1,1,1) = 0");

    //------------------------------------------------------- degenerate input
    ck(eval_gate(K_AND, {}), Bit::X, "eval_gate with no inputs returns x, not a crash");
    ck(eval_gate(K_NOT, {}), Bit::X, "eval_gate(NOT) with no inputs returns x, not a crash");

    if (fails == 0) std::printf("test_gate_eval: PASS\n");
    else            std::printf("test_gate_eval: FAIL (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
