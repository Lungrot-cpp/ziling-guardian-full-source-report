// Derived from the official TinyML top; original notices are retained in src/tinyml_soc_top.v.
module person_engine(
input core_clk, input cal_done, input board_reset_n,
output system_uart_0_io_txd, input system_uart_0_io_rxd,
output boot_spi_clk, output boot_spi_cs, output boot_spi_mosi, input boot_spi_miso,
input jtag_inst1_TCK, input jtag_inst1_TDI, input jtag_inst1_SEL, input jtag_inst1_CAPTURE, input jtag_inst1_SHIFT, input jtag_inst1_UPDATE, input jtag_inst1_RESET, output jtag_inst1_TDO,
output [15:0] apb_paddr, output apb_psel, output apb_penable, output apb_pwrite, output [31:0] apb_pwdata, input [31:0] apb_prdata, input apb_pready, input apb_pslverror, output io_systemReset,
input [31:0] axi_inter_s1_awaddr,
input [7:0] axi_inter_s1_awlen,
input  axi_inter_s1_awvalid,
input [127:0] axi_inter_s1_wdata,
input [15:0] axi_inter_s1_wstrb,
input  axi_inter_s1_wlast,
input  axi_inter_s1_wvalid,
input  axi_inter_s1_bready,
input [31:0] axi_inter_s1_araddr,
input [7:0] axi_inter_s1_arlen,
input  axi_inter_s1_arvalid,
input  axi_inter_s1_rready,
output  axi_inter_s1_awready,
output  axi_inter_s1_wready,
output [1:0] axi_inter_s1_bresp,
output  axi_inter_s1_bvalid,
output  axi_inter_s1_arready,
output [127:0] axi_inter_s1_rdata,
output [1:0] axi_inter_s1_rresp,
output  axi_inter_s1_rlast,
output  axi_inter_s1_rvalid,
output [7:0] axi_inter_m_awid,
output [31:0] axi_inter_m_awaddr,
output [7:0] axi_inter_m_awlen,
output [2:0] axi_inter_m_awsize,
output [1:0] axi_inter_m_awburst,
output  axi_inter_m_awlock,
output [3:0] axi_inter_m_awcache,
output [2:0] axi_inter_m_awprot,
output  axi_inter_m_awvalid,
output [127:0] axi_inter_m_wdata,
output [15:0] axi_inter_m_wstrb,
output  axi_inter_m_wlast,
output  axi_inter_m_wvalid,
output  axi_inter_m_bready,
output [7:0] axi_inter_m_arid,
output [31:0] axi_inter_m_araddr,
output [7:0] axi_inter_m_arlen,
output [2:0] axi_inter_m_arsize,
output [1:0] axi_inter_m_arburst,
output  axi_inter_m_arlock,
output [3:0] axi_inter_m_arcache,
output [2:0] axi_inter_m_arprot,
output  axi_inter_m_arvalid,
output  axi_inter_m_rready,
input  axi_inter_m_awready,
input  axi_inter_m_wready,
input [1:0] axi_inter_m_bresp,
input  axi_inter_m_bvalid,
input  axi_inter_m_arready,
input [127:0] axi_inter_m_rdata,
input [1:0] axi_inter_m_rresp,
input  axi_inter_m_rlast,
input  axi_inter_m_rvalid
);
localparam AXI_DATA_WIDTH=128;
//Custom instruction
wire                    cpu_customInstruction_cmd_valid;
wire                    cpu_customInstruction_cmd_ready;
wire  [9:0]             cpu_customInstruction_function_id;
wire  [31:0]            cpu_customInstruction_inputs_0;
wire  [31:0]            cpu_customInstruction_inputs_1;
wire                    cpu_customInstruction_rsp_valid;
wire                    cpu_customInstruction_rsp_ready;
wire  [31:0]            cpu_customInstruction_outputs_0;
wire                    cpu_customInstruction_cmd_int;
wire                    userInterruptA;

localparam AXI_TINYML_DATA_WIDTH = 128;

wire [7:0]              axi_inter_s0_awid;
wire [31:0]             axi_inter_s0_awaddr;
wire [7:0]              axi_inter_s0_awlen;
wire [2:0]              axi_inter_s0_awsize;
wire [1:0]              axi_inter_s0_awburst;
wire                    axi_inter_s0_awlock;
wire [3:0]              axi_inter_s0_awcache;
wire [2:0]              axi_inter_s0_awprot;
wire [3:0]              axi_inter_s0_awqos;
wire                    axi_inter_s0_awvalid;
wire                    axi_inter_s0_awready;
wire [127:0]            axi_inter_s0_wdata;
wire [15:0]             axi_inter_s0_wstrb;
wire                    axi_inter_s0_wlast;
wire                    axi_inter_s0_wvalid;
wire                    axi_inter_s0_wready;
wire [7:0]              axi_inter_s0_bid;
wire [1:0]              axi_inter_s0_bresp;
wire                    axi_inter_s0_bvalid;
wire                    axi_inter_s0_bready;
wire [7:0]              axi_inter_s0_arid;
wire [31:0]             axi_inter_s0_araddr;
wire [7:0]              axi_inter_s0_arlen;
wire [2:0]              axi_inter_s0_arsize;
wire [1:0]              axi_inter_s0_arburst;
wire                    axi_inter_s0_arlock;
wire [3:0]              axi_inter_s0_arcache;
wire [2:0]              axi_inter_s0_arprot;
wire [3:0]              axi_inter_s0_arqos;
wire                    axi_inter_s0_arvalid;
wire                    axi_inter_s0_arready;
wire [7:0]              axi_inter_s0_rid;
wire [127:0]            axi_inter_s0_rdata;
wire [1:0]              axi_inter_s0_rresp;
wire                    axi_inter_s0_rlast;
wire                    axi_inter_s0_rvalid;
wire                    axi_inter_s0_rready;

wire [7:0]              axi_inter_s1_awid;


wire [2:0]              axi_inter_s1_awsize;
wire [1:0]              axi_inter_s1_awburst;
wire                    axi_inter_s1_awlock;
wire [3:0]              axi_inter_s1_awcache;
wire [2:0]              axi_inter_s1_awprot;
wire [3:0]              axi_inter_s1_awqos;







wire [7:0]              axi_inter_s1_bid;



wire [7:0]              axi_inter_s1_arid;


wire [2:0]              axi_inter_s1_arsize;
wire [1:0]              axi_inter_s1_arburst;
wire                    axi_inter_s1_arlock;
wire [3:0]              axi_inter_s1_arcache;
wire [2:0]              axi_inter_s1_arprot;
wire [3:0]              axi_inter_s1_arqos;


wire [7:0]              axi_inter_s1_rid;






wire [7:0]              axi_inter_s2_awid;
wire [31:0]             axi_inter_s2_awaddr;
wire [7:0]              axi_inter_s2_awlen;
wire [2:0]              axi_inter_s2_awsize;
wire [1:0]              axi_inter_s2_awburst;
wire                    axi_inter_s2_awlock;
wire [3:0]              axi_inter_s2_awcache;
wire [2:0]              axi_inter_s2_awprot;
wire [3:0]              axi_inter_s2_awqos;
wire                    axi_inter_s2_awvalid;
wire                    axi_inter_s2_awready;
wire [AXI_TINYML_DATA_WIDTH-1:0]            axi_inter_s2_wdata;
wire [AXI_TINYML_DATA_WIDTH/8-1:0]             axi_inter_s2_wstrb;
wire                    axi_inter_s2_wlast;
wire                    axi_inter_s2_wvalid;
wire                    axi_inter_s2_wready;
wire [7:0]              axi_inter_s2_bid;
wire [1:0]              axi_inter_s2_bresp;
wire                    axi_inter_s2_bvalid;
wire                    axi_inter_s2_bready;
wire [7:0]              axi_inter_s2_arid;
wire [31:0]             axi_inter_s2_araddr;
wire [7:0]              axi_inter_s2_arlen;
wire [2:0]              axi_inter_s2_arsize;
wire [1:0]              axi_inter_s2_arburst;
wire                    axi_inter_s2_arlock;
wire [3:0]              axi_inter_s2_arcache;
wire [2:0]              axi_inter_s2_arprot;
wire [3:0]              axi_inter_s2_arqos;
wire                    axi_inter_s2_arvalid;
wire                    axi_inter_s2_arready;
wire [7:0]              axi_inter_s2_rid;
wire [AXI_TINYML_DATA_WIDTH-1:0]            axi_inter_s2_rdata;
wire [1:0]              axi_inter_s2_rresp;
wire                    axi_inter_s2_rlast;
wire                    axi_inter_s2_rvalid;
wire                    axi_inter_s2_rready;









wire [3:0]              axi_inter_m_awqos;
wire [3:0]              axi_inter_m_awregion;







wire [7:0]              axi_inter_m_bid;











wire [3:0]              axi_inter_m_arqos;
wire [3:0]              axi_inter_m_arregion;


wire [7:0]              axi_inter_m_rid;







wire io_memoryClk = core_clk;
assign userInterruptA = cpu_customInstruction_cmd_int;
assign axi_inter_s0_rid=8'b0;
assign axi_inter_s0_bid=8'b0;
sapphire_soc u_sapphire_soc(
    .io_asyncReset                      (!cal_done || !board_reset_n                        ),
    .io_systemClk                       (core_clk                           ),

    //UART 0
    .system_uart_0_io_txd               (system_uart_0_io_txd               ),
    .system_uart_0_io_rxd               (system_uart_0_io_rxd               ),
    .io_memoryClk                       (core_clk                           ),
    .io_memoryReset                     (                                   ),
    .io_systemReset                     (io_systemReset                                   ),
    .io_peripheralClk                   (core_clk                      ),
//    .io_peripheralReset                 (                                   ),    
    //External Memory AXI4 Interface
    .io_ddrA_aw_payload_prot            (                                  ),
    .io_ddrA_aw_payload_qos             (                                  ),
    .io_ddrA_aw_payload_cache           (                                  ),
    .io_ddrA_aw_payload_lock            (axi_inter_s0_awlock                     ),
    .io_ddrA_aw_payload_burst           (axi_inter_s0_awburst                    ),
    .io_ddrA_aw_payload_size            (axi_inter_s0_awsize                     ),
    .io_ddrA_aw_payload_len             (axi_inter_s0_awlen                      ),
    .io_ddrA_aw_payload_region          (                                  ),
    .io_ddrA_aw_payload_id              (axi_inter_s0_awid                       ),
    .io_ddrA_aw_payload_addr            (axi_inter_s0_awaddr                     ),
    .io_ddrA_aw_ready                   (axi_inter_s0_awready                    ),
    .io_ddrA_aw_valid                   (axi_inter_s0_awvalid                    ),

    .io_ddrA_w_payload_last             (axi_inter_s0_wlast                      ),
    .io_ddrA_w_ready                    (axi_inter_s0_wready                     ),
    .io_ddrA_w_valid                    (axi_inter_s0_wvalid                     ),    
    .io_ddrA_w_payload_strb             (axi_inter_s0_wstrb                      ),
    .io_ddrA_w_payload_data             (axi_inter_s0_wdata                      ),

    .io_ddrA_b_payload_resp             (                                  ),
    .io_ddrA_b_payload_id               (axi_inter_s0_bid                        ),
    .io_ddrA_b_ready                    (axi_inter_s0_bready                     ),
    .io_ddrA_b_valid                    (axi_inter_s0_bvalid                     ),

    .io_ddrA_ar_payload_prot            (                                  ),
    .io_ddrA_ar_payload_qos             (                                  ),
    .io_ddrA_ar_payload_cache           (                                  ),
    .io_ddrA_ar_payload_region          (                                  ),
    .io_ddrA_ar_payload_lock            (axi_inter_s0_arlock                     ),
    .io_ddrA_ar_payload_burst           (axi_inter_s0_arburst                    ),
    .io_ddrA_ar_payload_size            (axi_inter_s0_arsize                     ),
    .io_ddrA_ar_payload_len             (axi_inter_s0_arlen                      ),
    .io_ddrA_ar_payload_id              (axi_inter_s0_arid                       ),
    .io_ddrA_ar_payload_addr            (axi_inter_s0_araddr                     ),
    .io_ddrA_ar_ready                   (axi_inter_s0_arready                    ),
    .io_ddrA_ar_valid                   (axi_inter_s0_arvalid                    ),

    .io_ddrA_r_payload_last             (axi_inter_s0_rlast                      ),
    .io_ddrA_r_payload_resp             (axi_inter_s0_rresp                      ),
    .io_ddrA_r_payload_id               (axi_inter_s0_rid                        ),
    .io_ddrA_r_payload_data             (axi_inter_s0_rdata                      ),
    .io_ddrA_r_ready                    (axi_inter_s0_rready                     ),
    .io_ddrA_r_valid                    (axi_inter_s0_rvalid                     ),
    
    //custom instruction
    .cpu0_customInstruction_cmd_valid   (cpu_customInstruction_cmd_valid),
    .cpu0_customInstruction_cmd_ready   (cpu_customInstruction_cmd_ready),
    .cpu0_customInstruction_function_id (cpu_customInstruction_function_id),
    .cpu0_customInstruction_inputs_0    (cpu_customInstruction_inputs_0),
    .cpu0_customInstruction_inputs_1    (cpu_customInstruction_inputs_1),
    .cpu0_customInstruction_rsp_valid   (cpu_customInstruction_rsp_valid),
    .cpu0_customInstruction_rsp_ready   (cpu_customInstruction_rsp_ready),
    .cpu0_customInstruction_outputs_0   (cpu_customInstruction_outputs_0),
     //SPI 0
    .system_spi_0_io_sclk_write         (boot_spi_clk),
    .system_spi_0_io_data_0_writeEnable (),
    .system_spi_0_io_data_0_read        (1'b1),
    .system_spi_0_io_data_0_write       (boot_spi_mosi),
    .system_spi_0_io_data_1_writeEnable (),
    .system_spi_0_io_data_1_read        (boot_spi_miso),
    .system_spi_0_io_data_1_write       (),
    .system_spi_0_io_data_2_writeEnable (                                   ),
    .system_spi_0_io_data_2_read        (1'b1),
    .system_spi_0_io_data_2_write       (                                   ),
    .system_spi_0_io_data_3_writeEnable (                                   ),
    .system_spi_0_io_data_3_read        (1'b1),
    .system_spi_0_io_data_3_write       (                                   ),
    .system_spi_0_io_ss                 (boot_spi_cs),
    //APB 0
    .io_apbSlave_0_PADDR                (apb_paddr                          ),
    .io_apbSlave_0_PSEL                 (apb_psel                           ),
    .io_apbSlave_0_PENABLE              (apb_penable                        ),
    .io_apbSlave_0_PREADY               (apb_pready                         ),
    .io_apbSlave_0_PWRITE               (apb_pwrite                         ),
    .io_apbSlave_0_PWDATA               (apb_pwdata                         ),
    .io_apbSlave_0_PRDATA               (apb_prdata                         ),
    .io_apbSlave_0_PSLVERROR            (apb_pslverror                      ),
    .userInterruptA                     (userInterruptA),
    .userInterruptB                     (1'b0),

    //Hard Jtag Tap
    .jtagCtrl_tck                       (jtag_inst1_TCK                     ),
    .jtagCtrl_tdi                       (jtag_inst1_TDI                     ),
    .jtagCtrl_tdo                       (jtag_inst1_TDO                     ),
    .jtagCtrl_enable                    (jtag_inst1_SEL                     ),
    .jtagCtrl_capture                   (jtag_inst1_CAPTURE                 ),
    .jtagCtrl_shift                     (jtag_inst1_SHIFT                   ),
    .jtagCtrl_update                    (jtag_inst1_UPDATE                  ),
    .jtagCtrl_reset                     (jtag_inst1_RESET                   )

);


tinyml_top#(
  .AXI_DW             (AXI_TINYML_DATA_WIDTH)
) u_tinyml_top(
   .clk              (core_clk),
   .reset            (io_systemReset),
   .cmd_valid        (cpu_customInstruction_cmd_valid),
   .cmd_ready        (cpu_customInstruction_cmd_ready),
   .cmd_function_id  (cpu_customInstruction_function_id),
   .cmd_inputs_0     (cpu_customInstruction_inputs_0),
   .cmd_inputs_1     (cpu_customInstruction_inputs_1),
   .cmd_int          (cpu_customInstruction_cmd_int),
   .rsp_valid        (cpu_customInstruction_rsp_valid),
   .rsp_ready        (cpu_customInstruction_rsp_ready),
   .rsp_outputs_0    (cpu_customInstruction_outputs_0),
   .m_axi_clk        (io_memoryClk),
   .m_axi_rstn       (!io_systemReset),
   .m_axi_awvalid    (axi_inter_s2_awvalid),
   .m_axi_awaddr     (axi_inter_s2_awaddr),
   .m_axi_awlen      (axi_inter_s2_awlen),
   .m_axi_awsize     (axi_inter_s2_awsize),
   .m_axi_awburst    (axi_inter_s2_awburst),
   .m_axi_awprot     (axi_inter_s2_awprot),
   .m_axi_awlock     (axi_inter_s2_awlock),
   .m_axi_awcache    (axi_inter_s2_awcache),
   .m_axi_awready    (axi_inter_s2_awready),
   .m_axi_wdata      (axi_inter_s2_wdata),
   .m_axi_wstrb      (axi_inter_s2_wstrb),
   .m_axi_wlast      (axi_inter_s2_wlast),
   .m_axi_wvalid     (axi_inter_s2_wvalid),
   .m_axi_wready     (axi_inter_s2_wready),
   .m_axi_bresp      (axi_inter_s2_bresp),
   .m_axi_bvalid     (axi_inter_s2_bvalid),
   .m_axi_bready     (axi_inter_s2_bready),
   .m_axi_arvalid    (axi_inter_s2_arvalid),
   .m_axi_araddr     (axi_inter_s2_araddr),
   .m_axi_arlen      (axi_inter_s2_arlen),
   .m_axi_arsize     (axi_inter_s2_arsize),
   .m_axi_arburst    (axi_inter_s2_arburst),
   .m_axi_arprot     (axi_inter_s2_arprot),
   .m_axi_arlock     (axi_inter_s2_arlock),
   .m_axi_arcache    (axi_inter_s2_arcache),
   .m_axi_arready    (axi_inter_s2_arready),
   .m_axi_rvalid     (axi_inter_s2_rvalid),
   .m_axi_rdata      (axi_inter_s2_rdata),
   .m_axi_rlast      (axi_inter_s2_rlast),
   .m_axi_rresp      (axi_inter_s2_rresp),
   .m_axi_rready     (axi_inter_s2_rready)
);

axi_interconnect_beta #(
    .S_COUNT                            (3                                                 ),
    .SLAVE_ASYN_ARRAY                   ({1'b0,1'b0,1'b0}                                  ),
    .S_AXI_DW_ARRAY                     ({AXI_TINYML_DATA_WIDTH,AXI_DATA_WIDTH,AXI_DATA_WIDTH}    ),
    .CB_DW                              (AXI_DATA_WIDTH                                    ),
    .M_AXI_DW                           (AXI_DATA_WIDTH                                    ),
    .ARB_MODE                           (1                                                 ),
    .FAMILY                             ("TITANIUM"                                        ),
    .RD_QUEUE_FIFO_RAM_STYLE            ("block_ram"                                       ),
    .RD_QUEUE_FIFO_DEPTH                (256                                               )
) u_axi_interconnect (
   //AXI slave interfaces - S0: Connected to RISC-V SoC; S1: Connected to DMA controller; S2: Connected to TinyML accelerator
   .s_axi_clk       ({io_memoryClk         , io_memoryClk        , io_memoryClk        }),
   .s_axi_rstn      ({3{cal_done && board_reset_n}}),
   .s_axi_awaddr     ({axi_inter_s2_awaddr , axi_inter_s1_awaddr , axi_inter_s0_awaddr }),
   .s_axi_awlen      ({axi_inter_s2_awlen  , axi_inter_s1_awlen  , axi_inter_s0_awlen  }),
   .s_axi_awvalid    ({axi_inter_s2_awvalid, axi_inter_s1_awvalid, axi_inter_s0_awvalid}),
   .s_axi_awready    ({axi_inter_s2_awready, axi_inter_s1_awready, axi_inter_s0_awready}),
   .s_axi_wdata      ({axi_inter_s2_wdata  , axi_inter_s1_wdata  , axi_inter_s0_wdata  }),
   .s_axi_wstrb      ({axi_inter_s2_wstrb  , axi_inter_s1_wstrb  , axi_inter_s0_wstrb  }),
   .s_axi_wlast      ({axi_inter_s2_wlast  , axi_inter_s1_wlast  , axi_inter_s0_wlast  }),
   .s_axi_wvalid     ({axi_inter_s2_wvalid , axi_inter_s1_wvalid , axi_inter_s0_wvalid }),
   .s_axi_wready     ({axi_inter_s2_wready , axi_inter_s1_wready , axi_inter_s0_wready }),
   .s_axi_bresp      ({axi_inter_s2_bresp  , axi_inter_s1_bresp  , axi_inter_s0_bresp  }),
   .s_axi_bvalid     ({axi_inter_s2_bvalid , axi_inter_s1_bvalid , axi_inter_s0_bvalid }),
   .s_axi_bready     ({axi_inter_s2_bready , axi_inter_s1_bready , axi_inter_s0_bready }),
   .s_axi_araddr     ({axi_inter_s2_araddr , axi_inter_s1_araddr , axi_inter_s0_araddr }),
   .s_axi_arlen      ({axi_inter_s2_arlen  , axi_inter_s1_arlen  , axi_inter_s0_arlen  }),
   .s_axi_arvalid    ({axi_inter_s2_arvalid, axi_inter_s1_arvalid, axi_inter_s0_arvalid}),
   .s_axi_arready    ({axi_inter_s2_arready, axi_inter_s1_arready, axi_inter_s0_arready}),
   .s_axi_rdata      ({axi_inter_s2_rdata  , axi_inter_s1_rdata  , axi_inter_s0_rdata  }),
   .s_axi_rresp      ({axi_inter_s2_rresp  , axi_inter_s1_rresp  , axi_inter_s0_rresp  }),
   .s_axi_rlast      ({axi_inter_s2_rlast  , axi_inter_s1_rlast  , axi_inter_s0_rlast  }),
   .s_axi_rvalid     ({axi_inter_s2_rvalid , axi_inter_s1_rvalid , axi_inter_s0_rvalid }),
   .s_axi_rready     ({axi_inter_s2_rready , axi_inter_s1_rready , axi_inter_s0_rready }),
   //AXI master interface - Connect to HyperRAM controller
   .m_axi_clk        (io_memoryClk),
   .m_axi_rstn       (cal_done && board_reset_n),

   .m_axi_awid       (axi_inter_m_awid),
   .m_axi_awaddr     (axi_inter_m_awaddr),
   .m_axi_awlen      (axi_inter_m_awlen),
   .m_axi_awsize     (axi_inter_m_awsize),
   .m_axi_awburst    (axi_inter_m_awburst),
   .m_axi_awlock     (axi_inter_m_awlock),
   .m_axi_awcache    (axi_inter_m_awcache),
   .m_axi_awprot     (axi_inter_m_awprot),
   .m_axi_awvalid    (axi_inter_m_awvalid),
   .m_axi_awready    (axi_inter_m_awready),
   .m_axi_wdata      (axi_inter_m_wdata),
   .m_axi_wstrb      (axi_inter_m_wstrb),
   .m_axi_wlast      (axi_inter_m_wlast),
   .m_axi_wvalid     (axi_inter_m_wvalid),
   .m_axi_wready     (axi_inter_m_wready),
   .m_axi_bresp      (axi_inter_m_bresp),
   .m_axi_bvalid     (axi_inter_m_bvalid),
   .m_axi_bready     (axi_inter_m_bready),
   .m_axi_arid       (axi_inter_m_arid),
   .m_axi_araddr     (axi_inter_m_araddr),
   .m_axi_arlen      (axi_inter_m_arlen),
   .m_axi_arsize     (axi_inter_m_arsize),
   .m_axi_arburst    (axi_inter_m_arburst),
   .m_axi_arlock     (axi_inter_m_arlock),
   .m_axi_arcache    (axi_inter_m_arcache),
   .m_axi_arprot     (axi_inter_m_arprot),
   .m_axi_arvalid    (axi_inter_m_arvalid),
   .m_axi_arready    (axi_inter_m_arready),
   .m_axi_rdata      (axi_inter_m_rdata),
   .m_axi_rresp      (axi_inter_m_rresp),
   .m_axi_rlast      (axi_inter_m_rlast),
   .m_axi_rvalid     (axi_inter_m_rvalid),
   .m_axi_rready     (axi_inter_m_rready)
);

endmodule
