// Open-drain transaction engine. tx includes the write address; reads use a repeated START.
module sensor_i2c #(parameter CLOCK_HZ=100000000, HALF_US=2)(
 input clk,resetn,go,input [39:0] tx,input [2:0] tx_count,
 input [10:0] rx_count,input [7:0] read_address,input scl,sda,
 output reg scl_low,sda_low,busy,done,error,
 output reg rx_valid,output reg [10:0] rx_index,output reg [7:0] rx_data
);
 localparam IDLE=0,START0=1,START1=2,START2=3,LOW=4,HIGH=5,FALL=6,
 ACK0=7,ACK1=8,ACK2=9,STOP0=10,STOP1=11,STOP2=12,REC0=13,REC1=14,REC2=15;
 reg [3:0] state;
 reg [31:0] timer,stretch;
 reg [39:0] payload;
 reg [2:0] left,bitpos;
 reg [10:0] reads,index;
 reg [7:0] shift,read_addr;
 reg reading,restart_read,address_read;
 reg [3:0] recovery;
 wire elapsed=timer==CLOCK_HZ/1000000*HALF_US-1;
 always @(posedge clk) begin
  if(!resetn) begin
   state<=IDLE;scl_low<=0;sda_low<=0;busy<=0;done<=0;error<=0;
   timer<=0;stretch<=0;rx_valid<=0;rx_index<=0;rx_data<=0;
   payload<=0;left<=0;bitpos<=0;reads<=0;index<=0;shift<=0;read_addr<=0;
   reading<=0;restart_read<=0;address_read<=0;recovery<=0;
  end else begin
   done<=0;rx_valid<=0;
   if(state!=IDLE) begin
    // Time high phases from the actual SCL rising edge, including clock stretch.
    if(!scl_low && !scl) begin timer<=0;stretch<=stretch+1;end
    else begin stretch<=0;if(elapsed)timer<=0;else timer<=timer+1;end
    if(!scl_low && !scl && stretch>=CLOCK_HZ/500) begin
     state<=IDLE;scl_low<=0;sda_low<=0;busy<=0;done<=1;error<=1;
    end else if(elapsed && (scl_low || scl)) case(state)
     START0: begin scl_low<=0;sda_low<=0;state<=START1;end
     START1: begin
      if(!sda)begin error<=1;recovery<=0;state<=REC0;end
      else begin sda_low<=1;state<=START2;end
     end
     START2: begin scl_low<=1;bitpos<=7;state<=LOW;end
     LOW: begin sda_low<=reading ? 0 : !shift[bitpos];state<=HIGH;end
     HIGH: begin scl_low<=0;state<=FALL;end
     FALL: begin
      scl_low<=1;
      if(reading)shift<={shift[6:0],sda};
      if(bitpos==0)state<=ACK0;else begin bitpos<=bitpos-1;state<=LOW;end
     end
     ACK0: begin sda_low<=reading && index+1<reads;state<=ACK1;end
     ACK1: begin scl_low<=0;state<=ACK2;end
     ACK2: begin
      scl_low<=1;sda_low<=0;
      if(reading)begin
       rx_valid<=1;rx_index<=index;rx_data<=shift;
       if(index+1==reads)state<=STOP0;
       else begin index<=index+1;bitpos<=7;state<=LOW;end
      end else if(sda)begin error<=1;state<=STOP0;end
      else if(address_read)begin reading<=1;bitpos<=7;state<=LOW;end
      else if(left>1)begin
       payload<=payload>>8;shift<=payload[15:8];left<=left-1;bitpos<=7;state<=LOW;
      end else if(restart_read)begin
       restart_read<=0;address_read<=1;shift<=read_addr;state<=START0;
      end else state<=STOP0;
     end
     STOP0: begin scl_low<=1;sda_low<=1;state<=STOP1;end
     STOP1: begin scl_low<=0;state<=STOP2;end
     STOP2: begin sda_low<=0;busy<=0;done<=1;state<=IDLE;end
     REC0:begin scl_low<=1;sda_low<=0;state<=REC1;end
     REC1:begin scl_low<=0;state<=REC2;end
     REC2:begin
      if(recovery==8)state<=STOP0;else begin recovery<=recovery+1;state<=REC0;end
     end
     default:state<=IDLE;
    endcase
   end else if(go)begin
    busy<=1;error<=0;timer<=0;stretch<=0;index<=0;
    payload<=tx;left<=tx_count;reads<=rx_count;read_addr<=read_address;
    reading<=tx_count==0;restart_read<=rx_count!=0 && tx_count!=0;address_read<=0;
    // A zero tx count is a direct read: still transmit its address first.
    if(tx_count==0)begin reading<=0;address_read<=1;shift<=read_address;end
    else shift<=tx[7:0];
    state<=START0;
   end
  end
 end
endmodule
