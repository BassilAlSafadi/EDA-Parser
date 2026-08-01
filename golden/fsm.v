// Even/odd-style FSM (04 "Finite State Machines (FSMs)").
// Exercises: two-block pattern, async reset via if, full case coverage.
module fsm(input clk, input rst, input x, output reg y);
  reg state, next;
  always @(posedge clk or posedge rst)      // state register
    if (rst) state <= 1'b0;
    else     state <= next;
  always @(*) begin                          // next-state / output logic
    case (state)
      1'b0:    begin next = x ? 1'b1 : 1'b0; y = 1'b0; end
      1'b1:    begin next = 1'b0;            y = 1'b1; end
      default: next = 1'b0;
    endcase
  end
endmodule
