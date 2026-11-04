// Reuse Sapphire's UART; firmware switches only after the transmitter is idle.
module voice_uart_mux (
    input wire clk, resetn, write, select_voice, uart_tx, module_busy,
    output wire debug_tx, tx, playing,
    output reg selected
);
    (* async_reg = "true" *) reg busy_meta, busy_sync;
    assign debug_tx = selected ? 1'b1 : uart_tx;
    assign tx = selected ? uart_tx : 1'b1;
    assign playing = busy_sync;
    always @(posedge clk) begin
        if (!resetn) begin
            selected <= 0; busy_meta <= 0; busy_sync <= 0;
        end else begin
            if (write) selected <= select_voice;
            busy_meta <= module_busy; busy_sync <= busy_meta;
        end
    end
endmodule
