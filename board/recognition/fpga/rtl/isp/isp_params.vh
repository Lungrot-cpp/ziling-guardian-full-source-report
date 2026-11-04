// =====================================================================
// isp_params.vh  --  由 isp_matlab/isp_export_rtl.m 自动生成, 请勿手改
// 数据: rawdata.hex  1920x1080  GBRG
// 算法: rtl/isp/isp_top.v, 与 isp_matlab/isp_core.m ('fixed') 逐像素一致
// =====================================================================
`ifndef ISP_PARAMS_VH
`define ISP_PARAMS_VH

// ---------- 图像尺寸 ----------
`define ISP_H_ACTIVE   1080
`define ISP_W_ACTIVE   1920
`define ISP_WORDS      960   // 每行字(2像素/字) = W_ACTIVE/2

// ---------- BLC 黑电平校正 ----------
`define BLACK_LEVEL    20

// ---------- AWB 白平衡 (Q8.8 绝对增益) ----------
`define AWB_GAIN_R_Q8  912
`define AWB_GAIN_G_Q8  980
`define AWB_GAIN_B_Q8  1926

// ---------- DPC 坏点校正阈值 (10bit 域, 按通道) ----------
`define DPC_TH_R       114
`define DPC_TH_G       123
`define DPC_TH_B       241

// ---------- RAW 降噪平坦判据阈值 (10bit 域, 按通道) ----------
`define NR_TH_R        21
`define NR_TH_G        23
`define NR_TH_B        45

// ---------- CCM 颜色校正 (Q2.8, 乘后 +128 再 >>8) ----------
`define CCM_RR 256
`define CCM_RG 0
`define CCM_RB 0
`define CCM_GR 0
`define CCM_GG 256
`define CCM_GB 0
`define CCM_BR 0
`define CCM_BG 0
`define CCM_BB 256

// ---------- Gamma ----------
`define GAMMA_WP       900   // 10bit 白点
`define GAMMA_MEM_FILE "gamma_lut_1024x8.mem"

`endif // ISP_PARAMS_VH
