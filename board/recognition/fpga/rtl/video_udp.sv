// Fixed local peer: 192.168.0.3 / B0:25:AA:6C:40:DF. Rebuild when the receiving NIC changes.
module video_udp #(
 parameter WIDTH=1920, HEIGHT=1080
)(
 input vclk, vresetn, vs, de,
 input [23:0] rgb,
 input [31:0] video_id, video_time,input low_power,
 input txclk, txresetn,tx_allow,
 output tx_busy,
 output reg [7:0] txdata,
 output reg txen
);
 localparam OW=WIDTH/6, OH=HEIGHT/6;
 localparam PAYLOAD=20+OW*3, UDP_LEN=8+PAYLOAD, IP_LEN=20+UDP_LEN, BODY=14+IP_LEN;
 function automatic [15:0] checksum(input [31:0] sum);
   reg [16:0] folded;
   begin folded=sum[15:0]+sum[31:16];checksum=~(folded[15:0]+folded[16]);end
 endfunction
 localparam [15:0] IP_CHECK=checksum(32'h4500+IP_LEN+32'h4000+32'h4011+32'hc0a8+2+32'hc0a8+3);
 localparam [335:0] HEADER={48'hb025aa6c40df,48'h000a3501fec0,16'h0800,
   8'h45,8'h00,16'(IP_LEN),16'h0000,16'h4000,8'h40,8'h11,IP_CHECK,
   32'hc0a80002,32'hc0a80003,16'd8090,16'd8090,16'(UDP_LEN),16'h0000};
 reg [23:0] pixels[0:1023];
 reg [31:0] ids[0:1], times[0:1];
 reg [15:0] rows[0:1];
 reg [1:0] ready, ack, ack0, ack1, ready0, ready1;
 reg vs_d,de_d,write_bank,line_ok,active;
 reg [10:0] x,y;
 reg [2:0] xp,yp;
 reg [8:0] col;
 reg [31:0] last_id,last_time,frame_id,frame_time;
 wire [31:0] frame_period=low_power?32'd500000:32'd100000;
 always @(posedge vclk or negedge vresetn) begin
   if(!vresetn) begin
     ready<=0;ack0<=0;ack1<=0;vs_d<=0;de_d<=0;write_bank<=0;line_ok<=0;
     x<=0;y<=0;xp<=0;yp<=0;col<=0;active<=0;last_id<=0;last_time<=0;frame_id<=0;frame_time<=0;
   end else begin
     ack0<=ack;ack1<=ack0;vs_d<=vs;de_d<=de;
     if(vs && !vs_d) begin x<=0;y<=0;xp<=0;yp<=0;col<=0;active<=0;end
     if(de && !de_d) begin
       line_ok<=ready[write_bank]==ack1[write_bank];
       if(y==0) begin
         frame_id<=video_id;frame_time<=video_time;
         active<=video_id!=0 && video_id!=last_id && (last_id==0 || (video_time-last_time)>=frame_period);
         if(video_id!=0 && video_id!=last_id && (last_id==0 || (video_time-last_time)>=frame_period))begin
           last_id<=video_id;
           last_time<=(last_id==0 || (video_time-last_time)>=frame_period*2)?video_time:last_time+frame_period;
         end
       end
     end
     if(de) begin
       x<=x+1'b1;xp<=xp==5?0:xp+1'b1;
       if(active && line_ok && yp==3 && xp==3 && y<HEIGHT && x<WIDTH) begin
         pixels[{write_bank,col}]<=rgb;
         if(col==OW-1) begin
           ids[write_bank]<=frame_id;times[write_bank]<=frame_time;rows[write_bank]<=y/6;
           ready[write_bank]<=~ready[write_bank];write_bank<=~write_bank;col<=0;
         end else col<=col+1'b1;
       end
     end
     if(!de && de_d) begin x<=0;xp<=0;col<=0;y<=y+1'b1;yp<=yp==5?0:yp+1'b1;end
   end
 end
 function automatic [31:0] crc_byte(input [31:0] crc,input [7:0] value);
   reg [31:0] c;integer i;
   begin c=crc^{24'b0,value};for(i=0;i<8;i=i+1)c=(c>>1)^((c[0])?32'hedb88320:0);crc_byte=c;end
 endfunction
 localparam IDLE=0,PREAMBLE=1,DATA=2,FCS=3,GAP=4;
 reg [2:0] state;
 assign tx_busy=state!=IDLE;
 reg [10:0] index;
 reg read_bank;
 reg [31:0] send_id,send_time,crc,fcs;
 reg [15:0] send_row;
 reg [8:0] read_col;
 reg [1:0] channel;
 reg [23:0] pixel_q;
 wire [8:0] ahead_col=(state==DATA && index>=62 && channel==2)?read_col+1'b1:read_col;
 always @(posedge txclk) pixel_q<=pixels[{read_bank,ahead_col}];
 reg [7:0] next_byte;
 always @* begin
   next_byte=0;
   if(index<42) next_byte=HEADER[(41-index)*8+:8];
   else case(index)
     42:next_byte="V";43:next_byte="6";44:next_byte="V";45:next_byte="1";
     46:next_byte=send_id[31:24];47:next_byte=send_id[23:16];48:next_byte=send_id[15:8];49:next_byte=send_id[7:0];
     50:next_byte=send_time[31:24];51:next_byte=send_time[23:16];52:next_byte=send_time[15:8];53:next_byte=send_time[7:0];
     54:next_byte=OW>>8;55:next_byte=OW;56:next_byte=OH>>8;57:next_byte=OH;
     58:next_byte=send_row[15:8];59:next_byte=send_row[7:0];60:next_byte=0;61:next_byte=2; // RGB888
     default:case(channel)0:next_byte=pixel_q[23:16];1:next_byte=pixel_q[15:8];2:next_byte=pixel_q[7:0];default:next_byte=0;endcase
   endcase
 end
 always @(posedge txclk or negedge txresetn) begin
   if(!txresetn) begin
     state<=IDLE;index<=0;read_bank<=0;ack<=0;ready0<=0;ready1<=0;txen<=0;txdata<=0;
     crc<=32'hffffffff;fcs<=0;send_id<=0;send_time<=0;send_row<=0;read_col<=0;channel<=0;
   end else begin
     ready0<=ready;ready1<=ready0;
     case(state)
       IDLE:begin
         txen<=0;index<=0;crc<=32'hffffffff;read_col<=0;channel<=0;
         if(tx_allow && ready1[read_bank]!=ack[read_bank]) begin
           send_id<=ids[read_bank];send_time<=times[read_bank];send_row<=rows[read_bank];state<=PREAMBLE;
         end else read_bank<=~read_bank;
       end
       PREAMBLE:begin
         txen<=1;txdata<=index==7?8'hd5:8'h55;
         if(index==7)begin index<=0;state<=DATA;end else index<=index+1'b1;
       end
       DATA:begin
         txen<=1;txdata<=next_byte;crc<=crc_byte(crc,next_byte);
         if(index>=62)begin
           if(channel==2)begin channel<=0;read_col<=read_col+1'b1;end else channel<=channel+1'b1;
         end
         if(index==BODY-1)begin fcs<=~crc_byte(crc,next_byte);index<=0;state<=FCS;end else index<=index+1'b1;
       end
       FCS:begin
         txen<=1;txdata<=fcs[7:0];fcs<=fcs>>8;
         if(index==3)begin ack[read_bank]<=ready1[read_bank];read_bank<=~read_bank;index<=0;state<=GAP;end else index<=index+1'b1;
       end
       GAP:begin txen<=0;if(index==11)state<=IDLE;else index<=index+1'b1;end
       default:begin state<=IDLE;txen<=0;end
     endcase
   end
 end
endmodule
