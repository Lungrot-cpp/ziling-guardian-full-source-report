// Independent SHT and MLX transactions continue while Sapphire runs inference.
module sensor_acquire #(parameter CLOCK_HZ=100000000)(
 input clk,resetn,input [31:0] usec,input enable,
 input [1:0] scl,sda,output [1:0] scl_low,sda_low,
 input [10:0] read_index,output reg [15:0] read_data,
 output reg [31:0] seq0,seq1,stamp0,stamp1,errors,
 output reg [15:0] control0,control1,
 output reg [31:0] env_seq,env_stamp,output reg [47:0] env_data
);
 reg mgo,sgo;
 reg [39:0] mtx,stx;
 reg [2:0] mlen,slen;
 reg [10:0] mread,sread;
 wire mbusy,mdone,merror,mvalid,sbusy,sdone,serror,svalid;
 wire [10:0] mi,si;
 wire [7:0] md,sd;
 sensor_i2c #(.CLOCK_HZ(CLOCK_HZ),.HALF_US(2)) mlx(
  .clk(clk),.resetn(resetn),.go(mgo),.tx(mtx),.tx_count(mlen),.rx_count(mread),
  .read_address(8'h67),.scl(scl[1]),.sda(sda[1]),.scl_low(scl_low[1]),.sda_low(sda_low[1]),
  .busy(mbusy),.done(mdone),.error(merror),.rx_valid(mvalid),.rx_index(mi),.rx_data(md));
 sensor_i2c #(.CLOCK_HZ(CLOCK_HZ),.HALF_US(5)) sht(
  .clk(clk),.resetn(resetn),.go(sgo),.tx(stx),.tx_count(slen),.rx_count(sread),
  .read_address(8'h89),.scl(scl[0]),.sda(sda[0]),.scl_low(scl_low[0]),.sda_low(sda_low[0]),
  .busy(sbusy),.done(sdone),.error(serror),.rx_valid(svalid),.rx_index(si),.rx_data(sd));
 (* ram_style="block_ram" *) reg [15:0] raw [0:1663];
 reg [7:0] hi;
 reg page;
 reg [3:0] ms;
 reg [2:0] ss;
 reg [15:0] value;
 reg [31:0] mpoll,spoll,measure_stamp,burst_stamp;
 reg [47:0] env_work;
 reg enabled;
 wire [10:0] raw_index=(page ? 11'd832:11'd0)+mi[10:1];
 always @(posedge clk) begin
  read_data<=raw[read_index];
  if(mvalid && ms==6)begin
   if(!mi[0])hi<=md;else raw[raw_index]<={hi,md};
  end
 end
 task read_mlx(input [15:0] address,input [10:0] count);
  begin mtx<={16'b0,address[7:0],address[15:8],8'h66};mlen<=3;mread<=count;mgo<=1;end
 endtask
 always @(posedge clk) begin
  if(!resetn)begin
   mgo<=0;sgo<=0;mtx<=0;stx<=0;mlen<=0;slen<=0;mread<=0;sread<=0;
   ms<=0;ss<=0;page<=0;value<=0;mpoll<=0;spoll<=0;measure_stamp<=0;burst_stamp<=0;
   seq0<=0;seq1<=0;stamp0<=0;stamp1<=0;control0<=0;control1<=0;errors<=0;
   env_seq<=0;env_stamp<=0;env_data<=0;env_work<=0;enabled<=0;
  end else begin
   mgo<=0;sgo<=0;enabled<=enable;
   if(mvalid && ms!=6)value<={value[7:0],md};
   if(svalid)env_work<={env_work[39:0],sd};
   // CPU only disables acquisition between transactions; disabling mid-burst invalidates it.
   if(!enable)begin
    if(!mbusy)ms<=0;
    if(!sbusy)ss<=0;
    if(seq0[0] && !mbusy)seq0<=seq0+1;
    if(seq1[0] && !mbusy)seq1<=seq1+1;
   end else begin
    case(ms)
     0:if(!mbusy && (!enabled || usec-mpoll>=50000))begin
      mpoll<=usec;read_mlx(16'h8000,2);ms<=1;
     end
     1:if(mdone)begin
      if(merror)begin errors<=errors+1;ms<=0;end
      else if(value[3])begin page<=value[0];burst_stamp<=usec;ms<=2;end
      else ms<=0;
     end
     2:begin // Clear ready before reading RAM.
      mtx<=40'h3000008066;mlen<=5;mread<=0;mgo<=1;ms<=3;
     end
     3:if(mdone)begin
      if(merror)begin errors<=errors+1;ms<=0;end else ms<=4;
     end
     4:begin
      if(page)begin seq1<=seq1+1;stamp1<=0;end else begin seq0<=seq0+1;stamp0<=0;end
      read_mlx(16'h0400,1664);ms<=6;
     end
     6:if(mdone)begin
      if(merror)ms<=10;else begin read_mlx(16'h8000,2);ms<=7;end
     end
     7:if(mdone)begin
      if(merror || value[3] || value[0]!=page)ms<=10;
      else begin read_mlx(16'h800d,2);ms<=8;end
     end
     8:if(mdone)begin
      if(!merror && value[12])begin
       if(page)begin control1<=value;stamp1<=burst_stamp;seq1<=seq1+1;end
       else begin control0<=value;stamp0<=burst_stamp;seq0<=seq0+1;end
       ms<=0;
      end else ms<=10;
     end
     10:begin
      errors<=errors+1;
      if(page)seq1<=seq1+1;else seq0<=seq0+1;
      ms<=0;
     end
     default:ms<=0;
    endcase
    case(ss)
     0:if(!sbusy && (!enabled || usec-spoll>=250000))begin
      spoll<=usec;stx<=40'h0000002488;slen<=3;sread<=0;sgo<=1;ss<=1;
     end
     1:if(sdone)begin
      if(serror)ss<=0;else begin measure_stamp<=usec;ss<=2;end
     end
     2:if(usec-measure_stamp>=16000)begin slen<=0;sread<=6;sgo<=1;ss<=3;end
     3:if(sdone)begin
      if(!serror)begin env_data<=env_work;env_stamp<=measure_stamp;env_seq<=env_seq+1;end
      ss<=0;
     end
     default:ss<=0;
    endcase
   end
  end
 end
endmodule
