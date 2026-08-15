// Test case 5: 2-to-1 multiplexer, RTL style (conditional operator).
// Exercises: assign, ternary (?:) operator, X-propagation when the two
// branches disagree under an unknown select line.
module mux2to1_rtl(input sel, input a, input b, output y);
  assign y = sel ? a : b;
endmodule
