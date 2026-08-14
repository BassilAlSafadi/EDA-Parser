// N-bit counter (04 "Learning through Examples").
// Exercises: parameters, always @(posedge), asynchronous reset, vectors.
module counter #(parameter WIDTH = 8)
  (input clk, input rst, output reg [WIDTH-1:0] q);
  always @(posedge clk or posedge rst)
    if (rst) q <= 0;
    else     q <= q + 1;
endmodule
