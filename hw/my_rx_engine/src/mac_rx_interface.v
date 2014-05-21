//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
`timescale 1ns / 1ps
`include "includes.v"

module mac_rx_interface (

    input    clk,
    input    reset_n,

    // MAC Rx
    input    [63:0]      rx_data,
    input    [7:0]       rx_data_valid,
    input                rx_good_frame,
    input                rx_bad_frame,

    // Internal memory driver
    output reg    [`BF:0]     wr_addr,
    output        [63:0]      wr_data,
    output reg                wr_en,
    
    // Internal logic
    output reg    [`BF:0]     commited_wr_address,
    input                     rd_addr_change,               //250 MHz domain driven
    input         [`BF:0]     rd_addr                       //250 MHz domain driven

    );

    // localparam
    localparam s0 = 8'b00000000;
    localparam s1 = 8'b00000001;
    localparam s2 = 8'b00000010;
    localparam s3 = 8'b00000100;
    localparam s4 = 8'b00001000;

    //-------------------------------------------------------
    // Local ethernet frame reception and memory write
    //-------------------------------------------------------
    reg     [7:0]     mac_rx_fsm;
    reg     [31:0]    byte_counter;
    reg     [`BF:0]   aux_wr_addr;
    reg     [`BF:0]   start_wr_addr_next_pkt;
    wire    [`BF:0]   commited_wr_address_i;
    reg     [`BF:0]   diff;
    (* KEEP = "TRUE" *)reg     [31:0]   dropped_frames_counter;
    
    reg     [7:0]    rx_data_valid_reg;
    reg              rx_good_frame_reg;
    reg              rx_bad_frame_reg;

    //-------------------------------------------------------
    // Local ts_sec-and-ts_nsec-generation
    //-------------------------------------------------------
    reg     [31:0]   ts_sec;
    reg     [31:0]   ts_nsec;
    reg     [27:0]   free_running;

    //-------------------------------------------------------
    // Local 250 MHz signal synch
    //-------------------------------------------------------
    reg              rd_addr_change_reg0;
    reg              rd_addr_change_reg1;
    reg     [`BF:0]  rd_addr_reg0;
    reg     [`BF:0]  rd_addr_reg1;

    //-------------------------------------------------------
    // Local internal_true_dual_port_ram
    //-------------------------------------------------------
    reg     [`BF:0]   wr_addr_i;
    reg     [63:0]    wr_data_i;
    reg               wr_en_i;
    reg     [`BF:0]   rd_addr_i;
    wire    [63:0]    rd_data_i;
    wire    [63:0]    qspo_i;

    reg     [7:0]     aux_fsm;
    reg     [`BF:0]   diff_mac_buff;
    (* KEEP = "TRUE" *)reg     [`BF:0]   diff_tlp_buff;
    reg     [`BF:0]   rd_addr_i_prev1;
    reg     [`BF:0]   rd_addr_i_prev2;
    reg     [`BF:0]   commited_wr_address_i_reg;

    ////////////////////////////////////////////////
    // internal_true_dual_port_ram
    ////////////////////////////////////////////////
    dist_mem_gen_v7_2 my_bram_i (
        .a(wr_addr_i),                // I [`BF:0]
        .d(wr_data_i),                // I [63:0]
        .dpra(rd_addr_i),             // I [`BF:0]
        .clk(clk),               // I 
        .we(wr_en_i),                 // I
        .qdpo_clk(clk),          // I
        .qspo(qspo_i),                // O [63:0]
        .qdpo(rd_data_i)              // O [63:0]
        );  //see pg063

    ////////////////////////////////////////////////
    // ts_sec-and-ts_nsec-generation
    ////////////////////////////////////////////////
    always @( posedge clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            ts_sec <= 32'b0;
            ts_nsec <= 32'b0;
            free_running <= 28'b0;
        end
        
        else begin  // not reset
            free_running <= free_running +1;
            ts_nsec <= ts_nsec + 6;
            if (free_running == 28'd156250000) begin
              free_running <= 28'b0;
              ts_sec <= ts_sec +1;
              ts_nsec <= 32'b0;
            end

        end     // not reset
    end  //always

    ////////////////////////////////////////////////
    // 250 MHz signal synch
    ////////////////////////////////////////////////
    always @( posedge clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            rd_addr_change_reg0 <= 1'b0;
            rd_addr_change_reg1 <= 1'b0;
            rd_addr_reg0 <= 'b0;
            rd_addr_reg1 <= 'b0;
        end
        
        else begin  // not reset
            rd_addr_change_reg0 <= rd_addr_change;
            rd_addr_change_reg1 <= rd_addr_change_reg0;

            rd_addr_reg0 <= rd_addr;

            if (rd_addr_change_reg1) begin                                      // transitory off
                rd_addr_reg1 <= rd_addr_reg0;
            end

        end     // not reset
    end  //always

    assign commited_wr_address_i = start_wr_addr_next_pkt;    // address with valid data

    ////////////////////////////////////////////////
    // ethernet frame reception and memory write
    ////////////////////////////////////////////////
    always @( posedge clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            start_wr_addr_next_pkt <= 'b0;
            diff <= 'b0;
            dropped_frames_counter <= 32'b0;
            wr_en_i <= 1'b0;
            mac_rx_fsm <= s0;
        end
        
        else begin  // not reset
            
            diff <= aux_wr_addr + (~rd_addr_i) +1;
            
            case (mac_rx_fsm)

                s0 : begin                                  // configure mac core to present preamble and save the packet timestamp while its reception
                    byte_counter <= 32'b0;
                    aux_wr_addr <= start_wr_addr_next_pkt +1;
                    wr_en_i <= 1'b0;
                    if (rx_data_valid != 8'b0) begin      // wait for sof (preamble)
                        mac_rx_fsm <= s1;
                    end
                end

                s1 : begin
                    wr_data_i <= rx_data;
                    wr_addr_i <= aux_wr_addr;
                    wr_en_i <= 1'b1;
                    aux_wr_addr <= aux_wr_addr +1;

                    rx_data_valid_reg <= rx_data_valid;
                    rx_good_frame_reg <= rx_good_frame;
                    rx_bad_frame_reg <= rx_bad_frame;
                    
                    case (rx_data_valid)     // increment byte_counter accordingly
                        8'b00000000 : begin
                            byte_counter <= byte_counter;       // don't increment
                            aux_wr_addr <= aux_wr_addr;
                            wr_en_i <= 1'b0;
                        end
                        8'b00000001 : begin
                            byte_counter <= byte_counter + 1;
                        end
                        8'b00000011 : begin
                            byte_counter <= byte_counter + 2;
                        end
                        8'b00000111 : begin
                            byte_counter <= byte_counter + 3;
                        end
                        8'b00001111 : begin
                            byte_counter <= byte_counter + 4;
                        end
                        8'b00011111 : begin
                            byte_counter <= byte_counter + 5;
                        end
                        8'b00111111 : begin
                            byte_counter <= byte_counter + 6;
                        end
                        8'b01111111 : begin
                            byte_counter <= byte_counter + 7;
                        end
                        8'b11111111 : begin
                            byte_counter <= byte_counter + 8;
                        end
                    endcase

                    if (diff > `MAX_DIFF) begin         // buffer is more than 90%
                        mac_rx_fsm <= s3;
                    end
                    else if (rx_good_frame) begin        // eof (good frame)
                        mac_rx_fsm <= s2;
                    end
                    else if (rx_bad_frame) begin
                        mac_rx_fsm <= s0;
                    end
                end

                s2 : begin
                    wr_data_i <= {byte_counter, 32'b0};
                    wr_addr_i <= start_wr_addr_next_pkt;
                    wr_en_i <= 1'b1;

                    start_wr_addr_next_pkt <= aux_wr_addr;                      // commit the packet
                    aux_wr_addr <= aux_wr_addr +1;
                    byte_counter <= 32'b0;

                    if (rx_data_valid != 8'b0) begin        // sof (preamble)
                        mac_rx_fsm <= s1;
                    end
                    else begin
                        mac_rx_fsm <= s0;
                    end
                end
                
                s3 : begin                                  // drop current frame
                    if (rx_good_frame || rx_good_frame_reg || rx_bad_frame  || rx_bad_frame_reg) begin
                        dropped_frames_counter <= dropped_frames_counter +1; 
                        mac_rx_fsm <= s0;
                    end
                end

                default : begin 
                    mac_rx_fsm <= s0;
                end

            endcase
        end     // not reset
    end  //always

    
    assign wr_data = rd_data_i;

    ////////////////////////////////////////////////
    // read mac buffer -- write tlp buffer
    ////////////////////////////////////////////////
    always @( posedge clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            rd_addr_i <= 'b0;
            wr_addr <= 'b0;
            commited_wr_address <= 'b0;
            diff_mac_buff <= 'b0;
            diff_tlp_buff <= 'b0;
            aux_fsm <= s0;
        end
        
        else begin  // not reset
            
            diff_mac_buff <= commited_wr_address_i + (~rd_addr_i) +1;
            diff_tlp_buff <= wr_addr + (~rd_addr_reg1) +1;

            rd_addr_i_prev1 <= rd_addr_i;
            rd_addr_i_prev2 <= rd_addr_i_prev1;

            wr_en <= 1'b1;
            
            case (aux_fsm)

                s0 : begin
                    commited_wr_address_i_reg <= commited_wr_address_i;
                    if (diff_mac_buff) begin
                        aux_fsm <= s1;
                    end
                end

                s1 : begin
                    rd_addr_i <= rd_addr_i + 1;
                    aux_fsm <= s2;
                end

                s2 : begin
                    rd_addr_i <= rd_addr_i + 1;
                    aux_fsm <= s3;
                end

                s3 : begin
                    if (diff_tlp_buff < `MAX_DIFF) begin         // buffer is less than 90%
                        rd_addr_i <= rd_addr_i + 1;
                        wr_addr <= wr_addr + 1;
                        if (rd_addr_i == commited_wr_address_i_reg) begin
                            rd_addr_i <= rd_addr_i;
                            commited_wr_address <= commited_wr_address_i_reg;
                            aux_fsm <= s4;
                        end
                    end
                    else begin
                        rd_addr_i <= rd_addr_i_prev2;
                        aux_fsm <= s1;
                    end
                end

                s4 : begin
                    wr_addr <= wr_addr + 1;
                    aux_fsm <= s0;
                end

                default : begin 
                    aux_fsm <= s0;
                end

            endcase
        end     // not reset
    end  //always
   

endmodule // mac_rx_interface

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
