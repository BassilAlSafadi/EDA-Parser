// Test case 3: Full adder, RTL style (continuous assignment).
// Exercises: assign, ^ (XOR) chained, & (AND), | (OR).
module full_adder_rtl(input a, input b, input cin, output s, output cout);
  assign s    = a ^ b ^ cin;
  assign cout = (a & b) | (a & cin) | (b & cin);
endmodule
