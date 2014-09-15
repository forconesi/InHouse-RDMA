//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
`timescale 1ns / 1ps
//`default_nettype none
`include "includes.v"

module  pci_exp_64b_app (

    // Transaction ( TRN ) Interface  //
    input                                         trn_clk,
    input                                         trn_reset_n,
    input                                         trn_lnk_up_n,

    // Tx Local-Link  //
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

    // To rx_tlp_trigger  //
    input                                         rx_trigger_tlp,
    output                                        rx_trigger_tlp_ack,
    input                                         rx_change_huge_page,
    output                                        rx_change_huge_page_ack,
    input                                         rx_send_last_tlp,
    input      [4:0]                              rx_qwords_to_send,
    output                                        rx_huge_page_status_1,
    output                                        rx_huge_page_status_2,

    // To rx_mac_interface //
    output     [`BF:0]                            rx_commited_rd_address,

    // To mac_host_configuration_interface  //
    input                                         host_clk,
    input                                         host_reset_n,
    output     [1:0]                              host_opcode,
    output     [9:0]                              host_addr,
    output     [31:0]                             host_wr_data,
    input      [31:0]                             host_rd_data,
    output                                        host_miim_sel,
    output                                        host_req,
    input                                         host_miim_rdy,

    // To internal_true_dual_port_ram RX  //
    output     [`BF:0]                            rx_rd_addr,
    input      [63:0]                             rx_rd_data,

    // To internal_true_dual_port_ram TX  //
    output     [8:0]                              tx_wr_addr,
    output     [63:0]                             tx_wr_data,
    output                                        tx_wr_en,

    // To tx_mac_interface  //
    input      [9:0]                              tx_commited_rd_addr,
    output     [9:0]                              tx_commited_wr_addr,

    // Rx Local-Link  //
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

    // Host ( CFG ) Interface  //
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

    //////////////////////////////////////////////////////////////////////////////////////////
    // Reception side of the NIC signal declaration
    //////////////////////////////////////////////////////////////////////////////////////////
    //-------------------------------------------------------
    // Local rx_huge_pages_addr_mod
    //-------------------------------------------------------
    wire   [63:0]     rx_huge_page_addr_1;
    wire   [63:0]     rx_huge_page_addr_2;
    //wire              rx_huge_page_status_1;
    //wire              rx_huge_page_status_2;
    wire              rx_huge_page_free_1;
    wire              rx_huge_page_free_2;

    wire   [63:0]     rx_trn_td;
    wire   [7:0]      rx_trn_trem_n;
    wire              rx_trn_tsof_n;
    wire              rx_trn_teof_n;
    wire              rx_trn_tsrc_rdy_n;
    wire              rx_cfg_interrupt_n;

    //////////////////////////////////////////////////////////////////////////////////////////
    // Transmition side of the NIC signal declaration
    //////////////////////////////////////////////////////////////////////////////////////////
    //-------------------------------------------------------
    // Local tx_huge_pages_addr_mod
    //-------------------------------------------------------
    wire   [63:0]     tx_huge_page_addr_1;
    wire   [63:0]     tx_huge_page_addr_2;
    wire   [31:0]     tx_huge_page_qwords_1;
    wire   [31:0]     tx_huge_page_qwords_2;
    wire              tx_huge_page_status_1;
    wire              tx_huge_page_status_2;
    wire              tx_huge_page_free_1;
    wire              tx_huge_page_free_2;

    //-------------------------------------------------------
    // Local tx_rd_host_mem_mod
    //-------------------------------------------------------
    wire   [63:0]     tx_trn_td;
    wire   [7:0]      tx_trn_trem_n;
    wire              tx_trn_tsof_n;
    wire              tx_trn_teof_n;
    wire              tx_trn_tsrc_rdy_n;
    wire              tx_cfg_interrupt_n;
    wire   [3:0]      tx_tlp_tag;

    //-------------------------------------------------------
    // Local tx_wr_pkt_to_bram_mod
    //-------------------------------------------------------
    wire   [63:0]     tx_completed_buffer_address;
    wire   [63:0]     tx_huge_page_addr_read_from;
    wire              tx_read_chunk;
    wire   [8:0]      tx_qwords_to_rd;
    wire              tx_read_chunk_ack;
    wire              tx_send_rd_completed;
    wire              tx_send_rd_completed_ack;
    wire              tx_send_interrupt;
    wire              tx_send_interrupt_ack;
    wire              tx_notify;
    wire   [63:0]     tx_notification_message;
    wire              tx_notify_ack;

    //////////////////////////////////////////////////////////////////////////////////////////
    // PCIe Endpoint Arbitrations
    //////////////////////////////////////////////////////////////////////////////////////////
    //-------------------------------------------------------
    // Local arbitration logic
    //-------------------------------------------------------
    wire              rx_turn;
    wire              rx_driven;
    wire              tx_turn;
    wire              tx_driven;

    //////////////////////////////////////////////////////////////////////////////////////////
    // Interrupt control logic
    //////////////////////////////////////////////////////////////////////////////////////////
    //-------------------------------------------------------
    // Local Interrupt control logic
    //-------------------------------------------------------
    wire              interrupts_enabled;
    wire              mdio_access_cfg_interrupt_n;

    //-------------------------------------------------------
    // Core input tie-offs
    //-------------------------------------------------------
    assign trn_rnp_ok_n = 1'b0;
    //assign trn_rcpl_streaming_n = 1'b1; 
    assign trn_rcpl_streaming_n = 1'b0;             // MF
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

    //////////////////////////////////////////////////////////////////////////////////////////
    // MDIO host interface
    //////////////////////////////////////////////////////////////////////////////////////////
    mdio_host_interface mdio_host_interface_mod (
        .trn_clk(trn_clk),                                     // I
        .trn_lnk_up_n(trn_lnk_up_n),                           // I
        .trn_rd(trn_rd),                                       // I [63:0]
        .trn_rrem_n(trn_rrem),                                 // I [7:0]
        .trn_rsof_n(trn_rsof_n),                               // I
        .trn_reof_n(trn_reof_n),                               // I
        .trn_rsrc_rdy_n(trn_rsrc_rdy_n),                       // I
        .trn_rsrc_dsc_n(trn_rsrc_dsc_n),                       // I
        .trn_rbar_hit_n(trn_rbar_hit_n),                       // I [6:0]
        .trn_rdst_rdy_n(trn_rdst_rdy_n),                       // I
        .cfg_interrupt_n(mdio_access_cfg_interrupt_n),         // O
        .cfg_interrupt_rdy_n(cfg_interrupt_rdy_n),             // I
        //50MHz domain
        .host_clk(host_clk),                                   // I
        .host_reset_n(host_reset_n),                           // I
        .host_opcode(host_opcode),                             // O [1:0]
        .host_addr(host_addr),                                 // O [9:0]
        .host_wr_data(host_wr_data),                           // O [31:0]
        .host_rd_data(host_rd_data),                           // I [31:0]
        .host_miim_sel(host_miim_sel),                         // O
        .host_req(host_req),                                   // O
        .host_miim_rdy(host_miim_rdy)                          // I
        );

    //////////////////////////////////////////////////////////////////////////////////////////
    // Reception side of the NIC (START)
    //////////////////////////////////////////////////////////////////////////////////////////
    //-------------------------------------------------------
    // rx_huge_pages_addr
    //-------------------------------------------------------
    rx_huge_pages_addr rx_huge_pages_addr_mod (
        .trn_clk(trn_clk),                                     // I
        .trn_lnk_up_n(trn_lnk_up_n),                           // I
        .trn_rd(trn_rd),                                       // I [63:0]
        .trn_rrem_n(trn_rrem),                                 // I [7:0]
        .trn_rsof_n(trn_rsof_n),                               // I
        .trn_reof_n(trn_reof_n),                               // I
        .trn_rsrc_rdy_n(trn_rsrc_rdy_n),                       // I
        .trn_rsrc_dsc_n(trn_rsrc_dsc_n),                       // I
        .trn_rbar_hit_n(trn_rbar_hit_n),                       // I [6:0]
        .trn_rdst_rdy_n(trn_rdst_rdy_n),                       // I
        .huge_page_addr_1(rx_huge_page_addr_1),                // O [63:0]
        .huge_page_addr_2(rx_huge_page_addr_2),                // O [63:0]
        .huge_page_status_1(rx_huge_page_status_1),            // O
        .huge_page_status_2(rx_huge_page_status_2),            // O
        .huge_page_free_1(rx_huge_page_free_1),                // I
        .huge_page_free_2(rx_huge_page_free_2)                 // I
        );

    //-------------------------------------------------------
    // rx_wr_pkt_to_hugepages
    //-------------------------------------------------------
    rx_wr_pkt_to_hugepages rx_wr_pkt_to_hugepages_mod (
        .trn_clk(trn_clk),                                     // I
        .trn_lnk_up_n(trn_lnk_up_n),                           // I
        .trn_td(rx_trn_td),                                    // O [63:0]
        .trn_trem_n(rx_trn_trem_n),                            // O [7:0]
        .trn_tsof_n(rx_trn_tsof_n),                            // O
        .trn_teof_n(rx_trn_teof_n),                            // O
        .trn_tsrc_rdy_n(rx_trn_tsrc_rdy_n),                    // O
        .trn_tdst_rdy_n(trn_tdst_rdy_n),                       // I
        .trn_tbuf_av(trn_tbuf_av),                             // I [3:0]
        .cfg_completer_id(cfg_completer_id),                   // I [15:0]
        .cfg_interrupt_n(rx_cfg_interrupt_n),                  // O
        .cfg_interrupt_rdy_n(cfg_interrupt_rdy_n),             // I
        .huge_page_addr_1(rx_huge_page_addr_1),                // I [63:0]
        .huge_page_addr_2(rx_huge_page_addr_2),                // I [63:0]
        .huge_page_status_1(rx_huge_page_status_1),            // I
        .huge_page_status_2(rx_huge_page_status_2),            // I
        .huge_page_free_1(rx_huge_page_free_1),                // O
        .huge_page_free_2(rx_huge_page_free_2),                // O
        .interrupts_enabled(interrupts_enabled),               // I
        .trigger_tlp(rx_trigger_tlp),                          // I
        .trigger_tlp_ack(rx_trigger_tlp_ack),                  // O
        .change_huge_page(rx_change_huge_page),                // I
        .change_huge_page_ack(rx_change_huge_page_ack),        // O
        .send_last_tlp(rx_send_last_tlp),                      // I
        .qwords_to_send(rx_qwords_to_send),                    // I [4:0]
        .commited_rd_address(rx_commited_rd_address),          // O [`BF:0]
        .rd_addr(rx_rd_addr),                                  // O [`BF:0]
        .rd_data(rx_rd_data),                                  // I [63:0]
        .my_turn(rx_turn),                                     // I
        .driving_interface(rx_driven)                          // O
        );

    //////////////////////////////////////////////////////////////////////////////////////////
    // Reception side of the NIC (END)
    //////////////////////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////////////////////
    // Transmition side of the NIC (START)
    //////////////////////////////////////////////////////////////////////////////////////////
    //-------------------------------------------------------
    // tx_huge_pages_addr
    //-------------------------------------------------------
    tx_huge_pages_addr tx_huge_pages_addr_mod (
        .trn_clk(trn_clk),                                     // I
        .trn_lnk_up_n(trn_lnk_up_n),                           // I
        .trn_rd(trn_rd),                                       // I [63:0]
        .trn_rrem_n(trn_rrem),                                 // I [7:0]
        .trn_rsof_n(trn_rsof_n),                               // I
        .trn_reof_n(trn_reof_n),                               // I
        .trn_rsrc_rdy_n(trn_rsrc_rdy_n),                       // I
        .trn_rsrc_dsc_n(trn_rsrc_dsc_n),                       // I
        .trn_rbar_hit_n(trn_rbar_hit_n),                       // I [6:0]
        .trn_rdst_rdy_n(trn_rdst_rdy_n),                       // I
        .huge_page_addr_1(tx_huge_page_addr_1),                // O [63:0]
        .huge_page_addr_2(tx_huge_page_addr_2),                // O [63:0]
        .huge_page_qwords_1(tx_huge_page_qwords_1),            // O [31:0]
        .huge_page_qwords_2(tx_huge_page_qwords_2),            // O [31:0]
        .huge_page_status_1(tx_huge_page_status_1),            // O
        .huge_page_status_2(tx_huge_page_status_2),            // O
        .huge_page_free_1(tx_huge_page_free_1),                // I
        .huge_page_free_2(tx_huge_page_free_2),                // I
        .completed_buffer_address(tx_completed_buffer_address) // O [63:0]
        );

    //-------------------------------------------------------
    // tx_rd_host_mem
    //-------------------------------------------------------
    tx_rd_host_mem tx_rd_host_mem_mod (
        .trn_clk(trn_clk),                                     // I
        .trn_lnk_up_n(trn_lnk_up_n),                           // I
        .trn_td(tx_trn_td),                                    // O [63:0]
        .trn_trem_n(tx_trn_trem_n),                            // O [7:0]
        .trn_tsof_n(tx_trn_tsof_n),                            // O
        .trn_teof_n(tx_trn_teof_n),                            // O
        .trn_tsrc_rdy_n(tx_trn_tsrc_rdy_n),                    // O
        .trn_tdst_rdy_n(trn_tdst_rdy_n),                       // I
        .trn_tbuf_av(trn_tbuf_av),                             // I [3:0]
        .cfg_completer_id(cfg_completer_id),                   // I [15:0]
        .cfg_interrupt_n(tx_cfg_interrupt_n),                  // O
        .cfg_interrupt_rdy_n(cfg_interrupt_rdy_n),             // I
        .completed_buffer_address(tx_completed_buffer_address),// I [63:0]
        .huge_page_addr(tx_huge_page_addr_read_from),          // I [63:0]
        .read_chunk(tx_read_chunk),                            // I
        .tlp_tag(tx_tlp_tag),                                  // O [3:0]
        .qwords_to_rd(tx_qwords_to_rd),                        // I [8:0]
        .read_chunk_ack(tx_read_chunk_ack),                    // O
        .send_rd_completed(tx_send_rd_completed),              // I
        .send_rd_completed_ack(tx_send_rd_completed_ack),      // O
        .notify(tx_notify),                                    // I
        .notification_message(tx_notification_message),        // I [63:0]
        .notify_ack(tx_notify_ack),                            // O
        .send_interrupt(tx_send_interrupt),                    // I
        .send_interrupt_ack(tx_send_interrupt_ack),            // O
        .my_turn(tx_turn),                                     // I
        .driving_interface(tx_driven)                          // O
        );

    //-------------------------------------------------------
    // tx_wr_pkt_to_bram
    //-------------------------------------------------------
    tx_wr_pkt_to_bram tx_wr_pkt_to_bram_mod (
        .trn_clk(trn_clk),                                     // I
        .trn_lnk_up_n(trn_lnk_up_n),                           // I
        .trn_rd(trn_rd),                                       // I [63:0]
        .trn_rrem_n(trn_rrem),                                 // I [7:0]
        .trn_rsof_n(trn_rsof_n),                               // I
        .trn_reof_n(trn_reof_n),                               // I
        .trn_rsrc_rdy_n(trn_rsrc_rdy_n),                       // I
        .trn_rsrc_dsc_n(trn_rsrc_dsc_n),                       // I
        .trn_rbar_hit_n(trn_rbar_hit_n),                       // I [6:0]
        .trn_rdst_rdy_n(trn_rdst_rdy_n),                       // I
        .huge_page_addr_1(tx_huge_page_addr_1),                // I [63:0]
        .huge_page_addr_2(tx_huge_page_addr_2),                // I [63:0]
        .huge_page_qwords_1(tx_huge_page_qwords_1),            // I [31:0]
        .huge_page_qwords_2(tx_huge_page_qwords_2),            // I [31:0]
        .huge_page_status_1(tx_huge_page_status_1),            // I
        .huge_page_status_2(tx_huge_page_status_2),            // I
        .huge_page_free_1(tx_huge_page_free_1),                // O
        .huge_page_free_2(tx_huge_page_free_2),                // O
        .interrupts_enabled(interrupts_enabled),               // I
        .huge_page_addr_read_from(tx_huge_page_addr_read_from),// O [63:0]
        .read_chunk(tx_read_chunk),                            // O
        .tlp_tag(tx_tlp_tag),                                  // I [3:0]
        .qwords_to_rd(tx_qwords_to_rd),                        // O [8:0]
        .read_chunk_ack(tx_read_chunk_ack),                    // I
        .send_rd_completed(tx_send_rd_completed),              // O
        .send_rd_completed_ack(tx_send_rd_completed_ack),      // I
        .notify(tx_notify),                                    // O
        .notification_message(tx_notification_message),        // O [63:0]
        .notify_ack(tx_notify_ack),                            // I
        .send_interrupt(tx_send_interrupt),                    // O 
        .send_interrupt_ack(tx_send_interrupt_ack),            // I
        .wr_addr(tx_wr_addr),                                  // O [8:0]
        .wr_data(tx_wr_data),                                  // O [63:0]
        .wr_en(tx_wr_en),                                      // O
        .commited_rd_addr(tx_commited_rd_addr),                // I [9:0]
        .commited_wr_addr(tx_commited_wr_addr)                 // O [9:0]
        );
    //////////////////////////////////////////////////////////////////////////////////////////
    // Transmition side of the NIC (END)
    //////////////////////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////////////////////
    // PCIe Endpoint Arbitrations Shared endpoint interface
    //////////////////////////////////////////////////////////////////////////////////////////
    enpoint_arbitration enpoint_arbitration_mod (
        .trn_clk(trn_clk),                                     // I
        .trn_lnk_up_n(trn_lnk_up_n),                           // I
        .rx_turn(rx_turn),                                     // O
        .rx_driven(rx_driven),                                 // I
        .tx_turn(tx_turn),                                     // O
        .tx_driven(tx_driven)                                  // I 
        );
    // debug
    //assign rx_turn = 1'b0;
    //assign tx_turn = 1'b1;

    assign trn_td = rx_trn_td | tx_trn_td;
    assign trn_trem_n = rx_trn_trem_n & tx_trn_trem_n;
    assign trn_tsof_n = rx_trn_tsof_n & tx_trn_tsof_n;
    assign trn_teof_n = rx_trn_teof_n & tx_trn_teof_n;
    assign trn_tsrc_rdy_n = rx_trn_tsrc_rdy_n & tx_trn_tsrc_rdy_n;
    assign cfg_interrupt_n = mdio_access_cfg_interrupt_n & rx_cfg_interrupt_n & tx_cfg_interrupt_n;               // Active low

    //////////////////////////////////////////////////////////////////////////////////////////
    // Interrupt control logic
    //////////////////////////////////////////////////////////////////////////////////////////
    interrupt_en interrupt_en_mod (
        .trn_clk(trn_clk),                                     // I
        .trn_lnk_up_n(trn_lnk_up_n),                           // I
        .trn_rd(trn_rd),                                       // I [63:0]
        .trn_rrem_n(trn_rrem),                                 // I [7:0]
        .trn_rsof_n(trn_rsof_n),                               // I
        .trn_reof_n(trn_reof_n),                               // I
        .trn_rsrc_rdy_n(trn_rsrc_rdy_n),                       // I
        .trn_rsrc_dsc_n(trn_rsrc_dsc_n),                       // I
        .trn_rbar_hit_n(trn_rbar_hit_n),                       // I [6:0]
        .trn_rdst_rdy_n(trn_rdst_rdy_n),                       // I
        .interrupts_enabled(interrupts_enabled)                // O
        );

endmodule // pci_exp_64b_app
