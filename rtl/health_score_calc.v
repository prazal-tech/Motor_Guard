// health_score_calc.v
module health_score_calc(
    input rpm_stall, rpm_over, rpm_degraded,
    input high_curr, low_curr, high_temp, high_vib, volt_bad,
    output reg [7:0] health_score
    );
    reg [7:0] deduction;
    always @(*) begin
        deduction = 8'd0;
        if (rpm_stall || rpm_over) deduction = deduction + 8'd20;
        else if (rpm_degraded)     deduction = deduction + 8'd10;
        if (volt_bad)              deduction = deduction + 8'd20;
        if (high_curr)             deduction = deduction + 8'd20;
        else if (low_curr)         deduction = deduction + 8'd10;
        if (high_temp)             deduction = deduction + 8'd20;
        if (high_vib)              deduction = deduction + 8'd20;
        health_score = (deduction >= 8'd100) ? 8'd0 : 8'd100 - deduction;
    end
endmodule