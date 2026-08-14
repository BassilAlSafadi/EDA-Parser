// Structural half-adder (04 "Verilog Description Styles - Examples").
// Exercises: gate instantiation, non-ANSI port association.
module half_adder_structural(A, B, Sum, Carry);
  input  A, B;
  output Sum, Carry;
  xor u1(Sum, A, B);
  and u2(Carry, A, B);
endmodule
