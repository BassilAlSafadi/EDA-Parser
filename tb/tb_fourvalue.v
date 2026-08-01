//============================================================================
// tb_fourvalue.v  --  assert the §2.2 truth table against native operators
//----------------------------------------------------------------------------
// The four-value semantics of 05 "Results of Evaluating Expressions" are a
// VERIFICATION TARGET here, not something the tool implements: a host `reg`
// bit is natively 0/1/x/z, so the host's own ==, ===, !=, !==, &, &&, |, ||, ^
// already produce the specified results, including x-propagation and === /
// !== treating x and z as literal values.  This bench evaluates all 16 (a,b)
// combinations and checks the native results against the lecture table.  If it
// passes, the semantics layer of Phase 3 is correct by construction (§2.2,
// acceptance §9.4).
//
// Per row the nine results are packed MSB->LSB as:
//   [8]=a==b [7]=a===b [6]=a!=b [5]=a!==b [4]=a&b [3]=a&&b [2]=a|b [1]=a||b [0]=a^b
// A host reg is four-state, so an expected 'x' is written literally as x.
//============================================================================
`timescale 1ns/1ns
module tb_fourvalue;

    integer ia, ib, fails;
    reg a, b;
    reg [8:0] got, exp;

    // map 0->0, 1->1, 2->x, 3->z
    function four; input [1:0] s;
        case (s) 2'd0: four = 1'b0; 2'd1: four = 1'b1; 2'd2: four = 1'bx; default: four = 1'bz; endcase
    endfunction

    // expected row for combination (ia,ib), indexed ia*4+ib, from §2.2
    function [8:0] exprow; input integer idx;
        case (idx)
            //                  ==  ===  !=  !==  &  &&  |  ||  ^
            0:  exprow = 9'b1_1_0_0_0_0_0_0_0;   // 0 0
            1:  exprow = 9'b0_0_1_1_0_0_1_1_1;   // 0 1
            2:  exprow = 9'bx_0_x_1_0_0_x_x_x;   // 0 x
            3:  exprow = 9'bx_0_x_1_0_0_x_x_x;   // 0 z
            4:  exprow = 9'b0_0_1_1_0_0_1_1_1;   // 1 0
            5:  exprow = 9'b1_1_0_0_1_1_1_1_0;   // 1 1
            6:  exprow = 9'bx_0_x_1_x_x_1_1_x;   // 1 x
            7:  exprow = 9'bx_0_x_1_x_x_1_1_x;   // 1 z
            8:  exprow = 9'bx_0_x_1_0_0_x_x_x;   // x 0
            9:  exprow = 9'bx_0_x_1_x_x_1_1_x;   // x 1
            10: exprow = 9'bx_1_x_0_x_x_x_x_x;   // x x
            11: exprow = 9'bx_0_x_1_x_x_x_x_x;   // x z
            12: exprow = 9'bx_0_x_1_0_0_x_x_x;   // z 0
            13: exprow = 9'bx_0_x_1_x_x_1_1_x;   // z 1
            14: exprow = 9'bx_0_x_1_x_x_x_x_x;   // z x
            default: exprow = 9'bx_1_x_0_x_x_x_x_x; // z z
        endcase
    endfunction

    function [7:0] ch; input [1:0] s;
        case (s) 2'd0: ch="0"; 2'd1: ch="1"; 2'd2: ch="x"; default: ch="z"; endcase
    endfunction

    initial begin
        fails = 0;
        for (ia = 0; ia < 4; ia = ia + 1)
        for (ib = 0; ib < 4; ib = ib + 1) begin
            a = four(ia[1:0]);
            b = four(ib[1:0]);
            got = { (a==b), (a===b), (a!=b), (a!==b),
                    (a&b), (a&&b), (a|b), (a||b), (a^b) };
            exp = exprow(ia*4 + ib);
            // === so an expected x/z must match exactly
            if (got !== exp) begin
                fails = fails + 1;
                $display("FAIL a=%0s b=%0s got=%b exp=%b", ch(ia[1:0]), ch(ib[1:0]), got, exp);
            end
        end
        if (fails == 0) $display("tb_fourvalue: PASS (all 16 rows match native operators)");
        else            $display("tb_fourvalue: FAIL (%0d mismatches)", fails);
        $finish;
    end

endmodule
