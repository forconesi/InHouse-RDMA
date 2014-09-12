//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
`timescale 1ns / 1ps
//`default_nettype none
`include "includes.v"

module rx_trigger_synch (

    input    clk_out,
    input    reset_n_clk_out,

    input    clk_in,
    input    reset_n_clk_in,

    input                     trigger_tlp_in,
    output reg                trigger_tlp_out,

    input                     trigger_tlp_ack_in,
    output reg                trigger_tlp_ack_out,

    input                     change_huge_page_in,
    output reg                change_huge_page_out,

    input                     change_huge_page_ack_in,
    output reg                change_huge_page_ack_out,

    input                     send_last_tlp_in,
    output reg                send_last_tlp_out,

    input         [4:0]       qwords_to_send_in,
    output reg    [4:0]       qwords_to_send_out,

    input                     huge_page_status_1_in,
    output reg                huge_page_status_1_out,

    input                     huge_page_status_2_in,
    output reg                huge_page_status_2_out
    );

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
    // Local clk_in - trigger_tlp & send_last_tlp & qwords_to_send
    //-------------------------------------------------------
    reg     [14:0]   fsm_clk_in;
    reg              trigger_tlp_internal;
    reg              send_last_tlp_internal;
    reg              change_huge_page_internal;
    reg              trigger_tlp_internal_ack_reg0;
    reg              trigger_tlp_internal_ack_reg1;
    reg              send_last_tlp_internal_ack_reg0;
    reg              send_last_tlp_internal_ack_reg1;
    reg              change_huge_page_internal_ack_reg0;
    reg              change_huge_page_internal_ack_reg1;
    reg     [4:0]    qwords_to_send_internal;
    reg              huge_page_status_1_reg0;
    reg              huge_page_status_2_reg0;

    //-------------------------------------------------------
    // Local clk_out - trigger_tlp & send_last_tlp & qwords_to_send
    //-------------------------------------------------------
    reg     [14:0]   fsm_clk_out;
    reg              trigger_tlp_internal_reg0;
    reg              trigger_tlp_internal_reg1;
    reg              trigger_tlp_internal_ack;
    reg              send_last_tlp_internal_reg0;
    reg              send_last_tlp_internal_reg1;
    reg              send_last_tlp_internal_ack;
    reg              change_huge_page_internal_reg0;
    reg              change_huge_page_internal_reg1;
    reg              change_huge_page_internal_ack;
    reg     [4:0]    qwords_to_send_internal_reg0;

    ////////////////////////////////////////////////
    // clk_in - trigger_tlp & send_last_tlp & qwords_to_send
    ////////////////////////////////////////////////
    always @( posedge clk_in or negedge reset_n_clk_in ) begin

        if (!reset_n_clk_in ) begin  // reset
            trigger_tlp_ack_out <= 1'b0;
            change_huge_page_ack_out <= 1'b0;

            trigger_tlp_internal <= 1'b0;
            send_last_tlp_internal <= 1'b0;
            change_huge_page_internal <= 1'b0;

            trigger_tlp_internal_ack_reg0 <= 1'b0;
            trigger_tlp_internal_ack_reg1 <= 1'b0;
            send_last_tlp_internal_ack_reg0 <= 1'b0;
            send_last_tlp_internal_ack_reg1 <= 1'b0;
            change_huge_page_internal_ack_reg0 <= 1'b0;
            change_huge_page_internal_ack_reg1 <= 1'b0;

            huge_page_status_1_reg0 <= 1'b0;
            huge_page_status_1_out <= 1'b0;
            huge_page_status_2_reg0 <= 1'b0;
            huge_page_status_2_out <= 1'b0;

            fsm_clk_in <= s0;
        end
        
        else begin  // not reset

            huge_page_status_1_reg0 <= huge_page_status_1_in;
            huge_page_status_1_out <= huge_page_status_1_reg0;
            huge_page_status_2_reg0 <= huge_page_status_2_in;
            huge_page_status_2_out <= huge_page_status_2_reg0;

            trigger_tlp_internal_ack_reg0 <= trigger_tlp_internal_ack;
            trigger_tlp_internal_ack_reg1 <= trigger_tlp_internal_ack_reg0;

            send_last_tlp_internal_ack_reg0 <= send_last_tlp_internal_ack;
            send_last_tlp_internal_ack_reg1 <= send_last_tlp_internal_ack_reg0;

            change_huge_page_internal_ack_reg0 <= change_huge_page_internal_ack;
            change_huge_page_internal_ack_reg1 <= change_huge_page_internal_ack_reg0;

            case (fsm_clk_in)

                s0 : begin
                    if (trigger_tlp_in) begin
                        trigger_tlp_ack_out <= 1'b1;
                        qwords_to_send_internal <= qwords_to_send_in;
                        fsm_clk_in <= s1;
                    end
                    else if (send_last_tlp_in) begin
                        change_huge_page_ack_out <= 1'b1;
                        qwords_to_send_internal <= qwords_to_send_in;
                        fsm_clk_in <= s3;
                    end
                    else if (change_huge_page_in) begin
                        change_huge_page_ack_out <= 1'b1;
                        change_huge_page_internal <= 1'b1;
                        fsm_clk_in <= s5;
                    end
                end

                s1 : begin
                    trigger_tlp_ack_out <= 1'b0;
                    trigger_tlp_internal <= 1'b1;
                    fsm_clk_in <= s2;
                end

                s2 : begin
                    if (trigger_tlp_internal_ack_reg1) begin
                        trigger_tlp_internal <= 1'b0;
                        fsm_clk_in <= s6;
                    end
                end

                s3 : begin
                    change_huge_page_ack_out <= 1'b0;
                    send_last_tlp_internal <= 1'b1;
                    fsm_clk_in <= s4;
                end

                s4 : begin
                    if (send_last_tlp_internal_ack_reg1) begin
                        send_last_tlp_internal <= 1'b0;
                        fsm_clk_in <= s6;
                    end
                end

                s5 : begin
                    change_huge_page_ack_out <= 1'b0;
                    if (change_huge_page_internal_ack_reg1) begin
                        change_huge_page_internal <= 1'b0;
                        fsm_clk_in <= s6;
                    end
                end

                s6 : fsm_clk_in <= s0;

                default : begin 
                    fsm_clk_in <= s0;
                end

            endcase
        end     // not reset
    end  //always

    ////////////////////////////////////////////////
    // clk_out - trigger_tlp & send_last_tlp & qwords_to_send
    ////////////////////////////////////////////////
    always @( posedge clk_out or negedge reset_n_clk_out ) begin

        if (!reset_n_clk_out ) begin  // reset
            trigger_tlp_out <= 1'b0;
            send_last_tlp_out <= 1'b0;
            change_huge_page_out <= 1'b0;

            trigger_tlp_internal_reg0 <= 1'b0;
            trigger_tlp_internal_reg1 <= 1'b0;
            trigger_tlp_internal_ack <= 1'b0;

            send_last_tlp_internal_reg0 <= 1'b0;
            send_last_tlp_internal_reg1 <= 1'b0;
            send_last_tlp_internal_ack <= 1'b0;

            change_huge_page_internal_reg0 <= 1'b0;
            change_huge_page_internal_reg1 <= 1'b0;
            change_huge_page_internal_ack <= 1'b0;

            fsm_clk_out <= s0;
        end
        
        else begin  // not reset

            qwords_to_send_internal_reg0 <= qwords_to_send_internal;
            
            trigger_tlp_internal_reg0 <= trigger_tlp_internal;
            trigger_tlp_internal_reg1 <= trigger_tlp_internal_reg0;

            send_last_tlp_internal_reg0 <= send_last_tlp_internal;
            send_last_tlp_internal_reg1 <= send_last_tlp_internal_reg0;

            change_huge_page_internal_reg0 <= change_huge_page_internal;
            change_huge_page_internal_reg1 <= change_huge_page_internal_reg0;

            case (fsm_clk_out)

                s0 : begin
                    if (trigger_tlp_internal_reg1) begin
                        trigger_tlp_out <= 1'b1;
                        qwords_to_send_out <= qwords_to_send_internal_reg0;
                        fsm_clk_out <= s1;
                    end
                    else if (send_last_tlp_internal_reg1) begin
                        send_last_tlp_out <= 1'b1;
                        qwords_to_send_out <= qwords_to_send_internal_reg0;
                        fsm_clk_out <= s2;
                    end
                    else if (change_huge_page_internal_reg1) begin
                        change_huge_page_out <= 1'b1;
                        fsm_clk_out <= s3;
                    end
                end

                s1 : begin
                    if (trigger_tlp_ack_in) begin
                        trigger_tlp_out <= 1'b0;
                        trigger_tlp_internal_ack <= 1'b1;
                        fsm_clk_out <= s4;
                    end
                end

                s2 : begin
                    if (change_huge_page_ack_in) begin
                        send_last_tlp_out <= 1'b0;
                        send_last_tlp_internal_ack <= 1'b1;
                        fsm_clk_out <= s4;
                    end
                end

                s3 : begin
                    if (change_huge_page_ack_in) begin
                        change_huge_page_out <= 1'b0;
                        change_huge_page_internal_ack <= 1'b1;
                        fsm_clk_out <= s4;
                    end
                end

                s4 : fsm_clk_out <= s5;

                s5 : begin
                    trigger_tlp_internal_ack <= 1'b0;
                    send_last_tlp_internal_ack <= 1'b0;
                    change_huge_page_internal_ack <= 1'b0;
                    fsm_clk_out <= s6;
                end

                s6 : fsm_clk_out <= s7;
                s7 : fsm_clk_out <= s8;
                s8 : fsm_clk_out <= s9;
                s9 : fsm_clk_out <= s10;
                s10 : fsm_clk_out <= s11;
                s11 : fsm_clk_out <= s12;
                s12 : fsm_clk_out <= s13;
                s13 : fsm_clk_out <= s14;
                s14 : fsm_clk_out <= s15;
                s15 : fsm_clk_out <= s0;

                default : begin 
                    fsm_clk_out <= s0;
                end

            endcase

        end     // not reset
    end  //always

endmodule // rx_trigger_synch

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
