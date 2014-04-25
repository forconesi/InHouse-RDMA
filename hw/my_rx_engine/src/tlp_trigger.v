//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////

module tlp_trigger (

    input    clk156,
    input    reset_n,

    // Internal logic
    input      [9:0]        commited_wr_address,     // 156.25 MHz domain driven
    input      [9:0]        commited_rd_address,     // 250 MHz domain driven
    
    input                   trigger_tlp_ack,         // 250 MHz domain driven
    output reg              trigger_tlp,
    input                   change_huge_page_ack,    // 250 MHz domain driven
    output reg              change_huge_page,
    output reg              send_last_tlp_change_huge_page,
    output reg [8:0]        qwords_to_send,

    output     [9:0]        commited_rd_address_out,
    output     [9:0]        commited_wr_address_out

    );

    // Local wires and reg

    //-------------------------------------------------------
    // Local timeout-generation
    //-------------------------------------------------------
    reg     [27:0]   free_running;
    reg              timeout;   

    //-------------------------------------------------------
    // Local trigger-logic
    //-------------------------------------------------------
    // localparam
    localparam s0 = 8'b00000000;
    localparam s1 = 8'b00000001;
    localparam s2 = 8'b00000010;
    localparam s3 = 8'b00000100;
    localparam s4 = 8'b00001000;
    localparam s5 = 8'b00010000;

    reg     [7:0]    main_fsm;
    reg     [9:0]    diff;
    reg     [9:0]    last_diff;
    reg     [8:0]    qwords_remaining;
    reg     [18:0]   huge_buffer_qword_counter;
    reg     [18:0]   look_ahead_huge_buffer_qword_counter;
    reg     [3:0]    number_of_tlp_to_send;
    reg     [3:0]    number_of_tlp_sent;
    reg              trigger_tlp_ack_reg;
    reg              change_huge_page_ack_reg;
    reg     [9:0]    commited_rd_address_reg;
    reg              timeout_reg;

    assign commited_rd_address_out = commited_rd_address_reg;
    assign commited_wr_address_out = commited_wr_address;

    ////////////////////////////////////////////////
    // timeout logic
    ////////////////////////////////////////////////
    always @( posedge clk156 or negedge reset_n ) begin
        if (!reset_n ) begin  // reset
            timeout <= 1'b0;
            free_running <= 28'b0;
        end
        
        else begin  // not reset

            if (main_fsm == s0) begin
                free_running <= free_running +1;
                timeout <= 1'b0;
                if (free_running == 28'hFFFFFFF) begin
                    timeout <= 1'b1;
                end
            end
            else begin
                timeout <= 1'b0;
                free_running <= 28'b0;
            end

        end     // not reset
    end  //always

    ////////////////////////////////////////////////
    // trigger-logic
    ////////////////////////////////////////////////
    always @( posedge clk156 or negedge reset_n ) begin
        
        if (!reset_n ) begin  // reset
            trigger_tlp <= 1'b0;
            change_huge_page <= 1'b0;
            send_last_tlp_change_huge_page <= 1'b0;

            diff <= 10'b0;
            last_diff <= 10'b0;
            qwords_remaining <= 9'b0;

            huge_buffer_qword_counter <= 19'h10;
            look_ahead_huge_buffer_qword_counter <= 19'b0;

            number_of_tlp_to_send <= 4'b0;
            number_of_tlp_sent <= 4'b0;

            trigger_tlp_ack_reg <= 1'b0;
            change_huge_page_ack_reg <= 1'b0;
            commited_rd_address_reg <= 10'b0;

            timeout_reg <= 1'b0;
            
            main_fsm <= s0;
        end

        else begin  // not reset
            
            trigger_tlp_ack_reg <= trigger_tlp_ack;
            change_huge_page_ack_reg <= change_huge_page_ack;
            commited_rd_address_reg <= commited_rd_address;

            diff <= commited_wr_address + (~commited_rd_address_reg) +1;

            case (main_fsm)
                
                s0 : begin                                                      // waiting new eth frame(s) on internal buffer

                    last_diff <= diff;
                    look_ahead_huge_buffer_qword_counter <= huge_buffer_qword_counter + diff[8:0];
                    number_of_tlp_to_send <= diff[8:4];                                          // divide by 16 (QW/TLP)

                    if (diff[8:0] >= 9'h10) begin                                               // greater than or equal to 16 QWORDs == 32 DWORDS == MAX_PAYLOAD_TLP
                        main_fsm <= s1;
                    end
                    else if ( (qwords_remaining > 9'b0) && (timeout) ) begin
                        timeout_reg <= 1'b1;
                        main_fsm <= s1;
                    end
                end

                s1 : begin                                                      // check that the full ethernet frame will fit in the current huge page
                    timeout_reg <= 1'b0;

                    if ( (look_ahead_huge_buffer_qword_counter[18]) || (timeout_reg) ) begin                    // overflow. no more than 2^18=262144 QW = 2MB in the huge page
                        if (qwords_remaining == 9'b0) begin                   // current eth frame(s) doesn't fit in the current huge_page signal to change it
                            change_huge_page <= 1'b1;
                            main_fsm <= s4;
                        end
                        else begin                                              // current eth frame doesn't fit in the current huge_page send the remainig of the last eth frame and change huge page
                            send_last_tlp_change_huge_page <= 1'b1;
                            qwords_to_send <= qwords_remaining;
                            main_fsm <= s4;
                        end
                    end

                    else begin
                        qwords_to_send <= 9'h10;                                                // 32 DW
                        huge_buffer_qword_counter <= huge_buffer_qword_counter + 9'h10;     // increment the number of QW written to the Huge page in 16 QWs
                        qwords_remaining <= {5'b0, last_diff[3:0]};                              // this remaining will not be written until new data arrive (or timeout). there is space in the huge page for this remainder
                        trigger_tlp <= 1'b1;
                        number_of_tlp_sent <= 4'b0;
                        main_fsm <= s2;
                    end
                end

                s2 : begin                                                      // waiting for TLP to be sent
                    if (trigger_tlp_ack_reg) begin
                        trigger_tlp <= 1'b0;
                        number_of_tlp_sent <= number_of_tlp_sent +1;
                        main_fsm <= s3;
                    end
                end

                s3 : begin
                    if (number_of_tlp_sent < number_of_tlp_to_send) begin
                        qwords_to_send <= 9'h10;
                        huge_buffer_qword_counter <= huge_buffer_qword_counter + 9'h10;
                        trigger_tlp <= 1'b1;
                        main_fsm <= s2;
                    end
                    else begin
                        main_fsm <= s0;
                    end
                end

                s4 : begin
                    huge_buffer_qword_counter <= 19'h10;        // the initial offset of a huge page is 32 DWs which are reserved
                    qwords_remaining <= 9'b0;
                    if (change_huge_page_ack_reg) begin
                        send_last_tlp_change_huge_page <= 1'b0;
                        change_huge_page <= 1'b0;
                        main_fsm <= s0;
                    end
                end

                default : begin
                    main_fsm <= s0;
                end

            endcase

        end     // not reset
    end  //always

endmodule // tlp_trigger

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
