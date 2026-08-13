//============================================================================
// gate_eval.cpp -- see gate_eval.h for the design note.
//============================================================================
#include "gate_eval.h"

namespace vsim {

Bit gate_normalize(Bit b) {
    return (b == Bit::Z) ? Bit::X : b;
}

// AND: 0 dominates (0 & anything = 0); 1 & 1 = 1; 1 & x = x; x & x = x.
Bit gate_and2(Bit a, Bit b) {
    a = gate_normalize(a);
    b = gate_normalize(b);
    if (a == Bit::Zero || b == Bit::Zero) return Bit::Zero;
    if (a == Bit::One && b == Bit::One)   return Bit::One;
    return Bit::X;
}

// OR: 1 dominates (1 | anything = 1); 0 | 0 = 0; 0 | x = x; x | x = x.
Bit gate_or2(Bit a, Bit b) {
    a = gate_normalize(a);
    b = gate_normalize(b);
    if (a == Bit::One || b == Bit::One)     return Bit::One;
    if (a == Bit::Zero && b == Bit::Zero)   return Bit::Zero;
    return Bit::X;
}

// XOR: any x input makes the parity unknown; otherwise standard parity.
Bit gate_xor2(Bit a, Bit b) {
    a = gate_normalize(a);
    b = gate_normalize(b);
    if (a == Bit::X || b == Bit::X) return Bit::X;
    return (a == b) ? Bit::Zero : Bit::One;
}

// NOT: simple complement; z is normalized to x first, like every other gate.
Bit gate_not1(Bit a) {
    a = gate_normalize(a);
    if (a == Bit::Zero) return Bit::One;
    if (a == Bit::One)  return Bit::Zero;
    return Bit::X;
}

Bit eval_gate(Kind kind, const std::vector<Bit>& inputs) {
    if (kind == K_NOT) {
        if (inputs.empty()) return Bit::X;
        return gate_not1(inputs[0]);
    }
    if (kind == K_BUF) {
        if (inputs.empty()) return Bit::X;
        return gate_normalize(inputs[0]);   // buf passes the bit through, unnegated
    }

    if (inputs.empty()) return Bit::X;

    switch (kind) {
        case K_AND:
        case K_NAND: {
            Bit acc = gate_normalize(inputs[0]);
            for (std::size_t i = 1; i < inputs.size(); ++i) acc = gate_and2(acc, inputs[i]);
            return (kind == K_NAND) ? gate_not1(acc) : acc;
        }
        case K_OR:
        case K_NOR: {
            Bit acc = gate_normalize(inputs[0]);
            for (std::size_t i = 1; i < inputs.size(); ++i) acc = gate_or2(acc, inputs[i]);
            return (kind == K_NOR) ? gate_not1(acc) : acc;
        }
        case K_XOR:
        case K_XNOR: {
            Bit acc = gate_normalize(inputs[0]);
            for (std::size_t i = 1; i < inputs.size(); ++i) acc = gate_xor2(acc, inputs[i]);
            return (kind == K_XNOR) ? gate_not1(acc) : acc;
        }
        default:
            return Bit::X;   // not a recognised gate kind
    }
}

} // namespace vsim
