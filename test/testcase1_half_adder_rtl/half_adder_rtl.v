// Test case 1: Half adder, RTL style (continuous assignment).
// Exercises: assign, ^ (XOR), & (AND), ANSI port list.
module half_adder_rtl(input a, input b, output sum, output carry);
  assign sum   = a ^ b;
  assign carry = a & b;
endmodule
