
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////

module msi_rd_vector (

    input    trn_clk,
    input    trn_lnk_up_n,

    output reg  [9:0]       cfg_dwaddr,
    output reg              cfg_rd_en_n,
    input       [31:0]      cfg_do,
    input                   cfg_rd_wr_done_n,
    input                   cfg_interrupt_msienable,

    output reg  [63:0]      msi_message_addr_reg,
    output reg  [15:0]      msi_message_data_reg
    
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

    wire            reset_n = ~trn_lnk_up_n;

    //-------------------------------------------------------
    // Local msi_rd_vector
    //-------------------------------------------------------
    reg     [7:0]   fsm;
    reg     [8:0]   timeout;
    reg     [27:0]  wait_alot_counter;
    (* KEEP = "TRUE" *)wire    [9:0]                  cfg_dwaddr_mon = cfg_dwaddr;

    
    ////////////////////////////////////////////////
    // msi_rd_vector
    ////////////////////////////////////////////////
    always @( posedge trn_clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            cfg_dwaddr <= 10'b0;
            cfg_rd_en_n <= 1'b1;
            //msi_message_addr_reg <= 64'b0;
            //msi_message_data_reg <= 16'b0;
            //msi_message_addr_reg <= 64'h00000000_FEE0100C;
            msi_message_addr_reg <= 64'h00000000_FEEFF00C;
            msi_message_data_reg <= 16'h4162;
            timeout <= 9'b0;
            wait_alot_counter <= 28'b0;
            fsm <= s0;
        end
        
        else begin  // not reset

            case (fsm)

                s0 : begin
                    if (cfg_interrupt_msienable) begin
                        //fsm <= s1;
                        fsm <= s0;
                    end
                end

                s1 : begin
                    wait_alot_counter <= wait_alot_counter +1;
                    if (wait_alot_counter == 28'hFFFFFFF) begin
                        fsm <= s2;
                    end
                end

                // See ug341

                s2 : begin
                    cfg_dwaddr <= 10'h023;                                              // Message Address
                    cfg_rd_en_n <= 1'b0;
                    timeout <= 9'b0;
                    fsm <= s3;
                end

                s3 : begin
                    cfg_rd_en_n <= 1'b1;
                    timeout <= timeout +1;
                    if (!cfg_rd_wr_done_n) begin
                        msi_message_addr_reg[31:0] <= cfg_do;
                        fsm <= s4;
                    end
                    else if (timeout == 9'h1ff) begin
                        fsm <= s2;
                    end
                end

                s4 : begin
                    cfg_dwaddr <= 10'h024;                                              // Message Upper Address 
                    cfg_rd_en_n <= 1'b0;
                    timeout <= 9'b0;
                    fsm <= s5;
                end

                s5 : begin
                    cfg_rd_en_n <= 1'b1;
                    timeout <= timeout +1;
                    if (!cfg_rd_wr_done_n) begin
                        msi_message_addr_reg[63:32] <= cfg_do;
                        fsm <= s6;
                    end
                    else if (timeout == 9'h1ff) begin
                        fsm <= s4;
                    end
                end

                s6 : begin
                    cfg_dwaddr <= 10'h025;                                              // Reserved (16 bits); Message Data (16 bits) 
                    cfg_rd_en_n <= 1'b0;
                    timeout <= 9'b0;
                    fsm <= s7;
                end

                s7 : begin
                    cfg_rd_en_n <= 1'b1;
                    timeout <= timeout +1;
                    if (!cfg_rd_wr_done_n) begin
                        msi_message_data_reg <= cfg_do[15:0];
                        fsm <= s8;
                    end
                    else if (timeout == 9'h1ff) begin
                        fsm <= s6;
                    end
                end

                s8 : begin
                    fsm <= s8;                  // Zzzz.....
                end

                default : begin 
                    fsm <= s0;
                end

            endcase
        end     // not reset
    end  //always

  

endmodule // msi_rd_vector