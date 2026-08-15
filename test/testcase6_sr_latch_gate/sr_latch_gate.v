// Test case 6: SR latch, structural style (cross-coupled NOR gates).
// Exercises: nor gate primitive, a feedback loop (q feeds qn's gate and
// vice versa), and the delta-cycle fixed-point solver needed to settle it.
// NOTE: this simulator re-initialises every net to X at the start of each
// vector (see NOTES.txt) -- there is no carried state between cycles, so
// the "hold" row (s=0,r=0) below settles to X rather than remembering the
// previous set/reset. That is expected for this tool, not a latch bug.
module sr_latch_gate(input s, input r, output q, output qn);
  nor u1(q,  r, qn);
  nor u2(qn, s, q);
endmodule
