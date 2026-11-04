`timescale 1ns/1ps
/////////////////////////////////////////////////////////////////////////////
// gamma_lut_v1.v —— 增益无关的固定 gamma 表
//
//   本文件由 gen_gamma_lut.py 生成, 不要手工编辑。
//   重新生成:  python gen_gamma_lut.py
//
//   表项 = round( 255 * (min(k/2,255)/255) ** (1/2.2000) ),  k = 0..511
//   语义与 raw_pattern_test.m 的 `255 * (rgb/255).^(1/2.2)` 一致, 逐项四舍五入。
//
//   索引 k 是白平衡输出的 Q8.1 表示(每像素 9 位, 低 1 位是小数):
//   带着这 1 位小数查表, 而不是先取整成 8 位, 与 MATLAB double 的实测最大差
//   从 3 降到 1。表头 k=511 对应 255.5, 钳位后 gamma 恒为 255。
//
//   只写一张表: gamma 是逐通道相同的固定函数(增益已在上游 wb_gain_3ch 乘掉),
//   写三张的唯一后果是将来改指数时漏改一份。
//
//   为什么用 case 而不是 $readmemh 读 .hex: 仿真按 cwd 解析表文件、综合按工程
//   目录解析, 两者行为不一致, 是最容易埋下"仿真过、综合不过"的坑。case 是
//   纯 Verilog-2001, 无文件依赖。default 分支是必须的, 否则综合会判 case
//   不完整而推出锁存器。
/////////////////////////////////////////////////////////////////////////////

module gamma_lut_v1
#(
	parameter	P_DEPTH	= 8,		// 每个像素的输出位宽
	parameter	IN_FRAC	= 1		// 输入索引的小数位数 (Q8.1)
)
(
	input				i_pclk,
	input				i_arstn,

	input				i_vs,
	input				i_hs,
	input				i_de,
	input				i_valid,
	input	[2*(P_DEPTH+IN_FRAC)-1:0]	i_r,	// R 平面, {高字节像素, 低字节像素}, 每像素 9 位 Q8.1
	input	[2*(P_DEPTH+IN_FRAC)-1:0]	i_g,
	input	[2*(P_DEPTH+IN_FRAC)-1:0]	i_b,

	output	reg			o_vs,
	output	reg			o_hs,
	output	reg			o_de,
	output	reg			o_valid,
	output	reg	[2*P_DEPTH-1:0]	o_r,	// 低字节像素 = 偶列, 与 raw_to_rgb 的输出约定一致
	output	reg	[2*P_DEPTH-1:0]	o_g,
	output	reg	[2*P_DEPTH-1:0]	o_b
);

localparam	IW = P_DEPTH + IN_FRAC;	// 索引位宽 = 9

	// 纯组合查表。一个 function 被调用 6 次(3 个平面 x 高/低两个像素),
	// 综合会展开成 6 份分布式 ROM; 用 function 写, 代码量是 6 段 case 的 1/6,
	// 而且将来改指数时不可能漏改其中一份。
	// Exact original lookup values implemented as logic to release six block RAMs.
	(* syn_romstyle="logic" *) reg [7:0] gamma_rom[0:511];
	initial begin
	 gamma_rom[0]=8'd0;
	 gamma_rom[1]=8'd15;
	 gamma_rom[2]=8'd21;
	 gamma_rom[3]=8'd25;
	 gamma_rom[4]=8'd28;
	 gamma_rom[5]=8'd31;
	 gamma_rom[6]=8'd34;
	 gamma_rom[7]=8'd36;
	 gamma_rom[8]=8'd39;
	 gamma_rom[9]=8'd41;
	 gamma_rom[10]=8'd43;
	 gamma_rom[11]=8'd45;
	 gamma_rom[12]=8'd46;
	 gamma_rom[13]=8'd48;
	 gamma_rom[14]=8'd50;
	 gamma_rom[15]=8'd51;
	 gamma_rom[16]=8'd53;
	 gamma_rom[17]=8'd54;
	 gamma_rom[18]=8'd56;
	 gamma_rom[19]=8'd57;
	 gamma_rom[20]=8'd59;
	 gamma_rom[21]=8'd60;
	 gamma_rom[22]=8'd61;
	 gamma_rom[23]=8'd62;
	 gamma_rom[24]=8'd64;
	 gamma_rom[25]=8'd65;
	 gamma_rom[26]=8'd66;
	 gamma_rom[27]=8'd67;
	 gamma_rom[28]=8'd68;
	 gamma_rom[29]=8'd69;
	 gamma_rom[30]=8'd70;
	 gamma_rom[31]=8'd71;
	 gamma_rom[32]=8'd72;
	 gamma_rom[33]=8'd73;
	 gamma_rom[34]=8'd74;
	 gamma_rom[35]=8'd75;
	 gamma_rom[36]=8'd76;
	 gamma_rom[37]=8'd77;
	 gamma_rom[38]=8'd78;
	 gamma_rom[39]=8'd79;
	 gamma_rom[40]=8'd80;
	 gamma_rom[41]=8'd81;
	 gamma_rom[42]=8'd82;
	 gamma_rom[43]=8'd83;
	 gamma_rom[44]=8'd84;
	 gamma_rom[45]=8'd85;
	 gamma_rom[46]=8'd85;
	 gamma_rom[47]=8'd86;
	 gamma_rom[48]=8'd87;
	 gamma_rom[49]=8'd88;
	 gamma_rom[50]=8'd89;
	 gamma_rom[51]=8'd90;
	 gamma_rom[52]=8'd90;
	 gamma_rom[53]=8'd91;
	 gamma_rom[54]=8'd92;
	 gamma_rom[55]=8'd93;
	 gamma_rom[56]=8'd93;
	 gamma_rom[57]=8'd94;
	 gamma_rom[58]=8'd95;
	 gamma_rom[59]=8'd96;
	 gamma_rom[60]=8'd96;
	 gamma_rom[61]=8'd97;
	 gamma_rom[62]=8'd98;
	 gamma_rom[63]=8'd99;
	 gamma_rom[64]=8'd99;
	 gamma_rom[65]=8'd100;
	 gamma_rom[66]=8'd101;
	 gamma_rom[67]=8'd101;
	 gamma_rom[68]=8'd102;
	 gamma_rom[69]=8'd103;
	 gamma_rom[70]=8'd103;
	 gamma_rom[71]=8'd104;
	 gamma_rom[72]=8'd105;
	 gamma_rom[73]=8'd105;
	 gamma_rom[74]=8'd106;
	 gamma_rom[75]=8'd107;
	 gamma_rom[76]=8'd107;
	 gamma_rom[77]=8'd108;
	 gamma_rom[78]=8'd109;
	 gamma_rom[79]=8'd109;
	 gamma_rom[80]=8'd110;
	 gamma_rom[81]=8'd110;
	 gamma_rom[82]=8'd111;
	 gamma_rom[83]=8'd112;
	 gamma_rom[84]=8'd112;
	 gamma_rom[85]=8'd113;
	 gamma_rom[86]=8'd114;
	 gamma_rom[87]=8'd114;
	 gamma_rom[88]=8'd115;
	 gamma_rom[89]=8'd115;
	 gamma_rom[90]=8'd116;
	 gamma_rom[91]=8'd116;
	 gamma_rom[92]=8'd117;
	 gamma_rom[93]=8'd118;
	 gamma_rom[94]=8'd118;
	 gamma_rom[95]=8'd119;
	 gamma_rom[96]=8'd119;
	 gamma_rom[97]=8'd120;
	 gamma_rom[98]=8'd120;
	 gamma_rom[99]=8'd121;
	 gamma_rom[100]=8'd122;
	 gamma_rom[101]=8'd122;
	 gamma_rom[102]=8'd123;
	 gamma_rom[103]=8'd123;
	 gamma_rom[104]=8'd124;
	 gamma_rom[105]=8'd124;
	 gamma_rom[106]=8'd125;
	 gamma_rom[107]=8'd125;
	 gamma_rom[108]=8'd126;
	 gamma_rom[109]=8'd126;
	 gamma_rom[110]=8'd127;
	 gamma_rom[111]=8'd128;
	 gamma_rom[112]=8'd128;
	 gamma_rom[113]=8'd129;
	 gamma_rom[114]=8'd129;
	 gamma_rom[115]=8'd130;
	 gamma_rom[116]=8'd130;
	 gamma_rom[117]=8'd131;
	 gamma_rom[118]=8'd131;
	 gamma_rom[119]=8'd132;
	 gamma_rom[120]=8'd132;
	 gamma_rom[121]=8'd133;
	 gamma_rom[122]=8'd133;
	 gamma_rom[123]=8'd134;
	 gamma_rom[124]=8'd134;
	 gamma_rom[125]=8'd135;
	 gamma_rom[126]=8'd135;
	 gamma_rom[127]=8'd136;
	 gamma_rom[128]=8'd136;
	 gamma_rom[129]=8'd137;
	 gamma_rom[130]=8'd137;
	 gamma_rom[131]=8'd137;
	 gamma_rom[132]=8'd138;
	 gamma_rom[133]=8'd138;
	 gamma_rom[134]=8'd139;
	 gamma_rom[135]=8'd139;
	 gamma_rom[136]=8'd140;
	 gamma_rom[137]=8'd140;
	 gamma_rom[138]=8'd141;
	 gamma_rom[139]=8'd141;
	 gamma_rom[140]=8'd142;
	 gamma_rom[141]=8'd142;
	 gamma_rom[142]=8'd143;
	 gamma_rom[143]=8'd143;
	 gamma_rom[144]=8'd144;
	 gamma_rom[145]=8'd144;
	 gamma_rom[146]=8'd144;
	 gamma_rom[147]=8'd145;
	 gamma_rom[148]=8'd145;
	 gamma_rom[149]=8'd146;
	 gamma_rom[150]=8'd146;
	 gamma_rom[151]=8'd147;
	 gamma_rom[152]=8'd147;
	 gamma_rom[153]=8'd148;
	 gamma_rom[154]=8'd148;
	 gamma_rom[155]=8'd148;
	 gamma_rom[156]=8'd149;
	 gamma_rom[157]=8'd149;
	 gamma_rom[158]=8'd150;
	 gamma_rom[159]=8'd150;
	 gamma_rom[160]=8'd151;
	 gamma_rom[161]=8'd151;
	 gamma_rom[162]=8'd151;
	 gamma_rom[163]=8'd152;
	 gamma_rom[164]=8'd152;
	 gamma_rom[165]=8'd153;
	 gamma_rom[166]=8'd153;
	 gamma_rom[167]=8'd154;
	 gamma_rom[168]=8'd154;
	 gamma_rom[169]=8'd154;
	 gamma_rom[170]=8'd155;
	 gamma_rom[171]=8'd155;
	 gamma_rom[172]=8'd156;
	 gamma_rom[173]=8'd156;
	 gamma_rom[174]=8'd156;
	 gamma_rom[175]=8'd157;
	 gamma_rom[176]=8'd157;
	 gamma_rom[177]=8'd158;
	 gamma_rom[178]=8'd158;
	 gamma_rom[179]=8'd158;
	 gamma_rom[180]=8'd159;
	 gamma_rom[181]=8'd159;
	 gamma_rom[182]=8'd160;
	 gamma_rom[183]=8'd160;
	 gamma_rom[184]=8'd160;
	 gamma_rom[185]=8'd161;
	 gamma_rom[186]=8'd161;
	 gamma_rom[187]=8'd162;
	 gamma_rom[188]=8'd162;
	 gamma_rom[189]=8'd162;
	 gamma_rom[190]=8'd163;
	 gamma_rom[191]=8'd163;
	 gamma_rom[192]=8'd164;
	 gamma_rom[193]=8'd164;
	 gamma_rom[194]=8'd164;
	 gamma_rom[195]=8'd165;
	 gamma_rom[196]=8'd165;
	 gamma_rom[197]=8'd165;
	 gamma_rom[198]=8'd166;
	 gamma_rom[199]=8'd166;
	 gamma_rom[200]=8'd167;
	 gamma_rom[201]=8'd167;
	 gamma_rom[202]=8'd167;
	 gamma_rom[203]=8'd168;
	 gamma_rom[204]=8'd168;
	 gamma_rom[205]=8'd169;
	 gamma_rom[206]=8'd169;
	 gamma_rom[207]=8'd169;
	 gamma_rom[208]=8'd170;
	 gamma_rom[209]=8'd170;
	 gamma_rom[210]=8'd170;
	 gamma_rom[211]=8'd171;
	 gamma_rom[212]=8'd171;
	 gamma_rom[213]=8'd171;
	 gamma_rom[214]=8'd172;
	 gamma_rom[215]=8'd172;
	 gamma_rom[216]=8'd173;
	 gamma_rom[217]=8'd173;
	 gamma_rom[218]=8'd173;
	 gamma_rom[219]=8'd174;
	 gamma_rom[220]=8'd174;
	 gamma_rom[221]=8'd174;
	 gamma_rom[222]=8'd175;
	 gamma_rom[223]=8'd175;
	 gamma_rom[224]=8'd175;
	 gamma_rom[225]=8'd176;
	 gamma_rom[226]=8'd176;
	 gamma_rom[227]=8'd177;
	 gamma_rom[228]=8'd177;
	 gamma_rom[229]=8'd177;
	 gamma_rom[230]=8'd178;
	 gamma_rom[231]=8'd178;
	 gamma_rom[232]=8'd178;
	 gamma_rom[233]=8'd179;
	 gamma_rom[234]=8'd179;
	 gamma_rom[235]=8'd179;
	 gamma_rom[236]=8'd180;
	 gamma_rom[237]=8'd180;
	 gamma_rom[238]=8'd180;
	 gamma_rom[239]=8'd181;
	 gamma_rom[240]=8'd181;
	 gamma_rom[241]=8'd181;
	 gamma_rom[242]=8'd182;
	 gamma_rom[243]=8'd182;
	 gamma_rom[244]=8'd182;
	 gamma_rom[245]=8'd183;
	 gamma_rom[246]=8'd183;
	 gamma_rom[247]=8'd183;
	 gamma_rom[248]=8'd184;
	 gamma_rom[249]=8'd184;
	 gamma_rom[250]=8'd184;
	 gamma_rom[251]=8'd185;
	 gamma_rom[252]=8'd185;
	 gamma_rom[253]=8'd185;
	 gamma_rom[254]=8'd186;
	 gamma_rom[255]=8'd186;
	 gamma_rom[256]=8'd186;
	 gamma_rom[257]=8'd187;
	 gamma_rom[258]=8'd187;
	 gamma_rom[259]=8'd187;
	 gamma_rom[260]=8'd188;
	 gamma_rom[261]=8'd188;
	 gamma_rom[262]=8'd188;
	 gamma_rom[263]=8'd189;
	 gamma_rom[264]=8'd189;
	 gamma_rom[265]=8'd189;
	 gamma_rom[266]=8'd190;
	 gamma_rom[267]=8'd190;
	 gamma_rom[268]=8'd190;
	 gamma_rom[269]=8'd191;
	 gamma_rom[270]=8'd191;
	 gamma_rom[271]=8'd191;
	 gamma_rom[272]=8'd192;
	 gamma_rom[273]=8'd192;
	 gamma_rom[274]=8'd192;
	 gamma_rom[275]=8'd193;
	 gamma_rom[276]=8'd193;
	 gamma_rom[277]=8'd193;
	 gamma_rom[278]=8'd194;
	 gamma_rom[279]=8'd194;
	 gamma_rom[280]=8'd194;
	 gamma_rom[281]=8'd194;
	 gamma_rom[282]=8'd195;
	 gamma_rom[283]=8'd195;
	 gamma_rom[284]=8'd195;
	 gamma_rom[285]=8'd196;
	 gamma_rom[286]=8'd196;
	 gamma_rom[287]=8'd196;
	 gamma_rom[288]=8'd197;
	 gamma_rom[289]=8'd197;
	 gamma_rom[290]=8'd197;
	 gamma_rom[291]=8'd198;
	 gamma_rom[292]=8'd198;
	 gamma_rom[293]=8'd198;
	 gamma_rom[294]=8'd199;
	 gamma_rom[295]=8'd199;
	 gamma_rom[296]=8'd199;
	 gamma_rom[297]=8'd199;
	 gamma_rom[298]=8'd200;
	 gamma_rom[299]=8'd200;
	 gamma_rom[300]=8'd200;
	 gamma_rom[301]=8'd201;
	 gamma_rom[302]=8'd201;
	 gamma_rom[303]=8'd201;
	 gamma_rom[304]=8'd202;
	 gamma_rom[305]=8'd202;
	 gamma_rom[306]=8'd202;
	 gamma_rom[307]=8'd202;
	 gamma_rom[308]=8'd203;
	 gamma_rom[309]=8'd203;
	 gamma_rom[310]=8'd203;
	 gamma_rom[311]=8'd204;
	 gamma_rom[312]=8'd204;
	 gamma_rom[313]=8'd204;
	 gamma_rom[314]=8'd205;
	 gamma_rom[315]=8'd205;
	 gamma_rom[316]=8'd205;
	 gamma_rom[317]=8'd205;
	 gamma_rom[318]=8'd206;
	 gamma_rom[319]=8'd206;
	 gamma_rom[320]=8'd206;
	 gamma_rom[321]=8'd207;
	 gamma_rom[322]=8'd207;
	 gamma_rom[323]=8'd207;
	 gamma_rom[324]=8'd207;
	 gamma_rom[325]=8'd208;
	 gamma_rom[326]=8'd208;
	 gamma_rom[327]=8'd208;
	 gamma_rom[328]=8'd209;
	 gamma_rom[329]=8'd209;
	 gamma_rom[330]=8'd209;
	 gamma_rom[331]=8'd210;
	 gamma_rom[332]=8'd210;
	 gamma_rom[333]=8'd210;
	 gamma_rom[334]=8'd210;
	 gamma_rom[335]=8'd211;
	 gamma_rom[336]=8'd211;
	 gamma_rom[337]=8'd211;
	 gamma_rom[338]=8'd212;
	 gamma_rom[339]=8'd212;
	 gamma_rom[340]=8'd212;
	 gamma_rom[341]=8'd212;
	 gamma_rom[342]=8'd213;
	 gamma_rom[343]=8'd213;
	 gamma_rom[344]=8'd213;
	 gamma_rom[345]=8'd213;
	 gamma_rom[346]=8'd214;
	 gamma_rom[347]=8'd214;
	 gamma_rom[348]=8'd214;
	 gamma_rom[349]=8'd215;
	 gamma_rom[350]=8'd215;
	 gamma_rom[351]=8'd215;
	 gamma_rom[352]=8'd215;
	 gamma_rom[353]=8'd216;
	 gamma_rom[354]=8'd216;
	 gamma_rom[355]=8'd216;
	 gamma_rom[356]=8'd217;
	 gamma_rom[357]=8'd217;
	 gamma_rom[358]=8'd217;
	 gamma_rom[359]=8'd217;
	 gamma_rom[360]=8'd218;
	 gamma_rom[361]=8'd218;
	 gamma_rom[362]=8'd218;
	 gamma_rom[363]=8'd218;
	 gamma_rom[364]=8'd219;
	 gamma_rom[365]=8'd219;
	 gamma_rom[366]=8'd219;
	 gamma_rom[367]=8'd220;
	 gamma_rom[368]=8'd220;
	 gamma_rom[369]=8'd220;
	 gamma_rom[370]=8'd220;
	 gamma_rom[371]=8'd221;
	 gamma_rom[372]=8'd221;
	 gamma_rom[373]=8'd221;
	 gamma_rom[374]=8'd221;
	 gamma_rom[375]=8'd222;
	 gamma_rom[376]=8'd222;
	 gamma_rom[377]=8'd222;
	 gamma_rom[378]=8'd223;
	 gamma_rom[379]=8'd223;
	 gamma_rom[380]=8'd223;
	 gamma_rom[381]=8'd223;
	 gamma_rom[382]=8'd224;
	 gamma_rom[383]=8'd224;
	 gamma_rom[384]=8'd224;
	 gamma_rom[385]=8'd224;
	 gamma_rom[386]=8'd225;
	 gamma_rom[387]=8'd225;
	 gamma_rom[388]=8'd225;
	 gamma_rom[389]=8'd225;
	 gamma_rom[390]=8'd226;
	 gamma_rom[391]=8'd226;
	 gamma_rom[392]=8'd226;
	 gamma_rom[393]=8'd227;
	 gamma_rom[394]=8'd227;
	 gamma_rom[395]=8'd227;
	 gamma_rom[396]=8'd227;
	 gamma_rom[397]=8'd228;
	 gamma_rom[398]=8'd228;
	 gamma_rom[399]=8'd228;
	 gamma_rom[400]=8'd228;
	 gamma_rom[401]=8'd229;
	 gamma_rom[402]=8'd229;
	 gamma_rom[403]=8'd229;
	 gamma_rom[404]=8'd229;
	 gamma_rom[405]=8'd230;
	 gamma_rom[406]=8'd230;
	 gamma_rom[407]=8'd230;
	 gamma_rom[408]=8'd230;
	 gamma_rom[409]=8'd231;
	 gamma_rom[410]=8'd231;
	 gamma_rom[411]=8'd231;
	 gamma_rom[412]=8'd231;
	 gamma_rom[413]=8'd232;
	 gamma_rom[414]=8'd232;
	 gamma_rom[415]=8'd232;
	 gamma_rom[416]=8'd232;
	 gamma_rom[417]=8'd233;
	 gamma_rom[418]=8'd233;
	 gamma_rom[419]=8'd233;
	 gamma_rom[420]=8'd233;
	 gamma_rom[421]=8'd234;
	 gamma_rom[422]=8'd234;
	 gamma_rom[423]=8'd234;
	 gamma_rom[424]=8'd234;
	 gamma_rom[425]=8'd235;
	 gamma_rom[426]=8'd235;
	 gamma_rom[427]=8'd235;
	 gamma_rom[428]=8'd235;
	 gamma_rom[429]=8'd236;
	 gamma_rom[430]=8'd236;
	 gamma_rom[431]=8'd236;
	 gamma_rom[432]=8'd236;
	 gamma_rom[433]=8'd237;
	 gamma_rom[434]=8'd237;
	 gamma_rom[435]=8'd237;
	 gamma_rom[436]=8'd237;
	 gamma_rom[437]=8'd238;
	 gamma_rom[438]=8'd238;
	 gamma_rom[439]=8'd238;
	 gamma_rom[440]=8'd238;
	 gamma_rom[441]=8'd239;
	 gamma_rom[442]=8'd239;
	 gamma_rom[443]=8'd239;
	 gamma_rom[444]=8'd239;
	 gamma_rom[445]=8'd240;
	 gamma_rom[446]=8'd240;
	 gamma_rom[447]=8'd240;
	 gamma_rom[448]=8'd240;
	 gamma_rom[449]=8'd241;
	 gamma_rom[450]=8'd241;
	 gamma_rom[451]=8'd241;
	 gamma_rom[452]=8'd241;
	 gamma_rom[453]=8'd242;
	 gamma_rom[454]=8'd242;
	 gamma_rom[455]=8'd242;
	 gamma_rom[456]=8'd242;
	 gamma_rom[457]=8'd243;
	 gamma_rom[458]=8'd243;
	 gamma_rom[459]=8'd243;
	 gamma_rom[460]=8'd243;
	 gamma_rom[461]=8'd244;
	 gamma_rom[462]=8'd244;
	 gamma_rom[463]=8'd244;
	 gamma_rom[464]=8'd244;
	 gamma_rom[465]=8'd245;
	 gamma_rom[466]=8'd245;
	 gamma_rom[467]=8'd245;
	 gamma_rom[468]=8'd245;
	 gamma_rom[469]=8'd245;
	 gamma_rom[470]=8'd246;
	 gamma_rom[471]=8'd246;
	 gamma_rom[472]=8'd246;
	 gamma_rom[473]=8'd246;
	 gamma_rom[474]=8'd247;
	 gamma_rom[475]=8'd247;
	 gamma_rom[476]=8'd247;
	 gamma_rom[477]=8'd247;
	 gamma_rom[478]=8'd248;
	 gamma_rom[479]=8'd248;
	 gamma_rom[480]=8'd248;
	 gamma_rom[481]=8'd248;
	 gamma_rom[482]=8'd249;
	 gamma_rom[483]=8'd249;
	 gamma_rom[484]=8'd249;
	 gamma_rom[485]=8'd249;
	 gamma_rom[486]=8'd249;
	 gamma_rom[487]=8'd250;
	 gamma_rom[488]=8'd250;
	 gamma_rom[489]=8'd250;
	 gamma_rom[490]=8'd250;
	 gamma_rom[491]=8'd251;
	 gamma_rom[492]=8'd251;
	 gamma_rom[493]=8'd251;
	 gamma_rom[494]=8'd251;
	 gamma_rom[495]=8'd252;
	 gamma_rom[496]=8'd252;
	 gamma_rom[497]=8'd252;
	 gamma_rom[498]=8'd252;
	 gamma_rom[499]=8'd252;
	 gamma_rom[500]=8'd253;
	 gamma_rom[501]=8'd253;
	 gamma_rom[502]=8'd253;
	 gamma_rom[503]=8'd253;
	 gamma_rom[504]=8'd254;
	 gamma_rom[505]=8'd254;
	 gamma_rom[506]=8'd254;
	 gamma_rom[507]=8'd254;
	 gamma_rom[508]=8'd255;
	 gamma_rom[509]=8'd255;
	 gamma_rom[510]=8'd255;
	 gamma_rom[511]=8'd255;
	end
	function [7:0] gamma8;
	 input [IW-1:0] k;
	 begin gamma8=gamma_rom[k]; end
	endfunction

	wire [P_DEPTH-1:0] w_r_lo = gamma8(i_r[  IW-1:0]);
	wire [P_DEPTH-1:0] w_r_hi = gamma8(i_r[2*IW-1:IW]);
	wire [P_DEPTH-1:0] w_g_lo = gamma8(i_g[  IW-1:0]);
	wire [P_DEPTH-1:0] w_g_hi = gamma8(i_g[2*IW-1:IW]);
	wire [P_DEPTH-1:0] w_b_lo = gamma8(i_b[  IW-1:0]);
	wire [P_DEPTH-1:0] w_b_hi = gamma8(i_b[2*IW-1:IW]);

	// 标志与数据在同一个 always 块里一起打拍; 带复位, 否则出复位是 X,
	// X 会顺着乘法传进 case 的输入。
	always @(posedge i_pclk)
	begin
		if (!i_arstn)
		begin
			o_vs	<= 1'b0;
			o_hs	<= 1'b0;
			o_de	<= 1'b0;
			o_valid	<= 1'b0;
			o_r	<= {(2*P_DEPTH){1'b0}};
			o_g	<= {(2*P_DEPTH){1'b0}};
			o_b	<= {(2*P_DEPTH){1'b0}};
		end
		else
		begin
			o_vs	<= i_vs;
			o_hs	<= i_hs;
			o_de	<= i_de;
			o_valid	<= i_valid;
			o_r	<= { w_r_hi, w_r_lo };
			o_g	<= { w_g_hi, w_g_lo };
			o_b	<= { w_b_hi, w_b_lo };
		end
	end

endmodule
