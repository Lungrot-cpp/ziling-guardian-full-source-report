// CAR1 + STR1: DDR-backed CPU frame, two-word streaming serializer; no block RAM.
module care_peripherals #(parameter integer CLK_HZ=100000000,LEDS=288)(
 input wire clk,resetn,input wire [15:0] paddr,input wire psel,penable,pwrite,
 input wire [31:0] pwdata,output reg [31:0] prdata,output wire ws_data,servo_pwm
);
 localparam integer US_DIV=CLK_HZ/1000000,BIT_CYCLES=CLK_HZ/800000;
 localparam integer ZERO_HIGH=CLK_HZ/10000000*35/10,ONE_HIGH=CLK_HZ/10000000*70/10;
 reg [6:0] us_divider;wire us_tick=us_divider==US_DIV-1;
 reg [9:0] ms_divider;wire ms_tick=us_tick&&ms_divider==999;
 reg [11:0] heartbeat_age;wire fresh=heartbeat_age<3000;
 reg [8:0] idle_us,pixel_index;
 reg [23:0] queued_color,shift_color;
 reg queued,running,bit_active,black_frame,clear_pending,blanked,underrun;
 reg [7:0] phase;reg [4:0] bit_index;
 wire busy=running||idle_us<320;
 wire ready=running&&!black_frame&&!queued;
 wire access=psel&&penable&&pwrite;
 wire begin_frame=access&&paddr[7:0]==8'h04&&pwdata[1]&&!busy;
 reg armed,disarm_pending;
 reg [15:0] neutral_us,open_us,close_us,motion_ms,remaining_ms,pulse_us,applied_us,servo_phase;
 reg [2:0] door_state;
 assign ws_data=bit_active&&phase<(shift_color[bit_index]?ONE_HIGH:ZERO_HIGH);
 assign servo_pwm=armed&&servo_phase<applied_us;
 always @(*)begin
  prdata=0;
  case(paddr[7:0])
   8'h00:prdata=32'h43415231;
   8'h04:prdata={26'b0,underrun,1'b1,fresh,1'b1,busy,ready};
   8'h10:prdata=neutral_us;8'h14:prdata=open_us;8'h18:prdata=close_us;
   8'h1c:prdata=motion_ms;8'h20:prdata={12'b0,remaining_ms,door_state,armed};
   8'h24:prdata=LEDS;8'h2c:prdata=32'h53545231;
   default:;
  endcase
 end
 always @(posedge clk)begin
  if(!resetn)begin
   us_divider<=0;ms_divider<=0;heartbeat_age<=3000;
   idle_us<=0;pixel_index<=0;queued_color<=0;shift_color<=0;
   queued<=0;running<=0;bit_active<=0;black_frame<=0;clear_pending<=1;blanked<=0;underrun<=0;
   phase<=0;bit_index<=23;
   armed<=0;disarm_pending<=0;neutral_us<=1500;open_us<=1650;close_us<=1350;motion_ms<=400;
   remaining_ms<=0;pulse_us<=1500;applied_us<=1500;servo_phase<=0;door_state<=0;
  end else begin
   us_divider<=us_tick?0:us_divider+1'b1;
   if(us_tick)begin
    ms_divider<=ms_tick?0:ms_divider+1'b1;
    servo_phase<=servo_phase==19999?0:servo_phase+1'b1;
    if(servo_phase==19999)applied_us<=pulse_us;
    if(!bit_active&&idle_us!=511)idle_us<=idle_us+1'b1;
   end
   if(ms_tick)begin
    if(heartbeat_age<4095)heartbeat_age<=heartbeat_age+1'b1;
    if(remaining_ms!=0)begin
     remaining_ms<=remaining_ms-1'b1;
     if(remaining_ms==1)begin
      pulse_us<=neutral_us;door_state<=4;
      if(disarm_pending)begin armed<=0;disarm_pending<=0;door_state<=0;end
     end
    end
   end
   if(!fresh)begin
    pulse_us<=neutral_us;remaining_ms<=0;if(armed)door_state<=4;
    if(!blanked)clear_pending<=1;
   end
   if(access)case(paddr[7:0])
    8'h0c:if(ready)begin queued_color<=pwdata[23:0];queued<=1;end
    8'h10:if(!armed&&pwdata>=1000&&pwdata<=2000)begin neutral_us<=pwdata[15:0];pulse_us<=pwdata[15:0];applied_us<=pwdata[15:0];end
    8'h14:if(!armed&&pwdata>=1000&&pwdata<=2000)open_us<=pwdata[15:0];
    8'h18:if(!armed&&pwdata>=1000&&pwdata<=2000)close_us<=pwdata[15:0];
    8'h1c:if(!armed&&pwdata>=20&&pwdata<=2000)motion_ms<=pwdata[15:0];
    8'h28:heartbeat_age<=0;
    8'h20:case(pwdata[2:0])
     0:begin pulse_us<=neutral_us;remaining_ms<=60;disarm_pending<=armed;door_state<=armed?5:0;end
     1:begin armed<=1;pulse_us<=neutral_us;applied_us<=neutral_us;servo_phase<=0;remaining_ms<=0;door_state<=1;disarm_pending<=0;end
     2:if(armed&&fresh&&!disarm_pending)begin pulse_us<=open_us;remaining_ms<=motion_ms;door_state<=2;end
     3:if(armed&&fresh&&!disarm_pending)begin pulse_us<=close_us;remaining_ms<=motion_ms;door_state<=3;end
     4:begin pulse_us<=neutral_us;remaining_ms<=0;door_state<=4;disarm_pending<=0;end
     default:;
    endcase
    default:;
   endcase
   if(begin_frame)begin
    running<=1;black_frame<=0;bit_active<=0;queued<=0;pixel_index<=0;
    phase<=0;bit_index<=23;idle_us<=0;heartbeat_age<=0;clear_pending<=0;blanked<=0;underrun<=0;
   end else if(!running)begin
    if(idle_us>=320&&clear_pending)begin
     running<=1;black_frame<=1;bit_active<=1;queued<=0;pixel_index<=0;
     shift_color<=0;phase<=0;bit_index<=23;idle_us<=0;
    end
   end else if(!bit_active)begin
    if(queued)begin
     shift_color<=queued_color;queued<=0;bit_active<=1;phase<=0;bit_index<=23;idle_us<=0;
    end else if(idle_us>=30)begin
     running<=0;queued<=0;clear_pending<=1;underrun<=1;idle_us<=0;
    end
   end else if(phase==BIT_CYCLES-1)begin
    phase<=0;
    if(bit_index==0)begin
     bit_index<=23;
     if(pixel_index==LEDS-1)begin
      running<=0;bit_active<=0;queued<=0;idle_us<=0;
      if(black_frame)begin blanked<=1;clear_pending<=0;end
     end else begin
      pixel_index<=pixel_index+1'b1;
      if(black_frame)shift_color<=0;
      else if(queued)begin shift_color<=queued_color;queued<=0;end
      else begin bit_active<=0;idle_us<=0;end
     end
    end else bit_index<=bit_index-1'b1;
   end else phase<=phase+1'b1;
  end
 end
endmodule
