//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
`timescale 1ns / 1ps
//`default_nettype none
`include "includes.v"

module rx_rd_addr_synch (

    input    clk_out,
    input    reset_n_clk_out,

    input    clk_in,
    input    reset_n_clk_in,

    input         [`BF:0]     commited_rd_address_in,
    output reg    [`BF:0]     commited_rd_address_out
    );

    // localparam
    localparam s0 = 8'b00000000;
    localparam s1 = 8'b00000001;
    localparam s2 = 8'b00000010;
    localparam s3 = 8'b00000100;
    localparam s4 = 8'b00001000;
    localparam s5 = 8'b00010000;
    localparam s6 = 8'b00100000;
    localparam s7 = 8'b01000000;
    localparam s8 = 8'b10000000;

    //-------------------------------------------------------
    // Local a
    //-------------------------------------------------------
    reg     [7:0]    fsm_a;
    reg     [`BF:0]  bus_in_last;
    reg              synch;
    reg     [`BF:0]  cross;

    //-------------------------------------------------------
    // Local b
    //-------------------------------------------------------
    reg              synch_reg0;
    reg              synch_reg1;
    reg     [`BF:0]  cross_reg0;

    ////////////////////////////////////////////////
    // a
    ////////////////////////////////////////////////
    always @( posedge clk_in or negedge reset_n_clk_in ) begin

        if (!reset_n_clk_in ) begin  // reset
            bus_in_last <= 'b0;
            synch <= 1'b0;
            fsm_a <= s0;
        end
        
        else begin  // not reset

            case (fsm_a)

                s0 : begin
                    if (bus_in_last != commited_rd_address_in) begin
                        cross <= commited_rd_address_in;
                        fsm_a <= s1;
                    end
                end

                s1 : begin
                    bus_in_last <= cross;
                    synch <= 1'b1;
                    fsm_a <= s2;
                end

                s2 : fsm_a <= s3;

                s3 : begin
                    synch <= 1'b0;
                    fsm_a <= s4;
                end

                s4 : fsm_a <= s5;
                s5 : fsm_a <= s6;
                s6 : fsm_a <= s7;
                s7 : fsm_a <= s0;

                default : begin 
                    fsm_a <= s0;
                end

            endcase
        end     // not reset
    end  //always

    ////////////////////////////////////////////////
    // b
    ////////////////////////////////////////////////
    always @( posedge clk_out or negedge reset_n_clk_out ) begin

        if (!reset_n_clk_out ) begin  // reset
            commited_rd_address_out <= 'b0;
            synch_reg0 <= 1'b0;
            synch_reg1 <= 1'b0;
        end
        
        else begin  // not reset

            cross_reg0 <= cross;
            synch_reg0 <= synch;
            synch_reg1 <= synch_reg0;

            if (synch_reg1) begin
                commited_rd_address_out <= cross_reg0;
            end

        end     // not reset
    end  //always

endmodule // rx_rd_addr_synch

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
