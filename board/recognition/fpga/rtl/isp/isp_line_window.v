////////////////////////////////////////////////////////////////////////////////
// isp_line_window.v  ——  5 行 x 3 字 滑动窗 (2 像素/字)
//
//  给 DPC / RAW-NR (5x5 同色邻域) 和 Demosaic (3x3) 提供同一组窗数据。
//
//  结构
//    5 个单读口 RAM ("slot"), 每个存一整行(WORDS 个字, 20bit/字)。
//    第 L 行写进 slot = L%5; 同时从 slot (L-1)%5 .. (L-4)%5 读前 4 行。
//    5 > 4, 所以"正在写的槽"永远不会和要求读出的 4 个槽撞车 —— 这是槽数不能
//    取 4 的原因(4 槽时 (L-4)%4 == L%4, 会把当前行自己读进来)。
//
//  时序对齐(对不上就会整幅错位, 改这里务必重新跑 tb)
//    列计数 col 从 0 数到 WORDS(含)。每个输入字在 col=c 这一拍被消费:
//        直接通道(当前行)  : right = data_i(=w_c)  , center = d0(=w_{c-1}), left = d1(=w_{c-2})
//        RAM 通道(前 4 行) : right = rd*   (=w_c)  , center = r0(=w_{c-1}), left = r1(=w_{c-2})
//    因为 RAM 的 dout 比地址晚 1 拍, 读地址取 min(col+1, WORDS-1) 正好抵消这 1 拍,
//    两条通道的 w_c / w_{c-1} / w_{c-2} 就严格对齐了。
//    本拍输出的中心字是第 k = col-1 个字 -> 有效输出区间 col = 1..WORDS。
//
//  边界填充 —— 与 isp_matlab/isp_core.m 的 pad_pixel() 严格一致
//    列: k==0 时左邻居替换为 {p0,p0};  k==WORDS-1 时右邻居替换为 {p_last,p_last}
//        => 左右各复制 2 个像素(正好一个字)。
//    行: 越界行号先钳到 [0, H_ACTIVE-1] 再取 slot, 等价于上下各复制 2 行。
//        帧首把 sl1..sl4 清零, 恰好等于"上边界复制第 0 行"(row 0 的 slot 就是 0)。
//
//  帧结构
//    每帧跑 H_ACTIVE 个真实行 + 2 个 flush 行。flush 期间不出输入、不写 RAM,
//    只把最后 2 行的结果推出去。这样输出行号 row_o = line-2 正好是 0..H_ACTIVE-1
//    顺序输出, 不会出现"首尾颠倒 / 画面整体下移 2 行"的问题。
//
//  流控
//    本模块每行需要 WORDS+1 个时钟, 而源每行只给 WORDS 个字, 多的 1 拍靠行消隐吸收。
//    只要源的消隐 >= 1 个字时钟就没问题; 若源的消隐为 0, 请用 in_req_o 做流控。
////////////////////////////////////////////////////////////////////////////////
`include "isp_params.vh"

module isp_line_window #(
    parameter integer WORDS    = `ISP_WORDS,
    parameter integer H_ACTIVE = `ISP_H_ACTIVE,
    parameter integer AW       = 10                     // 地址位宽, 2^AW >= WORDS
)(
    input  wire         clk,
    input  wire         rst_n,

    input  wire         frame_start_i,  // 与帧首第一个字同拍的一拍脉冲
    input  wire         valid_i,        // 输入字有效
    input  wire [19:0]  data_i,         // {奇像素[9:0], 偶像素[9:0]}
    output wire         in_req_o,       // 本拍可以接收一个字(供源做流控)

    output reg          valid_o,
    output reg          frame_start_o,  // 输出帧首(行 0 的第一个字)
    output reg  [9:0]   col_o,          // 中心字列号 k = 0..WORDS-1
    output reg  [11:0]  row_o,          // 输出行号 0..H_ACTIVE-1
    output wire [299:0] win_o           // 5行 x 3字, row0=line-4 ... row4=line
);

    localparam [1:0] S_IDLE = 2'd0, S_RUN = 2'd1, S_FLUSH = 2'd2;

    localparam [AW:0]   COL_MAX   = WORDS;              // 列计数上界(含)
    localparam [AW-1:0] LAST_ADDR = WORDS - 1;
    localparam [11:0]   H_LAST    = H_ACTIVE - 1;
    localparam [11:0]   H_ACT     = H_ACTIVE;

    reg  [1:0]  state;
    reg  [AW:0] col;                    // 0..WORDS
    reg  [11:0] line;                   // 输入行号, flush 期间到 H_ACTIVE+1
    reg  [1:0]  flush_cnt;

    reg  [2:0]  sl0, sl1, sl2, sl3, sl4;    // slK 存放"第 line-K 行"所在的 slot

    wire        line_end = (col == COL_MAX);
    wire        run      = (state == S_RUN) || frame_start_i;
    wire        take     = run && valid_i && (col < COL_MAX);   // 本拍消费一个输入字

    //==========================================================================
    // 行 / 列 计数 与 状态机
    //==========================================================================
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state     <= S_IDLE;
            col       <= 0;
            line      <= 12'd0;
            flush_cnt <= 2'd0;
        end else begin
            case (state)
            //------------------------------------------------------------------
            S_IDLE: begin
                if (frame_start_i) begin
                    state     <= S_RUN;
                    col       <= 1;         // 帧首那一拍已经消费掉 w0
                    line      <= 12'd0;
                    flush_cnt <= 2'd0;      // 必须每帧清零, 否则第 2 帧起只跑 1 个 flush 行
                end
            end
            //------------------------------------------------------------------
            S_RUN: begin
                if (line_end) begin
                    col  <= 0;
                    line <= line + 12'd1;
                    if (line == H_LAST) state <= S_FLUSH;
                end else if (valid_i) begin
                    col <= col + 1;
                end
            end
            //------------------------------------------------------------------
            S_FLUSH: begin
                if (line_end) begin
                    col  <= 0;
                    line <= line + 12'd1;
                    if (flush_cnt == 2'd1) state <= S_IDLE;
                    else                   flush_cnt <= flush_cnt + 2'd1;
                end else begin
                    col <= col + 1;
                end
            end
            //------------------------------------------------------------------
            default: state <= S_IDLE;
            endcase
        end
    end

    //==========================================================================
    // slot 轮转:  写 slot = line%5, 每个行末整体前移一格
    //==========================================================================
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            sl0 <= 3'd0;  sl1 <= 3'd0;  sl2 <= 3'd0;  sl3 <= 3'd0;  sl4 <= 3'd0;
        end else if (frame_start_i && state == S_IDLE) begin
            // 帧首清零 = 上边界复制第 0 行(row 0 的 slot 就是 0)
            sl0 <= 3'd0;  sl1 <= 3'd0;  sl2 <= 3'd0;  sl3 <= 3'd0;  sl4 <= 3'd0;
        end else if (line_end && state != S_IDLE) begin
            sl0 <= (sl0 == 3'd4) ? 3'd0 : (sl0 + 3'd1);
            sl1 <= sl0;   sl2 <= sl1;   sl3 <= sl2;   sl4 <= sl3;
        end
    end

    //==========================================================================
    // RAM 读写端口
    //==========================================================================
    wire          wr_en   = take;
    wire [2:0]    sl0_eff = frame_start_i ? 3'd0 : sl0;
    wire [4:0]    we_dec  = wr_en ? (5'd1 << sl0_eff) : 5'd0;
    wire [AW-1:0] wr_addr = col[AW-1:0];

    //--------------------------------------------------------------------------
    // 读地址 —— 改动前务必重跑 isp_matlab/verify_line_window.m
    //
    //   RAM 的 dout 比地址晚 1 拍, 而 center/left 又是它在寄存器链上再晚 1/2 拍,
    //   所以 col=c 那拍看到的三个字分别是 addr(c-1)/addr(c-2)/addr(c-3) 读出来的。
    //   要让它们等于 w_c / w_{c-1} / w_{c-2}, 三个条件等价于同一个式子:
    //       addr(c) = c + 1                (并钳到 LAST_ADDR)
    //
    //   难的是行首。col=1 那拍要求:
    //       right  = rd(1)  = ram[addr(col=0)]   = w1   -> col=0 那拍地址必须是 1
    //       center = r0(1)  = rd(0) = ram[addr(col=0 的上一拍)] = w0  -> 上一拍必须是 0
    //   也就是"col=0 那拍的地址取 1, 它前面那拍取 0"。
    //
    //   于是要区分 col=0 的两种含义:
    //     - 这一拍过后列计数就变 1 (S_RUN 里正在 take, 或 S_FLUSH 里列计数自由跑)
    //         -> 地址 = 1   (给下一拍的 right 用)
    //     - 只是行消隐里多出来的空拍
    //         -> 地址 = 0   (把 ram[0] 顶进流水线, 给下一拍的 center 用)
    //   另外行末那拍 col=COL_MAX 也取 0: 它只影响下一个时钟, 用来给下一行做 priming
    //   (这样即使没有行消隐、或者这一行是 flush 行, 也能对齐)。
    //
    //   少了这些 0, 每行第一列的历史行中心字会取成别的字, 画面最左/最右各 2 列
    //   颜色是错的 —— 而且 flush 行和普通行错法还不一样, 很难从图像上看出来。
    //--------------------------------------------------------------------------
    localparam [AW-1:0] ADDR_ZERO = {AW{1'b0}};
    localparam [AW-1:0] ADDR_ONE  = {{(AW-1){1'b0}}, 1'b1};

    wire col0_step = (state == S_FLUSH) || take;   // col=0 且这一拍过后 col 就变 1

    wire [AW-1:0] rd_addr = (col == 0)          ? (col0_step ? ADDR_ONE : ADDR_ZERO)
                          : (col == COL_MAX)    ? ADDR_ZERO
                          : (col >= LAST_ADDR)  ? LAST_ADDR
                          :                       (col[AW-1:0] + 1'b1);

    assign in_req_o = (state == S_RUN) && (col < COL_MAX);

    wire [19:0] rd0, rd1, rd2, rd3, rd4;

    true_dual_port_ram #(.DATA_WIDTH(20), .ADDR_WIDTH(AW),
        .WRITE_MODE_1("NO_CHANGE"), .WRITE_MODE_2("READ_FIRST"),
        .OUTPUT_REG_1("FALSE"), .OUTPUT_REG_2("FALSE"), .RAM_INIT_FILE(""))
    u_ram0 (.we1(1'b0), .we2(we_dec[0]), .clka(clk), .clkb(clk),
            .din1(20'd0), .din2(data_i), .addr1(rd_addr), .addr2(wr_addr),
            .dout1(rd0), .dout2());

    true_dual_port_ram #(.DATA_WIDTH(20), .ADDR_WIDTH(AW),
        .WRITE_MODE_1("NO_CHANGE"), .WRITE_MODE_2("READ_FIRST"),
        .OUTPUT_REG_1("FALSE"), .OUTPUT_REG_2("FALSE"), .RAM_INIT_FILE(""))
    u_ram1 (.we1(1'b0), .we2(we_dec[1]), .clka(clk), .clkb(clk),
            .din1(20'd0), .din2(data_i), .addr1(rd_addr), .addr2(wr_addr),
            .dout1(rd1), .dout2());

    true_dual_port_ram #(.DATA_WIDTH(20), .ADDR_WIDTH(AW),
        .WRITE_MODE_1("NO_CHANGE"), .WRITE_MODE_2("READ_FIRST"),
        .OUTPUT_REG_1("FALSE"), .OUTPUT_REG_2("FALSE"), .RAM_INIT_FILE(""))
    u_ram2 (.we1(1'b0), .we2(we_dec[2]), .clka(clk), .clkb(clk),
            .din1(20'd0), .din2(data_i), .addr1(rd_addr), .addr2(wr_addr),
            .dout1(rd2), .dout2());

    true_dual_port_ram #(.DATA_WIDTH(20), .ADDR_WIDTH(AW),
        .WRITE_MODE_1("NO_CHANGE"), .WRITE_MODE_2("READ_FIRST"),
        .OUTPUT_REG_1("FALSE"), .OUTPUT_REG_2("FALSE"), .RAM_INIT_FILE(""))
    u_ram3 (.we1(1'b0), .we2(we_dec[3]), .clka(clk), .clkb(clk),
            .din1(20'd0), .din2(data_i), .addr1(rd_addr), .addr2(wr_addr),
            .dout1(rd3), .dout2());

    true_dual_port_ram #(.DATA_WIDTH(20), .ADDR_WIDTH(AW),
        .WRITE_MODE_1("NO_CHANGE"), .WRITE_MODE_2("READ_FIRST"),
        .OUTPUT_REG_1("FALSE"), .OUTPUT_REG_2("FALSE"), .RAM_INIT_FILE(""))
    u_ram4 (.we1(1'b0), .we2(we_dec[4]), .clka(clk), .clkb(clk),
            .din1(20'd0), .din2(data_i), .addr1(rd_addr), .addr2(wr_addr),
            .dout1(rd4), .dout2());

    //==========================================================================
    // 历史行: rd* 就是"本拍中心字所在的 right", 再打 1/2 拍得到 center/left
    //   rd*  = w_c      -> right
    //   r0*  = w_{c-1}  -> center
    //   r1*  = w_{c-2}  -> left
    //==========================================================================
    reg [19:0] r00, r01, r02, r03, r04;
    reg [19:0] r10, r11, r12, r13, r14;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            r00<=20'd0; r01<=20'd0; r02<=20'd0; r03<=20'd0; r04<=20'd0;
            r10<=20'd0; r11<=20'd0; r12<=20'd0; r13<=20'd0; r14<=20'd0;
        end else begin
            r00 <= rd0;  r01 <= rd1;  r02 <= rd2;  r03 <= rd3;  r04 <= rd4;
            r10 <= r00;  r11 <= r01;  r12 <= r02;  r13 <= r03;  r14 <= r04;
        end
    end

    //==========================================================================
    // 当前行(直接通道)的延迟
    //   center = d0 = w_{c-1} ,  left = d1 = w_{c-2} ,  right = data_i = w_c
    //==========================================================================
    reg [19:0] d0, d1, d2;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            d0 <= 20'd0;  d1 <= 20'd0;  d2 <= 20'd0;
        end else if (take) begin
            d0 <= data_i;  d1 <= d0;  d2 <= d1;
        end
    end

    //==========================================================================
    // 行号钳位 -> 窗口每一行取哪个 slot
    //   fl1 : line == H_ACTIVE   (flush 第 1 行) -> 行 H   用行 H-1 的 slot
    //   fl2 : line >  H_ACTIVE   (flush 第 2 行) -> 行 H/H+1 都用行 H-1 的 slot
    //==========================================================================
    wire fl1   = (line == H_ACT);
    wire fl2   = (line >  H_ACT);
    wire flush = fl1 | fl2;

    wire [2:0] wsl4 = sl4;                              // 行 line-4
    wire [2:0] wsl3 = sl3;                              // 行 line-3
    wire [2:0] wsl2 = sl2;                              // 行 line-2  (中心行)
    wire [2:0] wsl1 = fl2 ? sl2 : sl1;                  // 行 line-1
    wire [2:0] wsl0 = fl1 ? sl1 : (fl2 ? sl2 : 3'd0);   // 行 line

    //==========================================================================
    // 5 选 1
    //==========================================================================
    function [19:0] sel5;
        input [2:0]  s;
        input [99:0] v;
        begin
            case (s)
                3'd0:    sel5 = v[19:0];
                3'd1:    sel5 = v[39:20];
                3'd2:    sel5 = v[59:40];
                3'd3:    sel5 = v[79:60];
                3'd4:    sel5 = v[99:80];
                default: sel5 = 20'd0;
            endcase
        end
    endfunction

    wire [99:0] bus_ra = {rd4, rd3, rd2, rd1, rd0};   // right
    wire [99:0] bus_r0 = {r04, r03, r02, r01, r00};   // center
    wire [99:0] bus_r1 = {r14, r13, r12, r11, r10};   // left

    //---- 4 个历史行的原始 (left/center/right) ----
    wire [19:0] h4l = sel5(wsl4, bus_r1), h4c = sel5(wsl4, bus_r0), h4r = sel5(wsl4, bus_ra);
    wire [19:0] h3l = sel5(wsl3, bus_r1), h3c = sel5(wsl3, bus_r0), h3r = sel5(wsl3, bus_ra);
    wire [19:0] h2l = sel5(wsl2, bus_r1), h2c = sel5(wsl2, bus_r0), h2r = sel5(wsl2, bus_ra);
    wire [19:0] h1l = sel5(wsl1, bus_r1), h1c = sel5(wsl1, bus_r0), h1r = sel5(wsl1, bus_ra);

    //---- 当前行: 正常走直接通道, flush 时从 slot 取 ----
    wire [19:0] c_l = flush ? sel5(wsl0, bus_r1) : d1;
    wire [19:0] c_c = flush ? sel5(wsl0, bus_r0) : d0;
    wire [19:0] c_r = flush ? sel5(wsl0, bus_ra) : data_i;

    //==========================================================================
    // 列边界: 左邻居 / 右邻居替换 (等价于左右各复制 2 个像素)
    //==========================================================================
    wire k_first = (col == 1);
    wire k_last  = (col == COL_MAX);

    wire [19:0] w4l = k_first ? {h4c[9:0],  h4c[9:0]}  : h4l,  w4c = h4c,
                w4r = k_last  ? {h4r[19:10],h4r[19:10]}: h4r;
    wire [19:0] w3l = k_first ? {h3c[9:0],  h3c[9:0]}  : h3l,  w3c = h3c,
                w3r = k_last  ? {h3r[19:10],h3r[19:10]}: h3r;
    wire [19:0] w2l = k_first ? {h2c[9:0],  h2c[9:0]}  : h2l,  w2c = h2c,
                w2r = k_last  ? {h2r[19:10],h2r[19:10]}: h2r;
    wire [19:0] w1l = k_first ? {h1c[9:0],  h1c[9:0]}  : h1l,  w1c = h1c,
                w1r = k_last  ? {h1r[19:10],h1r[19:10]}: h1r;
    wire [19:0] w0l = k_first ? {c_c[9:0],   c_c[9:0]} : c_l,  w0c = c_c,
                w0r = k_last  ? {c_r[19:10], c_r[19:10]} : c_r;

    // win_o[(r*3+col)*20 +: 20] ,  r=0 -> line-4 ... r=4 -> line
    assign win_o = { w0r, w0c, w0l,
                     w1r, w1c, w1l,
                     w2r, w2c, w2l,
                     w3r, w3c, w3l,
                     w4r, w4c, w4l };

    //==========================================================================
    // 输出同步信号 (与 win_o 同拍)
    //==========================================================================
    always @(*) begin
        valid_o       = (state != S_IDLE) && (col >= 1) && (line >= 12'd2);
        frame_start_o = valid_o && (line == 12'd2) && (col == 1);
        col_o         = (col >= 1) ? (col[9:0] - 10'd1) : 10'd0;
        row_o         = line - 12'd2;
    end

endmodule
