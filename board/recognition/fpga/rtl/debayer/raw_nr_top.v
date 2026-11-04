`timescale 1ns/1ps
/////////////////////////////////////////////////////////////////////////////
// raw_nr_top.v —— RAW 域同色保边降噪 (NR) 的协议适配层
//
// 位置: 插在 raw_data_blc **之后**、debayer_top_wbg_2to1 **之前**:
//         csi_rx -> raw_data_blc -> [ raw_nr_top ] -> debayer_top_wbg_2to1
//
// 为什么放在白平衡之前: 本工程的 WB 是 demosaic **之后**的 RGB 域增益
//   (wb_gain_3ch, Q2.14)。所以降噪这一级的噪声幅度是传感器原生单位, 平坦判据
//   阈值可以是一个不随增益变化的常数 P_NR_TH。反过来, 兄弟工程
//   Ti60f225_sc431hai2ge_capture_v8 的 AWB 在 RAW 域、降噪之前, 那边的
//   NR_TH 是 21/23/45 —— 那是**已乘过 AWB 增益**的值。照抄那三个数到这里会
//   把阈值放大 3~4 倍, 平坦判据几乎恒真, 整幅被抹平。两边单位不同。
//
// ---------------------------------------------------------------------------
// 本模块**不含算法**, 只做协议转换。算法原样来自:
//     rtl/isp/isp_line_window.v   5 行 x 3 字 滑动窗 (逐字复制)
//     rtl/isp/isp_dpc_nr.v        DPC + NR 同色邻域 (逐字复制)
//   这两个文件与 isp_matlab/isp_core.m 的 RAWNR 已经逐位对齐过, 且
//   isp_matlab/lw_model.m 是 isp_line_window 的逐拍黄金模型
//   (verify_line_window.m 验证)。**不要再写第二份窗口实现** —— lw_model.m
//   的文件头明确记着: 之前就是因为有两份拷贝, 其中一份把 wr_addr 算在了 col
//   更新之后, 白查了半天。
//
// ---------------------------------------------------------------------------
// 协议转换
//   输入(本工程约定): i_vs/i_hs 电平, i_valid 有效, i_data 每拍一个字(2 像素)
//   中间(isp 链约定): {valid, sof, col, row} 24bit + win_o[299:0]
//   输出(本工程约定): o_vs 电平 / o_hs 脉冲 / o_de == o_valid / o_data
//
//   *** 为什么 o_de 和 o_valid 必须同源 ***
//   debayer_top_2to1.v:83-84 把 line_buffer 的 .i_de 和 .i_valid 接到了同一根
//   网线上, 所以本工程约定 raw_de_i == raw_valid_i。这里两个输出都接
//   dnr_valid, 天然满足。
//
// ---------------------------------------------------------------------------
// *** 关于 vs/hs: 不能换成 frame_start_o 那种窄脉冲 ***
//   rgb_vs 一路被送去做 HDMI 的场同步**电平**(ti60f225_oob_top.v:875 的
//   hdmi_tx_vs <= rgb_vs), 另一路才做 pos_vs 的上升沿检测。换成 1 拍脉冲会把
//   HDMI 的 vs 变成每帧一个毛刺。所以这里把 vs 做回"有效视频期低、消隐期高"
//   的电平, 由输出帧的首/末自锁得到 —— 它与输出数据严格同拍, 比原始 ch0_vs
//   晚 2 行(因为窗口模块本来就把画面推迟了 2 行, 这正是对的)。
//
//   下游对 vs 的实际依赖(已核):
//     line_buffer.v:144    vs 上升沿复位地址   —— 每帧一次, 对极性不敏感
//     raw_to_rgb.v:172     vs 下降沿复位 r_y_cnt —— 每帧一次, 对极性不敏感
//     raw_to_rgb.v:166-167 的 neg_vs/pos_hs 是**死代码**, 声明了没用到
//     raw_to_rgb.v:167/441 只管把 i_hsync 寄存透传, i_hsync 不参与任何计数
//   所以 vs 只要"每帧恰好一个上升沿+一个下降沿, 且落在消隐期"即可。
//   hs 功能上无人使用(同上), 这里生成一个每行起点的单拍脉冲, 只为波形合理。
//
// ---------------------------------------------------------------------------
// 数据约定: {高字节像素, 低字节像素}, 低字节 = 偶列。与 raw_data_blc.v:83-84、
//   raw_to_rgb、wb_gain_3ch 一致。
//   isp_line_window / isp_dpc_nr 内部是 10bit/像素、{奇像素[9:0], 偶像素[9:0]},
//   本模块负责 8bit <-> 10bit 的零扩展与拆装。上下各 2 位恒 0, 不损失信息。
//
// ---------------------------------------------------------------------------
// 时延: 输入字 -> 输出字 = 1 拍(读出对齐) + 1 拍(isp_dpc_nr 的输出寄存器)
//       , 外加 2 行的行缓冲推迟。标志与数据全部取自 isp_dpc_nr 的寄存输出,
//       所以四根标志与 o_data **严格同拍**, 不引入任何相对错拍。
//       (debayer 内部那个"标志比数据早 1 拍"的错拍是 raw_to_rgb 自己产生的,
//       与本模块无关, 也不会被本模块改动 —— 见 debayer_top_wbg_2to1.v:15-27)
//
// ---------------------------------------------------------------------------
// 流控: isp_line_window.v:31-33 说本模块每行要 WORDS+1 个时钟, 源每行只给
//   WORDS 个字, 多出的 1 拍靠行消隐吸收。本工程的 ch0_de 有行消隐, 所以
//   in_req_o 悬空即可。若将来换成无行消隐的源, 必须用 in_req_o 反压。
//   同理帧末 2 个 flush 行要占用约 2*(WORDS+1) = 1922 个时钟, 需要 ch0_vs 的
//   垂直消隐够长(1080p 标准消隐约 45 行, 远大于此)。
//
// TODO: DPC 本轮关闭(P_DPC_TH = 16'hFFFF): bad = (ce > nmax + 65535) 在 10bit
//       域恒为假。要启用坏点校正就把 P_DPC_TH 改成 32 一类的真实阈值, 算法
//       本身就在 isp_dpc_nr.v 里原样躺着, 不用改结构。
// TODO: NR_TH 三通道取同一个值, 是为了绕开 CFA 相位问题 —— isp_dpc_nr.v 里
//       "通道 -> 阈值" 那段的映射是按 GBRG 写的(偶行偶像素=G), 而本工程的
//       rawdata 是 BGGR。阈值三通道相同时这个映射就无关紧要。将来要分通道给
//       阈值, 必须先把两边的 CFA 相位对齐。
//
// 注: P_NR_EDGE (强边缘否决门限) 是新加的 —— 只靠逐邻居相似度会在边缘斜坡上
//     抹掉 1~2 LSB, 必须有一道全局否决。取值依据和实测见 isp_dpc_nr.v 文件头。
//     它是标量, 不分通道, 所以不涉及上面的 CFA 相位问题。
/////////////////////////////////////////////////////////////////////////////

module raw_nr_top
#(
	// parameter integer		P_BYPASS	= 0,			// 1 = 只走延迟链不做滤波(结构回归用)
	parameter [15:0]		P_NR_TH		= 16'd6,		// 平坦判据阈值, 单位 RAW 8bit LSB
	parameter [15:0]		P_NR_EDGE	= 16'd16,		// 强边缘否决门限(见 isp_dpc_nr.v 文件头)
	parameter [15:0]		P_DPC_TH	= 16'hFFFF,		// 0xFFFF = DPC 关闭
	parameter integer		WORDS		= 960,			// 每行字数(2 像素/字)
	parameter integer		H_ACTIVE	= 1080,
	parameter integer		AW			= 10			// RAM 地址位宽, 2^AW >= WORDS
)
(
	input						i_pclk,
	input						i_arstn,		// 异步复位, 低有效(与 isp_line_window 一致)
	input  						P_BYPASS,
	input						i_vs,
	input						i_hs,
	input						i_valid,		// 输入字有效, 顶层接 ch0_de
	input	[15:0]				i_data,			// {高字节像素, 低字节像素}

	output						o_vs,
	output						o_hs,
	output						o_de,			// 与 o_valid 同源, 见文件头
	output						o_valid,
	output	[15:0]				o_data
);

	localparam [11:0]	H_LAST	= H_ACTIVE - 1;
	localparam [9:0]	W_LAST	= WORDS - 1;

	//======================================================================
	// 1. 帧首脉冲 frame_start_i
	//
	// isp_line_window 要的是"与帧首第一个有效字**同拍**的一拍脉冲":
	//   isp_line_window.v:88-93 在 S_IDLE 收到 frame_start_i 就当 col=1,
	//   文件里注明"帧首那一拍已经消费掉 w0" —— 也就是它和第一个字必须同拍。
	//   isp_line_window.v:72 的 run = (state==S_RUN) || frame_start_i, 配合
	//   :73 的 take 条件, 只有同拍才吃得到第一个字。
	//
	// 源这边只有 vs 电平(消隐期高), 没有现成的帧首脉冲, 所以:
	//   vs 下降沿 -> 挂起(arm) -> 等到的第一个有效字就是帧首
	//   vs 下降沿与第一个字同拍的情况也要吃得到, 所以 arm 的置位与消费
	//   在同一个表达式里判, 不让优先级把同拍那一拍吞掉。
	//======================================================================
	reg		r_vs_1P;

	always @(posedge i_pclk or negedge i_arstn)
	begin
		if (~i_arstn)
			r_vs_1P	<= 1'b1;				// 复位后先当作"在消隐里"
		else
			r_vs_1P	<= i_vs;
	end

	wire	w_vs_fall	= r_vs_1P & ~i_vs;	// vs 下降沿 = 有效视频开始

	reg		r_arm;
	wire	w_first_word= i_valid & (r_arm | w_vs_fall);

	always @(posedge i_pclk or negedge i_arstn)
	begin
		if (~i_arstn)
			r_arm	<= 1'b0;
		else if (w_first_word)
			r_arm	<= 1'b0;
		else if (w_vs_fall)
			r_arm	<= 1'b1;
	end

	//======================================================================
	// 2. 8bit <-> 10bit 打包
	//    isp_dpc_nr.v:43 的约定是 {奇像素[9:0], 偶像素[9:0]}, 奇像素在高位。
	//    本工程一个字里低字节 = 偶列, 高字节 = 奇列。上下各补 2 个 0。
	//======================================================================
	wire	[19:0]	w_win_in	= { 2'b0, i_data[15:8], 2'b0, i_data[7:0] };

	//======================================================================
	// 3. 5 行 x 3 字 滑动窗 (逐字复用的既有模块)
	//======================================================================
	wire			w_win_valid;
	wire			w_win_sof;
	wire	[9:0]	w_win_col;
	wire	[11:0]	w_win_row;
	wire	[299:0]	w_win;
	wire			w_in_req;					// 悬空: 源有行消隐, 无需反压

	isp_line_window
	#(
		.WORDS		(WORDS	),
		.H_ACTIVE	(H_ACTIVE),
		.AW			(AW		)
	)
	inst_line_window
	(
		.clk			(i_pclk			),
		.rst_n			(i_arstn		),

		.frame_start_i	(w_first_word	),
		.valid_i		(i_valid		),
		.data_i			(w_win_in		),
		.in_req_o		(w_in_req		),

		.valid_o		(w_win_valid	),
		.frame_start_o	(w_win_sof		),
		.col_o			(w_win_col		),
		.row_o			(w_win_row		),
		.win_o			(w_win		)
	);

	//======================================================================
	// 4. NR 算法 (逐字复用的既有模块)
	//    sync 打包: {valid, sof, col[9:0], row[11:0]}, 与 isp_dpc_nr.v:38 一致。
	//    NR_TH 三通道同值 -> isp_dpc_nr.v:117-118 的 CFA 相位映射变得无关紧要
	//    (本工程是 BGGR, 那边是按 GBRG 写的)。
	//======================================================================
	wire			w_dnr_valid;
	wire	[23:0]	w_dnr_sync;
	wire	[19:0]	w_dnr_data;

	isp_dpc_nr
	#(
		.DPC_TH_R	(P_DPC_TH	),
		.DPC_TH_G	(P_DPC_TH	),
		.DPC_TH_B	(P_DPC_TH	),
		.NR_TH_R	(P_NR_TH	),
		.NR_TH_G	(P_NR_TH	),
		.NR_TH_B	(P_NR_TH	),
		.NR_EDGE	(P_NR_EDGE	)
	)
	inst_dpc_nr
	(
		.clk		(i_pclk								),
		.rst_n		(i_arstn							),

		.valid_i	(w_win_valid						),
		.sync_i		({w_win_valid, w_win_sof, w_win_col, w_win_row}),
		.win_i		(w_win								),

		.valid_o	(w_dnr_valid						),
		.sync_o		(w_dnr_sync							),
		.data_o		(w_dnr_data							)
	);

	wire		w_dnr_sof	= w_dnr_sync[22];
	wire	[9:0]	w_dnr_col	= w_dnr_sync[21:12];
	wire	[11:0]	w_dnr_row	= w_dnr_sync[11:0];

	//======================================================================
	// 5. BYPASS: 取窗口的正中心像素(未滤波), 同样打 1 拍对齐 isp_dpc_nr 的输出寄存器。
	//    win_o[(r*3+c)*20 +: 20], 中心是 r=2, c=1 -> win_o[159:140]。
	//    这样 bypass 与正常模式**延迟完全相同**, 只 mux 数据, 不动标志。
	//======================================================================
	wire	[19:0]	w_ce_word	= w_win[159:140];

	reg		[19:0]	r_ce_word;
	always @(posedge i_pclk or negedge i_arstn)
	begin
		if (~i_arstn)
			r_ce_word	<= 20'd0;
		else
			r_ce_word	<= w_ce_word;
	end

	wire	[19:0]	w_out_word	= P_BYPASS ? r_ce_word : w_dnr_data;

	// 10bit -> 8bit: 高 2 位恒 0, 取 [17:10] / [7:0] 即回 8bit
	assign	o_data	= { w_out_word[17:10], w_out_word[7:0] };

	//======================================================================
	// 6. 输出标志 —— 全部取自 isp_dpc_nr 的寄存输出, 与 o_data 严格同拍
	//======================================================================
	assign	o_de	= w_dnr_valid;
	assign	o_valid	= w_dnr_valid;

	// hs: 每行第一个有效字的一拍脉冲。功能上无人使用(见文件头), 只为波形合理。
	assign	o_hs	= w_dnr_valid & (w_dnr_col == 10'd0);

	// vs: 有效视频期低、消隐期高。帧首拉低、帧末拉高, 每帧恰好一对边沿。
	reg		r_vs;

	always @(posedge i_pclk or negedge i_arstn)
	begin
		if (~i_arstn)
			r_vs	<= 1'b1;						// 复位后在"消隐"
		else if (w_dnr_sof)
			r_vs	<= 1'b0;						// 帧首 -> 进入有效视频
		else if (w_dnr_valid & (w_dnr_row == H_LAST) & (w_dnr_col == W_LAST))
			r_vs	<= 1'b1;						// 帧末 -> 回到消隐
	end

	assign	o_vs	= r_vs;

endmodule
