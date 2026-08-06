//============================================================================
// vsim_diag.v  --  diagnostic collection, sorting, printing   (§7)
//----------------------------------------------------------------------------
// Include fragment.  Policy (decision 0.7): collect every diagnostic in an
// array, print sorted by source position, and set a failure flag.  add_diag is
// a FUNCTION, not a task, precisely so the recursive-descent parser -- which is
// built from functions, and functions may not call tasks -- can report errors.
// Callers use it as  ignore = add_diag(...);
//============================================================================

reg  [7:0]      diag_sev  [0:`MAX_DIAG-1];
reg  [15:0]     diag_line [0:`MAX_DIAG-1];
reg  [15:0]     diag_col  [0:`MAX_DIAG-1];
reg  [8*80-1:0] diag_msg  [0:`MAX_DIAG-1];
// Optional identifier appended to the message as  'name'.  Elaboration needs
// it: "instantiation of undefined module 'foo'" is far more useful than the
// bare sentence, and diag_msg is a fixed-width literal that cannot carry a
// run-time name.  0 = no identifier, which is what add_diag records (§7).
reg  [`IDW-1:0] diag_id   [0:`MAX_DIAG-1];
integer         n_diag;

// Set once any SEV_ERROR is recorded -> non-zero exit policy (§7).
reg             had_error;

// Source file name, kept for the "file:line:col:" prefix.
reg [8*128-1:0] g_fname;

integer         ignore;   // scratch sink for function-call-as-statement

// ---------------------------------------------------------------------------
// add_diag -- record one diagnostic.  Silently drops everything past MAX_DIAG
// (the parser also stops after MAX_DIAG errors, §7) but still marks failure.
// ---------------------------------------------------------------------------
function automatic integer add_diag_id;
    input [7:0]      sev;
    input [15:0]     line;
    input [15:0]     col;
    input [8*80-1:0] msg;
    input [`IDW-1:0] id;                 // 0 = no identifier to quote
    begin
        if (sev == `SEV_ERROR) had_error = 1'b1;
        if (n_diag < `MAX_DIAG) begin
            diag_sev [n_diag] = sev;
            diag_line[n_diag] = line;
            diag_col [n_diag] = col;
            diag_msg [n_diag] = msg;
            diag_id  [n_diag] = id;
            n_diag = n_diag + 1;
        end
        add_diag_id = 0;
    end
endfunction

// plain form: message only.  Every Phase-1 call site uses this.
function automatic integer add_diag;
    input [7:0]      sev;
    input [15:0]     line;
    input [15:0]     col;
    input [8*80-1:0] msg;
    begin
        add_diag = add_diag_id(sev, line, col, msg, `IDW'd0);
    end
endfunction

function automatic [8*8-1:0] sev_name;
    input [7:0] sev;
    case (sev)
        `SEV_ERROR:   sev_name = "error";
        `SEV_WARNING: sev_name = "warning";
        default:      sev_name = "note";
    endcase
endfunction

// ---------------------------------------------------------------------------
// print_diags -- selection-sort the collected diagnostics by (line, col) then
// emit them as  file.v:LINE:COL: sev: message .  n_diag <= MAX_DIAG (64) so an
// O(n^2) sort is trivially fine and keeps output deterministic (§7).
// ---------------------------------------------------------------------------
task print_diags;
    integer i, j, best;
    reg [7:0]      ts; reg [15:0] tl, tc; reg [8*80-1:0] tm; reg [`IDW-1:0] ti;
    begin
        for (i = 0; i < n_diag; i = i + 1) begin
            best = i;
            for (j = i + 1; j < n_diag; j = j + 1) begin
                if ( (diag_line[j] <  diag_line[best]) ||
                     (diag_line[j] == diag_line[best] &&
                      diag_col[j]  <  diag_col[best]) )
                    best = j;
            end
            if (best != i) begin
                ts = diag_sev[i];  tl = diag_line[i];  tc = diag_col[i];  tm = diag_msg[i];
                ti = diag_id[i];
                diag_sev[i]=diag_sev[best]; diag_line[i]=diag_line[best];
                diag_col[i]=diag_col[best]; diag_msg[i]=diag_msg[best];
                diag_id [i]=diag_id [best];
                diag_sev[best]=ts; diag_line[best]=tl; diag_col[best]=tc; diag_msg[best]=tm;
                diag_id [best]=ti;
            end
        end
        for (i = 0; i < n_diag; i = i + 1) begin
            $fwrite(32'h8000_0001, "%0s:%0d:%0d: %0s: %0s", g_fname,
                    diag_line[i], diag_col[i],
                    sev_name(diag_sev[i]), diag_msg[i]);
            if (diag_id[i] != `IDW'd0) begin
                $fwrite(32'h8000_0001, " '");
                fput_ident(diag_id[i], 32'h8000_0001);
                $fwrite(32'h8000_0001, "'");
            end
            $fwrite(32'h8000_0001, "\n");
        end
    end
endtask
