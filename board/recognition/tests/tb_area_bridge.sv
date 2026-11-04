`timescale 1ns/1ps
module tb_area_bridge;
 reg vclk=0,pclk=0;always #5 vclk=~vclk;always #7 pclk=~pclk;
 reg resetn=0,vs=0,de=0,psel=0,penable=0,pwrite=0;
 reg [15:0]paddr=0;reg[31:0]pwdata=0;
 wire[31:0]prdata;wire pready,pslverror;
 reg[31:0]video_id=100;reg[23:0]value=0;
 reg[23:0]stimulus[0:2073599],expected[0:9215];
 wire[7:0]r,g,b;wire od,oh,ov;
 integer f,x,y,i;reg[31:0]answer;
 person_video_bridge dut(.pclk(pclk),.presetn(resetn),.paddr(paddr),.psel(psel),.penable(penable),.pwrite(pwrite),.pwdata(pwdata),.prdata(prdata),.pready(pready),.pslverror(pslverror),.video_time(32'd1000000),.video_id(video_id),.preview_bank_ids(96'd0),.preview_write_bank(2'd0),.vclk(vclk),.vresetn(resetn),.vs(vs),.hs(de),.de(de),.r(value[23:16]),.g(value[15:8]),.b(value[7:0]),.out_r(r),.out_g(g),.out_b(b),.out_de(od),.out_hs(oh),.out_vs(ov));
 task write_request;
  begin
   @(negedge pclk);paddr=4;pwdata=1;psel=1;penable=1;pwrite=1;
   @(negedge pclk);psel=0;penable=0;pwrite=0;
   repeat(8)@(negedge vclk);
  end
 endtask
 task read_word(input[15:0]address,output[31:0]data);
  begin
   @(negedge pclk);paddr=address;psel=1;
   repeat(2)@(negedge pclk);data=prdata;psel=0;
  end
 endtask
 initial begin
  $readmemh("validation/area_stimulus.hex",stimulus);$readmemh("validation/area_expected.hex",expected);
  repeat(8)@(negedge pclk);resetn=1;repeat(20)@(negedge pclk);
  for(f=0;f<2;f=f+1)begin
   write_request();vs=1;repeat(4)@(negedge vclk);vs=0;repeat(4)@(negedge vclk);
   for(y=0;y<1080;y=y+1)begin
    if(f==1 && y==500)video_id=101;
    for(x=0;x<1920;x=x+1)begin de=1;value=stimulus[y*1920+x];@(negedge vclk);end
    de=0;repeat(12)@(negedge vclk);
   end
   repeat(20)@(negedge pclk);
   read_word(8,answer);if(!(answer&1))$fatal(1,"BRIDGE_NOT_DONE");
   read_word(28,answer);if(answer!==(f==0?32'd1:32'd0))$fatal(1,"BRIDGE_VALID %0d %h",f,answer);
   for(i=0;i<9216;i=i+1)begin
    read_word(16'h1000+i*4,answer);
    if(answer!=={8'b0,expected[i]})$fatal(1,"BRIDGE_PIXEL %0d %h %h",i,answer,expected[i]);
   end
  end
  $display("AREA_BRIDGE_PASS valid_frame_and_changed_tag_rejection; APB_pixels=18432");$finish;
 end
endmodule
