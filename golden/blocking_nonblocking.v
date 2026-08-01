// Blocking vs non-blocking pair (04 "Blocking vs Nonblocking Assignments").
// Highest-value test: the two must ultimately produce DIFFERENT results.
module blocking_nonblocking(input clk, output reg b, output reg nb);
  reg t;
  always @(posedge clk) begin      // blocking: t updates, then b sees new t
    t = 1'b1;
    b = t;
  end
  always @(posedge clk) begin      // non-blocking: nb sees the OLD t
    t <= 1'b1;
    nb <= t;
  end
endmodule
