`timescale 1ns/1ps
module tb_rgb_area;
 reg clk=0;always #5 clk=~clk;
 reg resetn=0,active=0,vs=0,de=0;
 reg [10:0] y=0;reg [23:0] pixel_rgb=0;
 wire valid;wire[13:0]index;wire[23:0]rgb;
 reg [23:0] stimulus[0:2073599],expected[0:9215];
 integer frame=0,x,row,seen=0,total=0;
 rgb_area_downsample dut(clk,resetn,active,vs,de,y,pixel_rgb[23:16],pixel_rgb[15:8],pixel_rgb[7:0],valid,index,rgb);
 always @(posedge clk)begin
  #1;
  if(valid)begin
   if(index!==seen || rgb!==(frame==0?expected[seen]:24'hffffff))
    $fatal(1,"AREA_MISMATCH frame=%0d index=%0d expected_index=%0d rgb=%h expected=%h",frame,index,seen,rgb,frame==0?expected[seen]:24'hffffff);
   seen=seen+1;total=total+1;
  end
 end
 initial begin
  $readmemh("validation/area_stimulus.hex",stimulus);
  $readmemh("validation/area_expected.hex",expected);
  repeat(4)@(negedge clk);resetn=1;active=1;
  for(frame=0;frame<2;frame=frame+1)begin
   seen=0;vs=1;repeat(4)@(negedge clk);vs=0;repeat(4)@(negedge clk);
   for(row=0;row<1080;row=row+1)begin
    y=row;
    for(x=0;x<1920;x=x+1)begin
     de=1;pixel_rgb=frame==0?stimulus[row*1920+x]:24'hffffff;@(negedge clk);
    end
    de=0;repeat(4+(row%7))@(negedge clk);
   end
   repeat(16)@(negedge clk);
   if(seen!=9216)$fatal(1,"AREA_COUNT %0d",seen);
  end
  active=0;repeat(20)@(negedge clk);
  if(total!=18432)$fatal(1,"AREA_TOTAL %0d",total);
  $display("AREA_PASS pixels=%0d",total);$finish;
 end
endmodule
