// 4-bit ripple-carry adder (04 "Learning through Examples": structural reuse).
// The Task-5.1 workhorse: three modules, two levels of nesting, one module
// instantiated four times and another instantiated twice inside it, positional
// connections at the lower level and named connections at the upper one.
module half_adder(input a, input b, output s, output c);
  xor u_x(s, a, b);
  and u_a(c, a, b);
endmodule

module full_adder(input a, input b, input cin, output s, output cout);
  wire s0, c0, c1;
  half_adder h0(a, b, s0, c0);
  half_adder h1(s0, cin, s, c1);
  or u_o(cout, c0, c1);
endmodule

module ripple_adder4(input [3:0] a, input [3:0] b, input cin,
                     output [3:0] s, output cout);
  wire c1, c2, c3;
  full_adder fa0(.a(a[0]), .b(b[0]), .cin(cin), .s(s[0]), .cout(c1));
  full_adder fa1(.a(a[1]), .b(b[1]), .cin(c1),  .s(s[1]), .cout(c2));
  full_adder fa2(.a(a[2]), .b(b[2]), .cin(c2),  .s(s[2]), .cout(c3));
  full_adder fa3(.a(a[3]), .b(b[3]), .cin(c3),  .s(s[3]), .cout(cout));
endmodule
