//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////

module tlp_trigger (

    input    clk156,
    input    reset_n,

    // Internal logic
    input      [10:0]       commited_wr_address,     // 156.25 MHz domain driven
    input      [10:0]       commited_rd_address,     // 250 MHz domain driven
    
    input                   trigger_tlp_ack,         // 250 MHz domain driven
    output reg              trigger_tlp,
    input                   change_huge_page_ack,    // 250 MHz domain driven
    output reg              change_huge_page,
    output reg              send_last_tlp_change_huge_page,
    output reg [4:0]        qwords_to_send

    );

    // Local wires and reg

    //-------------------------------------------------------
    // Local 250 MHz signal synch
    //-------------------------------------------------------
    reg     [10:0]   commited_rd_address_reg;
    reg              trigger_tlp_ack_reg;
    reg              change_huge_page_ack_reg;

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
    localparam s6 = 8'b00100000;

    reg     [7:0]    main_fsm;
    reg     [10:0]   diff;
    reg     [10:0]   last_diff;
    reg     [3:0]    qwords_remaining;
    reg     [18:0]   huge_buffer_qword_counter;
    reg     [18:0]   look_ahead_huge_buffer_qword_counter;
    reg     [4:0]    number_of_tlp_to_send;
    reg     [4:0]    number_of_tlp_sent;
    reg              huge_page_dirty;

    
    ////////////////////////////////////////////////
    // 250 MHz signal synch
    ////////////////////////////////////////////////
    always @( posedge clk156 or negedge reset_n ) begin
        if (!reset_n ) begin  // reset
            commited_rd_address_reg <= 11'b0;
            trigger_tlp_ack_reg <= 1'b0;
            change_huge_page_ack_reg <= 1'b0;
        end
        
        else begin  // not reset
        
            commited_rd_address_reg <= commited_rd_address;
            trigger_tlp_ack_reg <= trigger_tlp_ack;
            change_huge_page_ack_reg <= change_huge_page_ack;

        end     // not reset
    end  //always

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

            diff <= 11'b0;
            last_diff <= 11'b0;
            qwords_remaining <= 4'b0;
            huge_page_dirty <= 1'b0;

            huge_buffer_qword_counter <= 19'h10;
            look_ahead_huge_buffer_qword_counter <= 19'b0;

            number_of_tlp_to_send <= 5'b0;
            number_of_tlp_sent <= 5'b0;

            main_fsm <= s0;
        end

        else begin  // not reset
            
            diff <= commited_wr_address + (~commited_rd_address_reg) +1;

            case (main_fsm)
                
                s0 : begin                                                      // waiting new eth frame(s) on internal buffer

                    last_diff <= diff;
                    look_ahead_huge_buffer_qword_counter <= huge_buffer_qword_counter + diff[9:0];
                    number_of_tlp_to_send <= diff[9:4];                                          // divide by 16 (QW/TLP)
                    qwords_to_send <= 5'h10;                                                // 32 DW

                    if (diff[9:0] >= 10'h10) begin                                              // greater than or equal to 16 QWORDs == 32 DWORDS == MAX_PAYLOAD_TLP
                        main_fsm <= s1;
                    end
                    else if ( (huge_page_dirty) && (timeout) ) begin
                        main_fsm <= s6;
                    end
                    else if ( (diff[9:0] > 10'b0) && (timeout) ) begin
                        qwords_to_send <= {1'b0, diff[3:0]};
                        main_fsm <= s4;
                    end
                end

                s1 : begin                                                      // check that the full ethernet frame will fit in the current huge page
                    huge_page_dirty <= 1'b1;

                    if ( look_ahead_huge_buffer_qword_counter[18] ) begin                    // overflow. no more than 2^18=262144 QW = 2MB in the huge page
                        if (qwords_remaining == 4'b0) begin                   // current eth frame(s) doesn't fit in the current huge_page signal to change it
                            change_huge_page <= 1'b1;
                            main_fsm <= s5;
                        end
                        else begin                                              // current eth frame doesn't fit in the current huge_page send the remainig of the last eth frame and change huge page
                            qwords_to_send <= {1'b0, qwords_remaining};
                            main_fsm <= s4;
                        end
                    end

                    else begin
                        qwords_remaining <= last_diff[3:0];                              // this remaining will not be written until new data arrive (or timeout). there is space in the huge page for this remainder
                        trigger_tlp <= 1'b1;
                        number_of_tlp_sent <= 5'b0;
                        main_fsm <= s2;
                    end
                end

                s2 : begin                                                      // waiting for TLP to be sent
                    if (trigger_tlp_ack_reg) begin
                        trigger_tlp <= 1'b0;
                        huge_buffer_qword_counter <= huge_buffer_qword_counter + 9'h10;     // increment the number of QW written to the Huge page in 16 QWs
                        number_of_tlp_sent <= number_of_tlp_sent +1;
                        main_fsm <= s3;
                    end
                end

                s3 : begin
                    if (number_of_tlp_sent < number_of_tlp_to_send) begin
                        trigger_tlp <= 1'b1;
                        main_fsm <= s2;
                    end
                    else begin
                        main_fsm <= s0;
                    end
                end

                s4 : begin
                    send_last_tlp_change_huge_page <= 1'b1;
                    main_fsm <= s5;
                end

                s5 : begin
                    huge_buffer_qword_counter <= 19'h10;        // the initial offset of a huge page is 32 DWs which are reserved
                    qwords_remaining <= 4'b0;
                    huge_page_dirty <= 1'b0;
                    if (change_huge_page_ack_reg) begin
                        send_last_tlp_change_huge_page <= 1'b0;
                        change_huge_page <= 1'b0;
                        main_fsm <= s0;
                    end
                end

                s6 : begin
                    if (qwords_remaining == 4'b0) begin
                        change_huge_page <= 1'b1;
                        main_fsm <= s5;
                    end
                    else begin
                        qwords_to_send <= {1'b0, qwords_remaining};
                        main_fsm <= s4;
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
