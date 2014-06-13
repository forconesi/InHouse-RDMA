
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////

`define PIO_64_RX_MEM_RD32_FMT_TYPE 7'b00_00000
`define RX_MEM_WR32_FMT_TYPE 7'b10_00000
`define PIO_64_RX_MEM_RD64_FMT_TYPE 7'b01_00000
`define RX_MEM_WR64_FMT_TYPE 7'b11_00000
`define PIO_64_RX_IO_RD32_FMT_TYPE  7'b00_00010
`define PIO_64_RX_IO_WR32_FMT_TYPE  7'b10_00010

module rx_huge_pages_addr (

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
    output reg  [63:0]      huge_page_addr_1,
    output reg  [63:0]      huge_page_addr_2,
    output reg              huge_page_status_1,
    output reg              huge_page_status_2,
    input                   huge_page_free_1,
    input                   huge_page_free_2
    );

    // localparam
    localparam s0 = 8'b00000000;
    localparam s1 = 8'b00000001;
    localparam s2 = 8'b00000010;
    localparam s3 = 8'b00000100;

    // Local wires and reg
    wire            reset_n = ~trn_lnk_up_n;

    reg     [7:0]   state;
    reg             huge_page_unlock_1;
    reg             huge_page_unlock_2;

    ////////////////////////////////////////////////
    // huge_page_status
    ////////////////////////////////////////////////
    always @( posedge trn_clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            huge_page_status_1 <= 1'b0;
            huge_page_status_2 <= 1'b0;
        end
        
        else begin  // not reset
            if (huge_page_unlock_1) huge_page_status_1 <= 1'b1;
            else if (huge_page_free_1) huge_page_status_1 <= 1'b0;

            if (huge_page_unlock_2) huge_page_status_2 <= 1'b1;
            else if (huge_page_free_2) huge_page_status_2 <= 1'b0;

        end     // not reset
    end  //always

    ////////////////////////////////////////////////
    // huge_page_address and unlock TLP reception
    ////////////////////////////////////////////////
    always @( posedge trn_clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            huge_page_unlock_1 <= 1'b0;
            huge_page_unlock_2 <= 1'b0;
            state <= s0;
        end
        
        else begin  // not reset
            case (state)

                s0 : begin
                    huge_page_unlock_1 <= 1'b0;
                    huge_page_unlock_2 <= 1'b0;
                    if ( (!trn_rsrc_rdy_n) && (!trn_rsof_n) && (!trn_rdst_rdy_n) && (!trn_rbar_hit_n[2])) begin
                        if (trn_rd[62:56] == `RX_MEM_WR32_FMT_TYPE) begin   // extend this to receive RX_MEM_WR64_FMT_TYPE
                            state <= s1;
                        end
                    end
                end

                s1 : begin
                    if ( (!trn_rsrc_rdy_n) && (!trn_rdst_rdy_n)) begin
                        case (trn_rd[39:34])

                            6'b010000 : begin     // huge page address
                                huge_page_addr_1[7:0] <= trn_rd[31:24];
                                huge_page_addr_1[15:8] <= trn_rd[23:16];
                                huge_page_addr_1[23:16] <= trn_rd[15:8];
                                huge_page_addr_1[31:24] <= trn_rd[7:0];
                                state <= s2;
                            end

                            6'b010010 : begin     // huge page address
                                huge_page_addr_2[7:0] <= trn_rd[31:24];
                                huge_page_addr_2[15:8] <= trn_rd[23:16];
                                huge_page_addr_2[23:16] <= trn_rd[15:8];
                                huge_page_addr_2[31:24] <= trn_rd[7:0];
                                state <= s3;
                            end

                            6'b011000 : begin     // huge page un-lock
                                huge_page_unlock_1 <= 1'b1;
                                state <= s0;
                            end

                            6'b011001 : begin     // huge page un-lock
                                huge_page_unlock_2 <= 1'b1;
                                state <= s0;
                            end

                            default : begin //other addresses
                                state <= s0;
                            end

                        endcase
                    end
                end

                s2 : begin
                    if ( (!trn_rsrc_rdy_n) && (!trn_rdst_rdy_n)) begin
                        huge_page_addr_1[39:32] <= trn_rd[63:56];
                        huge_page_addr_1[47:40] <= trn_rd[55:48];
                        huge_page_addr_1[55:48] <= trn_rd[47:40];
                        huge_page_addr_1[63:56] <= trn_rd[39:32];
                        state <= s0;
                    end
                end

                s3 : begin
                    if ( (!trn_rsrc_rdy_n) && (!trn_rdst_rdy_n)) begin
                        huge_page_addr_2[39:32] <= trn_rd[63:56];
                        huge_page_addr_2[47:40] <= trn_rd[55:48];
                        huge_page_addr_2[55:48] <= trn_rd[47:40];
                        huge_page_addr_2[63:56] <= trn_rd[39:32];
                        state <= s0;
                    end
                end

                default : begin //other TLPs
                    state <= s0;
                end

            endcase
        end     // not reset
    end  //always
   

endmodule // rx_huge_pages_addr