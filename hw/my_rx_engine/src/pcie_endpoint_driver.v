//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
`include "includes.v"

module  pci_exp_64b_app (

    // Transaction ( TRN ) Interface
    input                                         trn_clk,
    input                                         trn_reset_n,
    input                                         trn_lnk_up_n,

    // Tx Local-Link

    output     [63:0]                             trn_td,
    output     [7:0]                              trn_trem_n,
    output                                        trn_tsof_n,
    output                                        trn_teof_n,
    output                                        trn_tsrc_rdy_n,
    input                                         trn_tdst_rdy_n,
    output                                        trn_tsrc_dsc_n,
    input                                         trn_tdst_dsc_n,
    output                                        trn_terrfwd_n,
    input      [3:0]                              trn_tbuf_av,

    //-------------------------------------------------------
    // To tlp_trigger
    //-------------------------------------------------------
    output                                        trigger_tlp_ack,
    input                                         trigger_tlp,
    output                                        change_huge_page_ack,
    input                                         change_huge_page,
    input                                         send_last_tlp_change_huge_page,
    output     [`BF+1:0]                          commited_rd_address,
    input      [4:0]                              qwords_to_send,

    //-------------------------------------------------------
    // To mac_rx_interface
    //-------------------------------------------------------
    output                                        rd_addr_change,
    output     [`BF+1:0]                          rd_addr_extended,

    //-------------------------------------------------------
    // To mac_host_configuration_interface
    //-------------------------------------------------------
    input                                         host_clk,
    input                                         host_reset_n,
    output     [1:0]                              host_opcode,
    output     [9:0]                              host_addr,
    output     [31:0]                             host_wr_data,
    input      [31:0]                             host_rd_data,
    output                                        host_miim_sel,
    output                                        host_req,
    input                                         host_miim_rdy,

    //-------------------------------------------------------
    // To internal_true_dual_port_ram
    //-------------------------------------------------------
    output     [`BF:0]                            rd_addr,
    input      [63:0]                             rd_data,

    // Rx Local-Link

    input      [63:0]                             trn_rd,
    input      [7:0]                              trn_rrem,
    input                                         trn_rsof_n,
    input                                         trn_reof_n,
    input                                         trn_rsrc_rdy_n,
    input                                         trn_rsrc_dsc_n,
    output                                        trn_rdst_rdy_n,
    input                                         trn_rerrfwd_n,
    output                                        trn_rnp_ok_n,
    input      [6:0]                              trn_rbar_hit_n,
    input      [7:0]                              trn_rfc_nph_av,
    input      [11:0]                             trn_rfc_npd_av,
    input      [7:0]                              trn_rfc_ph_av,
    input      [11:0]                             trn_rfc_pd_av,
    output                                        trn_rcpl_streaming_n,

    //
    // Host ( CFG ) Interface
    //
    input      [31:0]                             cfg_do,
    output     [31:0]                             cfg_di,
    output     [3:0]                              cfg_byte_en_n,
    output     [9:0]                              cfg_dwaddr,

    input                                         cfg_rd_wr_done_n,
    output                                        cfg_wr_en_n,
    output                                        cfg_rd_en_n,
    output                                        cfg_err_cor_n,
    output                                        cfg_err_ur_n,
    input                                         cfg_err_cpl_rdy_n,
    output                                        cfg_err_ecrc_n,
    output                                        cfg_err_cpl_timeout_n,
    output                                        cfg_err_cpl_abort_n,
    output                                        cfg_err_cpl_unexpect_n,
    output                                        cfg_err_posted_n,
    output                                        cfg_interrupt_n,
    input                                         cfg_interrupt_rdy_n,
    output                                        cfg_interrupt_assert_n,
    output     [7:0]                              cfg_interrupt_di,
    input      [7:0]                              cfg_interrupt_do,
    input      [2:0]                              cfg_interrupt_mmenable,
    input                                         cfg_interrupt_msienable,
    output                                        cfg_turnoff_ok_n,
    input                                         cfg_to_turnoff_n,
    output                                        cfg_pm_wake_n,

    output     [47:0]                             cfg_err_tlp_cpl_header,
    input      [15:0]                             cfg_status,
    input      [15:0]                             cfg_command,
    input      [15:0]                             cfg_dstatus,
    input      [15:0]                             cfg_dcommand,
    input      [15:0]                             cfg_lstatus,
    input      [15:0]                             cfg_lcommand,
    input      [7:0]                              cfg_bus_number,
    input      [4:0]                              cfg_device_number,
    input      [2:0]                              cfg_function_number,
    input      [2:0]                              cfg_pcie_link_state_n,
    output                                        cfg_trn_pending_n,
    output     [63:0]                             cfg_dsn
    );



    //-------------------------------------------------------
    // Local Wires PCIe
    //-------------------------------------------------------
    
    //wire   [15:0]                                     cfg_completer_id;
    //wire                                              cfg_bus_mstr_enable;
    wire                                              cfg_ext_tag_en;
    wire   [2:0]                                      cfg_max_rd_req_size;
    wire   [2:0]                                      cfg_max_payload_size;

    //-------------------------------------------------------
    // Local rx_huge_pages_addr_mod
    //-------------------------------------------------------

    wire   [63:0]     huge_page_addr_1;
    wire   [63:0]     huge_page_addr_2;
    wire              huge_page_status_1;
    wire              huge_page_status_2;
    wire              huge_page_free_1;
    wire              huge_page_free_2;

    //-------------------------------------------------------
    // Local interruption logic
    //-------------------------------------------------------
    wire              mdio_access_cfg_interrupt_n;
    wire              wr_pkt_cfg_interrupt_n;

    assign cfg_interrupt_n = mdio_access_cfg_interrupt_n & wr_pkt_cfg_interrupt_n;

    //
    // Core input tie-offs
    //

    assign trn_rnp_ok_n = 1'b0;
    assign trn_rcpl_streaming_n = 1'b1; 
    assign trn_terrfwd_n = 1'b1;

    assign cfg_err_cor_n = 1'b1;
    assign cfg_err_ur_n = 1'b1;
    assign cfg_err_ecrc_n = 1'b1;
    assign cfg_err_cpl_timeout_n = 1'b1;
    assign cfg_err_cpl_abort_n = 1'b1;
    assign cfg_err_cpl_unexpect_n = 1'b1;
    assign cfg_err_posted_n = 1'b0;
    assign cfg_pm_wake_n = 1'b1;
    assign cfg_trn_pending_n = 1'b1;
    //assign cfg_interrupt_n = 1'b1;
    assign cfg_interrupt_assert_n = 1'b1;
    assign cfg_interrupt_di = 8'b0;
    assign cfg_dwaddr = 0;
    assign cfg_rd_en_n = 1;

    assign cfg_err_tlp_cpl_header = 0;
    assign cfg_di = 0;
    assign cfg_byte_en_n = 4'hf;
    assign cfg_wr_en_n = 1;
    assign cfg_dsn = {32'h00000001,  {{8'h1},24'h000A35}};

    assign trn_rdst_rdy_n = 1'b0;   // always listen
    assign trn_tsrc_dsc_n = 1'b1;   // not discontinue


    wire [15:0] cfg_completer_id = {cfg_bus_number,
                                    cfg_device_number,
                                    cfg_function_number};

    wire cfg_bus_mstr_enable = cfg_command[2];

    assign cfg_ext_tag_en = cfg_dcommand[8];
    assign cfg_max_rd_req_size = cfg_dcommand[14:12];
    assign cfg_max_payload_size = cfg_dcommand[7:5];


    mdio_host_interface mdio_host_interface_mod (
        .trn_clk ( trn_clk ),                       // I
        .trn_lnk_up_n ( trn_lnk_up_n ),             // I

        .trn_rd ( trn_rd ),                         // I [63:0]
        .trn_rrem_n ( trn_rrem ),                   // I [7:0]
        .trn_rsof_n ( trn_rsof_n ),                 // I
        .trn_reof_n ( trn_reof_n ),                 // I
        .trn_rsrc_rdy_n ( trn_rsrc_rdy_n ),         // I
        .trn_rsrc_dsc_n ( trn_rsrc_dsc_n ),         // I

        .trn_rbar_hit_n ( trn_rbar_hit_n ),         // I [6:0]
        .trn_rdst_rdy_n ( trn_rdst_rdy_n ),         // I

        .cfg_interrupt_n ( mdio_access_cfg_interrupt_n ),               // O
        .cfg_interrupt_rdy_n ( cfg_interrupt_rdy_n ),                   // I

        //50MHz domain
        .host_clk(host_clk),                        // I
        .host_reset_n(host_reset_n),                // I
        .host_opcode ( host_opcode ),               // O [1:0]
        .host_addr ( host_addr ),                   // O [9:0]
        .host_wr_data ( host_wr_data ),             // O [31:0]
        .host_rd_data ( host_rd_data ),             // I [31:0]
        .host_miim_sel ( host_miim_sel ),           // O
        .host_req ( host_req ),                     // O
        .host_miim_rdy ( host_miim_rdy )            // I
        );


    rx_huge_pages_addr rx_huge_pages_addr_mod (
        .trn_clk ( trn_clk ),                       // I
        .trn_lnk_up_n ( trn_lnk_up_n ),             // I

        .trn_rd ( trn_rd ),                         // I [63:0]
        .trn_rrem_n ( trn_rrem ),                   // I [7:0]
        .trn_rsof_n ( trn_rsof_n ),                 // I
        .trn_reof_n ( trn_reof_n ),                 // I
        .trn_rsrc_rdy_n ( trn_rsrc_rdy_n ),         // I
        .trn_rsrc_dsc_n ( trn_rsrc_dsc_n ),         // I

        .trn_rbar_hit_n ( trn_rbar_hit_n ),         // I [6:0]
        .trn_rdst_rdy_n ( trn_rdst_rdy_n ),         // I

        .huge_page_addr_1 ( huge_page_addr_1 ),     // O [63:0]
        .huge_page_addr_2 ( huge_page_addr_2 ),     // O [63:0]
        .huge_page_status_1 ( huge_page_status_1 ), // O
        .huge_page_status_2 ( huge_page_status_2 ), // O
        .huge_page_free_1 ( huge_page_free_1 ),     // I
        .huge_page_free_2 ( huge_page_free_2 )      // I

        );

    wr_rx_pkt_to_hugepages wr_rx_pkt_to_hugepages_mod (
        .trn_clk ( trn_clk ),                       // I
        .trn_lnk_up_n ( trn_lnk_up_n ),             // I

        .trn_td ( trn_td ),                         // O [63:0]
        .trn_trem_n ( trn_trem_n ),                 // O [7:0]
        .trn_tsof_n ( trn_tsof_n ),                 // O
        .trn_teof_n ( trn_teof_n ),                 // O
        .trn_tsrc_rdy_n ( trn_tsrc_rdy_n ),         // O
        .trn_tdst_rdy_n ( trn_tdst_rdy_n ),         // I
        .trn_tbuf_av ( trn_tbuf_av ),               // I [3:0]
        .cfg_completer_id ( cfg_completer_id ),     // I [15:0]
        .cfg_interrupt_n ( wr_pkt_cfg_interrupt_n ),        // O
        .cfg_interrupt_rdy_n ( cfg_interrupt_rdy_n ),       // I

        .huge_page_addr_1 ( huge_page_addr_1 ),     // I [63:0]
        .huge_page_addr_2 ( huge_page_addr_2 ),     // I [63:0]
        .huge_page_status_1 ( huge_page_status_1 ), // I
        .huge_page_status_2 ( huge_page_status_2 ), // I
        .huge_page_free_1 ( huge_page_free_1 ),     // O
        .huge_page_free_2 ( huge_page_free_2 ),     // O

        .trigger_tlp_ack(trigger_tlp_ack),          // O
        .trigger_tlp(trigger_tlp),                  // I
        .change_huge_page_ack(change_huge_page_ack),          // O
        .change_huge_page(change_huge_page),                  // I
        .send_last_tlp_change_huge_page(send_last_tlp_change_huge_page),    // I
        .rd_addr(rd_addr),                          // O [`BF:0]
        .rd_data(rd_data),                          // I [63:0]
        .commited_rd_address(commited_rd_address),  // O [`BF+1:0]
        .qwords_to_send(qwords_to_send),            // I [4:0]
        .rd_addr_change(rd_addr_change),            // O 
        .commited_rd_address_to_mac(rd_addr_extended)     // O [`BF+1:0]

        );

endmodule // pci_exp_64b_app
