// Task-5.2 negative test: the connection expression on the RIGHT of a
// connection (the parent-side net) is never declared anywhere in the parent
// module. Distinct from bad_conn.v (Task 5.1): that fixture names a PORT
// that doesn't exist on the target module. This one names a target module
// port that DOES exist -- the module and the port association are both
// fine -- but the net the parent tries to wire it to does not exist.
// Expected: one error, "reference to undefined signal 'ghost'".
module leaf(input a, output y);
  assign y = a;
endmodule
 
module top_undef_sig(input p, output q);
  leaf u0(.a(p), .y(ghost));
endmodule