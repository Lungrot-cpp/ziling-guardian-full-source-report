module raw_to_rgb
#(
	parameter	P_DEPTH			= 10,
	parameter	PW				= P_DEPTH*2,
	parameter	LEGACY			= 1'b1
)
(
	input			i_arstn,
	input			i_pclk,
	
	input			i_vsync,
	input			i_hsync,
	input			i_de,
	input			i_valid,
	input[PW-1:0]	i_p_11,
	input[PW-1:0]	i_p_00,	
	input[PW-1:0]	i_p_01,
	
	output			o_vsync,
	output			o_hsync,
	output			o_de,
	output			o_valid,
	output[10:0]	o_x_cnt,
	output[10:0]	o_y_cnt,
	output[PW-1:0]	o_r,
	output[PW-1:0]	o_g,	
	output[PW-1:0]	o_b
);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// y -1  0  +1  x
// 
//   -1  0  +1 
//   -1  0  +1 
//   -1  0  +1 
//
//				r_bayer_11_11_2P						r_bayer_11_01_1P                       	r_bayer_11_01_0P
//				r_bayer_00_11_2P						r_bayer_00_00_1P                       	r_bayer_00_01_0P
//				r_bayer_01_11_2P						r_bayer_01_01_1P                       	r_bayer_01_01_0P
//
//	r_bayer_11_11_2P_0	r_bayer_11_11_2P_1	r_bayer_11_00_1P_0	r_bayer_11_00_1P_1	r_bayer_11_01_0P_0	r_bayer_11_01_0P_1
//	r_bayer_00_11_2P_0	r_bayer_00_11_2P_1	r_bayer_00_00_1P_0	r_bayer_00_00_1P_1	r_bayer_00_01_0P_0	r_bayer_00_01_0P_1
//	r_bayer_01_11_2P_0	r_bayer_01_11_2P_1	r_bayer_01_00_1P_0	r_bayer_01_00_1P_1	r_bayer_01_01_0P_0	r_bayer_01_01_0P_1
//
//  R | G | R | G | R | G              R G B | R G B | R G B | R G B | R G B | R G B
// --- --- --- --- --- ---            ------- ------- ------- ------- ------- -------
//  G | B | G | B | G | B        |\    R G B | R G B | R G B | R G B | R G B | R G B
// --- --- --- --- --- ---  -----  \  ------- ------- ------- ------- ------- -------
//  R | G | R | G | R | G   -----  /   R G B | R G B | R G B | R G B | R G B | R G B
// --- --- --- --- --- ---       |/   ------- ------- ------- ------- ------- -------
//  G | B | G | B | G | B              R G B | R G B | R G B | R G B | R G B | R G B
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
reg	[10:0]			r_x_cnt;
reg	[10:0]			r_x_cnt_1P;
reg	[10:0]			r_x_cnt_2P;
reg	[10:0]			r_x_cnt_3P;
reg	[10:0]			r_y_cnt;
reg	[10:0]			r_y_cnt_1P;
reg	[10:0]			r_y_cnt_2P;
reg	[10:0]			r_y_cnt_3P;

reg	[PW-1:0]		r_r_00_00_1P;
reg	[PW-1:0]		r_g_00_00_1P;
reg	[PW-1:0]		r_b_00_00_1P;

reg	[P_DEPTH-1:0]	r_bayer_11_01_0P_0;
reg	[P_DEPTH-1:0]	r_bayer_11_01_0P_1;
reg	[P_DEPTH-1:0]	r_bayer_00_01_0P_0;
reg	[P_DEPTH-1:0]	r_bayer_00_01_0P_1;
reg	[P_DEPTH-1:0]	r_bayer_01_01_0P_0;
reg	[P_DEPTH-1:0]	r_bayer_01_01_0P_1;
reg	[P_DEPTH-1:0]	r_bayer_11_00_1P_0;
reg	[P_DEPTH-1:0]	r_bayer_11_00_1P_1;
reg	[P_DEPTH-1:0]	r_bayer_00_00_1P_0;
reg	[P_DEPTH-1:0]	r_bayer_00_00_1P_1;
reg	[P_DEPTH-1:0]	r_bayer_01_00_1P_0;
reg	[P_DEPTH-1:0]	r_bayer_01_00_1P_1;
reg	[P_DEPTH-1:0]	r_bayer_11_11_2P_0;
reg	[P_DEPTH-1:0]	r_bayer_11_11_2P_1;
reg	[P_DEPTH-1:0]	r_bayer_00_11_2P_0;
reg	[P_DEPTH-1:0]	r_bayer_00_11_2P_1;
reg	[P_DEPTH-1:0]	r_bayer_01_11_2P_0;
reg	[P_DEPTH-1:0]	r_bayer_01_11_2P_1;

reg					r_vsync_00_1P;
reg					r_hsync_00_1P;
reg					r_de_00_1P;
reg					r_valid_1P;
reg					r_vsync_00_2P;
reg					r_hsync_00_2P;
reg					r_de_00_2P;
reg					r_valid_2P;
//数据通路是 3 拍(i_p -> _01_0P -> _00_1P -> 输出寄存器), 控制原来只有 2 拍,
//两者差一拍 => 每个 de 拍上挂的是 f(前一个字), 整行左移 2 像素、行尾丢 2 像素。
//补一级 _3P, 让输出控制(含 vs/hs/de/valid/x_cnt/y_cnt)与数据对齐。
reg					r_vsync_00_3P;
reg					r_hsync_00_3P;
reg					r_de_00_3P;
reg					r_valid_3P;

// wire	[P_DEPTH+1:0]w_r_00_00_odd_lsb;
// wire	[P_DEPTH+1:0]w_g_00_00_odd_lsb;
// wire	[P_DEPTH+1:0]w_b_00_00_odd_lsb;

// wire	[P_DEPTH+1:0]w_r_00_00_odd_msb;
// wire	[P_DEPTH+1:0]w_g_00_00_odd_msb;
// wire	[P_DEPTH+1:0]w_b_00_00_odd_msb;

// wire	[P_DEPTH+1:0]w_r_00_00_even_lsb;
// wire	[P_DEPTH+1:0]w_g_00_00_even_lsb;
// wire	[P_DEPTH+1:0]w_b_00_00_even_lsb;

// wire	[P_DEPTH+1:0]w_r_00_00_even_msb;
// wire	[P_DEPTH+1:0]w_g_00_00_even_msb;
// wire	[P_DEPTH+1:0]w_b_00_00_even_msb;

// assign	w_r_00_00_odd_lsb	= {1'b0, r_bayer_00_00_1P_1 + r_bayer_00_11_2P_1};
// assign	w_g_00_00_odd_lsb	= {2'b0, r_bayer_00_00_1P_0};
// assign	w_b_00_00_odd_lsb	= {1'b0, r_bayer_11_00_1P_0 + r_bayer_01_00_1P_0};
				
// assign	w_r_00_00_odd_msb	= {1'b0, r_bayer_00_00_1P_1 + r_bayer_00_00_1P_1};
// assign	w_g_00_00_odd_msb	= r_bayer_00_01_0P_0 + r_bayer_00_00_1P_0 + r_bayer_11_00_1P_1 + r_bayer_01_00_1P_1;
// assign	w_b_00_00_odd_msb	= r_bayer_11_01_0P_0 + r_bayer_01_00_1P_0 + r_bayer_01_01_0P_0 + r_bayer_11_00_1P_0;

// assign	w_r_00_00_even_lsb	= r_bayer_11_00_1P_1 + r_bayer_01_11_2P_1 + r_bayer_01_00_1P_1 + r_bayer_11_11_2P_1;
// assign	w_g_00_00_even_lsb	= r_bayer_11_00_1P_0 + r_bayer_01_00_1P_0 + r_bayer_00_00_1P_1 + r_bayer_00_11_2P_1;
// assign	w_b_00_00_even_lsb	= {1'b0, r_bayer_00_00_1P_0 + r_bayer_00_00_1P_0};
							
// assign	w_r_00_00_even_msb	= {1'b0, r_bayer_11_00_1P_1 + r_bayer_01_00_1P_1};
// assign	w_g_00_00_even_msb	= {2'b0, r_bayer_00_00_1P_1};
// assign	w_b_00_00_even_msb	= {1'b0, r_bayer_00_01_0P_0 + r_bayer_00_00_1P_0};

/* RAW to RGB Debayer filter */
always@(posedge i_pclk)
begin
	if (~i_arstn)
	begin
		r_vsync_00_1P		<= 1'b0;
		r_hsync_00_1P		<= 1'b0;
		r_de_00_1P			<= 1'b0;	
		r_valid_1P			<= 1'b0;	
		r_vsync_00_2P		<= 1'b0;
		r_hsync_00_2P		<= 1'b0;
		r_de_00_2P			<= 1'b0;
		r_valid_2P			<= 1'b0;
		r_vsync_00_3P		<= 1'b0;
		r_hsync_00_3P		<= 1'b0;
		r_de_00_3P			<= 1'b0;
		r_valid_3P			<= 1'b0;
	end else begin
		r_vsync_00_1P		<= i_vsync;
		r_hsync_00_1P		<= i_hsync;
		r_de_00_1P			<= i_de;
		r_valid_1P			<= i_valid;
		r_vsync_00_2P		<= r_vsync_00_1P;
		r_hsync_00_2P		<= r_hsync_00_1P;
		r_de_00_2P			<= r_de_00_1P;
		r_valid_2P			<= r_valid_1P;
		r_vsync_00_3P		<= r_vsync_00_2P;
		r_hsync_00_3P		<= r_hsync_00_2P;
		r_de_00_3P			<= r_de_00_2P;
		r_valid_3P			<= r_valid_2P;
	end
end
wire neg_vs = !i_vsync && r_vsync_00_1P;
wire pos_hs = i_hsync && !r_hsync_00_1P;
always@(posedge i_pclk)
begin
	if (~i_arstn)
		r_y_cnt	<= 11'b0;
	else if (!i_vsync && r_vsync_00_1P)		//Falling edge of VSYNC
		r_y_cnt	<= 11'b0;
	else if (r_de_00_1P && !i_de)			//Falling edge of DE		
		r_y_cnt <= r_y_cnt + 1'b1;

end 

always@(posedge i_pclk)
begin
	if (~i_arstn)
		r_x_cnt <= 11'b0;
	else if (i_de )
		r_x_cnt	<= r_x_cnt + 1'b1;
	else 
		r_x_cnt <= 11'b0;

end 

always@(posedge i_pclk)
begin
	if (~i_arstn)
	begin
		r_x_cnt_1P			<= 11'b0;
		r_x_cnt_2P			<= 11'b0;
		r_x_cnt_3P			<= 11'b0;
	end else begin
		r_x_cnt_1P			<= r_x_cnt;
		r_x_cnt_2P          <= r_x_cnt_1P;
		r_x_cnt_3P          <= r_x_cnt_2P;
	end
end

always@(posedge i_pclk)
begin
	if (~i_arstn) begin
		r_y_cnt_1P			<= 11'b0;
		r_y_cnt_2P			<= 11'b0;
		r_y_cnt_3P			<= 11'b0;
	end else begin
		r_y_cnt_1P			<= r_y_cnt;
		r_y_cnt_2P          <= r_y_cnt_1P;
		r_y_cnt_3P          <= r_y_cnt_2P;
	end

end

always @( posedge i_pclk )
begin
	if (~i_arstn)
	begin

		r_bayer_11_01_0P_0	<= {P_DEPTH{1'b0}};
        r_bayer_11_01_0P_1	<= {P_DEPTH{1'b0}};
        r_bayer_00_01_0P_0	<= {P_DEPTH{1'b0}};
        r_bayer_00_01_0P_1	<= {P_DEPTH{1'b0}};
        r_bayer_01_01_0P_0	<= {P_DEPTH{1'b0}};
        r_bayer_01_01_0P_1	<= {P_DEPTH{1'b0}};
	end else begin 
	if (LEGACY)
		begin
			if (r_y_cnt == 11'd0) //第0行是同一行的数据
			begin
				if (i_valid)
				begin
					r_bayer_11_01_0P_0	<= i_p_01[P_DEPTH-1:0]; //低8位
					r_bayer_11_01_0P_1	<= i_p_01[PW-1:P_DEPTH];//高8位
					r_bayer_00_01_0P_0	<= i_p_01[P_DEPTH-1:0]; //低8位
					r_bayer_00_01_0P_1	<= i_p_01[PW-1:P_DEPTH];//高8位
				end
			end else if (r_y_cnt == 11'd1) begin //第1行时使用的是第0和1行的数据
				if (i_valid)
				begin
					r_bayer_11_01_0P_0	<= i_p_00[P_DEPTH-1:0];
					r_bayer_11_01_0P_1	<= i_p_00[PW-1:P_DEPTH];
					r_bayer_00_01_0P_0	<= i_p_00[P_DEPTH-1:0];
					r_bayer_00_01_0P_1	<= i_p_00[PW-1:P_DEPTH];
				end
			end else begin
				if (i_valid) //以后的行数据都
				begin
					r_bayer_11_01_0P_0	<= i_p_11[P_DEPTH-1:0];
					r_bayer_11_01_0P_1	<= i_p_11[PW-1:P_DEPTH];
					r_bayer_00_01_0P_0	<= i_p_00[P_DEPTH-1:0];
					r_bayer_00_01_0P_1	<= i_p_00[PW-1:P_DEPTH];
				end
			end
			
			if (i_valid)
			begin
				r_bayer_01_01_0P_0	<= i_p_01[P_DEPTH-1:0];
				r_bayer_01_01_0P_1	<= i_p_01[PW-1:P_DEPTH];
			end
		end else begin
			if (i_valid)
			begin
				r_bayer_11_01_0P_0	<= i_p_11[P_DEPTH-1:0];
				r_bayer_11_01_0P_1	<= i_p_11[PW-1:P_DEPTH];
				r_bayer_00_01_0P_0	<= i_p_00[P_DEPTH-1:0];
				r_bayer_00_01_0P_1	<= i_p_00[PW-1:P_DEPTH];
				r_bayer_01_01_0P_0	<= i_p_01[P_DEPTH-1:0];
				r_bayer_01_01_0P_1	<= i_p_01[PW-1:P_DEPTH];
			end
		end
	end
end 



always@(posedge i_pclk)
begin
	if (~i_arstn)
	begin
        r_bayer_11_00_1P_0	<= {P_DEPTH{1'b0}};
        r_bayer_11_00_1P_1	<= {P_DEPTH{1'b0}};
        r_bayer_00_00_1P_0	<= {P_DEPTH{1'b0}};
        r_bayer_00_00_1P_1	<= {P_DEPTH{1'b0}};
        r_bayer_01_00_1P_0	<= {P_DEPTH{1'b0}};
        r_bayer_01_00_1P_1	<= {P_DEPTH{1'b0}};
	end else begin 
		if (r_x_cnt == 11'd0) begin
			r_bayer_11_00_1P_0	<= {P_DEPTH{1'b0}};
			r_bayer_11_00_1P_1	<= {P_DEPTH{1'b0}};
			r_bayer_00_00_1P_0	<= {P_DEPTH{1'b0}};
			r_bayer_00_00_1P_1	<= {P_DEPTH{1'b0}};
			r_bayer_01_00_1P_0	<= {P_DEPTH{1'b0}};
			r_bayer_01_00_1P_1	<= {P_DEPTH{1'b0}};
		end else begin
			r_bayer_11_00_1P_0	<= r_bayer_11_01_0P_0;
			r_bayer_11_00_1P_1	<= r_bayer_11_01_0P_1;
			r_bayer_00_00_1P_0	<= r_bayer_00_01_0P_0;
			r_bayer_00_00_1P_1	<= r_bayer_00_01_0P_1;
			r_bayer_01_00_1P_0	<= r_bayer_01_01_0P_0;
			r_bayer_01_00_1P_1	<= r_bayer_01_01_0P_1;
		end
	end 
end 


always@(posedge i_pclk)
begin
	if (~i_arstn)
	begin
        r_bayer_11_11_2P_0	<= {P_DEPTH{1'b0}};
        r_bayer_11_11_2P_1	<= {P_DEPTH{1'b0}};
        r_bayer_00_11_2P_0	<= {P_DEPTH{1'b0}};
        r_bayer_00_11_2P_1	<= {P_DEPTH{1'b0}};
        r_bayer_01_11_2P_0	<= {P_DEPTH{1'b0}};
        r_bayer_01_11_2P_1	<= {P_DEPTH{1'b0}};
			
	end
	else
	begin
		if (r_x_cnt == 11'd0)
		begin
			
			r_bayer_11_11_2P_0	<= {P_DEPTH{1'b0}};
			r_bayer_11_11_2P_1	<= {P_DEPTH{1'b0}};
			r_bayer_00_11_2P_0	<= {P_DEPTH{1'b0}};
			r_bayer_00_11_2P_1	<= {P_DEPTH{1'b0}};
			r_bayer_01_11_2P_0	<= {P_DEPTH{1'b0}};
			r_bayer_01_11_2P_1	<= {P_DEPTH{1'b0}};
		end else begin
			r_bayer_11_11_2P_0	<= r_bayer_11_00_1P_0;
			r_bayer_11_11_2P_1	<= r_bayer_11_00_1P_1;
			r_bayer_00_11_2P_0	<= r_bayer_00_00_1P_0;
			r_bayer_00_11_2P_1	<= r_bayer_00_00_1P_1;
			r_bayer_01_11_2P_0	<= r_bayer_01_00_1P_0;
			r_bayer_01_11_2P_1	<= r_bayer_01_00_1P_1;
		end
			
	end 
end 
wire [P_DEPTH  :0] w_r_00_00_l1 = r_bayer_00_00_1P_1 + r_bayer_00_11_2P_1;
wire [P_DEPTH  :0] w_b_00_00_l1 = r_bayer_11_00_1P_0  + r_bayer_01_00_1P_0 ;
wire [P_DEPTH+1:0] w_g_00_00_h1	= r_bayer_11_00_1P_1 + r_bayer_01_00_1P_1 + r_bayer_00_01_0P_0 + r_bayer_00_00_1P_0;	
wire [P_DEPTH+1:0] w_b_00_00_h1	= r_bayer_01_01_0P_0 + r_bayer_11_00_1P_0 + r_bayer_11_01_0P_0 + r_bayer_01_00_1P_0;


wire [P_DEPTH+1:0] w_r_00_00_l0	= r_bayer_11_00_1P_1 + r_bayer_01_11_2P_1 + r_bayer_01_00_1P_1 + r_bayer_11_11_2P_1;
wire [P_DEPTH+1:0] w_g_00_00_l0	= r_bayer_11_00_1P_0 + r_bayer_01_00_1P_0 + r_bayer_00_00_1P_1 + r_bayer_00_11_2P_1;
//else 分支(内容行=偶行 B G B G)里奇列那个 G 像素: R 必须取上下两行的奇列(R 位),
//B 必须取本行左右偶列(B 位)。原写法把两个来源写反了, 于是奇输出行的奇列像素
//R/B 整格互换 —— 蓝色区域里 1/4 像素变红, 看上去就是"蓝偏紫"。
//对照 if 分支的 w_r_00_00_l1(取高字节=R 位) / w_b_00_00_l1(取低字节=B 位) 即可。
wire [P_DEPTH  :0] w_r_00_00_h0 = r_bayer_11_00_1P_1 + r_bayer_01_00_1P_1;
wire [P_DEPTH  :0] w_b_00_00_h0 = r_bayer_00_01_0P_0 + r_bayer_00_00_1P_0;

always@(posedge i_pclk)
begin
	if (~i_arstn) begin	
		r_r_00_00_1P		<= {PW{1'b0}};
		r_g_00_00_1P		<= {PW{1'b0}};
		r_b_00_00_1P		<= {PW{1'b0}};		
	end else begin

		//清零边界与锁存门控一起再延一拍: 输出寄存器实际是 3 拍
		//(i_p -> _01_0P -> _00_1P -> 本寄存器), 若还用 _1P 判边界,
		//输出窗口会比数据早一拍, 整行左移 2 像素、行尾丢 2 像素。
		if (!r_de_00_2P) begin
			r_r_00_00_1P	<= {PW{1'b0}};
			r_g_00_00_1P	<= {PW{1'b0}};
			r_b_00_00_1P	<= {PW{1'b0}};	

			

		//窗口中心是源行 (r_y_cnt-1), 比标号 r_y_cnt 小 1 行 —— 两者奇偶相反,
		//所以分支判据必须取反: !r_y_cnt[0] 为真时内容行是奇行。
		//(原代码用 r_y_cnt[0], 相位整体错半格, 渲染成 G B G B / R G R G)
		end else if (!r_y_cnt[0])begin
			/* R Gr RG R */
			// if (r_valid_1P)
			// begin
			// 	r_r_00_00_1P[P_DEPTH-1:0]	<= r_bayer_00_00_1P_0;
			// 	r_g_00_00_1P[P_DEPTH-1:0]	<= (((r_bayer_11_00_1P_0 >> 1) + (r_bayer_01_00_1P_0 >> 1)) >> 1) + (((r_bayer_00_00_1P_1 >> 1) + (r_bayer_00_11_2P_1 >> 1)) >> 1);
			// 	r_b_00_00_1P[P_DEPTH-1:0]	<= (((r_bayer_11_00_1P_1 >> 1) + (r_bayer_01_11_2P_1 >> 1)) >> 1) + (((r_bayer_01_00_1P_1 >> 1) + (r_bayer_11_11_2P_1 >> 1)) >> 1);
				
			// 	r_r_00_00_1P[PW-1:P_DEPTH]	<= (r_bayer_00_01_0P_0 >> 1) + (r_bayer_00_00_1P_0 >> 1);
			// 	r_g_00_00_1P[PW-1:P_DEPTH]	<= r_bayer_00_00_1P_1;				
			// 	r_b_00_00_1P[PW-1:P_DEPTH]	<= (r_bayer_11_00_1P_1 >> 1) + (r_bayer_01_00_1P_1 >> 1);
			// end
			//内容行为奇行 -> BGGR 的 G R G R (低字节 G, 高字节 R)
			if (r_valid_2P)
			begin
				
				r_r_00_00_1P[P_DEPTH-1:0]	<= w_r_00_00_l1>>1;//(r_bayer_00_00_1P_1 >> 1) + (r_bayer_00_11_2P_1 >> 1); 
				r_g_00_00_1P[P_DEPTH-1:0]	<= r_bayer_00_00_1P_0;				
				r_b_00_00_1P[P_DEPTH-1:0]	<= w_b_00_00_l1>>1;//(r_bayer_11_00_1P_0 >> 1) + (r_bayer_01_00_1P_0 >> 1);
							
				r_r_00_00_1P[PW-1:P_DEPTH]	<= r_bayer_00_00_1P_1;
				r_g_00_00_1P[PW-1:P_DEPTH]	<= w_g_00_00_h1>>2;//(((r_bayer_11_00_1P_1 >> 1) + (r_bayer_01_00_1P_1 >> 1)) >> 1) + (((r_bayer_00_01_0P_0 >> 1) + (r_bayer_00_00_1P_0 >> 1)) >> 1);		
				r_b_00_00_1P[PW-1:P_DEPTH]	<= w_b_00_00_h1>>2;//(((r_bayer_01_01_0P_0 >> 1) + (r_bayer_11_00_1P_0 >> 1)) >> 1) + (((r_bayer_11_01_0P_0 >> 1) + (r_bayer_01_00_1P_0 >> 1)) >> 1);
			
			end
		end
		else
		begin
			/* Gb B Gb G */
			// if (r_valid_1P)
			// begin
			// 	r_r_00_00_1P[P_DEPTH-1:0]	<= (r_bayer_11_00_1P_0 >> 1) + (r_bayer_01_00_1P_0 >> 1);
			// 	r_g_00_00_1P[P_DEPTH-1:0]	<= r_bayer_00_00_1P_0;				
			// 	r_b_00_00_1P[P_DEPTH-1:0]	<= (r_bayer_00_00_1P_1 >> 1) + (r_bayer_00_11_2P_1 >> 1);
							
			// 	r_r_00_00_1P[PW-1:P_DEPTH]	<= (((r_bayer_01_01_0P_0 >> 1) + (r_bayer_11_00_1P_0 >> 1)) >> 1) + (((r_bayer_11_01_0P_0 >> 1) + (r_bayer_01_00_1P_0 >> 1)) >> 1);
			// 	r_g_00_00_1P[PW-1:P_DEPTH]	<= (((r_bayer_11_00_1P_1 >> 1) + (r_bayer_01_00_1P_1 >> 1)) >> 1) + (((r_bayer_00_01_0P_0 >> 1) + (r_bayer_00_00_1P_0 >> 1)) >> 1);		
			// 	r_b_00_00_1P[PW-1:P_DEPTH]	<= r_bayer_00_00_1P_1;
			
			// end
			//内容行为偶行 -> BGGR 的 B G B G (低字节 B, 高字节 G)
			if (r_valid_2P)
			begin
				r_r_00_00_1P[P_DEPTH-1:0]	<= w_r_00_00_l0>>2;//(((r_bayer_11_00_1P_1 >> 1) + (r_bayer_01_11_2P_1 >> 1)) >> 1) + (((r_bayer_01_00_1P_1 >> 1) + (r_bayer_11_11_2P_1 >> 1)) >> 1);
				r_g_00_00_1P[P_DEPTH-1:0]	<= w_g_00_00_l0>>2;//(((r_bayer_11_00_1P_0 >> 1) + (r_bayer_01_00_1P_0 >> 1)) >> 1) + (((r_bayer_00_00_1P_1 >> 1) + (r_bayer_00_11_2P_1 >> 1)) >> 1);
				r_b_00_00_1P[P_DEPTH-1:0]	<= r_bayer_00_00_1P_0; 
				
				r_r_00_00_1P[PW-1:P_DEPTH]	<= w_r_00_00_h0>>1;//(r_bayer_00_01_0P_0 >> 1) + (r_bayer_00_00_1P_0 >> 1);
				r_g_00_00_1P[PW-1:P_DEPTH]	<= r_bayer_00_00_1P_1;				
				r_b_00_00_1P[PW-1:P_DEPTH]	<= w_b_00_00_h0>>1;//(r_bayer_11_00_1P_1 >> 1) + (r_bayer_01_00_1P_1 >> 1);
				
			end
		end
	end	
end



//输出控制统一用 _3P, 与 3 拍的数据通路(r_r/g/b_00_00_1P)同拍,
//保证第 k 个 de 拍上挂的就是第 k 个字的去马赛克结果。
assign	o_vsync	= r_vsync_00_3P;
assign	o_hsync	= r_hsync_00_3P;
assign	o_de	= r_de_00_3P;
assign	o_valid	= r_valid_3P;
assign	o_x_cnt	= r_x_cnt_3P;
assign	o_y_cnt	= r_y_cnt_3P;
assign	o_r		= r_r_00_00_1P;
assign	o_g		= r_g_00_00_1P;
assign	o_b		= r_b_00_00_1P;

endmodule
