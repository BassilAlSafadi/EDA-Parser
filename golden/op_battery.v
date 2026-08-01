// Operator expression battery (05 "Results of Evaluating Expressions").
// Exercises: bitwise, reduction, equality/case-equality, shift, part-select,
// concatenation, replication and the conditional operator.
module op_battery(input [3:0] a, input [3:0] b,
                  output [3:0] y1, output y2, output [3:0] y3, output [7:0] y4);
  assign y1 = (a & b) | (a ^ b);
  assign y2 = (a === b) ? &a : ~|b;
  assign y3 = a[3:1] + {1'b0, b[1:0]} - (a << 1);
  assign y4 = {2{a}};
endmodule
