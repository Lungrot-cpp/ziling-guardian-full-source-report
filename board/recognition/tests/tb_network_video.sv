`timescale 1ns/1ps
module tb_network_video #(parameter WIDTH=24,HEIGHT=12,FULL=0);
 reg pclk=0,vclk=0,txclk=0;always #5 pclk=~pclk;always #4 vclk=~vclk;always #4 txclk=~txclk;
 reg resetn=0,psel=0,penable=0,pwrite=0;reg[15:0] paddr=0;reg[31:0] pwdata=0;wire[31:0] prdata;
 reg[7:0] rxdata=0;reg rxdv=0;reg vs=0,de=0,low_power=0;reg[23:0] rgb=0;reg[31:0] id=0,stamp=0;
 wire[7:0] cpu_data,video_data;wire cpu_en,video_en,cpu_busy,cpu_pending,video_busy;
 wire[7:0] data=video_busy?video_data:cpu_data;wire en=video_busy?video_en:cpu_en;
 network_packet cpu(.pclk(pclk),.presetn(resetn),.paddr(paddr),.psel(psel),.penable(penable),.pwrite(pwrite),.pwdata(pwdata),.prdata(prdata),
 .rxclk(txclk),.rxresetn(resetn),.rxdata(rxdata),.rxdv(rxdv),.rxerror(1'b0),
 .txclk(txclk),.txresetn(resetn),.tx_allow(!video_busy),.tx_busy(cpu_busy),.tx_pending(cpu_pending),.txdata(cpu_data),.txen(cpu_en));
 video_udp #(.WIDTH(WIDTH),.HEIGHT(HEIGHT)) video(.vclk(vclk),.vresetn(resetn),.vs(vs),.de(de),.rgb(rgb),.video_id(id),.video_time(stamp),.low_power(low_power),
 .txclk(txclk),.txresetn(resetn),.tx_allow(!cpu_busy&&!cpu_pending),.tx_busy(video_busy),.txdata(video_data),.txen(video_en));
 integer file;reg previous=0;
 always @(negedge txclk)begin
  if(cpu_busy&&video_busy)$fatal(1,"packet arbitration overlap");
  if(en)begin if(!previous)$fwrite(file,"PACKET ");$fwrite(file,"%02x",data);end
  else if(previous)$fwrite(file,"\n");
  previous=en;
 end
 task write(input[15:0] address,input[31:0] value);begin
  @(negedge pclk);paddr=address;pwdata=value;psel=1;penable=1;pwrite=1;
  @(negedge pclk);psel=0;penable=0;pwrite=0;
 end endtask
 function[31:0] crc_byte(input[31:0] c,input[7:0] b);reg[31:0] v;integer i;begin
  v=c^b;for(i=0;i<8;i=i+1)v=(v>>1)^(v[0]?32'hedb88320:0);crc_byte=v;
 end endfunction
 task incoming(input bad);integer i;reg[31:0] crc;begin
  for(i=0;i<8;i=i+1)begin @(negedge txclk);rxdv=1;rxdata=i==7?8'hd5:8'h55;end
  crc=32'hffffffff;
  for(i=0;i<100;i=i+1)begin @(negedge txclk);rxdata=i;crc=crc_byte(crc,rxdata);end
  crc=~crc;if(bad)crc=crc^1;
  for(i=0;i<4;i=i+1)begin @(negedge txclk);rxdata=crc[7:0];crc=crc>>8;end
  @(negedge txclk);rxdv=0;repeat(20)@(negedge txclk);
 end endtask
 task frame(input integer n,input[31:0] time_us);integer x,y;begin
  @(negedge vclk);vs=1;id=n;stamp=time_us;
  repeat(4)@(negedge vclk);vs=0;
  for(y=0;y<HEIGHT;y=y+1)begin
   repeat(12)@(negedge vclk);
   for(x=0;x<WIDTH;x=x+1)begin @(negedge vclk);de=1;rgb={8'(n),8'(y),8'(x)};end
   @(negedge vclk);de=0;
  end
  repeat(300)@(negedge vclk);
 end endtask
 integer i;
 initial begin
  file=$fopen("../validation/network-video-packets.txt","w");
  repeat(8)@(negedge pclk);resetn=1;
  for(i=0;i<25;i=i+1)write(16'h0800+i*4,{8'(i*4+3),8'(i*4+2),8'(i*4+1),8'(i*4)});
  fork
   begin frame(1,1000);if(!FULL)begin frame(2,80000);frame(3,159000);frame(4,238000);frame(5,317000);frame(6,396000);frame(7,475000);frame(8,554000);frame(9,633000);frame(10,712000);end end
   begin repeat((WIDTH+12)*3-33)@(negedge txclk);write(20,100);end
   begin incoming(0);incoming(0);incoming(0);end
  join
  if(!FULL)begin
   low_power=1;frame(11,812000);frame(12,912000);frame(13,1012000);frame(14,1112000);frame(15,1212000);
   frame(16,1312000);frame(17,1412000);frame(18,1512000);frame(19,1612000);frame(20,1712000);
   low_power=0;frame(21,1812000);frame(22,1912000);
  end
  if(cpu.ready==0 || cpu.dropped!=1)$fatal(1,"two-slot RX fullness/drop");
  write(8,0);write(16,1);repeat(10)@(negedge txclk);incoming(1);
  if(cpu.bad!=1)$fatal(1,"RX CRC rejection");
  $fclose(file);$display("NETWORK_VIDEO_RTL_PASS");$finish;
 end
 initial begin #1000000000;$fatal(1,"timeout");end
endmodule
