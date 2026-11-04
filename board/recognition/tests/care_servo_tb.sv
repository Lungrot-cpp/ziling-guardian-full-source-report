`timescale 1ns/1ps
module care_servo_tb;
 // A 1MHz test clock makes each cycle one microsecond; servo timing is
 // unchanged in real time. The separate WS test uses the production 100MHz.
 reg clk=0;always #500 clk=~clk;
 reg resetn=0;reg [15:0] addr=0;reg sel=0,en=0,wr=0;reg [31:0] data=0;wire [31:0] q;wire ws,pwm;
 care_peripherals #(.CLK_HZ(1000000)) dut(clk,resetn,addr,sel,en,wr,data,q,ws,pwm);
 task put(input [15:0] a,input [31:0] d);begin
  @(negedge clk);addr=a;data=d;sel=1;wr=1;en=0;
  @(negedge clk);en=1;@(negedge clk);en=0;sel=0;wr=0;
 end endtask
 task pulse(input integer width);time t;begin
  @(posedge pwm);t=$time;@(negedge pwm);
  if($time-t!=width*1000)begin $display("FAIL servo pulse %d expected %d",$time-t,width*1000);$finish;end
 end endtask
 initial begin
  repeat(4)@(negedge clk);resetn=1;
  #21000000;if(pwm!==0)begin $display("FAIL servo not disabled at boot");$finish;end
  put('h528,1);put('h51c,40);put('h520,2); // Disarmed movement ignored.
  #21000000;if(pwm!==0)begin $display("FAIL disarmed servo moved");$finish;end
  put('h520,1);pulse(1500);
  put('h520,2);pulse(1650);pulse(1650);pulse(1500);
  put('h520,3);pulse(1350);pulse(1350);pulse(1500);
  put('h520,0);#85000000;
  addr='h520;#1000;if(q[0]!==0||pwm!==0)begin $display("FAIL disarm did not finish");$finish;end
  put('h528,1);put('h520,1);#(64'd3050000000);
  pulse(1500);addr='h504;#1000;if(q[3]!==0)begin $display("FAIL heartbeat did not expire");$finish;end
  $display("CARE_SERVO_PASS: boot disabled; arming; forward/reverse widths; hardware timed neutral; delayed disarm; 3s CPU watchdog");$finish;
 end
 initial begin #(64'd4000000000);$display("FAIL servo timeout");$finish;end
endmodule
