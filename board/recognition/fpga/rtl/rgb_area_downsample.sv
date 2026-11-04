// Exact 1920x1080 -> 96x96 area average, with quarter-row weights.
module rgb_area_downsample(
 input clk,resetn,active,vs,de,input [10:0] y,
 input [7:0] r,g,b,
 output reg valid,output reg [13:0] index,output reg [23:0] rgb
);
 reg vs_prev,de_prev;
 reg [4:0] pixel;
 reg [6:0] column;
 reg [5:0] remaining;
 reg [12:0] sum_r,sum_g,sum_b,row_r,row_g,row_b;
 reg [2:0] weight;
 reg first_row,emit_row;
 reg [3:0] state;
 reg [8:0] address;
 (* ram_style="block_ram" *) reg [17:0] vertical[0:287];
 reg [17:0] previous;
 reg [35:0] product;
 reg [7:0] red,green;
 reg [13:0] count;
 function [17:0] weighted(input [12:0] value,input [2:0] quarters);
  case(quarters)
   0:weighted=0;
   1:weighted={5'b0,value};
   2:weighted={4'b0,value,1'b0};
   3:weighted={4'b0,value,1'b0}+{5'b0,value};
   default:weighted={3'b0,value,2'b0};
  endcase
 endfunction
 wire [12:0] row_value=state==2?row_r:state==4?row_g:row_b;
 wire [17:0] total=(first_row?18'd0:previous)+weighted(row_value,weight);
 always @(posedge clk)if(resetn)begin
  if(state==1 || state==3 || state==5)previous<=vertical[address];
  if(state==2 || state==4 || state==6)vertical[address]<=emit_row?weighted(row_value,3'd4-weight):total;
 end
 always @(posedge clk or negedge resetn)begin
  if(!resetn)begin
   vs_prev<=0;de_prev<=0;pixel<=0;column<=0;remaining<=45;
   sum_r<=0;sum_g<=0;sum_b<=0;row_r<=0;row_g<=0;row_b<=0;
   weight<=0;first_row<=0;emit_row<=0;state<=0;address<=0;
   product<=0;red<=0;green<=0;count<=0;valid<=0;index<=0;rgb<=0;
  end else begin
   vs_prev<=vs;de_prev<=de;valid<=0;
   if(vs && !vs_prev)begin
    pixel<=0;column<=0;remaining<=45;count<=0;state<=0;
    sum_r<=0;sum_g<=0;sum_b<=0;
   end else begin
    if(!de)begin pixel<=0;column<=0;sum_r<=0;sum_g<=0;sum_b<=0;end
    if(active && !de && de_prev)remaining<=remaining<=4?remaining+41:remaining-4;
    if(active && de)begin
     if(pixel==19)begin
      row_r<=sum_r+r;row_g<=sum_g+g;row_b<=sum_b+b;
      pixel<=0;column<=column+1'b1;sum_r<=0;sum_g<=0;sum_b<=0;
      weight<=remaining<=4?remaining[2:0]:3'd4;
      emit_row<=remaining<=4;first_row<=y==0;address<=column*3;state<=1;
     end else begin pixel<=pixel+1'b1;sum_r<=sum_r+r;sum_g<=sum_g+g;sum_b<=sum_b+b;end
    end
    case(state)
     1,3,5:begin
      if(state==3)red<=product[34:27];
      if(state==5)green<=product[34:27];
      state<=state+1'b1;
     end
     2,4,6:begin
      // ceil(2^27/900) gives exact rounded /900 over the complete input range.
      product<=(total+18'd450)*18'd149131;
      address<=address+1'b1;state<=state+1'b1;
     end
     7:begin
      if(emit_row)begin rgb<={red,green,product[34:27]};index<=count;count<=count+1'b1;valid<=1;end
      state<=0;
     end
    endcase
   end
  end
 end
endmodule
