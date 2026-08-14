// RTL half-adder (same lecture slide).
// Exercises: continuous assignment, ^ and &, ANSI ports.
module half_adder_rtl(input a, input b, output sum, output carry);
  assign sum   = a ^ b;
  assign carry = a & b;
endmodule
