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
    output reg                commited_rd_address,
    output                    commited_rd_address_change,
    input         [`BF:0]     wr_addr,                         //250 MHz domain driven
    input                     wr_addr_updated                         //250 MHz domain driven

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
    reg     [`BF+1:0] aux_wr_addr;
    reg     [`BF+1:0] start_wr_addr_next_pkt;
    reg     [`BF+1:0] wr_addr_extended;
    reg     [`BF+1:0] diff;
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
    reg              wr_addr_updated_reg0;
    reg              wr_addr_updated_reg1;
    reg     [`BF:0]  wr_addr_reg0;
    reg     [`BF:0]  wr_addr_reg0;

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
            wr_addr_reg0 <= 'b0;
            wr_addr_reg1 <= 'b0;
        end
        
        else begin  // not reset
            wr_addr_updated_reg0 <= wr_addr_updated;
            wr_addr_updated_reg1 <= wr_addr_updated_reg0;

            wr_addr_reg0 <= wr_addr;

            if (wr_addr_updated_reg1) begin                                      // transitory off
                wr_addr_reg1 <= wr_addr_reg0;
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
            qwords_in_eth <= 'b0;
            tx_start <= 1'b0;
            tx_data_valid <= 'b0;
            tx_data <= 'b0;
            state <= s0;
        end
        
        else begin  // not reset
            
            diff <= wr_addr_reg1 + (~rd_addr) +1;
            tx_start <= 1'b0;

            rd_addr_prev1 <= rd_addr;
            rd_addr_prev2 <= rd_addr_prev1;

            case (state)

                s0 : begin
                    byte_counter <= rd_data[63:32];
                    qwords_in_eth <= byte_counter[31:3];
                    if (byte_counter[2:0]) begin
                        qwords_in_eth <= byte_counter[31:3] +1;
                    end
                    
                    tx_data_valid <= 'b0;
                    next_rd_addr <= rd_addr + 1;
                    if (diff >= qwords_in_eth) begin
                        rd_addr <= next_rd_addr;
                        tx_start <= 1'b1;
                        state <= s1;
                    end
                end

                s1 : begin
                    tx_data <= rd_data;
                    tx_data_valid <= 'hFF;
                    next_rd_addr <= rd_addr + 1;
                    if (tx_ack) begin
                        rd_addr <= next_rd_addr;
                        state <= s2;
                    end
                end

                s2 : begin
                    rd_addr <= rd_addr + 1;
                    tx_data <= rd_data;
                    if (llenando al ultimo) begin
                        tx_data_valid <= tvalid;
                        state <= s3;
                    end
                end

                default : begin 
                    state <= s0;
                end

            endcase
        end     // not reset
    end  //always
   

endmodule // tx_mac_interface

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
