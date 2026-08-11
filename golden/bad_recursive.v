// Task-5.1 negative test: a cycle in the instantiation graph, reached from a
// legitimate top.  top_cyc is instantiated by nobody, so it is the top; ping
// instantiates pong and pong instantiates ping again.  Without the ancestor
// check in elab_body this elaborates forever.
// Expected: one error, "recursive instantiation of module 'ping'".
module ping(input a, output y);
  pong u0(a, y);
endmodule

module pong(input a, output y);
  ping u0(a, y);
endmodule

module top_cyc(input a, output y);
  ping u0(a, y);
endmodule
