`timescale 1ns/1ps
module tb_video_fifo;
 reg wc=0,rc=0,reset=1;always #5 wc=~wc;always #7 rc=~rc;
 reg we=0,re=0;reg [127:0]data=0;
 wire full,empty,valid;wire[8:0] wn,rn;wire[127:0]out;
 integer written=0,read_count=0,cycles=0;reg take;
 DC_FIFO #(.DATA_WIDTH(128),.FIFO_DEPTH(256)) dut(.Reset(reset),.WrClk(wc),.WrEn(we),.WrDNum(wn),.WrFull(full),.WrData(data),.RdClk(rc),.RdEn(re),.RdDNum(rn),.RdEmpty(empty),.DataVal(valid),.RdData(out));
 initial begin #200;reset=0;end
 always @(negedge wc)begin
  if(!reset && !dut.WrClkRst && written<4096)begin
   we=!full && (cycles%5!=0);data={4{32'(written)}};
  end else we=0;
 end
 always @(posedge wc)begin
  if(we && !full)written=written+1;
  cycles=cycles+1;
  if(cycles>30000)$fatal(1,"FIFO_TIMEOUT written=%0d read=%0d",written,read_count);
 end
 always @(negedge rc)re=!reset && !dut.RdClkRst && !empty && ((cycles%11)>2);
 always @(posedge rc)begin
  take=re && !empty;
  #2;
  if(take)begin
   if(out!=={4{32'(read_count)}})$fatal(1,"FIFO_DATA %0d %h",read_count,out);
   read_count=read_count+1;
   if(read_count==4096)begin $display("VIDEO_FIFO_PASS 4096 words with asynchronous clocks and stalls");$finish;end
  end
 end
endmodule
