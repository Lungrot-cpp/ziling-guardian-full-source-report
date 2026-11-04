// Acknowledged bank swap at vertical blank. CPU never writes the visible bank.
module sensor_overlay (
    input wire sys_clk, sys_resetn,
    input wire wr_en,
    input wire [10:0] wr_addr,
    input wire [15:0] wr_data,
    input wire request,
    output reg acknowledge,
    input wire pclk, resetn,
    input wire [23:0] rgb_in,
    input wire de_in, hs_in, vs_in,
    output reg [23:0] rgb_out,
    output reg de_out, hs_out, vs_out
);
    // Four parity banks read a 2x2 window without duplicating the heatmap.
    reg [15:0] heat00 [0:511],heat01 [0:511],heat10 [0:511],heat11 [0:511];
    reg [15:0] info [0:511];
    reg [7:0] font [0:4095];
    initial $readmemh("rtl/sensors/font.hex",font);
    wire [8:0] heat_wr_addr={wr_addr[10],wr_addr[9:6],wr_addr[4:1]};
    always @(posedge sys_clk)
        if (sys_resetn && wr_en) begin
            if(wr_addr[9:0]<768) begin
                case({wr_addr[5],wr_addr[0]})
                    0: heat00[heat_wr_addr] <= wr_data;
                    1: heat01[heat_wr_addr] <= wr_data;
                    2: heat10[heat_wr_addr] <= wr_data;
                    3: heat11[heat_wr_addr] <= wr_data;
                endcase
            end else info[{wr_addr[10],wr_addr[7:0]}] <= wr_data;
        end

    (* async_reg = "true" *) reg [1:0] req_sync;
    (* async_reg = "true" *) reg [2:0] pixel_reset = 0;
    always @(posedge pclk or negedge resetn)
        if(!resetn) pixel_reset<=0;
        else pixel_reset<={pixel_reset[1:0],1'b1};
    reg bank, available, vs_d, de_d;
    reg [11:0] x,y;
    wire [11:0] active_x = de_d ? x : 12'd0;
    wire heat_area = de_in && active_x>=1600 && active_x<1856 && y>=64 && y<256;
    wire text_area = de_in && active_x<1536 && y>=992 && y<1056;
    wire scale_area = de_in && active_x>=1576 && active_x<1592 && y>=64 && y<256;
    wire label_area = de_in && active_x>=1440 && active_x<1568 &&
                      ((y>=64&&y<96)||(y>=144&&y<176)||(y>=224&&y<256));
    wire [11:0] hx=active_x-1600, hy=y-64, ty=y-992;
    wire [11:0] label_x=active_x-1440;
    wire [11:0] label_y=y-(y>=224?12'd224:y>=144?12'd144:12'd64);
    wire [9:0] rd_index = heat_area ? {hy[7:3],hx[7:3]} :
                          scale_area ? 10'd960+{5'b0,hy[7:3]} :
                          label_area ? 10'd984+(y>=224?10'd16:y>=144?10'd8:10'd0)+{7'b0,label_x[6:4]} :
                          10'd768 + (ty[5] ? 10'd96 : 10'd0) + {3'b0,active_x[10:4]};
    wire [4:0] next_x=hx[7:3]==31?5'd31:hx[7:3]+5'd1;
    wire [4:0] next_y=hy[7:3]==23?5'd23:hy[7:3]+5'd1;
    wire [3:0] even_x=hx[3]?next_x[4:1]:hx[7:4];
    wire [3:0] odd_x=hx[3]?hx[7:4]:next_x[4:1];
    wire [3:0] even_y=hy[3]?next_y[4:1]:hy[7:4];
    wire [3:0] odd_y=hy[3]?hy[7:4]:next_y[4:1];
    reg [15:0] q00,q01,q10,q11,qinfo;
    reg [1:0] corner0,right0,down0,diag0;
    reg heat0,smooth0,text0,available0,de0,hs0,vs0;
    reg heat1,smooth1,text1,available1,de1,hs1,vs1,glyph1;
    reg [2:0] fx0,fy0,fy1;
    reg [3:0] font_y0;
    reg [2:0] font_x0;
    reg [23:0] rgb0,rgb1,color1,top1,bottom1;
    wire [63:0] neighbours={q11,q10,q01,q00};
    wire [15:0] q=smooth0?neighbours[{corner0,4'b0}+:16]:qinfo;
    wire [15:0] qr=neighbours[{right0,4'b0}+:16];
    wire [15:0] qd=neighbours[{down0,4'b0}+:16];
    wire [15:0] qdiag=neighbours[{diag0,4'b0}+:16];
    wire glyph = font[{q[7:0],font_y0}][7-font_x0];
    wire [23:0] color={q[15:11],q[15:13],q[10:5],q[10:9],q[4:0],q[4:2]};
    function [23:0] expand;
        input [15:0] pixel;
        begin expand={pixel[15:11],pixel[15:13],pixel[10:5],pixel[10:9],pixel[4:0],pixel[4:2]}; end
    endfunction
    function [7:0] blend;
        input [7:0] a,b;
        input [2:0] fraction;
        reg signed [11:0] delta,sum;
        begin
            // Preserve floor((a*(8-f)+b*f)/8), including negative differences.
            delta=$signed({1'b0,b})-$signed({1'b0,a});
            sum=$signed({1'b0,a,3'b0})+(fraction[0]?delta:12'sd0)+
                (fraction[1]?(delta<<<1):12'sd0)+(fraction[2]?(delta<<<2):12'sd0);
            blend=sum[10:3];
        end
    endfunction
    function [23:0] mix_rgb;
        input [23:0] a,b;
        input [2:0] fraction;
        begin mix_rgb={blend(a[23:16],b[23:16],fraction),blend(a[15:8],b[15:8],fraction),blend(a[7:0],b[7:0],fraction)}; end
    endfunction
    always @(posedge pclk) begin
        if(!pixel_reset[2]) begin
            req_sync<=0;acknowledge<=0;bank<=0;available<=0;
            x<=0;y<=0;vs_d<=0;de_d<=0;
            q00<=0;q01<=0;q10<=0;q11<=0;qinfo<=0;
            corner0<=0;right0<=0;down0<=0;diag0<=0;
            heat0<=0;smooth0<=0;text0<=0;available0<=0;rgb0<=0;
            heat1<=0;smooth1<=0;text1<=0;available1<=0;de1<=0;hs1<=0;vs1<=0;glyph1<=0;
            rgb1<=0;color1<=0;top1<=0;bottom1<=0;fx0<=0;fy0<=0;fy1<=0;
            de0<=0;hs0<=0;vs0<=0;font_x0<=0;font_y0<=0;
            rgb_out<=0;de_out<=0;hs_out<=0;vs_out<=0;
        end else begin
            req_sync<={req_sync[0],request};
            vs_d<=vs_in;de_d<=de_in;
            if(vs_in&&!vs_d) begin
                x<=0;y<=0;
                if(req_sync[1]!=acknowledge) begin
                    bank<=req_sync[1];acknowledge<=req_sync[1];available<=1;
                end
            end else if(de_in) x<=active_x+1'b1;
            else if(de_d) begin x<=0;y<=y+1'b1;end
            q00<=heat00[{bank,even_y,even_x}];
            q01<=heat01[{bank,even_y,odd_x}];
            q10<=heat10[{bank,odd_y,even_x}];
            q11<=heat11[{bank,odd_y,odd_x}];
            qinfo<=info[{bank,rd_index[7:0]}];
            corner0<={hy[3],hx[3]};right0<={hy[3],next_x[0]};
            down0<={next_y[0],hx[3]};diag0<={next_y[0],next_x[0]};
            fx0<=hx[2:0];fy0<=hy[2:0];
            smooth0<=heat_area;
            heat0<=heat_area||scale_area;text0<=text_area||label_area;available0<=available;
            font_x0<=active_x[3:1];font_y0<=label_area?label_y[4:1]:ty[4:1];
            rgb0<=rgb_in;de0<=de_in;hs0<=hs_in;vs0<=vs_in;
            top1<=mix_rgb(color,expand(qr),fx0);
            bottom1<=mix_rgb(expand(qd),expand(qdiag),fx0);
            fy1<=fy0;color1<=color;glyph1<=glyph;
            heat1<=heat0;smooth1<=smooth0;text1<=text0;available1<=available0;
            rgb1<=rgb0;de1<=de0;hs1<=hs0;vs1<=vs0;
            rgb_out<=rgb1;de_out<=de1;hs_out<=hs1;vs_out<=vs1;
            if(available1&&heat1)rgb_out<=smooth1?mix_rgb(top1,bottom1,fy1):color1;
            if(available1&&text1)rgb_out<=glyph1?24'hffffff:24'h102030;
        end
    end
endmodule
