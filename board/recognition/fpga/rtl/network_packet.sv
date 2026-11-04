// Single-line packet mailbox MAC. CPU runs ARP/IP/TCP/HTTP; all frames carry Ethernet CRC.
module network_packet(
 input pclk,presetn,input [15:0] paddr,input psel,penable,pwrite,input [31:0] pwdata,output reg [31:0] prdata,
 input rxclk,rxresetn,input [7:0] rxdata,input rxdv,rxerror,
 input txclk,txresetn,tx_allow,output tx_busy,tx_pending,output reg [7:0] txdata,output reg txen
);
 function automatic [31:0] crc_byte(input [31:0] c,input [7:0] b);
   reg [31:0] v;integer i;
   begin v=c^{24'b0,b};for(i=0;i<8;i=i+1)v=(v>>1)^(v[0]?32'hedb88320:0);crc_byte=v;end
 endfunction
 reg [7:0] rx0[0:1023],rx1[0:1023],rx2[0:1023],rx3[0:1023];
 reg [7:0] tx0[0:511],tx1[0:511],tx2[0:511],tx3[0:511];
 // Two receive slots release four BRAMs for the independent video line buffers.
 reg [1:0] ready,ack,ack0,ack1,ready0,ready1;
 reg [10:0] lengths[0:1];reg wslot,rslot;
 reg [10:0] rcount;reg [31:0] rcrc,bad,dropped;reg [3:0] preamble;
 reg receiving,discard,error_seen;reg dv_d;
 wire [9:0] rword={wslot,rcount[10:2]};
 always @(posedge rxclk)if(receiving && rxdv && !discard && rcount<1518)case(rcount[1:0])
  0:rx0[rword]<=rxdata;1:rx1[rword]<=rxdata;2:rx2[rword]<=rxdata;3:rx3[rword]<=rxdata;
 endcase
 always @(posedge rxclk or negedge rxresetn)begin
  if(!rxresetn)begin ready<=0;ack0<=0;ack1<=0;wslot<=0;rcount<=0;rcrc<=32'hffffffff;preamble<=0;receiving<=0;discard<=0;error_seen<=0;dv_d<=0;bad<=0;dropped<=0;end
  else begin
   ack0<=ack;ack1<=ack0;dv_d<=rxdv;
   if(rxdv)begin
    if(!receiving)begin
     if(rxdata==8'h55)begin if(preamble<7)preamble<=preamble+1'b1;end
     else if(rxdata==8'hd5 && preamble>=7)begin receiving<=1;rcount<=0;rcrc<=32'hffffffff;discard<=ready[wslot]!=ack1[wslot];error_seen<=rxerror;end
     else preamble<=0;
    end else begin
     rcrc<=crc_byte(rcrc,rxdata);error_seen<=error_seen|rxerror;
     if(rcount<2047)rcount<=rcount+1'b1;else discard<=1;
    end
   end else begin
    preamble<=0;receiving<=0;
    if(dv_d && receiving)begin
     if(discard)dropped<=dropped+1'b1;
     else if(rcount>=64 && rcount<=1518 && !error_seen && rcrc==32'hdebb20e3)begin
      lengths[wslot]<=rcount-4;ready[wslot]<=~ready[wslot];wslot<=wslot+1'b1;
     end else bad<=bad+1'b1;
    end
   end
  end
 end
 reg request,request0,request1,done,done0,done1;
 reg [10:0] tlength;reg [10:0] index;reg [2:0] state;
 assign tx_busy=state!=0;
 assign tx_pending=request1!=done;
 reg [31:0] tcrc,fcs;reg [7:0] t0,t1,t2,t3;reg [31:0] rxword;
 wire [10:0] next_index=index+1'b1;
 wire [8:0] taddress=state==2 ? next_index[10:2] : 9'b0;
 always @(posedge txclk)begin t0<=tx0[taddress];t1<=tx1[taddress];t2<=tx2[taddress];t3<=tx3[taddress];end
 reg [7:0] data_byte;
 always @*case(index[1:0])0:data_byte=t0;1:data_byte=t1;2:data_byte=t2;default:data_byte=t3;endcase
 wire [7:0] padded_byte=index<tlength?data_byte:8'b0;
 always @(posedge txclk or negedge txresetn)begin
  if(!txresetn)begin request0<=0;request1<=0;done<=0;state<=0;index<=0;txen<=0;txdata<=0;tcrc<=32'hffffffff;fcs<=0;end
  else begin
   request0<=request;request1<=request0;
   case(state)
    0:begin txen<=0;index<=0;tcrc<=32'hffffffff;if(request1!=done && tx_allow)state<=1;end
    1:begin txen<=1;txdata<=index==7?8'hd5:8'h55;if(index==7)begin index<=0;state<=2;end else index<=index+1'b1;end
    2:begin txen<=1;txdata<=padded_byte;tcrc<=crc_byte(tcrc,padded_byte);
      if(index+1>=tlength && index>=59)begin fcs<=~crc_byte(tcrc,padded_byte);index<=0;state<=3;end else index<=index+1'b1;end
    3:begin txen<=1;txdata<=fcs[7:0];fcs<=fcs>>8;if(index==3)begin index<=0;state<=4;end else index<=index+1'b1;end
    4:begin txen<=0;if(index==11)begin done<=request1;state<=0;end else index<=index+1'b1;end
    default:begin state<=0;txen<=0;end
   endcase
  end
 end
 always @(posedge pclk)begin
  rxword<={rx3[{rslot,paddr[10:2]}],rx2[{rslot,paddr[10:2]}],rx1[{rslot,paddr[10:2]}],rx0[{rslot,paddr[10:2]}]};
  if(psel && penable && pwrite && paddr[12:11]==1 && request==done1)begin
   tx0[paddr[10:2]]<=pwdata[7:0];tx1[paddr[10:2]]<=pwdata[15:8];tx2[paddr[10:2]]<=pwdata[23:16];tx3[paddr[10:2]]<=pwdata[31:24];
  end
 end
 always @(posedge pclk or negedge presetn)begin
  if(!presetn)begin ready0<=0;ready1<=0;done0<=0;done1<=0;ack<=0;rslot<=0;request<=0;tlength<=0;end
  else begin
   ready0<=ready;ready1<=ready0;done0<=done;done1<=done0;
   if(psel && penable && pwrite)case(paddr[12:0])
    8:rslot<=pwdata[0];16:ack[rslot]<=ready1[rslot];
    20:if(request==done1 && pwdata>=14 && pwdata<=1514)begin tlength<=pwdata[10:0];request<=~request;end
   endcase
  end
 end
 always @*begin
  prdata=0;
  if(paddr[12:11]==2)prdata=rxword;
  else case(paddr[12:0])
   0:prdata=32'h4e494331;
   4:prdata={26'b0,(ready1^ack),3'b0,request!=done1};
   8:prdata={31'b0,rslot};12:prdata={21'b0,lengths[rslot]};24:prdata=bad;28:prdata=dropped;
  endcase
 end
endmodule
