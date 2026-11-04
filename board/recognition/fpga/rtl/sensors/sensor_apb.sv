// Sensor GPIO/timer/display interface; program and buffers use Sapphire DDR.
module sensor_apb #(
    parameter CLOCK_HZ = 100000000
)(
    input wire clk, resetn,
    input wire [15:0] paddr,
    input wire psel, penable, pwrite,
    input wire [31:0] pwdata,
    output reg [31:0] prdata,
    output wire pslverror,
    input wire camera_vs,
    output wire [31:0] capture_time, capture_id,
    input wire [31:0] video_time, video_id,
    input wire [1:0] scl_in, sda_in,
    output wire [1:0] scl_low, sda_low,
    output wire alarm_active, alarm_beep,
    input wire voice_busy,
    input wire uart_tx,
    output wire voice_tx, debug_tx,
    output reg display_request,
    input wire display_ack,
    output wire display_write,
    output wire [10:0] display_addr,
    output wire [15:0] display_data
);
    wire access = psel && penable && pwrite;
    wire voice_selected, voice_playing;
    voice_uart_mux voice (
        .clk(clk),.resetn(resetn),.write(access && paddr==16'h0464),
        .select_voice(pwdata[0]),.module_busy(voice_busy),.uart_tx(uart_tx),
        .tx(voice_tx),.debug_tx(debug_tx),.selected(voice_selected),.playing(voice_playing));
    wire display_select = paddr[15:13] == 3'b101;
    (* async_reg = "true" *) reg [1:0] scl_meta, scl_sync, sda_meta, sda_sync;
    (* async_reg = "true" *) reg [2:0] vs_sync;
    (* async_reg = "true" *) reg [1:0] ack_sync;
    reg [6:0] us_div;
    reg [31:0] usec, frame_time, frame_id;
    reg [1:0] manual_scl,manual_sda;
    reg acquire_enable;
    reg [10:0] raw_index;
    wire [15:0] raw_data;
    wire [1:0] hw_scl,hw_sda;
    wire [31:0] seq0,seq1,stamp0,stamp1,acquire_errors,env_seq,env_stamp;
    wire [15:0] control0,control1;
    wire [47:0] env_data;
    assign scl_low=acquire_enable ? hw_scl:manual_scl;
    assign sda_low=acquire_enable ? hw_sda:manual_sda;
    sensor_acquire #(.CLOCK_HZ(CLOCK_HZ)) acquisition(
     .clk(clk),.resetn(resetn),.usec(usec),.enable(acquire_enable),
     .scl(scl_sync),.sda(sda_sync),.scl_low(hw_scl),.sda_low(hw_sda),
     .read_index(raw_index),.read_data(raw_data),.seq0(seq0),.seq1(seq1),
     .stamp0(stamp0),.stamp1(stamp1),.control0(control0),.control1(control1),.errors(acquire_errors),
     .env_seq(env_seq),.env_stamp(env_stamp),.env_data(env_data));
    reg [9:0] filter_index;
    reg [1:0] filter_warmup;
    wire [15:0] filter_result;
    wire filter_write=access && paddr==16'h0454 && filter_index<768;
    thermal_filter heat_filter(.clk(clk),.write(filter_write),.index(filter_index),
     .warmup(filter_warmup),.value(pwdata[15:0]),.result(filter_result));
    reg [31:0] alarm_time;
    reg [31:0] alarm_info;
    // CPU heartbeat expires both external outputs if inference stalls.
    assign alarm_active=resetn && alarm_info[0] && usec-alarm_time<2000000 && usec[18];
    assign alarm_beep=1'b0; // No buzzer fitted; retain LED/HDMI alarms.
    wire display_busy = display_request != ack_sync[1];
    assign pslverror = access && display_select &&
        (display_busy || paddr[12] == display_request);
    assign display_write = resetn && access && display_select && !pslverror;
    assign display_addr = paddr[12:2];
    assign display_data = pwdata[15:0];
    assign capture_time = frame_time;
    assign capture_id = frame_id;

    always @* begin
        prdata = 0;
        if (paddr[15:8]==8'h04) case(paddr[7:2])
            0:prdata={31'b0,acquire_enable};
            1:prdata={21'b0,raw_index};
            2:prdata={16'b0,raw_data};
            3:prdata=seq0;4:prdata=seq1;
            5:prdata=stamp0;6:prdata=stamp1;
            7:prdata={16'b0,control0};8:prdata={16'b0,control1};
            9:prdata=acquire_errors;10:prdata=env_seq;11:prdata=env_stamp;
            12:prdata=env_data[31:0];13:prdata={16'b0,env_data[47:32]};
            15:prdata=32'h41435132; // ACQ2: autonomous acquisition/filter/alarm interface.
            20:prdata={20'b0,filter_warmup,filter_index};
            21:prdata={16'b0,filter_result};22:prdata=alarm_info;
            24:prdata={30'b0,voice_playing,voice_selected};
            26:prdata=32'h56544332; // VTC2: shared UART with isolated debug/voice outputs.
            default:prdata=0;
        endcase
        else if (!display_select) case (paddr[7:2])
            0,1: prdata = {22'b0, sda_sync[paddr[2]], scl_sync[paddr[2]],
                          6'b0, sda_low[paddr[2]], scl_low[paddr[2]]};
            2: prdata = usec;
            3: prdata = frame_time;
            4: prdata = frame_id;
            5: prdata = {30'b0, ack_sync[1], display_request};
            6: prdata = video_time;
            7: prdata = video_id;
            8: prdata = 32'h53454e31; // SEN1
            default: prdata = 0;
        endcase
    end
    always @(posedge clk) begin
        if (!resetn) begin
            manual_scl <= 0; manual_sda <= 0; display_request <= 0;
            acquire_enable<=0;raw_index<=0;filter_index<=0;filter_warmup<=0;alarm_time<=0;alarm_info<=0;
            us_div <= 0; usec <= 0; frame_time <= 0; frame_id <= 0;
            vs_sync <= 0; ack_sync <= 0;
            scl_meta <= 3; scl_sync <= 3; sda_meta <= 3; sda_sync <= 3;
        end else begin
            scl_meta <= scl_in; scl_sync <= scl_meta;
            sda_meta <= sda_in; sda_sync <= sda_meta;
            vs_sync <= {vs_sync[1:0], camera_vs};
            ack_sync <= {ack_sync[0], display_ack};
            if (us_div == CLOCK_HZ/1000000-1) begin
                us_div <= 0; usec <= usec + 1;
            end else us_div <= us_div + 1'b1;
            if (vs_sync[2:1] == 2'b01) begin
                frame_time <= usec; frame_id <= frame_id + 1;
            end
            if(access && paddr[15:8]==8'h04)case(paddr[7:2])
                0:acquire_enable<=pwdata[0];
                1:if(pwdata<1664)raw_index<=pwdata[10:0];
                20:begin filter_index<=pwdata[9:0];filter_warmup<=pwdata[11:10];end
                22:begin alarm_info<=pwdata;alarm_time<=usec;end
                default:begin end
            endcase
            else if (access && !display_select) case (paddr[7:2])
                0,1: begin
                    manual_scl[paddr[2]] <= pwdata[0];
                    manual_sda[paddr[2]] <= pwdata[1];
                end
                5: if (!display_busy) display_request <= pwdata[0];
                default: begin end
            endcase
        end
    end
endmodule
