`timescale 1ns/1ps
module tb_voice_uart;
    reg clk=0,resetn=0,psel=0,penable=0,pwrite=0,module_busy=0;
    reg [15:0] addr=0;
    reg [31:0] data=0;
    wire [31:0] readback;
    wire tx,debug_tx,uart_tx;
    reg uart_valid=0;
    reg [5:0] uart_addr=0;
    reg [31:0] uart_data=0;
    wire uart_ready;
    always #5 clk=~clk;
    sensor_apb dut(.clk(clk),.resetn(resetn),.paddr(addr),.psel(psel),
        .penable(penable),.pwrite(pwrite),.pwdata(data),.prdata(readback),
        .camera_vs(1'b0),.video_time(32'b0),.video_id(32'b0),
        .scl_in(2'b11),.sda_in(2'b11),.display_ack(1'b0),
        .voice_busy(module_busy),.uart_tx(uart_tx),.voice_tx(tx),.debug_tx(debug_tx));
    // The actual unmodified Sapphire UART and its FIFO, not a replacement model.
    BmbUartCtrl_ba6762f3781649428952cf46a6343376 uart (
        .io_bus_cmd_valid(uart_valid),.io_bus_cmd_ready(uart_ready),
        .io_bus_cmd_payload_last(1'b1),.io_bus_cmd_payload_fragment_opcode(1'b1),
        .io_bus_cmd_payload_fragment_address(uart_addr),.io_bus_cmd_payload_fragment_length(2'b0),
        .io_bus_cmd_payload_fragment_data(uart_data),.io_bus_cmd_payload_fragment_context(4'b0),
        .io_bus_rsp_ready(1'b1),.io_uart_txd(uart_tx),.io_uart_rxd(1'b1),
        .io_peripheralClk(clk),.peripheralCd_logic_outputReset(!resetn));
    task step;begin @(posedge clk);#1;end endtask
    task write_uart(input [5:0] a,input [31:0] value);
        begin
            @(negedge clk);uart_addr=a;uart_data=value;uart_valid=1;
            do step();while(!uart_ready);
            @(negedge clk);uart_valid=0;
        end
    endtask
    task write_reg(input [15:0] a,input [31:0] value);
        begin
            @(negedge clk);addr=a;data=value;psel=1;penable=1;pwrite=1;
            step();@(negedge clk);psel=0;penable=0;pwrite=0;
        end
    endtask
    task expect_reg(input [15:0] a,input [31:0] value);
        begin addr=a;#1;if(readback!==value)$fatal(1,"register %h: %h != %h",a,readback,value);end
    endtask
    task expect_serial(input integer voice,input [7:0] value,input integer cycles);
        integer bit_index;
        begin
            if(voice)@(negedge tx);else @(negedge debug_tx);
            repeat(cycles/2)step();
            if((voice?tx:debug_tx)!==0)$fatal(1,"start bit");
            for(bit_index=0;bit_index<8;bit_index=bit_index+1)begin
                repeat(cycles)step();
                if((voice?tx:debug_tx)!==value[bit_index])$fatal(1,"UART data %h bit %d",value,bit_index);
                if((voice?debug_tx:tx)!==1)$fatal(1,"output isolation");
            end
            repeat(cycles)step();
            if((voice?tx:debug_tx)!==1)$fatal(1,"stop bit");
        end
    endtask
    integer i;
    initial begin
        repeat(3)step();resetn=1;repeat(10)step();
        if(tx!==1 || debug_tx!==1)$fatal(1,"reset idle");
        expect_reg(16'h0468,32'h56544332);expect_reg(16'h0460,0);
        write_reg(16'h0458,32'h12345678);
        write_uart(8,26);write_uart(12,7);
        fork expect_serial(0,8'h4c,216);write_uart(0,8'h4c);join
        repeat(500)step();
        write_uart(8,1301);write_reg(16'h0464,1);expect_reg(16'h0460,1);
        fork
            begin for(i=0;i<53;i=i+1)expect_serial(1,(i*37)&255,10416);end
            begin : enqueue
                integer j;
                for(j=0;j<53;j=j+1)write_uart(0,(j*37)&255);
            end
        join
        repeat(208400)step();
        write_uart(8,26);write_reg(16'h0464,0);
        fork expect_serial(0,8'h52,216);write_uart(0,8'h52);join
        expect_reg(16'h0458,32'h12345678);
        @(negedge clk);module_busy=1;
        step();expect_reg(16'h0460,0);step();expect_reg(16'h0460,2);
        @(negedge clk);module_busy=0;step();step();expect_reg(16'h0460,0);
        write_reg(16'h0464,1);resetn=0;step();
        if(tx!==1 || debug_tx!==1)$fatal(1,"reset routing");
        $display("VOICE_UART_PASS: actual Sapphire UART, 53-byte 9600 frame, debug before/after, output isolation, BUSY CDC, APB isolation");
        $finish;
    end
    initial begin #100000000;$fatal(1,"test timeout");end
endmodule
