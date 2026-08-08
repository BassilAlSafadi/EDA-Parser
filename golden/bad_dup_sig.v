// Task-5.2 negative test: a header port and a body wire declaration name the
// same signal.  Task 5.1 has no concept of a signal table at all, so this is
// invisible to it; Task 5.2's build_scope is the first pass that can catch a
// duplicate declaration within one instance's own scope.
// Expected: one error, "duplicate declaration of signal 'a'".
module top_dup_sig(input a, output y);
  wire a;
  assign y = a;
endmodule