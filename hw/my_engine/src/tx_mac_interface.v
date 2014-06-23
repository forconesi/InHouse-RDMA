//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
`include "includes.v"

module tx_mac_interface (

    input    clk,
    input    reset_n,

    // MAC Rx
    output reg                tx_underrun,
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
    localparam s6 = 8'b00100000;

    //-------------------------------------------------------
    // Local 250 MHz signal synch
    //-------------------------------------------------------
    reg              wr_addr_updated_reg0;
    reg              wr_addr_updated_reg1;
    reg     [`BF:0]  commited_wr_addr_reg0;
    reg     [`BF:0]  commited_wr_addr_reg1;

    //-------------------------------------------------------
    // Local trigger_eth_frame
    //-------------------------------------------------------
    reg     [7:0]     trigger_frame_fsm;
    reg     [31:0]    byte_counter;
    reg     [9:0]     qwords_in_eth;
    reg     [`BF:0]   diff;
    reg               trigger_tx_frame;
    reg     [7:0]     last_tx_data_valid;
    reg               take_your_chances;

    //-------------------------------------------------------
    // Local ethernet frame transmition and memory read
    //-------------------------------------------------------
    reg     [7:0]     tx_frame_fsm;
    reg     [9:0]     qwords_sent;
    reg               synch;
    reg     [`BF:0]   next_rd_addr;
    reg     [`BF:0]   rd_addr_sof;
    reg     [`BF:0]   rd_addr_prev0;
    reg     [63:0]    rd_data_aux;
    reg               end_of_eth_frame;
    ////////////////////////////////////////////////
    // INSTRUMENTATION
    ////////////////////////////////////////////////
    `ifdef INSTRUMENTATION
    (* KEEP = "TRUE" *)reg     [31:0]    frames_sent;
    `endif
    ////////////////////////////////////////////////
    // INSTRUMENTATION
    ////////////////////////////////////////////////

    //-------------------------------------------------------
    // Local signal_carefully
    //-------------------------------------------------------
    reg     [7:0]     signal_carefully_fsm;

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
    // trigger_eth_frame
    ////////////////////////////////////////////////
    always @( posedge clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            take_your_chances <= 1'b0;
            diff <= 'b0;
            trigger_tx_frame <= 1'b0;
            trigger_frame_fsm <= s0;
        end
        
        else begin  // not reset

            take_your_chances <= 1'b0;
            if (diff >= 'h8) begin                      // I haven't thought very well about this mechanism and this number
                take_your_chances <= 1'b1;
            end
            
            diff <= commited_wr_addr_reg1 + (~rd_addr) +1;
            
            case (trigger_frame_fsm)

                s0 : begin
                    byte_counter <= rd_data[63:32];
                    if (diff) begin
                        qwords_in_eth <= rd_data[44:35];
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

                    if (!diff) begin
                        trigger_frame_fsm <= s0;
                    end
                    else if (diff >= qwords_in_eth) begin
                        trigger_tx_frame <= 1'b1;
                        trigger_frame_fsm <= s2;
                    end
                end

                s2 : begin
                    trigger_tx_frame <= 1'b0;
                    byte_counter <= rd_data[63:32];
                    if (synch) begin
                        qwords_in_eth <= rd_data[44:35];
                        trigger_frame_fsm <= s1;
                    end
                end

                default : begin 
                    trigger_frame_fsm <= s0;
                end

            endcase
        end     // not reset
    end  //always

    ////////////////////////////////////////////////
    // ethernet frame transmition and memory read
    ////////////////////////////////////////////////
    always @( posedge clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            tx_underrun <= 1'b0;
            rd_addr <= 'b0;
            tx_start <= 1'b0;
            tx_data_valid <= 'b0;
            tx_data <= 'b0;
            synch <= 1'b0;
            end_of_eth_frame <= 1'b0;
            `ifdef INSTRUMENTATION
            frames_sent <= 'b0;
            `endif
            tx_frame_fsm <= s0;
        end
        
        else begin  // not reset
            
            synch <= 1'b0;
            tx_underrun <= 1'b0;
            tx_start <= 1'b0;
            tx_data_valid <= 'b0;
            rd_addr_prev0 <= rd_addr;
            end_of_eth_frame <= 1'b0;

////////////////////////////////////////////////
// INSTRUMENTATION
////////////////////////////////////////////////
`ifdef INSTRUMENTATION
            if (end_of_eth_frame) begin
                frames_sent <= frames_sent +1;
            end
`endif
////////////////////////////////////////////////
// INSTRUMENTATION
////////////////////////////////////////////////

            case (tx_frame_fsm)

                s0: begin
                    next_rd_addr <= rd_addr +1;
                    if (trigger_tx_frame) begin
                        rd_addr <= next_rd_addr;
                        tx_frame_fsm <= s1;
                    end
                end

                s1 : begin
                    rd_addr_sof <= rd_addr_prev0;
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
                    qwords_sent <= qwords_sent +1;
                    if (qwords_in_eth == qwords_sent) begin
                        synch <= 1'b1;
                        end_of_eth_frame <= 1'b1;
                        tx_data_valid <= last_tx_data_valid;
                        //if (!take_your_chances) begin           // the normal case
                            rd_addr <= rd_addr;
                            tx_frame_fsm <= s0;
                        //end
                        //else begin
                            //tx_frame_fsm <= s1;
                        //end
                    end
                    //else if (diff == 'h1) begin
                        //tx_underrun <= 1'b1;
                        //rd_addr <= rd_addr_sof;
                        //tx_frame_fsm <= s6;
                    //end
                end

                s6 : begin
                    synch <= 1'b1;
                    tx_frame_fsm <= s0;
                end

                default : begin 
                    tx_frame_fsm <= s0;
                end

            endcase

        end     // not reset
    end  //always

    ////////////////////////////////////////////////
    // signal_carefully
    ////////////////////////////////////////////////
    always @( posedge clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            commited_rd_address_change <= 1'b0;
            commited_rd_address <= 'b0;
            signal_carefully_fsm <= s0;
        end
        
        else begin  // not reset

            case (signal_carefully_fsm)

                s0: begin
                    commited_rd_address_change <= 1'b0;
                    if (end_of_eth_frame) begin
                        commited_rd_address <= rd_addr_prev0;
                        signal_carefully_fsm <= s1;
                    end
                end

                s1 : begin
                    commited_rd_address_change <= 1'b1;
                    signal_carefully_fsm <= s0;
                end

                default : begin 
                    signal_carefully_fsm <= s0;
                end

            endcase

        end     // not reset
    end  //always

endmodule // tx_mac_interface

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
