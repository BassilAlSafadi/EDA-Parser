// Task-5.1 negative test: the instantiation names a port the target module
// does not have.  The module itself exists, so this is not an "undefined
// module" error -- it is an unresolvable port association.
// Expected: one error, "instantiated module has no port named 'zzz'".
module leaf(input a, input b, output y);
  assign y = a & b;
endmodule

module top_conn(input p, output q);
  leaf u0(.a(p), .zzz(p), .y(q));
endmodule
