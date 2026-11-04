`timescale 1ns/1ps
/////////////////////////////////////////////////////////////////////////////
// wb_gain_3ch.v —— 三通道可编程白平衡增益 (demosaic 之后, 作用在 RGB 三条平面上)
//
// 位置: 接在 debayer_top_2to1 之后、gamma_lut_v1 之前。
//       与 rgb_gain_v1 的区别是根本性的 ——
//         rgb_gain_v1 : 在 demosaic **之前**, 作用在 Bayer 平面, 增益按行列奇偶选
//         本模块      : 在 demosaic **之后**, 作用在 RGB 三条平面, 每平面一个增益
//       这正是 raw_pattern_test.m 的语义(它在 demosaic 之后做灰度世界白平衡)。
//
// 数据约定(与 raw_to_rgb 的输出完全一致):
//   每个通道 16 位, 低字节 = 偶列像素(x=0,2,4...), 高字节 = 奇列像素(x=1,3,5...)
//   三条平面是独立的, 所以 R 平面两个字节都乘 wb_r_gain,
//   G 平面都乘 wb_g_gain, B 平面都乘 wb_b_gain。
//
//   *** 这里绝对不要按行奇偶去选增益 ***
//   请不要从 rgb_gain_v1.v:50-58 连带把 r_line_cnt 那套抄过来。那是 Bayer 平面的
//   分工(偶数行低字节=R/高字节=G, 奇数行低字节=G/高字节=B), 到了 RGB 输出这里
//   三条平面已经分开, 行奇偶不再参与。抄过来之后增益 = 1.0 时看不出来,
//   增益 != 1.0 时偶/奇行会串色。
//
// 定标: 增益是 **Q2.14** 定点(16 位, 范围 0~4, 覆盖钳位区间 [0.4, 2.5])。
//   为什么不用 rgb_gain_v1 那套 Q8.8: Q8.8 对 1.199977 只能表示成 307/256
//   = 1.19921875, 相对误差 0.06%, 在 v=255 时是 0.19 的绝对误差; Q2.14 的
//   量化误差 ~2.6e-5, 在本帧实测使"增益量化"这一项的贡献**恰好为 0**
//   (即 (v*19660 + 8192)>>14 == round(v*1.199977) 对全部 v∈[0,255] 成立)。
//   注意这是本帧实测而非数学保证: 换增益后需要重跑 golden_wbg.py 的 (a) 对比确认。
//
// *** 增益钳位 [0.4, 2.5] 由本模块负责 ***
//   MATLAB 是 min(max(gain,0.4),2.5), 钳在 double 域。本模块在 Q2.14 域钳位,
//   语义对齐。如果软件写 2.6 进来, 不钳的话低亮度区会有查不出来的系统性偏差。
//
// TODO: 本模块只做"乘 + 钳位", 不负责**算**增益。真实系统还需要一个统计单元
//       (整帧每通道均值) + 寄存器写入路径来产生这三个增益。
/////////////////////////////////////////////////////////////////////////////

module wb_gain_3ch
#(
	parameter	P_DEPTH		= 8,	// 每个像素输入位宽
	parameter	GAIN_FRAC	= 14,	// 增益的小数位数 (Q2.14)
	parameter	OUT_FRAC	= 1		// 输出保留的小数位数 (Q8.1, 交给 gamma 表)
)
(
	input					i_pclk,
	input					i_arstn,

	input					i_vs,
	input					i_hs,
	input					i_de,
	input					i_valid,
	input	[2*P_DEPTH-1:0]	i_r,		// 低字节 = 偶列像素
	input	[2*P_DEPTH-1:0]	i_g,
	input	[2*P_DEPTH-1:0]	i_b,

	input	[15:0]			wb_r_gain,	// Q2.14, 1.0 = 16384
	input	[15:0]			wb_g_gain,
	input	[15:0]			wb_b_gain,

	output	reg					o_vs,
	output	reg					o_hs,
	output	reg					o_de,
	output	reg					o_valid,
	// 端口位宽这里写全表达式而不是 OW: ANSI 风格端口声明**看不到**模块体内的
	// localparam, 写 2*OW-1:0 会直接 vlog-2730 Undefined variable: 'OW'.
	// (OW 在模块体内使用是合法的, 那里 localparam 已经在作用域里了。)
	output	reg	[2*(P_DEPTH+OUT_FRAC)-1:0]	o_r,	// 每像素 OW 位 Q8.1, 低字节 = 偶列像素
	output	reg	[2*(P_DEPTH+OUT_FRAC)-1:0]	o_g,
	output	reg	[2*(P_DEPTH+OUT_FRAC)-1:0]	o_b
);

localparam	OW		= P_DEPTH + OUT_FRAC;	// 输出每像素位宽 = 9
localparam	SHIFT	= GAIN_FRAC - OUT_FRAC;	// = 13

// 输出在 [0, OW 位满量程] 处饱和。255.5 之后 gamma 恒为 255, 所以饱和点就是表头:
//   OW=9 -> 511 对应 WB 值 255.5
localparam	[OW-1:0]	C_KMAX		= {OW{1'b1}};

// 钳位端点, Q2.14: 0.4*16384 = 6553.6 -> 向上取 6554;  2.5*16384 = 40960 恰好精确
localparam	[15:0]		C_GAIN_MIN	= 16'd6554;
localparam	[15:0]		C_GAIN_MAX	= 16'd40960;

// 增益钳位, 与 MATLAB 的 min(max(gain,0.4),2.5) 对齐
wire [15:0] w_r_gain = (wb_r_gain < C_GAIN_MIN) ? C_GAIN_MIN :
                       (wb_r_gain > C_GAIN_MAX) ? C_GAIN_MAX : wb_r_gain;
wire [15:0] w_g_gain = (wb_g_gain < C_GAIN_MIN) ? C_GAIN_MIN :
                       (wb_g_gain > C_GAIN_MAX) ? C_GAIN_MAX : wb_g_gain;
wire [15:0] w_b_gain = (wb_b_gain < C_GAIN_MIN) ? C_GAIN_MIN :
                       (wb_b_gain > C_GAIN_MAX) ? C_GAIN_MAX : wb_b_gain;

// Q(8+GAIN_FRAC) 的乘积右移到 Q8.1, 四舍五入, 再饱和。
//
// 位宽: 8 位像素 x 任意 16 位增益 <= 255*65535 = 16,711,425 < 2^24, 所以 24 位
// 对整个增益范围都够(加 4096 后是 16,719,617 也不溢出)。
//
// *** 饱和前必须留着高位比较, 不能先截成 OW 位再比大小 ***
// 右移后最大是 255*4*2 = 2040, 直接取低 9 位会把 v 大的像素 wrap 成小值。
// 本帧 R 有 28 个、B 有 622 个像素落在饱和区, 这个 bug 一定会在数据里现形。
// (G 通道增益恒为 1.0 永远不饱和, 别拿 G 去验饱和路径。)
function [OW-1:0] wb_q81;
	input [2*P_DEPTH+15:0] mul;			// 24 位乘积
	reg   [2*P_DEPTH+15:0] tmp;
	begin
		tmp = (mul + (1 << (SHIFT - 1))) >> SHIFT;
		wb_q81 = (tmp > C_KMAX) ? C_KMAX : tmp[OW-1:0];
	end
endfunction

wire [OW-1:0] w_r_lo = wb_q81(i_r[  P_DEPTH-1:0    ] * w_r_gain);
wire [OW-1:0] w_r_hi = wb_q81(i_r[2*P_DEPTH-1:P_DEPTH] * w_r_gain);
wire [OW-1:0] w_g_lo = wb_q81(i_g[  P_DEPTH-1:0    ] * w_g_gain);
wire [OW-1:0] w_g_hi = wb_q81(i_g[2*P_DEPTH-1:P_DEPTH] * w_g_gain);
wire [OW-1:0] w_b_lo = wb_q81(i_b[  P_DEPTH-1:0    ] * w_b_gain);
wire [OW-1:0] w_b_hi = wb_q81(i_b[2*P_DEPTH-1:P_DEPTH] * w_b_gain);

// 标志与数据在同一个 always 块里一起打拍 —— 必须严格同拍, 否则
// testbench 的 SKIP_WORDS/SKIP_LINES 对齐会失效。
//
// 带复位(rgb_gain_v1.v:92-99 那段是没有复位的, 不要照抄): 不复位的话出复位时
// o_data/o_de/o_valid 是 X, X 会顺着乘法传进 gamma 那个 case 的输入。
always @(posedge i_pclk)
begin
	if (!i_arstn)
	begin
		o_vs	<= 1'b0;
		o_hs	<= 1'b0;
		o_de	<= 1'b0;
		o_valid	<= 1'b0;
		o_r		<= {(2*OW){1'b0}};
		o_g		<= {(2*OW){1'b0}};
		o_b		<= {(2*OW){1'b0}};
	end
	else
	begin
		o_vs	<= i_vs;
		o_hs	<= i_hs;
		o_de	<= i_de;
		o_valid	<= i_valid;
		o_r		<= { w_r_hi, w_r_lo };
		o_g		<= { w_g_hi, w_g_lo };
		o_b		<= { w_b_hi, w_b_lo };
	end
end

endmodule
