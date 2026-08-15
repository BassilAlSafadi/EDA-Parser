// Test case 4: Full adder, structural style (gate primitives only, flat --
// no sub-module instantiation, see NOTES.txt in the test/ root for why).
// Exercises: xor/and/or gate instantiation, internal wires, multi-gate
// signal chaining within a single module.
module full_adder_gate(input a, input b, input cin, output s, output cout);
  wire t1, g1, g2;
  xor u1(t1, a, b);
  xor u2(s, t1, cin);
  and u3(g1, a, b);
  and u4(g2, t1, cin);
  or  u5(cout, g1, g2);
endmodule
