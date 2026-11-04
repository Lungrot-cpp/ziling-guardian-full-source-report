`timescale 1ns/1ps
/////////////////////////////////////////////////////////////////////////////
// debayer_top_wbg_2to1.v —— demosaic + 白平衡 + gamma 的 wrapper 顶层
//
// 流水线:  debayer_top_2to1  ->  wb_gain_3ch  ->  gamma_lut_v1
//          (含原来的 rgb_gain_v1,      三通道可编程          固定 gamma 表
//           已硬连成直通)               Q2.14 增益
//
// 为什么另开一个 wrapper 而不是改 debayer_top_2to1.v:
//   那一条通路已经验过 0/6220800 差异, 一个字都不动它就永远不会退化。
//
// ---------------------------------------------------------------------------
// *** 关于"标志与数据差 1 拍"—— 这是有意的, 不要去"修正"它 ***
//   raw_to_rgb 的 o_vsync/o_hsync/o_de/o_valid 是 2P 级, 而 o_r/o_g/o_b 是输出
//   寄存器第 3 级, 比那几个标志晚 1 个时钟。debayer_top_2to1 原样把两者透传出来,
//   testbench 用 de && valid 当采集门、采rgb_*out 数据, 靠 SKIP_WORDS=2 /
//   SKIP_LINES=2 把这个错拍兜掉 —— 整套参数就是配着这个错拍标定的。
//
//   本 wrapper 把 debayer 的标志和数据**一起**送进 wb_gain_3ch, 后者在同一个
//   always 块里用同一个时钟沿把标志和数据各打一拍, 所以**相对错拍原样保留**,
//   tb 的 SKIP_WORDS/SKIP_LINES 继续成立, 只是整条流水线多走了 2 拍。
//
//   千万别"顺手"在 wrapper 里把标志多打一拍去对齐数据: 那会把错拍从 -1 拍
//   翻成 0 拍, 采集窗口整体错开一个字, 逐字节比对会全线飘掉。
//   本模块对每像素只做无状态乘法/查表, 不关心当前这拍的数据属不属于当前这拍的
//   标志, 所以带着错拍穿过是安全的。
// ---------------------------------------------------------------------------
//
// 增益端口:  wb_r_gain / wb_g_gain / wb_b_gain 是 **Q2.14** (1.0 = 16384),
//   作用于 demosaic **之后**的 RGB 三条平面 —— 与 raw_pattern_test.m 的语义一致。
//
//   *** 这两个增益**不是** debayer_top_2to1.v:11-13 那三个 r_gain/g_gain/b_gain ***
//   那三个是 **Q8.8** (1.0 = 256)、作用在 demosaic **之前**的 Bayer 平面上。
//   同名同宽但定点格式和作用域都不同, 复用会得到"编译通过、波形有数据"的错值:
//   把 19660 (Q2.14 的 1.199977) 喂进 Q8.8 端口 = 76.8 倍, 直接爆白;
//   把 307 (Q8.8 的 1.1992) 喂进 Q2.14 端口 = 0.0187, R 平面黑掉。
//   所以本 wrapper 干脆把那三个旧端口从端口表里删掉, 内部硬连成直通, 从接口上
//   消除误接的可能。
//
// GAMMA_BYPASS = 1 时数据改用"WB 结果丢掉小数位", 用于结构回归。
//   **标志必须和数据一起绕开 gamma**(见文件末尾的 assign), 否则旁路模式下标志
//   走 2 拍、数据走 1 拍, 相对错拍被改 1 拍, 输出会整条平移一个字。
//   实测踩过这个坑, 症状是 byp[j] == base[j+1]。
//
// 既有隐患(不是本模块引入的): debayer_top_2to1.v:83-87 把 line_buffer 的
//   .i_de() 和 .i_valid() 接到了同一个信号上, 所以本模块要求
//   raw_de_i == raw_valid_i (本 tb 满足)。
//
// TODO: 三个增益需要外部软件产生(整帧每通道均值统计 + 寄存器写入路径)。
//       本模块只做乘法通路与钳位, 不负责算增益。增益逐帧静态, 场景突变时会滞后一帧。
/////////////////////////////////////////////////////////////////////////////

module debayer_top_wbg_2to1
#(
	parameter	GAMMA_BYPASS	= 1'b0		// 1 = 旁路 gamma 表(结构回归用)
)
(
	input					in_pclk,
	input					in_rstn,

	input					raw_vs_i,
	input					raw_hs_i,
	input					raw_de_i,
	input					raw_valid_i,
	input	[15:0]			raw_datax4_i,

	input	[15:0]			wb_r_gain,	// Q2.14, 1.0 = 16384
	input	[15:0]			wb_g_gain,
	input	[15:0]			wb_b_gain,

	output					rgb_vs_o,
	output					rgb_hs_o,
	output					rgb_de_o,
	output					rgb_valid_o,
	output	[15:0]			rgb_bout,
	output	[15:0]			rgb_gout,
	output	[15:0]			rgb_rout,
	output	[24*2-1:0]		rgb_datax2_o	// b,g,r,b,g,r
);

	localparam	P_DEPTH		= 8;
	localparam	OUT_FRAC	= 1;
	localparam	OW			= P_DEPTH + OUT_FRAC;	// 每像素 9 位 Q8.1

	// ------------------------------------------------------------------
	// 第 1 级: 原有 demosaic 顶层
	//   rgb_gain_v1 硬连成 Q8.8 的 16'h0100 (= 1.0)。这条路径逐位精确、零代价:
	//   v*256>>8 == v, 且 v <= 255 时不会触发它的饱和分支。
	// ------------------------------------------------------------------
	wire			w_dm_vs;
	wire			w_dm_hs;
	wire			w_dm_de;
	wire			w_dm_valid;
	wire	[15:0]	w_dm_r;
	wire	[15:0]	w_dm_g;
	wire	[15:0]	w_dm_b;
	wire	[47:0]	w_dm_datax2;		// 不用: 下面从处理后的 R/G/B 重算

	debayer_top_2to1
	inst_debayer
	(
		.in_pclk		(in_pclk		),
		.in_rstn		(in_rstn		),

		.raw_vs_i		(raw_vs_i		),
		.raw_hs_i		(raw_hs_i		),
		.raw_de_i		(raw_de_i		),
		.raw_valid_i	(raw_valid_i	),
		.raw_datax4_i	(raw_datax4_i	),

		.r_gain			(16'h0100		),
		.g_gain			(16'h0100		),
		.b_gain			(16'h0100		),

		.rgb_vs_o		(w_dm_vs		),
		.rgb_hs_o		(w_dm_hs		),
		.rgb_de_o		(w_dm_de		),
		.rgb_valid_o	(w_dm_valid		),
		.rgb_bout		(w_dm_b			),
		.rgb_gout		(w_dm_g			),
		.rgb_rout		(w_dm_r			),
		.rgb_datax2_o	(w_dm_datax2	)
	);
	// assign	rgb_vs_o		= w_dm_vs;
	// assign	rgb_hs_o		= w_dm_hs;
	// assign	rgb_de_o		= w_dm_de;
	// assign	rgb_valid_o		= w_dm_valid;
	// assign	rgb_bout		= w_dm_b;
	// assign	rgb_gout		= w_dm_g;
	// assign	rgb_rout		= w_dm_r;

	// 必须从**处理后的** R/G/B 重算。若透传 debayer_top_2to1 的 rgb_datax2_o,
	// 这条输出会绕过 WB+gamma, 与 rgb_rout/gout/bout 静默不一致。
	// assign	rgb_datax2_o	= {	w_dm_b[15:8], w_dm_g[15:8], w_dm_r[15:8],
	// 							w_dm_b[ 7:0], w_dm_g[ 7:0], w_dm_r[ 7:0] };
	// ------------------------------------------------------------------
	// 第 2 级: 三通道白平衡 (Q2.14), 输出每像素 9 位 Q8.1
	//   标志与数据从这里开始一起走, 相对错拍不变(见文件头说明)
	// ------------------------------------------------------------------
	wire			w_wb_vs;
	wire			w_wb_hs;
	wire			w_wb_de;
	wire			w_wb_valid;
	wire	[2*OW-1:0]	w_wb_r;
	wire	[2*OW-1:0]	w_wb_g;
	wire	[2*OW-1:0]	w_wb_b;

	wb_gain_3ch
	#(
		.P_DEPTH	(P_DEPTH),
		.GAIN_FRAC	(14		),
		.OUT_FRAC	(OUT_FRAC)
	)
	inst_wb_gain
	(
		.i_pclk		(in_pclk	),
		.i_arstn	(in_rstn	),

		.i_vs		(w_dm_vs	),
		.i_hs		(w_dm_hs	),
		.i_de		(w_dm_de	),
		.i_valid	(w_dm_valid	),
		.i_r		(w_dm_r		),
		.i_g		(w_dm_g		),
		.i_b		(w_dm_b		),

		.wb_r_gain	(wb_r_gain	),
		.wb_g_gain	(wb_g_gain	),
		.wb_b_gain	(wb_b_gain	),

		.o_vs		(w_wb_vs	),
		.o_hs		(w_wb_hs	),
		.o_de		(w_wb_de	),
		.o_valid	(w_wb_valid	),
		.o_r		(w_wb_r		),
		.o_g		(w_wb_g		),
		.o_b		(w_wb_b		)
	);

	// ------------------------------------------------------------------
	// 第 3 级: gamma 表 (固定, 逐通道相同)
	// ------------------------------------------------------------------
	wire			w_gm_vs;
	wire			w_gm_hs;
	wire			w_gm_de;
	wire			w_gm_valid;
	wire	[15:0]	w_gm_r;
	wire	[15:0]	w_gm_g;
	wire	[15:0]	w_gm_b;

	gamma_lut_v1
	#(
		.P_DEPTH	(P_DEPTH),
		.IN_FRAC	(OUT_FRAC)
	)
	inst_gamma
	(
		.i_pclk		(in_pclk	),
		.i_arstn	(in_rstn	),

		.i_vs		(w_wb_vs	),
		.i_hs		(w_wb_hs	),
		.i_de		(w_wb_de	),
		.i_valid	(w_wb_valid	),
		.i_r		(w_wb_r		),
		.i_g		(w_wb_g		),
		.i_b		(w_wb_b		),

		.o_vs		(w_gm_vs	),
		.o_hs		(w_gm_hs	),
		.o_de		(w_gm_de	),
		.o_valid	(w_gm_valid	),
		.o_r		(w_gm_r		),
		.o_g		(w_gm_g		),
		.o_b		(w_gm_b		)
	);

	// 旁路路径: 把 Q8.1 的小数位丢掉, 取整数部分 k[8:1] 拼回 8 位
	//   {高字节像素[8:1], 低字节像素[8:1]}
	wire [15:0]	w_byp_r = { w_wb_r[2*OW-1:OW+OUT_FRAC], w_wb_r[OW-1:OUT_FRAC] };
	wire [15:0]	w_byp_g = { w_wb_g[2*OW-1:OW+OUT_FRAC], w_wb_g[OW-1:OUT_FRAC] };
	wire [15:0]	w_byp_b = { w_wb_b[2*OW-1:OW+OUT_FRAC], w_wb_b[OW-1:OUT_FRAC] };

	// GAMMA_BYPASS 是 parameter, 综合时会被常量折叠掉, 不留 mux。
	//
	// 注意旁路模式丢小数位: 增益恰为 16384 时 WB 输出 k = 2v 精确, k[8:1] == v,
	// 所以结构回归能逐字节对上 outrgb.hex。增益不等于 1.0 时旁路会截断小数
	// (最多差 1 级), 那是 debug 路径的预期行为, 不要拿它当真值。
	// wire [15:0]	w_out_r = GAMMA_BYPASS ? w_byp_r : w_gm_r;
	// wire [15:0]	w_out_g = GAMMA_BYPASS ? w_byp_g : w_gm_g;
	// wire [15:0]	w_out_b = GAMMA_BYPASS ? w_byp_b : w_gm_b;

	// *** 标志必须跟着数据一起绕开 gamma, 不能一直用 gamma 的输出 ***
	// gamma_lut_v1 给标志也打了一拍。如果数据绕开它(走上面的 w_byp_*)而标志不绕,
	// 旁路模式下数据只延迟 1 拍、标志延迟 2 拍, 相对错拍被改动 1 拍, 采集窗口整体
	// 错开一个字 —— 实测症状就是 byp[j] == base[j+1] (整条流平移一个字)。
	// 本文件头强调过"不要改动这个错拍", 这里就是最容易无意中改掉它的地方。
	assign	rgb_vs_o		= GAMMA_BYPASS ? w_wb_vs	: w_gm_vs;
	assign	rgb_hs_o		= GAMMA_BYPASS ? w_wb_hs	: w_gm_hs;
	assign	rgb_de_o		= GAMMA_BYPASS ? w_wb_de	: w_gm_de;
	assign	rgb_valid_o		= GAMMA_BYPASS ? w_wb_valid	: w_gm_valid;
	assign	rgb_bout		= GAMMA_BYPASS ? w_byp_b    : w_gm_b;
	assign	rgb_gout		= GAMMA_BYPASS ? w_byp_g    : w_gm_g;
	assign	rgb_rout		= GAMMA_BYPASS ? w_byp_r    : w_gm_r;

	// // 必须从**处理后的** R/G/B 重算。若透传 debayer_top_2to1 的 rgb_datax2_o,
	// // 这条输出会绕过 WB+gamma, 与 rgb_rout/gout/bout 静默不一致。
	assign	rgb_datax2_o	= {	rgb_bout[15:8], rgb_gout[15:8], rgb_rout[15:8],
								rgb_bout[ 7:0], rgb_gout[ 7:0], rgb_rout[ 7:0] };
endmodule
