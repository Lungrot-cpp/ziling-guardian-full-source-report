// Current ISP image path, with its last programmed VIO values fixed as constants.
module v9_isp_pipeline(
 input clk,resetn,vs,hs,de,input [15:0] raw_data,
 output out_vs,out_hs,out_de,out_valid,output [47:0] out_rgb
);
 wire blc_vs,blc_hs,blc_de,blc_valid;
 wire [15:0] blc_data;
 raw_data_blc #(.P_DEPTH(8),.P_BLACK(8'd17)) blc(
  .i_vs(vs),.i_hs(hs),.i_de(de),.i_valid(de),
  .i_data({raw_data[7:0],raw_data[15:8]}),
  .o_vs(blc_vs),.o_hs(blc_hs),.o_de(blc_de),.o_valid(blc_valid),.o_data(blc_data));
 // The reference RAW NR is bypassed; omit its otherwise unused five-row cache.
 wire db_vs,db_hs,db_de,db_valid;
 wire [47:0] db_rgb;
 debayer_top_wbg_2to1 #(.GAMMA_BYPASS(0)) demosaic(
  .in_pclk(clk),.in_rstn(resetn),.raw_vs_i(blc_vs),.raw_hs_i(blc_hs),
  .raw_de_i(blc_de),.raw_valid_i(blc_valid),.raw_datax4_i(blc_data),
  .wb_r_gain(16'd24896),.wb_g_gain(16'd16384),.wb_b_gain(16'd32768),
  .rgb_vs_o(db_vs),.rgb_hs_o(db_hs),.rgb_de_o(db_de),.rgb_valid_o(db_valid),.rgb_datax2_o(db_rgb));
 wire sharp_vs,sharp_hs,sharp_de,sharp_valid;
 wire [47:0] sharp_rgb;
 // Reference 16'd95535 truncates to 29999; use its actual 16-bit value.
 rgb_sharpen #(.P_SHARP_GAIN(16'd29999),.X_CNT_WIDTH(10)) sharpen(
  .i_pclk(clk),.i_arstn(resetn),.P_BYPASS(1'b0),
  .i_vs(db_vs),.i_hs(db_hs),.i_de(db_de),.i_valid(db_valid),.i_datax2(db_rgb),
  .o_vs(sharp_vs),.o_hs(sharp_hs),.o_de(sharp_de),.o_valid(sharp_valid),.o_datax2(sharp_rgb));
 wire ccm_vs,ccm_hs,ccm_de,ccm_valid;
 wire [47:0] ccm_rgb;
 rgb_ccm #(
  .M_RR(16'sd16885),.M_RG(16'sd3851),.M_RB(-16'sd4352),
  .M_GR(-16'sd1126),.M_GG(16'sd15054),.M_GB(16'sd2456),
  .M_BR(16'sd4491),.M_BG(-16'sd3344),.M_BB(16'sd15237)
 ) ccm(
  .i_pclk(clk),.i_arstn(resetn),.P_BYPASS(1'b0),
  .i_vs(sharp_vs),.i_hs(sharp_hs),.i_de(sharp_de),.i_valid(sharp_valid),.i_datax2(sharp_rgb),
  .o_vs(ccm_vs),.o_hs(ccm_hs),.o_de(ccm_de),.o_valid(ccm_valid),.o_datax2(ccm_rgb));
 rgb_saturate #(
  .P_SAT_GAIN(16'd21299),.P_SAT_GAIN_LO(16'd8192),
  .P_Y_LO(8'd32),.P_Y_HI(8'd96),.P_RED_SHIFT(0)
 ) saturation(
  .i_pclk(clk),.i_arstn(resetn),.P_BYPASS(1'b0),
  .i_vs(ccm_vs),.i_hs(ccm_hs),.i_de(ccm_de),.i_valid(ccm_valid),.i_datax2(ccm_rgb),
  .o_vs(out_vs),.o_hs(out_hs),.o_de(out_de),.o_valid(out_valid),.o_datax2(out_rgb));
endmodule
