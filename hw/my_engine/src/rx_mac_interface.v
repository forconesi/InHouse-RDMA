//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
`timescale 1ns / 1ps
`include "includes.v"

module rx_mac_interface (

    input    clk,
    input    reset_n,

    // MAC Rx
    input    [63:0]      rx_data,
    input    [7:0]       rx_data_valid,
    input                rx_good_frame,
    input                rx_bad_frame,

    // Internal memory driver
    output reg    [`BF:0]     wr_addr,
    output reg    [63:0]      wr_data,
    output reg                wr_en,
    
    // Internal logic
    output reg    [`BF:0]     commited_wr_address,
    input                     rd_addr_updated,               //250 MHz domain driven
    input         [`BF:0]     commited_rd_address              //250 MHz domain driven

    );

    // localparam
    localparam s0 = 8'b00000000;
    localparam s1 = 8'b00000001;
    localparam s2 = 8'b00000010;
    localparam s3 = 8'b00000100;

    //-------------------------------------------------------
    // Local ethernet frame reception and memory write
    //-------------------------------------------------------
    reg     [7:0]     state;
    reg     [31:0]    byte_counter;
    reg     [`BF:0]   aux_wr_addr;
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
    reg              rd_addr_updated_reg0;
    reg              rd_addr_updated_reg1;
    reg     [`BF:0]  commited_rd_address_reg0;
    reg     [`BF:0]  commited_rd_address_reg1;

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
            rd_addr_updated_reg0 <= 1'b0;
            rd_addr_updated_reg1 <= 1'b0;
            commited_rd_address_reg0 <= 'b0;
            commited_rd_address_reg1 <= 'b0;
        end
        
        else begin  // not reset
            rd_addr_updated_reg0 <= rd_addr_updated;
            rd_addr_updated_reg1 <= rd_addr_updated_reg0;

            commited_rd_address_reg0 <= commited_rd_address;

            if (rd_addr_updated_reg1) begin                                      // transitory off
                commited_rd_address_reg1 <= commited_rd_address_reg0;
            end

        end     // not reset
    end  //always

    ////////////////////////////////////////////////
    // ethernet frame reception and memory write
    ////////////////////////////////////////////////
    always @( posedge clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            commited_wr_address <= 'b0;
            diff <= 'b0;
            dropped_frames_counter <= 32'b0;
            wr_en <= 1'b0;
            state <= s0;
        end
        
        else begin  // not reset
            
            diff <= aux_wr_addr + (~commited_rd_address_reg1) +1;
            
            case (state)

                s0 : begin                                  // configure mac core to present preamble and save the packet timestamp while its reception
                    byte_counter <= 32'b0;
                    aux_wr_addr <= commited_wr_address +1;
                    wr_en <= 1'b0;
                    if (rx_data_valid != 8'b0) begin      // wait for sof (preamble)
                        state <= s1;
                    end
                end

                s1 : begin
                    wr_data <= rx_data;
                    wr_addr <= aux_wr_addr;
                    wr_en <= 1'b1;
                    aux_wr_addr <= aux_wr_addr +1;

                    rx_data_valid_reg <= rx_data_valid;
                    rx_good_frame_reg <= rx_good_frame;
                    rx_bad_frame_reg <= rx_bad_frame;
                    
                    case (rx_data_valid)     // increment byte_counter accordingly
                        8'b00000000 : begin
                            byte_counter <= byte_counter;       // don't increment
                            aux_wr_addr <= aux_wr_addr;
                            wr_en <= 1'b0;
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

                    if (diff[`BF:0] > `MAX_DIFF) begin         // buffer is more than 90%
                        state <= s3;
                    end
                    else if (rx_good_frame) begin        // eof (good frame)
                        state <= s2;
                    end
                    else if (rx_bad_frame) begin
                        state <= s0;
                    end
                end

                s2 : begin
                    wr_data <= {byte_counter, 32'b0};
                    wr_addr <= commited_wr_address;
                    wr_en <= 1'b1;

                    commited_wr_address <= aux_wr_addr;                      // commit the packet
                    aux_wr_addr <= aux_wr_addr +1;
                    byte_counter <= 32'b0;

                    if (rx_data_valid != 8'b0) begin        // sof (preamble)
                        state <= s1;
                    end
                    else begin
                        state <= s0;
                    end
                end
                
                s3 : begin                                  // drop current frame
                    if (rx_good_frame || rx_good_frame_reg || rx_bad_frame  || rx_bad_frame_reg) begin
                        dropped_frames_counter <= dropped_frames_counter +1; 
                        state <= s0;
                    end
                end

                default : begin 
                    state <= s0;
                end

            endcase
        end     // not reset
    end  //always
   

endmodule // rx_mac_interface

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
