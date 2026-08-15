// Test case 2: Half adder, structural style (gate primitives).
// Exercises: xor/and gate instantiation, non-ANSI port association.
module half_adder_gate(A, B, Sum, Carry);
  input  A, B;
  output Sum, Carry;
  xor u1(Sum, A, B);
  and u2(Carry, A, B);
endmodule
