//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////

`include "includes.v"

`define TX_MEM_WR64_FMT_TYPE 7'b11_00000
`define TX_MEM_RD64_FMT_TYPE 7'b01_00000

module tx_rd_host_mem (

    input    trn_clk,
    input    trn_lnk_up_n,

    //Rx
    output reg  [63:0]     trn_td,
    output reg  [7:0]      trn_trem_n,
    output reg             trn_tsof_n,
    output reg             trn_teof_n,
    output reg             trn_tsrc_rdy_n,
    input                  trn_tdst_rdy_n,
    input       [3:0]      trn_tbuf_av,
    input       [15:0]     cfg_completer_id,
    output reg             cfg_interrupt_n,
    input                  cfg_interrupt_rdy_n,

    // Internal logic

    input       [63:0]     huge_page_addr,
    input                  read_chunk,
    output reg             read_chunk_ack,

    // Arbitrations hanshake

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
    // Local send_tlps_machine
    //-------------------------------------------------------   
    reg     [14:0]  state;
    reg     [63:0]  host_mem_addr;

    assign reset_n = ~trn_lnk_up_n;

    ////////////////////////////////////////////////
    // read request TLP generation to huge_page
    ////////////////////////////////////////////////
    always @( posedge trn_clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            
            trn_td <= 64'b0;
            trn_trem_n <= 8'hFF;
            trn_tsof_n <= 1'b1;
            trn_teof_n <= 1'b1;
            trn_tsrc_rdy_n <= 1'b1;
            cfg_interrupt_n <= 1'b1;

            read_chunk_ack <= 1'b0;

            driving_interface <= 1'b0;
            state <= s0;
        end
        
        else begin  // not reset

            read_chunk_ack <= 1'b0;

            case (state)

                s0 : begin
                    driving_interface <= 1'b0;
                    host_mem_addr <= huge_page_addr;
                    if ( (trn_tbuf_av[0]) && (!trn_tdst_rdy_n) && (my_turn) && (read_chunk)) begin          // credits available and endpointready and myturn
                        driving_interface <= 1'b1;
                        read_chunk_ack <= 1'b1;
                        state <= s1;
                    end
                end

                s1 : begin
                    trn_trem_n <= 8'b0;
                    trn_td[63:32] <= {
                                1'b0,   //reserved
                                `TX_MEM_RD64_FMT_TYPE, //memory read request 64bit addressing
                                1'b0,   //reserved
                                3'b0,   //TC (traffic class)
                                4'b0,   //reserved
                                1'b0,   //TD (TLP digest present)
                                1'b0,   //EP (poisoned data)
                                2'b10,  //Relaxed ordering, No spoon in processor cache
                                2'b0,   //reserved
                                10'h080   //lenght in DWs. 10-bit field 128DWs == 512 bytes
                            };
                    trn_td[31:0] <= {
                                cfg_completer_id,   //Requester ID
                                {4'b0, tlp_number[3:0] },   //Tag
                                4'hF,   //last DW byte enable
                                4'hF    //1st DW byte enable
                            };
                    trn_tsof_n <= 1'b0;
                    trn_tsrc_rdy_n <= 1'b0;
                    
                    state <= s2;
                end

                s2 : begin
                    if (!trn_tdst_rdy_n) begin
                        trn_tsof_n <= 1'b1;
                        trn_teof_n <= 1'b0;
                        trn_td <= host_mem_addr;
                        state <= s3;
                    end
                end

                s3 : begin
                    if (!trn_tdst_rdy_n) begin
                        trn_tsrc_rdy_n <= 1'b1;
                        trn_teof_n <= 1'b1;
                        trn_trem_n <= 8'hFF;
                        trn_td <= 64'b0;
                        driving_interface <= 1'b0;
                        state <= s0;
                    end
                end

                default : begin 
                    state <= s0;
                end

            endcase
        end     // not reset
    end  //always
   

endmodule // tx_rd_host_mem
