//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
`timescale 1ns / 1ps
//`default_nettype none
`include "includes.v"

module rx_tlp_trigger (

    input    clk,
    input    trn_lnk_up_n,

    output reg              cfg_interrupt_n,
    input                   cfg_interrupt_rdy_n,
    input                   interrupts_enabled,
    input      [31:0]       interrupt_period,

    // Internal logic
    input      [`BF:0]      commited_wr_address,
    output reg              trigger_tlp,
    input                   trigger_tlp_ack,
    output reg              change_huge_page,
    input                   change_huge_page_ack,
    output reg              send_last_tlp,
    output reg [4:0]        qwords_to_send,
    input                   huge_page_status_1,
    input                   huge_page_status_2
    );

    wire            reset_n;

    // localparam
    localparam s0  = 10'b0000000000;
    localparam s1  = 10'b0000000001;
    localparam s2  = 10'b0000000010;
    localparam s3  = 10'b0000000100;
    localparam s4  = 10'b0000001000;
    localparam s5  = 10'b0000010000;
    localparam s6  = 10'b0000100000;
    localparam s7  = 10'b0001000000;
    localparam s8  = 10'b0010000000;
    localparam s9  = 10'b0100000000;
    localparam s10 = 10'b1000000000;

    //-------------------------------------------------------
    // Local interrupt-logic
    //-------------------------------------------------------
    reg     [9:0]        interrupt_fsm;
    reg     [31:0]       interrupt_free_running;

    //-------------------------------------------------------
    // Local timeout-generation
    //-------------------------------------------------------
    reg     [15:0]       free_running;
    reg                  timeout;

    //-------------------------------------------------------
    // Local trigger-logic
    //-------------------------------------------------------
    reg     [9:0]        trigger_fsm;
    reg     [`BF:0]      diff;
    reg     [`BF:0]      diff_reg;
    reg     [`BF:0]      commited_rd_address;
    reg     [`BF:0]      look_ahead_commited_rd_address;
    reg                  huge_page_dirty;
    reg     [18:0]       huge_buffer_qword_counter;
    reg     [18:0]       aux_huge_buffer_qword_counter;
    reg     [18:0]       look_ahead_huge_buffer_qword_counter;
    reg     [3:0]        qwords_remaining;
    reg     [4:0]        number_of_tlp_sent;
    reg     [4:0]        look_ahead_number_of_tlp_sent;
    reg     [4:0]        number_of_tlp_to_send;

    assign reset_n = ~trn_lnk_up_n;

    ////////////////////////////////////////////////
    // interrupt-logic
    ////////////////////////////////////////////////
    always @( posedge clk or negedge reset_n ) begin
        if (!reset_n ) begin  // reset
            cfg_interrupt_n <= 1'b1;
            interrupt_free_running <= 'b0;
            interrupt_fsm <= s0;
        end
        else begin  // not reset

            case (interrupt_fsm)

                s0 : begin
                    interrupt_free_running <= interrupt_free_running +1;
                    if (interrupt_free_running == interrupt_period) begin
                        interrupt_fsm <= s1;
                    end
                end

                s1 : begin
                    interrupt_free_running <= 'b0;
                    if (!diff) begin
                        interrupt_fsm <= s2;
                    end
                end

                s2 : begin
                    if (interrupts_enabled) begin
                        cfg_interrupt_n <= 1'b0;
                        interrupt_fsm <= s3;
                    end
                    else begin
                        interrupt_fsm <= s0;
                    end
                end

                s3 : begin
                    if (!cfg_interrupt_rdy_n) begin
                        cfg_interrupt_n <= 1'b1;
                        interrupt_fsm <= s0;
                    end
                end

                default : begin
                    interrupt_fsm <= s0;
                end

            endcase
        end     // not reset
    end  //always

    ////////////////////////////////////////////////
    // timeout logic
    ////////////////////////////////////////////////
    always @( posedge clk or negedge reset_n ) begin
        if (!reset_n ) begin  // reset
            timeout <= 1'b0;
            free_running <= 'b0;
        end
        
        else begin  // not reset

            if (trigger_fsm == s0) begin
                free_running <= free_running +1;
                timeout <= 1'b0;
                if (free_running == 'h10000) begin
                    timeout <= 1'b1;
                end
                else if (huge_page_status_1 && huge_page_status_2) begin
                    if (free_running == 'h20) begin
                        timeout <= 1'b1;
                    end
                end
            end
            else begin
                timeout <= 1'b0;
                free_running <= 'b0;
            end

        end     // not reset
    end  //always

    ////////////////////////////////////////////////
    // trigger-logic
    ////////////////////////////////////////////////
    always @( posedge clk or negedge reset_n ) begin
        
        if (!reset_n ) begin  // reset
            trigger_tlp <= 1'b0;
            change_huge_page <= 1'b0;
            send_last_tlp <= 1'b0;

            diff <= 'b0;
            commited_rd_address <= 'b0;
            huge_buffer_qword_counter <= 'h10;
            huge_page_dirty <= 1'b0;
            qwords_remaining <= 'b0;

            trigger_fsm <= s0;
        end

        else begin  // not reset

            diff <= commited_wr_address + (~commited_rd_address) +1;
            
            case (trigger_fsm)

                s0 : begin
                    look_ahead_huge_buffer_qword_counter <= huge_buffer_qword_counter + diff;
                    diff_reg <= diff;
                    number_of_tlp_to_send <= diff[`BF:4];

                    if (diff >= 'h10) begin
                        trigger_fsm <= s1;
                    end
                    else if ( (huge_page_dirty) && (timeout) ) begin
                        trigger_fsm <= s9;
                    end
                    else if ( (diff) && (timeout) ) begin
                        trigger_fsm <= s10;
                    end
                end

                s1 : begin
                    huge_page_dirty <= 1'b1;
                    number_of_tlp_sent <= 'b0;
                    if (look_ahead_huge_buffer_qword_counter[18]) begin       // 2MB
                        if (!qwords_remaining) begin
                            change_huge_page <= 1'b1;
                            trigger_fsm <= s5;
                        end
                        else begin
                            qwords_to_send <= {1'b0, qwords_remaining};
                            send_last_tlp <= 1'b1;
                            trigger_fsm <= s6;
                        end
                    end
                    else begin
                        qwords_to_send <= 'h10;
                        trigger_tlp <= 1'b1;
                        qwords_remaining <= diff_reg[3:0];
                        trigger_fsm <= s2;
                    end
                end

                s2 : begin
                    look_ahead_commited_rd_address <= commited_rd_address + qwords_to_send;
                    look_ahead_number_of_tlp_sent <= number_of_tlp_sent +1;
                    aux_huge_buffer_qword_counter <= huge_buffer_qword_counter + qwords_to_send;
                    if (trigger_tlp_ack) begin
                        trigger_tlp <= 1'b0;
                        trigger_fsm <= s3;
                    end
                end

                s3 : begin
                    commited_rd_address <= look_ahead_commited_rd_address;
                    number_of_tlp_sent <= look_ahead_number_of_tlp_sent;
                    huge_buffer_qword_counter <= aux_huge_buffer_qword_counter;
                    trigger_fsm <= s4;
                end

                s4 : begin
                    if (number_of_tlp_sent < number_of_tlp_to_send) begin
                        trigger_tlp <= 1'b1;
                        trigger_fsm <= s2;
                    end
                    else begin
                        trigger_fsm <= s0;
                    end
                end

                s5 : begin
                    huge_page_dirty <= 1'b0;
                    huge_buffer_qword_counter <= 'h10;
                    if (change_huge_page_ack) begin
                        change_huge_page <= 1'b0;
                        trigger_fsm <= s0;
                    end
                end

                s6 : begin
                    look_ahead_commited_rd_address <= commited_rd_address + qwords_to_send;
                    if (change_huge_page_ack) begin
                        send_last_tlp <= 1'b0;
                        trigger_fsm <= s7;
                    end
                end

                s7 : begin
                    commited_rd_address <= look_ahead_commited_rd_address;
                    huge_buffer_qword_counter <= 'h10;
                    qwords_remaining <= 'b0;
                    huge_page_dirty <= 1'b0;
                    trigger_fsm <= s8;
                end

                s8 : begin
                    trigger_fsm <= s0;
                end

                s9 : begin
                    if (!qwords_remaining) begin
                        change_huge_page <= 1'b1;
                        trigger_fsm <= s5;
                    end
                    else begin
                        qwords_to_send <= {1'b0, qwords_remaining};
                        send_last_tlp <= 1'b1;
                        trigger_fsm <= s6;
                    end
                end

                s10 : begin
                    qwords_to_send <= diff_reg;
                    send_last_tlp <= 1'b1;
                    trigger_fsm <= s6;
                end
                
                default : begin
                    trigger_fsm <= s0;
                end

            endcase

        end     // not reset
    end  //always

endmodule // rx_tlp_trigger

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
