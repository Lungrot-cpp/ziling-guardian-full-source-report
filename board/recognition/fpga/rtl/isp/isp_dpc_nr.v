////////////////////////////////////////////////////////////////////////////////
// isp_dpc_nr.v  ——  坏点校正 (DPC) + RAW 域同色保边降噪 (NR)
//
//  两级共用同一组 5x5 窗寄存器(isp_line_window 的输出), 不用额外行缓冲 —— 这是
//  本设计把 DPC 和 NR 放在一个模块里的唯一原因。
//
//  窗里"同色"样本一共 9 个: 中心 + 上下左右(距离 2) + 4 个对角(距离 2, 即
//  (y±2, x±2))。任何 Bayer 相位下 (y±2, x±2) 都与中心同色 —— 行列奇偶各变化
//  2 次等于不变。(旧注释说 (y±1,x±1) 是同色对角, 那只在 GBRG 成立; 本工程的
//  rawdata 是 BGGR, 那里 (y±1,x±1) 是相反色度。反正它取不到, 无所谓。)
//
//  *** 2026-10-07: NR 从"5 点十字 + 二值门控"改成"9 点二项式 + 逐邻居相似度
//      + 强边缘否决", 与 isp_matlab/isp_core.m 不再逐位一致 ***
//
//      改的动机: 旧版只取 4 个正邻居, 把窗里另外 4 个对角样本(距离 2, 与中心
//      必定同色)白白浪费了; 而且旧版的 flat 判据是"5 样本极差 <= TH", 噪声一大
//      极差就超阈值, 最需要降噪的平坦像素反而第一个退出降噪。
//
//      但改法有讲究 —— 实测(rawdata.hex 真帧)推翻了两个想当然的做法:
//        1. **只把核换成 9 点、门控不动, 反而更差** (加噪 σ=1 时 RMSE 1.011,
//           旧版 1.000)。因为门控量只看 5 个十字样本, 4 个对角**完全没被 vetting**,
//           对角线/角点上十字极差很小而相邻对角已在边缘另一侧 —— 把没审查过的
//           对角平均进来就是往平坦区里掺边缘。
//        2. **只靠 sel() 逐个邻居否决也不够**。sel 是逐邻居的, 没有全局否决权:
//           在边缘的斜坡上仍会放进侧向邻居, 把斜坡抹平 1~2 LSB。实测 TH=3 时
//           改动 4.1% 的强边缘(rng>=30)、TH=6 时 27.8% —— 而旧版的二值门控在
//           任何 TH 下都是 0.00%。**边缘保护上旧版是严格更好的**, 这条不能丢。
//      所以最终形态是"sel + 最后一道全局否决", 见下面 NR 一段。
//
//  DPC:  z    = 中心像素
//        nmax = max(上,下,左,右) , nmin = min(...)
//        bad  = (z > nmax + TH) || (z + TH < nmin)
//        d    = bad ? (上+下+左+右+2)>>2 : z
//
//  NR :  hard = (5 个十字样本极差 > NR_EDGE)          // 强边缘: 整体不动
//        sel(n) = (|n - d| <= TH2) ? n : d            // 相似度选择: 不相似就用 d 顶替
//        核     = [1 2 1; 2 4 2; 1 2 1] / 16         // 9 个同色样本全覆盖
//        sm     = ( 4*d + 2*Σsel(上下左右) + Σsel(4 个对角) + 8 ) >> 4
//        out    = hard ? d : sm
//
//        两个极端自证 sm 是对的:
//          全选中   -> 纯 9 点二项式核, 平坦区噪声 std 0.375 (旧 5 点核 0.559);
//          全被顶替 -> (4+8+4)*d/16 = d, 与旧版 flat=false 的 out=d 逐位相同。
//        即它在"d"和"全平均"之间连续过渡, 没有旧的二值门控那种"相邻像素一个
//        降噪一个不降"造成的 1 LSB 台阶状斑块 —— 平坦区内过渡是连续的。
//
//        顶替用 d 而不是 ce: DPC 开时中心已被修正过, 判据和中心保持一致。
//
//  *** NR_EDGE / NR_TH 的取值和收益上限(实拍 rawdata 实测, 别凭感觉调) ***
//      σ 只有 1.33 LSB, 所以先接受一个事实: **本模块在 8bit RAW 域做不出肉眼
//      可见的降噪**, 收益上限就是 0.1 LSB 量级。它的作用是不添乱。
//      扫参结果 (旧版 TH=3 为基准: 改动 22.8% 像素, 强边缘被改 0.00%):
//        NR_EDGE=16 + TH=3 : 改动 44.5%, 强边缘 0.00%, 加噪恢复 RMSE
//                            σ=1 0.918(旧 1.000) σ=2 1.801(旧 1.997) σ=3 2.876(旧 3.006)
//        NR_EDGE 再调小(8/12) RMSE 几乎不变但高噪声下会误伤平坦区; 调到 24/30
//                            则否决失效, 强边缘改动回升到 0.3%~2.2%。
//        NR_TH 调到 6 在低噪声下**反而更差**(σ=1 时 1.014 > 旧 0.925), 因为
//                            TH=6 会把真实细节也当成相似邻居平均掉。
//      结论: 默认 NR_EDGE=16 / TH=3。要"看得见的降噪"必须换域(经 WB/gamma
//      幅度被放大之后)或做时域多帧, 那都不在这个模块里。
//
//  阈值按通道选取:  偶行 偶像素=G 奇像素=B ; 奇行 偶像素=R 奇像素=G
////////////////////////////////////////////////////////////////////////////////
`include "isp_params.vh"

module isp_dpc_nr #(
    parameter [15:0] DPC_TH_R = `DPC_TH_R,
    parameter [15:0] DPC_TH_G = `DPC_TH_G,
    parameter [15:0] DPC_TH_B = `DPC_TH_B,
    parameter [15:0] NR_TH_R  = `NR_TH_R,
    parameter [15:0] NR_TH_G  = `NR_TH_G,
    parameter [15:0] NR_TH_B  = `NR_TH_B,
    parameter [15:0] NR_EDGE   = 16'd16     // 强边缘一票否决门限(见文件头 NR 一段)
)(
    input  wire         clk,
    input  wire         rst_n,

    input  wire         valid_i,
    input  wire [23:0]  sync_i,     // {valid, sof, col[9:0], row[11:0]}
    input  wire [299:0] win_i,      // isp_line_window 的 5行x3字

    output reg          valid_o,
    output reg  [23:0]  sync_o,
    output reg  [19:0]  data_o      // {奇像素[9:0], 偶像素[9:0]}, 已过 DPC+NR
);

    //--------------------------------------------------------------------------
    // 取窗: W(行, 字)   行 0=line-4 ... 4=line ,  字 0=左 1=中 2=右
    //       PX(行, 字, p)  p=0 取偶像素(x 偶), p=1 取奇像素(x 奇)
    //--------------------------------------------------------------------------
    function [19:0] W;
        input [3:0] r;
        input [1:0] c;
        begin
            W = win_i[(r*3 + c)*20 +: 20];
        end
    endfunction

    function [9:0] PX;
        input [3:0] r;
        input [1:0] c;
        input       p;
        reg   [19:0] w;
        begin
            w  = W(r,c);                // 不能直接对函数返回值做位选, 先落到变量
            PX = p ? w[19:10] : w[9:0];
        end
    endfunction

    function [9:0] max2; input [9:0] a,b; begin max2 = (a > b) ? a : b; end endfunction
    function [9:0] min2; input [9:0] a,b; begin min2 = (a < b) ? a : b; end endfunction

    // 相似度选择: 邻居 n 与中心 c 相差不超过 th 就照用, 否则用 c 顶替。
    // 顶替相当于把该邻居从加权平均里剔除 —— 它那一份权重全落回中心。
    function [9:0] sel;
        input [9:0]  n;
        input [9:0]  c;
        input [15:0] th;
        reg   [9:0]  df;
        begin
            df  = (n > c) ? (n - c) : (c - n);
            sel = (df <= th) ? n : c;
        end
    endfunction

    //--------------------------------------------------------------------------
    // 单个像素的 DPC+NR
    //--------------------------------------------------------------------------
    function [9:0] dpc_nr;
        input        p;         // 0 = 偶像素, 1 = 奇像素
        input [15:0] th_dpc;
        input [15:0] th_nr;
        reg  [9:0]   ce, up, dn, lf, rt;
        reg  [9:0]   ul, ur, dl, dr;    // 4 个对角, 与中心同色 (任何 Bayer 相位)
        reg  [9:0]   nmax, nmin;
        reg  [9:0]   smax, smin;        // 5 个十字样本的极值, 用于强边缘否决
        reg  [9:0]   d;
        reg  [13:0]  s4;
        reg  [15:0]  w_pos, w_dia, acc;
        reg          bad, hard;
        begin
            ce = PX(4'd2, 2'd1, p);         // 中心           (y  , x  )
            up = PX(4'd0, 2'd1, p);         // line-4 同色    (y-2, x  )
            dn = PX(4'd4, 2'd1, p);         // line   同色    (y+2, x  )
            lf = PX(4'd2, 2'd0, p);         // 左 2 像素      (y  , x-2)
            rt = PX(4'd2, 2'd2, p);         // 右 2 像素      (y  , x+2)
            ul = PX(4'd0, 2'd0, p);         // 4 个对角       (y-2, x-2)
            ur = PX(4'd0, 2'd2, p);         //                (y-2, x+2)
            dl = PX(4'd4, 2'd0, p);         //                (y+2, x-2)
            dr = PX(4'd4, 2'd2, p);         //                (y+2, x+2)

            //---- DPC ----
            nmax = max2(max2(up, dn), max2(lf, rt));
            nmin = min2(min2(up, dn), min2(lf, rt));
            bad  = (ce > (nmax + th_dpc)) || ((ce + th_dpc) < nmin);
            s4   = up + dn + lf + rt;       // 最大 4092
            d    = bad ? ((s4 + 14'd2) >> 2) : ce;

            //---- NR: [1 2 1; 2 4 2; 1 2 1]/16 + 逐邻居相似度, 强边缘一票否决 ----
            // 十字 5 样本极差 > NR_EDGE 的像素整体不动。这一道是必须的: sel() 的
            // 否决是**逐个邻居**的, 在边缘的斜坡上仍会放进侧向邻居, 把斜坡抹平
            // 1~2 LSB —— 实测 TH=3 时会把 4.1% 的强边缘(rng>=30)改动, 而这正是
            // 旧版二值门控从来不会碰的。加上否决后强边缘改动回到 0.00%。
            smax = max2(max2(up, dn), max2(lf, rt)); smax = max2(smax, ce);
            smin = min2(min2(up, dn), min2(lf, rt)); smin = min2(smin, ce);
            hard = ((smax - smin) > NR_EDGE);

            w_pos = sel(up, d, th_nr) + sel(dn, d, th_nr)
                  + sel(lf, d, th_nr) + sel(rt, d, th_nr);          // <= 4092
            w_dia = sel(ul, d, th_nr) + sel(ur, d, th_nr)
                  + sel(dl, d, th_nr) + sel(dr, d, th_nr);          // <= 4092
            // 4*d + 2*w_pos + w_dia + 8, 全选时 = 16*1023+8 = 16376 < 2^14
            acc   = {4'b0, d, 2'b00} + {w_pos, 1'b0} + w_dia + 16'd8;

            dpc_nr = hard ? d : acc[13:4];  // acc[13:4] = acc >> 4 (带 +8 舍入), <= 1023
        end
    endfunction

    //--------------------------------------------------------------------------
    // 通道 -> 阈值
    //--------------------------------------------------------------------------
    wire par = sync_i[0];               // row[0], 中心行的行奇偶

    wire [15:0] thd_p0 = par ? DPC_TH_R : DPC_TH_G;   // 偶像素: 奇行=R, 偶行=G
    wire [15:0] thd_p1 = par ? DPC_TH_G : DPC_TH_B;   // 奇像素: 奇行=G, 偶行=B
    wire [15:0] thn_p0 = par ? NR_TH_R  : NR_TH_G;
    wire [15:0] thn_p1 = par ? NR_TH_G  : NR_TH_B;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            valid_o <= 1'b0;
            sync_o  <= 24'd0;
            data_o  <= 20'd0;
        end else begin
            valid_o <= valid_i;
            sync_o  <= sync_i;
            data_o  <= { dpc_nr(1'b1, thd_p1, thn_p1),
                         dpc_nr(1'b0, thd_p0, thn_p0) };
        end
    end

endmodule
