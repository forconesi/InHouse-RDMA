//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
`include "includes.v"

module tx_mac_interface (

    input    clk,
    input    reset_n,

    // MAC Rx
    output reg    [63:0]      tx_data,
    output reg    [7:0]       tx_data_valid,
    output reg                tx_start,
    input                     tx_ack,

    // Internal memory driver
    output reg    [`BF:0]     rd_addr,
    input         [63:0]      rd_data,
    
    
    // Internal logic
    output reg    [`BF:0]     commited_rd_address,
    output reg                commited_rd_address_change,
    input         [`BF:0]     wr_addr,                         //250 MHz domain driven
    input                     wr_addr_updated,                         //250 MHz domain driven
    input         [`BF:0]     commited_wr_addr                //250 MHz domain driven

    );

    // localparam
    localparam s0 = 8'b00000000;
    localparam s1 = 8'b00000001;
    localparam s2 = 8'b00000010;
    localparam s3 = 8'b00000100;
    localparam s4 = 8'b00001000;
    localparam s5 = 8'b00010000;

    //-------------------------------------------------------
    // Local ethernet frame reception and memory write
    //-------------------------------------------------------
    reg     [7:0]     trigger_frame_fsm;
    reg     [7:0]     tx_frame_fsm;
    reg     [31:0]    byte_counter;
    reg     [9:0]     qwords_in_eth;
    reg     [9:0]     qwords_sent;
    reg     [`BF:0]   diff;
    reg               advance_pointer;
    reg               synch;
    reg               trigger_tx_frame;
    reg     [7:0]     last_tx_data_valid;
    reg     [`BF:0]   wr_addr_new_start;
    reg     [`BF:0]   next_rd_addr;
    reg     [63:0]    rd_data_aux;
    
    //-------------------------------------------------------
    // Local ts_sec-and-ts_nsec-generation
    //-------------------------------------------------------
    reg     [31:0]   ts_sec;
    reg     [31:0]   ts_nsec;
    reg     [27:0]   free_running;

    //-------------------------------------------------------
    // Local 250 MHz signal synch
    //-------------------------------------------------------
    reg              wr_addr_updated_reg0;
    reg              wr_addr_updated_reg1;
    reg     [`BF:0]  commited_wr_addr_reg0;
    reg     [`BF:0]  commited_wr_addr_reg1;

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
            wr_addr_updated_reg0 <= 1'b0;
            wr_addr_updated_reg1 <= 1'b0;
            commited_wr_addr_reg0 <= 'b0;
            commited_wr_addr_reg1 <= 'b0;
        end
        
        else begin  // not reset
            wr_addr_updated_reg0 <= wr_addr_updated;
            wr_addr_updated_reg1 <= wr_addr_updated_reg0;

            commited_wr_addr_reg0 <= commited_wr_addr;

            if (wr_addr_updated_reg1) begin                                      // transitory off
                commited_wr_addr_reg1 <= commited_wr_addr_reg0;
            end

        end     // not reset
    end  //always

    ////////////////////////////////////////////////
    // ethernet frame transmition and memory read
    ////////////////////////////////////////////////
    always @( posedge clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            rd_addr <= 'b0;
            diff <= 'b0;
            tx_start <= 1'b0;
            tx_data_valid <= 'b0;
            tx_data <= 'b0;
            commited_rd_address_change <= 1'b0;
            advance_pointer <= 1'b0;
            synch <= 1'b0;
            trigger_tx_frame <= 1'b0;
            trigger_frame_fsm <= s0;
            tx_frame_fsm <= s0;
        end
        
        else begin  // not reset
            
            diff <= commited_wr_addr_reg1 + (~rd_addr) +1;
            
            case (trigger_frame_fsm)

                s0 : begin
                    byte_counter <= rd_data[63:32];
                    qwords_in_eth <= rd_data[44:35];
                    if (diff) begin
                        trigger_frame_fsm <= s1;
                    end
                end

                s1 : begin
                    if (byte_counter[2:0]) begin
                        qwords_in_eth <= byte_counter[12:3] +1;
                    end

                    case (byte_counter[2:0])                    // my deco
                        3'b000 : begin
                            last_tx_data_valid <= 8'b11111111;
                        end
                        3'b001 : begin
                            last_tx_data_valid <= 8'b00000001;
                        end
                        3'b010 : begin
                            last_tx_data_valid <= 8'b00000011;
                        end
                        3'b011 : begin
                            last_tx_data_valid <= 8'b00000111;
                        end
                        3'b100 : begin
                            last_tx_data_valid <= 8'b00001111;
                        end
                        3'b101 : begin
                            last_tx_data_valid <= 8'b00011111;
                        end
                        3'b110 : begin
                            last_tx_data_valid <= 8'b00111111;
                        end
                        3'b111 : begin
                            last_tx_data_valid <= 8'b01111111;
                        end
                    endcase

                    if (diff >= qwords_in_eth) begin
                        trigger_tx_frame <= 1'b1;
                        trigger_frame_fsm <= s2;
                    end
                end

                s2 : begin
                    trigger_tx_frame <= 1'b0;
                    if (synch) begin
                        byte_counter <= rd_data[63:32];
                        qwords_in_eth <= rd_data[44:35];
                        trigger_frame_fsm <= s1;

                        wr_addr_new_start <= commited_wr_addr_reg1;
                        if (rd_data[63]) begin
                            advance_pointer <= 1'b1;
                            trigger_frame_fsm <= s3;
                        end
                    end
                end

                s3 : begin
                    advance_pointer <= 1'b0;
                    trigger_frame_fsm <= s4;
                end

                s4 : begin
                    trigger_frame_fsm <= s0;
                end

                default : begin 
                    trigger_frame_fsm <= s0;
                end

            endcase

            synch <= 1'b0;
            commited_rd_address_change <= 1'b0;
            tx_start <= 1'b0;
            tx_data_valid <= 'b0;

            case (tx_frame_fsm)

                s0: begin
                    next_rd_addr <= rd_addr +1;
                    if (trigger_tx_frame) begin
                        rd_addr <= next_rd_addr;
                        tx_frame_fsm <= s1;
                    end
                    else if (advance_pointer) begin
                        commited_rd_address_change <= 1'b1;
                        rd_addr <= wr_addr_new_start;
                    end
                end

                s1 : begin
                    rd_addr <= rd_addr +1;
                    tx_start <= 1'b1;
                    tx_frame_fsm <= s2;
                end

                s2 : begin
                    tx_data <= rd_data;
                    tx_data_valid <= 'hFF;
                    rd_addr <= rd_addr +1;
                    tx_frame_fsm <= s3;
                end

                s3 : begin
                    tx_data_valid <= 'hFF;
                    next_rd_addr <= rd_addr +1;
                    rd_data_aux <= rd_data;
                    tx_frame_fsm <= s4;
                end

                s4 : begin
                    tx_data_valid <= 'hFF;
                    qwords_sent <= 'h003;
                    if (tx_ack) begin
                        tx_data <= rd_data_aux;
                        rd_addr <= next_rd_addr;
                        tx_frame_fsm <= s5;
                    end
                end

                s5 : begin
                    tx_data <= rd_data;
                    rd_addr <= rd_addr +1;
                    tx_data_valid <= 'hFF;
                    commited_rd_address_change <= commited_rd_address_change ? 1'b0 : 1'b1;
                    qwords_sent <= qwords_sent +1;
                    if (qwords_in_eth == qwords_sent) begin
                        synch <= 1'b1;
                        tx_data_valid <= last_tx_data_valid;
                        tx_frame_fsm <= s0;
                    end
                end

                default : begin 
                    tx_frame_fsm <= s0;
                end

            endcase
        end     // not reset
    end  //always
   

endmodule // tx_mac_interface

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////

//With this implementation we cannot drive the interface with back-to-back frames. We must process the trigger logic in other clock domain, similar to the rx part.
//this takes one or two clks.