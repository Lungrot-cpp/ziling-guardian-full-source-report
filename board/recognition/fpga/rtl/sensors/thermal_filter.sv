// Two accepted frames; invalid samples do not contaminate the next average.
module thermal_filter(input clk,input write,input [9:0] index,input [1:0] warmup,
 input signed [15:0] value,output reg signed [15:0] result);
 (* ram_style="block_ram" *) reg signed [15:0] previous[0:767];
 reg signed [15:0] old,current;
 reg [9:0] addr;reg valid,first;
 always @(posedge clk)begin
  valid<=write;
  if(write)begin old<=previous[index];current<=value;addr<=index;first<=warmup==0;end
  if(valid)begin
   previous[addr]<=current;
   if(current==-32768)result<=-32768;
   else if(first || old==-32768)result<=current;
   else result<=($signed({current[15],current})+$signed({old[15],old}))>>>1;
  end
 end
endmodule
