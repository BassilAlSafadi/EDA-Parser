//============================================================================
// four_state.hpp -- a 64-bit four-state (0/1/x/z) value
//----------------------------------------------------------------------------
// The host Verilog got four-value logic for free: a `reg` bit is natively
// 0/1/x/z, so a target literal's value was stored/compared directly in a
// native reg with no aval/bval bit-planes (spec §2.2, §3.5). C++ has no such
// type, so this class hand-implements exactly the operations the scanner and
// dumper actually perform on that reg -- nothing more:
//   * val = val << n            -> shiftLeft(n)   (vacated low bits become 0,
//                                                   bits shifted past bit 63
//                                                   are dropped, same as a
//                                                   fixed-width reg)
//   * val[hi:lo] = <uniform x/z/0/1 pattern>       -> setRange / fillRange
//   * val[i] === 1'bx / 1'bz                       -> isX / isZ
// It is never asked to add, subtract, or otherwise arithmetically evaluate
// two four-state values (the tool lexes and dumps target literals; it does
// not evaluate target expressions), so no arithmetic operators are provided.
//============================================================================
#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace vsim {

enum class Bit : std::uint8_t { Zero = 0, One = 1, X = 2, Z = 3 };

class FourState {
public:
    static constexpr int WIDTH = 64;   // VALW

    FourState() { bits_.fill(Bit::Zero); }

    Bit get(int i) const {
        return (i >= 0 && i < WIDTH) ? bits_[static_cast<std::size_t>(i)] : Bit::Zero;
    }
    void set(int i, Bit b) {
        if (i >= 0 && i < WIDTH) bits_[static_cast<std::size_t>(i)] = b;
    }

    bool isX(int i) const { return get(i) == Bit::X; }
    bool isZ(int i) const { return get(i) == Bit::Z; }
    bool isUnknown(int i) const { Bit b = get(i); return b == Bit::X || b == Bit::Z; }

    // val = val << n : vacated low bits become 0; bits shifted past bit
    // WIDTH-1 are lost, matching a fixed-width Verilog reg.
    void shiftLeft(int n) {
        if (n <= 0) return;
        if (n >= WIDTH) { bits_.fill(Bit::Zero); return; }
        for (int i = WIDTH - 1; i >= n; --i) bits_[static_cast<std::size_t>(i)] = bits_[static_cast<std::size_t>(i - n)];
        for (int i = 0; i < n; ++i) bits_[static_cast<std::size_t>(i)] = Bit::Zero;
    }

    // val[lo .. lo+width-1] = b, uniformly (e.g. "xxxx" / "zzzz" fill of a digit)
    void setRangeUniform(int lo, int width, Bit b) {
        for (int i = lo; i < lo + width; ++i) set(i, b);
    }

    // val[width-1 : 0] = binary(value), MSB-first, 0/1 digits only
    void setLowFromUint(int width, std::uint64_t value) {
        for (int i = 0; i < width; ++i)
            set(i, ((value >> i) & 1u) ? Bit::One : Bit::Zero);
    }

    // val[hi-1 : lo] = b for every position in [lo, hi)
    void fillRange(int lo, int hi, Bit b) {
        for (int i = lo; i < hi; ++i) set(i, b);
    }

    // any x/z among the low `width` bits? (dump_val's has_xz check)
    bool hasUnknown(int width) const {
        for (int i = 0; i < width && i < WIDTH; ++i)
            if (isUnknown(i)) return true;
        return false;
    }

    // low `width` bits as an unsigned integer (only meaningful when
    // hasUnknown(width) is false).
    std::uint64_t toUint(int width) const {
        std::uint64_t v = 0;
        for (int i = width - 1; i >= 0; --i)
            v = (v << 1) | (get(i) == Bit::One ? 1u : 0u);
        return v;
    }

    // low `width` bits as "10xz..." text, MSB first (dump_val's binary form).
    std::string toBitString(int width) const {
        std::string s;
        s.reserve(static_cast<std::size_t>(width));
        for (int i = width - 1; i >= 0; --i) {
            switch (get(i)) {
                case Bit::Zero: s.push_back('0'); break;
                case Bit::One:  s.push_back('1'); break;
                case Bit::X:    s.push_back('x'); break;
                case Bit::Z:    s.push_back('z'); break;
            }
        }
        return s;
    }

    static FourState fromUint(std::uint64_t value, int width) {
        FourState f;
        f.setLowFromUint(width, value);
        return f;
    }

private:
    std::array<Bit, WIDTH> bits_;
};

} // namespace vsim
