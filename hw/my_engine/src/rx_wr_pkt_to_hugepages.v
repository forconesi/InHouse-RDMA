//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
`timescale 1ns / 1ps
//`default_nettype none
`include "includes.v"

`define TX_MEM_WR64_FMT_TYPE 7'b11_00000

module rx_wr_pkt_to_hugepages (

    input                  trn_clk,
    input                  trn_lnk_up_n,

    // Rx Local-Link  //
    output reg  [63:0]     trn_td,
    output reg  [7:0]      trn_trem_n,
    output reg             trn_tsof_n,
    output reg             trn_teof_n,
    output reg             trn_tsrc_rdy_n,
    input                  trn_tdst_rdy_n,
    input       [3:0]      trn_tbuf_av,
    input       [15:0]     cfg_completer_id,

    // Internal logic  //
    input       [63:0]     huge_page_addr_1,
    input       [63:0]     huge_page_addr_2,
    input                  huge_page_status_1,
    input                  huge_page_status_2,
    output reg             huge_page_free_1,
    output reg             huge_page_free_2,

    input                  trigger_tlp,
    output reg             trigger_tlp_ack,
    input                  change_huge_page,
    output reg             change_huge_page_ack,
    input                  send_last_tlp,
    input       [4:0]      qwords_to_send,

    output reg  [`BF:0]    commited_rd_address,
    output reg  [`BF:0]    rd_addr,
    input       [63:0]     rd_data,

    // Arbitrations handshake  //
    input                  my_turn,
    output reg             driving_interface
    );

    wire            reset_n;
    
    // localparam
    localparam s0  = 15'b000000000000000;
    localparam s1  = 15'b000000000000001;
    localparam s2  = 15'b000000000000010;
    localparam s3  = 15'b000000000000100;
    localparam s4  = 15'b000000000001000;
    localparam s5  = 15'b000000000010000;
    localparam s6  = 15'b000000000100000;
    localparam s7  = 15'b000000001000000;
    localparam s8  = 15'b000000010000000;
    localparam s9  = 15'b000000100000000;
    localparam s10 = 15'b000001000000000;
    localparam s11 = 15'b000010000000000;
    localparam s12 = 15'b000100000000000;
    localparam s13 = 15'b001000000000000;
    localparam s14 = 15'b010000000000000;
    localparam s15 = 15'b100000000000000;

    //-------------------------------------------------------
    // Local current_huge_page_addr
    //-------------------------------------------------------
    reg     [63:0]      current_huge_page_addr;
    reg     [14:0]      give_huge_page_fsm;
    reg     [14:0]      free_huge_page_fsm;
    reg                 huge_page_available;

    //-------------------------------------------------------
    // Local send_tlps_machine
    //-------------------------------------------------------   
    reg     [14:0]      send_fsm;
    reg                 return_huge_page_to_host;
    reg     [8:0]       tlp_qword_counter;
    reg     [31:0]      tlp_number;
    reg     [31:0]      look_ahead_tlp_number;
    reg     [8:0]       qwords_in_tlp;
    reg     [63:0]      host_mem_addr;
    reg     [63:0]      look_ahead_host_mem_addr;
    reg     [31:0]      huge_page_qword_counter;
    reg     [31:0]      look_ahead_huge_page_qword_counter;
    reg                 endpoint_not_ready;
    reg                 remember_to_change_huge_page;
    reg     [`BF:0]     rd_addr_prev1;
    reg     [`BF:0]     rd_addr_prev2;
    
    assign reset_n = ~trn_lnk_up_n;

    ////////////////////////////////////////////////
    // current_huge_page_addr
    ////////////////////////////////////////////////
    always @( posedge trn_clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            huge_page_free_1 <= 1'b0;
            huge_page_free_2 <= 1'b0;
            huge_page_available <= 1'b0;
            current_huge_page_addr <= 64'b0;
            give_huge_page_fsm <= s0;
            free_huge_page_fsm <= s0;
        end

        else begin  // not reset

            case (free_huge_page_fsm)
                s0 : begin
                    if (return_huge_page_to_host) begin
                        huge_page_free_1 <= 1'b1;
                        free_huge_page_fsm <= s1;
                    end
                end
                s1 : begin
                    huge_page_free_1 <= 1'b0;
                    free_huge_page_fsm <= s2;
                end
                s2 : begin
                    if (return_huge_page_to_host) begin
                        huge_page_free_2 <= 1'b1;
                        free_huge_page_fsm <= s3;
                    end
                end
                s3 : begin
                    huge_page_free_2 <= 1'b0;
                    free_huge_page_fsm <= s0;
                end
            endcase

            case (give_huge_page_fsm)
                s0 : begin
                    if (huge_page_status_1) begin
                        huge_page_available <= 1'b1;
                        current_huge_page_addr <= huge_page_addr_1;
                        give_huge_page_fsm <= s1;
                    end
                end

                s1 : begin
                    if (return_huge_page_to_host) begin
                        huge_page_available <= 1'b0;
                        give_huge_page_fsm <= s2;
                    end
                end

                s2 : begin
                    if (huge_page_status_2) begin
                        huge_page_available <= 1'b1;
                        current_huge_page_addr <= huge_page_addr_2;
                        give_huge_page_fsm <= s3;
                    end
                end

                s3 : begin
                    if (return_huge_page_to_host) begin
                        huge_page_available <= 1'b0;
                        give_huge_page_fsm <= s0;
                    end
                end
            endcase

        end     // not reset
    end  //always

    ////////////////////////////////////////////////
    // write request TLP generation to huge_page
    ////////////////////////////////////////////////
    always @( posedge trn_clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            trn_td <= 'b0;
            trn_trem_n <= 'hFF;
            trn_tsof_n <= 1'b1;
            trn_teof_n <= 1'b1;
            trn_tsrc_rdy_n <= 1'b1;

            endpoint_not_ready <= 1'b0;
            remember_to_change_huge_page <= 1'b0;
            return_huge_page_to_host <= 1'b0;
            driving_interface <= 1'b0;

            trigger_tlp_ack <= 1'b0;
            change_huge_page_ack <= 1'b0;

            commited_rd_address <= 'b0;
            rd_addr <= 'b0;

            tlp_qword_counter <= 'b0;
            tlp_number <= 'b0;

            send_fsm <= s0;
        end
        
        else begin  // not reset

            rd_addr_prev1 <= rd_addr;
            rd_addr_prev2 <= rd_addr_prev1;

            case (send_fsm)

                s0 : begin
                    host_mem_addr <= current_huge_page_addr + 'h80;
                    huge_page_qword_counter <= 'b0;
                    if (huge_page_available) begin
                        send_fsm <= s1;
                    end
                end

                s1 : begin
                    qwords_in_tlp <= {4'b0, qwords_to_send};
                    endpoint_not_ready <= 1'b0;

                    driving_interface <= 1'b0;                                              // we're taking the risk of starving the tx process
                    trn_td <= 64'b0;
                    trn_trem_n <= 8'hFF;
                    if ( (trn_tbuf_av[1]) && (!trn_tdst_rdy_n) && (my_turn || driving_interface) ) begin
                        if (change_huge_page || remember_to_change_huge_page) begin
                            remember_to_change_huge_page <= 1'b0;
                            change_huge_page_ack <= 1'b1;
                            driving_interface <= 1'b1;
                            send_fsm <= s8;
                        end
                        else if (send_last_tlp) begin
                            remember_to_change_huge_page <= 1'b1;
                            driving_interface <= 1'b1;
                            send_fsm <= s2;
                        end
                        else if (trigger_tlp) begin
                            driving_interface <= 1'b1;
                            trigger_tlp_ack <= 1'b1;
                            send_fsm <= s2;
                        end
                    end
                end

                s2 : begin
                    trn_trem_n <= 8'b0;
                    trn_td[63:32] <= {
                                1'b0,   //reserved
                                `TX_MEM_WR64_FMT_TYPE, //memory write request 64bit addressing
                                1'b0,   //reserved
                                3'b0,   //TC (traffic class)
                                4'b0,   //reserved
                                1'b0,   //TD (TLP digest present)
                                1'b0,   //EP (poisoned data)
                                2'b00,  //Relaxed ordering, No snoop in processor cache
                                2'b0,   //reserved
                                {qwords_in_tlp, 1'b0}  //lenght in DWs. 10-bit field    // QWs x2 equals DWs
                            };
                    trn_td[31:0] <= {
                                cfg_completer_id,   //Requester ID
                                {4'b0, tlp_number[3:0] },   //Tag
                                4'hF,   //last DW byte enable
                                4'hF    //1st DW byte enable
                            };
                    trn_tsof_n <= 1'b0;
                    trn_tsrc_rdy_n <= 1'b0;
                    rd_addr <= rd_addr +1;
                    trigger_tlp_ack <= 1'b0;

                    look_ahead_host_mem_addr <= host_mem_addr + {qwords_in_tlp, 3'b0};
                    look_ahead_huge_page_qword_counter <= huge_page_qword_counter + qwords_in_tlp;
                    look_ahead_tlp_number <= tlp_number +1;

                    send_fsm <= s3;
                end

                s3 : begin
                    if (!trn_tdst_rdy_n) begin
                        trn_tsof_n <= 1'b1;
                        trn_tsrc_rdy_n <= 1'b0;
                        trn_td <= host_mem_addr;

                        if (!endpoint_not_ready) begin
                            rd_addr <= rd_addr +1;
                            send_fsm <= s4;
                        end
                        else begin
                            send_fsm <= s6;
                        end
                    end
                    else begin
                        endpoint_not_ready <= 1'b1;
                        rd_addr <= rd_addr_prev1;
                    end
                    tlp_qword_counter <= 9'b1;
                end

                s4 : begin
                    if (!trn_tdst_rdy_n) begin
                        trn_tsrc_rdy_n <= 1'b0;
                        trn_td <= {rd_data[7:0], rd_data[15:8], rd_data[23:16], rd_data[31:24], rd_data[39:32], rd_data[47:40], rd_data[55:48] ,rd_data[63:56]};

                        rd_addr <= rd_addr +1;

                        tlp_qword_counter <= tlp_qword_counter +1;
                        if (tlp_qword_counter == qwords_in_tlp) begin
                            trn_teof_n <= 1'b0;
                            send_fsm <= s5;
                        end
                    end
                    else begin
                        rd_addr <= rd_addr_prev2;
                        send_fsm <= s6;
                    end
                end

                s5 : begin
                    commited_rd_address <= rd_addr_prev2;
                    rd_addr <= rd_addr_prev2;
                    host_mem_addr <= look_ahead_host_mem_addr;
                    huge_page_qword_counter <= look_ahead_huge_page_qword_counter;
                    tlp_number <= look_ahead_tlp_number;
                    if (!trn_tdst_rdy_n) begin
                        trn_teof_n <= 1'b1;
                        trn_tsrc_rdy_n <= 1'b1;
                        send_fsm <= s1;
                    end
                end

                s6 : begin
                    if (!trn_tdst_rdy_n) begin
                        rd_addr <= rd_addr +1;
                        trn_tsrc_rdy_n <= 1'b1;
                        send_fsm <= s7;
                    end
                end

                s7 : begin
                    trn_tsrc_rdy_n <= 1'b1;
                    rd_addr <= rd_addr +1;
                    send_fsm <= s4;
                end
                
                s8 : begin
                    trn_trem_n <= 8'b0;
                    trn_td[63:32] <= {
                                1'b0,   //reserved
                                `TX_MEM_WR64_FMT_TYPE, //memory write request 64bit addressing
                                1'b0,   //reserved
                                3'b0,   //TC (traffic class)
                                4'b0,   //reserved
                                1'b0,   //TD (TLP digest present)
                                1'b0,   //EP (poisoned data)
                                2'b00,  //Relaxed ordering, No snoop in processor cache
                                2'b0,   //reserved
                                10'h02  //lenght equal 2 DW 
                            };
                    trn_td[31:0] <= {
                                cfg_completer_id,   //Requester ID
                                {4'b0, 4'b0 },   //Tag
                                4'hF,   //last DW byte enable
                                4'hF    //1st DW byte enable
                            };
                    trn_tsof_n <= 1'b0;
                    trn_tsrc_rdy_n <= 1'b0;
                    change_huge_page_ack <= 1'b0;
                    send_fsm <= s9;
                end

                s9 : begin
                    if (!trn_tdst_rdy_n) begin
                        trn_tsof_n <= 1'b1;
                        return_huge_page_to_host <= 1'b1;
                        trn_td <= current_huge_page_addr;
                        send_fsm <= s10;
                    end
                end

                s10 : begin
                    return_huge_page_to_host <= 1'b0;
                    if (!trn_tdst_rdy_n) begin
                        trn_td <= {huge_page_qword_counter[7:0], huge_page_qword_counter[15:8], huge_page_qword_counter[23:16], huge_page_qword_counter[31:24], 32'b0};
                        trn_teof_n <= 1'b0;
                        send_fsm <= s11;
                    end
                end

                s11 : begin
                    if (!trn_tdst_rdy_n) begin
                        trn_teof_n <= 1'b1;
                        trn_tsrc_rdy_n <= 1'b1;
                        send_fsm <= s0;
                    end
                end

                default : begin 
                    send_fsm <= s0;
                end

            endcase
        end     // not reset
    end  //always
   

endmodule // rx_wr_pkt_to_hugepages

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
