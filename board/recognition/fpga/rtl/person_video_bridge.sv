// One immutable 96x96 snapshot per CPU request. Pixel RGB matches V6 HDMI.
`timescale 1ns/1ps
module person_video_bridge(
 input pclk,presetn,input [15:0] paddr,input psel,penable,pwrite,
 input [31:0] pwdata,output reg [31:0] prdata,output pready,pslverror,
 input [31:0] video_time,video_id,
 input [95:0] preview_bank_ids,input [1:0] preview_write_bank,
 input vclk,vresetn,vs,hs,de,input [7:0] r,g,b,
 output reg [7:0] out_r,out_g,out_b,output reg out_de,out_hs,out_vs,output reg low_power
);
 localparam WIDTH=1920, HEIGHT=1080, PIXELS=9216, MAX_BOXES=16;
 (* ram_style="block_ram" *) reg [23:0] pixels [0:PIXELS-1];
 reg [23:0] pixel_q;
 reg [10:0] x,y;
 // Bit 8 accumulates motion between CPU snapshots; it uses the spare BRAM bit.
 (* ram_style="block_ram" *) reg [8:0] previous_gray[0:3599];
 (* ram_style="block_ram" *) reg [31:0] motion_map[0:224];
 reg [31:0] motion_q,motion_build;
 reg [8:0] previous_q;
 wire [7:0] gray_q=previous_q[7:0];
 wire accumulated=previous_q[8];
 reg [7:0] gray_current;
 reg accumulation_valid;
 reg [11:0] motion_index,motion_addr,motion_count,snapshot_motion_count;
 reg [11:0] motion_count_v;
 reg motion_valid_v;
 reg [10:0] motion_x,motion_y;
 reg [6:0] motion_col;
 reg motion_pending,motion_capture,previous_valid,motion_valid,last_scan_valid,scan_consistent;
 reg [31:0] previous_video_id,motion_video_id;
 (* async_reg="true" *) reg [31:0] motion_tag0,motion_tag1;
 wire motion_address=paddr>=16'hc000 && paddr<16'hf840;
 wire motion_packed=paddr>=16'hf900 && paddr<16'hfc84;
 wire [7:0] motion_word_index=motion_packed?(paddr-16'hf900)>>2:paddr[13:6];
 wire sample_motion=de && x==motion_x && y==motion_y && motion_col<80;
 wire [7:0] gray=(r>>2)+(g>>1)+(b>>2);
 wire [7:0] difference=gray_q>gray_current ? gray_q-gray_current:gray_current-gray_q;
 wire changed=(accumulation_valid && accumulated) || (previous_valid && difference>=16);
 always @(posedge vclk)begin
  if(sample_motion)begin
   previous_q<=previous_gray[motion_index];
   accumulation_valid<=last_scan_valid;
   gray_current<=gray;motion_addr<=motion_index;
  end
  if(motion_pending)begin
   previous_gray[motion_addr]<={motion_capture?1'b0:changed,gray_current};
   if(motion_capture)begin
    motion_build<={previous_valid && difference>=16,changed,motion_build[31:2]};
    if(motion_addr[3:0]==15)motion_map[motion_addr[11:4]]<={previous_valid && difference>=16,changed,motion_build[31:2]};
   end
  end
 end
 reg request_toggle,box_toggle;
 reg low_power_request;
 (* async_reg="true" *) reg [1:0] low_power_sync;
 reg [4:0] pending_count;
 reg [31:0] pending_x[0:MAX_BOXES-1],pending_y[0:MAX_BOXES-1];
 reg done_toggle,box_ack;
 reg snapshot_start_toggle;
 (* async_reg="true" *) reg [1:0] snapshot_start_sync;
 reg snapshot_start_seen,snapshot_done_seen,snapshot_started,snapshot_consistent;
 reg [31:0] snapshot_time,snapshot_id,video_id_d0,video_id_d1,video_id_d2;
 reg snapshot_valid;
 (* async_reg="true" *) reg [1:0] done_sync,box_ack_sync;
 (* async_reg="true" *) reg [1:0] request_sync,box_sync;
 wire pixel_address = paddr>=16'h1000 && paddr<16'ha000;
 wire [13:0] pixel_index=(paddr-16'h1000)>>2;
 wire [3:0] box_index=paddr[6:3];
 wire box_address=paddr>=16'h0100 && paddr<16'h0180;
 wire box_busy=box_toggle!=box_ack_sync[1];
 assign pready=1'b1;
 assign pslverror=psel && penable && pwrite &&
   (pixel_address || (paddr==16'h000c && (box_busy || pwdata>MAX_BOXES)));
 always @(posedge pclk) begin
   if (psel && pixel_address) pixel_q<=pixels[pixel_index];
   if (psel && (motion_address || motion_packed))motion_q<=motion_map[motion_word_index];
 end
 always @* begin
   prdata=0;
   if(pixel_address) prdata={8'b0,pixel_q};
   else if(motion_packed)prdata=motion_q;
   else if(motion_address)prdata=(motion_q>>(paddr[5:2]*2))&32'd3;
   else case(paddr)
     16'h0000: prdata=32'h56364631; // V6F1: X word bits 31:30 carry rule state.
     16'h0008: prdata={30'b0,box_busy,(done_sync[1]==request_toggle)};
     16'h0010: prdata={16'd1080,16'd1920};
     16'h0014: prdata=snapshot_time;
     16'h0018: prdata=snapshot_id;
     16'h001c: prdata={31'b0,snapshot_valid};
     16'h0020: prdata={18'b0,snapshot_motion_count,motion_valid,snapshot_valid};
     16'h0024: prdata=32'h50525631; // PRV1: read-only DDR bank ownership.
     16'h0028: prdata={30'b0,preview_write_bank};
     16'h002c: prdata=32'h4e494331; // NIC1: RISC-V serves HTTP over bidirectional Ethernet.
     16'h0030: prdata=preview_bank_ids[31:0];
     16'h0034: prdata=preview_bank_ids[63:32];
     16'h0038: prdata=preview_bank_ids[95:64];
     16'h003c: prdata={31'b0,low_power_request};
     default: prdata=0;
   endcase
 end
 integer pi;
 always @(posedge pclk or negedge presetn) begin
   if(!presetn) begin
     request_toggle<=0; box_toggle<=0; pending_count<=0;
     low_power_request<=0;
     done_sync<=0; box_ack_sync<=0;
     snapshot_start_sync<=0;snapshot_start_seen<=0;snapshot_done_seen<=0;
     snapshot_started<=0;snapshot_consistent<=0;snapshot_valid<=0;
     snapshot_time<=0;snapshot_id<=0;video_id_d0<=0;video_id_d1<=0;video_id_d2<=0;
     snapshot_motion_count<=0;motion_valid<=0;
     for(pi=0;pi<MAX_BOXES;pi=pi+1) begin pending_x[pi]<=0;pending_y[pi]<=0;end
   end else begin
     done_sync<={done_sync[0],done_toggle};
     box_ack_sync<={box_ack_sync[0],box_ack};
     snapshot_start_sync<={snapshot_start_sync[0],snapshot_start_toggle};
     video_id_d0<=video_id;video_id_d1<=video_id_d0;video_id_d2<=video_id_d1;
     if(snapshot_start_sync[1]!=snapshot_start_seen) begin
       snapshot_start_seen<=snapshot_start_sync[1];snapshot_started<=1;
       snapshot_time<=video_time;snapshot_id<=video_id;
       // Reject a DDR tag change near the asynchronous first sampled pixel.
       snapshot_consistent<=video_id!=0 && video_id==video_id_d0 &&
         video_id==video_id_d1 && video_id==video_id_d2;
     end else if(snapshot_started && video_id!=snapshot_id) snapshot_consistent<=0;
     if(done_sync[1]!=snapshot_done_seen) begin
       snapshot_done_seen<=done_sync[1];snapshot_started<=0;
       snapshot_valid<=snapshot_started && snapshot_consistent && snapshot_id!=0 && video_id==snapshot_id;
       snapshot_motion_count<=motion_count_v;motion_valid<=motion_valid_v;
     end
     if(psel && penable && pwrite && !pslverror) begin
       if(paddr==16'h003c)low_power_request<=pwdata[0];
       if(paddr==16'h0004 && done_sync[1]==request_toggle) begin
         request_toggle<=~request_toggle;snapshot_valid<=0;snapshot_started<=0;
       end
       if(box_address && !box_busy) begin
         if(paddr[2]) pending_y[box_index]<=pwdata;
         else pending_x[box_index]<=pwdata;
       end
       if(paddr==16'h000c) begin pending_count<=pwdata[4:0];box_toggle<=~box_toggle;end
     end
   end
 end

 reg vs_prev,de_prev,capturing;
 wire area_valid;wire [13:0] area_index;wire [23:0] area_rgb;
 rgb_area_downsample area(.clk(vclk),.resetn(vresetn),.active(capturing),.vs(vs),.de(de),.y(y),.r(r),.g(g),.b(b),.valid(area_valid),.index(area_index),.rgb(area_rgb));
 reg [4:0] active_count;
 reg [31:0] active_x[0:MAX_BOXES-1],active_y[0:MAX_BOXES-1];
 reg border;
 reg [1:0] border_state;
 integer vi;
 // Bundled data: pending boxes stay fixed until box_ack returns to the CPU.
 always @(posedge vclk or negedge vresetn) begin
   if(!vresetn) begin
     request_sync<=0;box_sync<=0;done_toggle<=0;box_ack<=0;
     low_power_sync<=0;low_power<=0;
     snapshot_start_toggle<=0;
     vs_prev<=0;de_prev<=0;capturing<=0;x<=0;y<=0;
     active_count<=0;out_r<=0;out_g<=0;out_b<=0;out_de<=0;out_hs<=0;out_vs<=0;
     motion_index<=0;motion_x<=0;motion_y<=0;motion_col<=0;motion_pending<=0;motion_capture<=0;
     previous_valid<=0;motion_valid_v<=0;last_scan_valid<=0;scan_consistent<=0;motion_count<=0;motion_count_v<=0;
     previous_video_id<=0;motion_video_id<=0;motion_tag0<=0;motion_tag1<=0;
     for(vi=0;vi<MAX_BOXES;vi=vi+1) begin active_x[vi]<=0;active_y[vi]<=0;end
   end else begin
     request_sync<={request_sync[0],request_toggle};
     low_power_sync<={low_power_sync[0],low_power_request};
     motion_tag0<=video_id;motion_tag1<=motion_tag0;
     motion_pending<=sample_motion;
     if(motion_index>0 && motion_index<3600 && motion_tag1!=motion_video_id)scan_consistent<=0;
     if(motion_pending && motion_capture && changed)motion_count<=motion_count+1;
     box_sync<={box_sync[0],box_toggle};
     vs_prev<=vs;de_prev<=de;
     out_de<=de;out_hs<=hs;out_vs<=vs;
     // Dim only the HDMI background; detector and motion inputs remain full brightness.
     out_r<=border ? (border_state==0 ? 8'd0:8'd255):(low_power?r>>2:r);
     out_g<=border ? (border_state==2 ? 8'd0:8'd255):(low_power?g>>2:g);out_b<=border ? 8'd0:(low_power?b>>2:b);
     if(vs && !vs_prev) begin
       low_power<=low_power_sync[1];
       x<=0;y<=0;
       capturing<=request_sync[1]!=done_toggle;
       motion_capture<=request_sync[1]!=done_toggle;
       motion_index<=0;motion_x<=0;motion_y<=0;motion_col<=0;motion_count<=0;
       if(box_sync[1]!=box_ack) begin
         active_count<=pending_count;
         for(vi=0;vi<MAX_BOXES;vi=vi+1) begin active_x[vi]<=pending_x[vi];active_y[vi]<=pending_y[vi];end
         box_ack<=box_sync[1];
       end
     end else begin
       if(de) x<=x+1'b1;
       else x<=0;
       if(!de && de_prev) begin y<=y+1'b1;motion_x<=0;motion_col<=0;end
       if(sample_motion)begin
        motion_index<=motion_index+1;motion_x<=motion_x+24;motion_col<=motion_col+1;
        if(motion_index==0)begin
         previous_valid<=last_scan_valid && previous_video_id!=0 && motion_tag1!=previous_video_id;
         motion_video_id<=motion_tag1;scan_consistent<=1;
        end
        if(motion_index==3599)begin
         last_scan_valid<=scan_consistent && motion_video_id==motion_tag1;previous_video_id<=motion_tag1;
        end
        if(motion_col==79)motion_y<=motion_y+24;
       end
       if(capturing && de && x==0 && y==0)snapshot_start_toggle<=~snapshot_start_toggle;
       if(capturing && area_valid && area_index==9215)begin
        capturing<=0;done_toggle<=request_sync[1];motion_capture<=0;
        motion_count_v<=motion_count;
        motion_valid_v<=previous_valid && scan_consistent && motion_index==3600 && motion_video_id==motion_tag1;
       end
     end
   end
 end
 // Separate RAM write process permits true dual-clock block RAM inference.
 always @(posedge vclk) begin
   if(vresetn && capturing && area_valid)
     pixels[area_index]<=area_rgb;
 end
 integer bi;
 always @* begin
   border=0;border_state=0;
   for(bi=0;bi<MAX_BOXES;bi=bi+1) begin
     if(bi<active_count && de &&
       x>=active_x[bi][10:0] && x<=active_x[bi][26:16] &&
       y>=active_y[bi][10:0] && y<=active_y[bi][26:16] &&
       (x-active_x[bi][10:0]<3 || active_x[bi][26:16]-x<3 ||
        y-active_y[bi][10:0]<3 || active_y[bi][26:16]-y<3)) begin
        border=1;
        if(active_x[bi][31:30]<=2 && active_x[bi][31:30]>border_state)
          border_state=active_x[bi][31:30];
     end
   end
 end
endmodule
