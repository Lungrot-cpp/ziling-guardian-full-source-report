`timescale 1ns/1ps
module care_stream_tb;
 reg clk=0;always #5 clk=~clk;
 reg resetn=0;reg [15:0] addr=0;reg sel=0,en=0,wr=0;reg [31:0] data=0;
 wire [31:0] q;wire ws,servo;
 localparam LEDS=288;
 care_peripherals #(.LEDS(LEDS)) dut(clk,resetn,addr,sel,en,wr,data,q,ws,servo);
 task put(input [15:0] a,input [31:0] d);begin
  @(negedge clk);addr=a;data=d;sel=1;wr=1;en=0;
  @(negedge clk);en=1;@(negedge clk);en=0;sel=0;wr=0;
 end endtask
 task idle;begin while(dut.busy)@(negedge clk);end endtask
 integer i,j;reg [23:0] color;time began,last_rise;
 initial begin
  repeat(4)@(negedge clk);resetn=1;
  wait(dut.blanked);idle();$display("startup blank complete");put('h504,2);
  fork
   begin
    for(i=0;i<LEDS;i=i+1)begin
     while(!dut.ready)@(negedge clk);
     put('h50c,24'h800100^i);
    end
   end
   begin
    for(integer pixel=0;pixel<LEDS;pixel=pixel+1)begin
     color=0;
     for(integer bitn=0;bitn<24;bitn=bitn+1)begin
      @(posedge ws);began=$time;
      if(pixel!=0||bitn!=0)if(began-last_rise!=1250)$fatal(1,"stream bit period");
      last_rise=began;@(negedge ws);
      if($time-began!=350&&$time-began!=700)$fatal(1,"stream pulse width");
      color={color[22:0],($time-began==700)};
     end
     if(color!==(24'h800100^pixel))$fatal(1,"stream pixel %d color %h",pixel,color);
    end
   end
  join
  $display("color frame complete");idle();put('h504,2);while(!dut.ready)@(negedge clk);put('h50c,24'habcdef);
  wait(dut.underrun);wait(dut.blanked);idle();$display("first underrun clear complete");
  put('h504,2);while(!dut.ready)@(negedge clk);put('h50c,24'hffffff);
  // Abandoning a frame must terminate it and send a complete black frame.
  wait(dut.underrun);wait(!dut.running);
  for(j=0;j<LEDS*24;j=j+1)begin
   @(posedge ws);began=$time;@(negedge ws);
   if($time-began!=350)$fatal(1,"underrun did not clear LEDs");
  end
  $display("CARE_STREAM_PASS: ordered colors, 350/700ns pulse, continuous 1250ns period, underrun abort and full black recovery");$finish;
 end
 initial begin #100000000;$fatal(1,"stream timeout");end
endmodule
