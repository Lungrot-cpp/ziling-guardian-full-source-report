`timescale 1ns/1ps
module tb_power_display;
 reg pclk=0,vclk=0;always #7 pclk=~pclk;always #5 vclk=~vclk;
 reg resetn=0,vs=0,hs=0,de=0,psel=0,penable=0,pwrite=0;
 reg [15:0] addr=0;reg [31:0] data=0;wire [31:0] readback;
 wire [7:0] r,g,b;wire od,oh,ov,low_power;
 person_video_bridge dut(.pclk(pclk),.presetn(resetn),.paddr(addr),.psel(psel),.penable(penable),.pwrite(pwrite),.pwdata(data),.prdata(readback),.pready(),.pslverror(),
 .video_time(32'd1),.video_id(32'd1),.preview_bank_ids(96'd0),.preview_write_bank(2'd0),
 .vclk(vclk),.vresetn(resetn),.vs(vs),.hs(hs),.de(de),.r(8'd240),.g(8'd128),.b(8'd64),
 .out_r(r),.out_g(g),.out_b(b),.out_de(od),.out_hs(oh),.out_vs(ov),.low_power(low_power));
 task request(input bit value);begin
  @(negedge pclk);addr=16'h003c;data=value;psel=1;penable=1;pwrite=1;
  @(negedge pclk);psel=0;penable=0;pwrite=0;
  #1;if(readback!=={31'b0,value})$fatal(1,"APB readback");
  repeat(5)@(negedge vclk);
 end endtask
 task boundary;begin
  @(negedge vclk);de=0;vs=1;repeat(4)@(negedge vclk);vs=0;repeat(4)@(negedge vclk);
 end endtask
 task pixels(input bit dimmed);integer i;begin
  for(i=0;i<8;i=i+1)begin
   @(negedge vclk);de=i[0];hs=i[1];
   @(posedge vclk);#1;
   if({od,oh,ov}!=={de,hs,vs})$fatal(1,"HDMI timing changed");
   if({r,g,b}!==(dimmed?24'h3c2010:24'hf08040))$fatal(1,"HDMI brightness %h",{r,g,b});
   if({dut.area.r,dut.area.g,dut.area.b}!==24'hf08040)$fatal(1,"detector input dimmed");
  end
 end endtask
 initial begin
  repeat(8)@(negedge pclk);resetn=1;boundary();pixels(0);
  request(1);if(low_power!==0)$fatal(1,"mid-frame dimming");pixels(0);
  boundary();if(low_power!==1)$fatal(1,"watch not latched");pixels(1);
  request(0);pixels(1);boundary();pixels(0);
  request(1);boundary();pixels(1);
  resetn=0;repeat(4)@(negedge vclk);resetn=1;boundary();pixels(0);
  $display("POWER_DISPLAY_RTL_PASS: APB, frame boundary, quarter brightness, wake/reset restore, unchanged HDMI timing and raw detector input");$finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
