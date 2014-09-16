//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
`timescale 1ns / 1ps
//`default_nettype none

`define PIO_64_RX_MEM_RD32_FMT_TYPE 7'b00_00000
`define RX_MEM_WR32_FMT_TYPE 7'b10_00000
`define PIO_64_RX_MEM_RD64_FMT_TYPE 7'b01_00000
`define RX_MEM_WR64_FMT_TYPE 7'b11_00000
`define PIO_64_RX_IO_RD32_FMT_TYPE  7'b00_00010
`define PIO_64_RX_IO_WR32_FMT_TYPE  7'b10_00010

module interrupt_en (

    input    trn_clk,
    input    trn_lnk_up_n,

    input       [63:0]      trn_rd,
    input       [7:0]       trn_rrem_n,
    input                   trn_rsof_n,
    input                   trn_reof_n,
    input                   trn_rsrc_rdy_n,
    input                   trn_rsrc_dsc_n,
    input       [6:0]       trn_rbar_hit_n,
    input                   trn_rdst_rdy_n,
    output reg              interrupts_enabled,
    output reg  [31:0]      interrupt_period
    );

    // localparam
    localparam s0 = 8'b00000000;
    localparam s1 = 8'b00000001;
    localparam s2 = 8'b00000010;
    localparam s3 = 8'b00000100;
    localparam s4 = 8'b00001000;

    // Local wires and reg
    wire            reset_n = ~trn_lnk_up_n;

    reg     [7:0]   state;
    reg     [31:0]  aux_dw;
    reg     [31:0]  aux_dw2;

    ////////////////////////////////////////////////
    // interrupts_enabled & TLP reception
    ////////////////////////////////////////////////
    always @( posedge trn_clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            interrupts_enabled <= 1'b1;
            interrupt_period <= 'h3D090;
            state <= s0;
        end
        
        else begin  // not reset
            case (state)

                s0 : begin
                    if ( (!trn_rsrc_rdy_n) && (!trn_rsof_n) && (!trn_rdst_rdy_n) && (!trn_rbar_hit_n[2])) begin
                        if (trn_rd[62:56] == `RX_MEM_WR32_FMT_TYPE) begin   // extend this to receive RX_MEM_WR64_FMT_TYPE
                            state <= s1;
                        end
                    end
                end

                s1 : begin
                    aux_dw <= trn_rd[31:0];
                    if ( (!trn_rsrc_rdy_n) && (!trn_rdst_rdy_n)) begin
                        case (trn_rd[39:34])

                            6'b001000 : begin     // interrupts eneable
                                interrupts_enabled <= 1'b1;
                                state <= s0;
                            end

                            6'b001001 : begin     // interrupts disable
                                interrupts_enabled <= 1'b0;
                                state <= s0;
                            end

                            6'b001010 : begin     // interrupts period
                                state <= s2;
                            end

                            default : begin //other addresses
                                state <= s0;
                            end

                        endcase
                    end
                end

                s2 : begin
                    aux_dw2[7:0] <= aux_dw[31:24];
                    aux_dw2[15:8] <= aux_dw[23:16];
                    aux_dw2[23:16] <= aux_dw[15:8];
                    aux_dw2[31:24] <= aux_dw[7:0];
                    state <= s3;
                    if (!aux_dw) begin  // zero is invalid
                        state <= s0;
                    end
                end

                s3 : begin
                    interrupt_period[29:0] <= aux_dw2[31:2];
                    state <= s0;
                end

                default : begin //other TLPs
                    state <= s0;
                end

            endcase
        end     // not reset
    end  //always
   

endmodule // interrupt_en

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////