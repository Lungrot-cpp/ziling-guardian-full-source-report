`timescale 1ns/1ps
module tb_isp_gamma;
 reg clk=0,resetn=0;always #5 clk=~clk;
 reg [17:0] r=0,g=0,b=0;
 wire [15:0] ar,ag,ab,br,bg,bb;
 wire av,bv;integer i;
 gamma_lut_v1 dut(.i_pclk(clk),.i_arstn(resetn),.i_vs(1'b0),.i_hs(1'b0),.i_de(1'b1),.i_valid(1'b1),.i_r(r),.i_g(g),.i_b(b),.o_r(ar),.o_g(ag),.o_b(ab),.o_valid(av));
 gamma_reference ref_lut(.i_pclk(clk),.i_arstn(resetn),.i_vs(1'b0),.i_hs(1'b0),.i_de(1'b1),.i_valid(1'b1),.i_r(r),.i_g(g),.i_b(b),.o_r(br),.o_g(bg),.o_b(bb),.o_valid(bv));
 initial begin
  repeat(3)@(negedge clk);resetn=1;
  for(i=0;i<512;i=i+1)begin
   r={9'(511-i),9'(i)};g={9'((i+77)%512),9'((i+211)%512)};b={9'((i+333)%512),9'((i+29)%512)};
   @(negedge clk);
   if({ar,ag,ab,av}!=={br,bg,bb,bv})$fatal(1,"GAMMA_MISMATCH %0d",i);
  end
  $display("ISP_GAMMA_PASS 512 indices on all six channels, identical cycle latency");$finish;
 end
endmodule
