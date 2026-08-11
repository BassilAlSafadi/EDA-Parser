// Task-5.1 negative test: the instantiated module is never defined.
// Expected: one error, "instantiation of undefined module 'missing_mod'",
// positioned at line 6 column 3, and ELABORATE FAILED.
module top_undef(input a, output y);
  wire t;
  missing_mod u0(a, t);
  assign y = t;
endmodule
