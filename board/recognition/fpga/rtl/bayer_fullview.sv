`timescale 1ns/1ps
// Keep three of four Bayer column pairs and three of four row pairs.
// shortcut: pair decimation can alias fine patterns; use filtered RGB scaling if needed.
module bayer_fullview(
 input clk,resetn,vs,de,input [31:0] data,
 output reg out_vs,out_de,output reg [31:0] out_data
);
 reg vs_prev;
 reg [9:0] word_x;
 reg [2:0] row_phase;
 reg [15:0] carry;
 always @(posedge clk or negedge resetn) begin
   if(!resetn) begin
     vs_prev<=0;word_x<=0;row_phase<=0;carry<=0;
     out_vs<=0;out_de<=0;out_data<=0;
   end else begin
     vs_prev<=vs;out_vs<=vs;out_de<=0;
     if(vs && !vs_prev) begin
       word_x<=0;row_phase<=0;carry<=0;
     end else if(de) begin
       if(word_x==639) begin word_x<=0;row_phase<=row_phase+1'b1;end
       else word_x<=word_x+1'b1;
       if(row_phase<6) begin
         case(word_x[1:0])
           0: begin out_data<=data;out_de<=1;end
           1: carry<=data[15:0];
           2: begin out_data<={data[15:0],carry};carry<=data[31:16];out_de<=1;end
           3: begin out_data<={data[15:0],carry};out_de<=1;end
         endcase
       end
     end
   end
 end
endmodule
