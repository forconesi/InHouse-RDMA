//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////

`define TX_MEM_WR64_FMT_TYPE 7'b11_00000
`define MAX_HUGE_PAGE_OFFSET 64'h100000 // 1MB change this to 2MB 1GB

module wr_rx_pkt_to_hugepages (

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

    input       [63:0]     huge_page_addr_1,
    input       [63:0]     huge_page_addr_2,
    input                  huge_page_status_1,
    input                  huge_page_status_2,
    output reg             huge_page_free_1,
    output reg             huge_page_free_2,

    output reg             trigger_tlp_ack,
    input                  trigger_tlp,     // 156.25 MHz domain driven
    output reg             change_huge_page_ack,
    input                  change_huge_page,     // 156.25 MHz domain driven
    input                  send_last_tlp_change_huge_page,          // 156.25 MHz domain driven
    output      [8:0]      rd_addr,
    input       [63:0]     rd_data,
    output      [9:0]      commited_rd_address,
    input       [8:0]      qwords_to_send,        // 156.25 MHz domain driven
    output reg  [9:0]      rd_addr_extended_out

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
    // Local trigger_tlp_ack & change_huge_page_ack pulse generation for 156.25 MHz domain
    //-------------------------------------------------------
    reg     [14:0]  pulse_gen_fsm1;
    reg     [14:0]  pulse_gen_fsm2;
    
    //-------------------------------------------------------
    // Local current_huge_page_addr
    //-------------------------------------------------------
    reg     [63:0]  current_huge_page_addr;
    reg     [14:0]  give_huge_page_fsm;
    reg     [14:0]  free_huge_page_fsm;
    reg             huge_page_available;


    //-------------------------------------------------------
    // Local send_tlps_machine
    //-------------------------------------------------------   
    reg             return_huge_page_to_host;
    reg             trigger_tlp_ack_internal;
    reg     [14:0]  state;

    reg     [8:0]   tlp_qword_counter;
    reg     [9:0]   last_rd_address;
    reg     [9:0]   next_rd_address;
    reg     [9:0]   recovery_last_rd_address;
    reg     [31:0]  tlp_number;
    reg     [31:0]  next_tlp_number;
    reg     [8:0]   qwords_in_tlp;
    reg     [63:0]  host_mem_addr;
    reg     [63:0]  next_host_mem_addr;
    reg     [31:0]  huge_page_qword_counter;
    reg     [31:0]  next_huge_page_qword_counter;
    reg             endpoint_not_ready_startover;
    reg             tlp_retry;
    reg     [9:0]   rd_addr_extended;

    reg             change_huge_page_reg;
    reg             send_last_tlp_change_huge_page_reg;
    reg             send_last_tlp_change_huge_page_reg0;
    reg             remember_to_change_huge_page;
    reg     [8:0]   qwords_to_send_reg;
    reg             trigger_tlp_reg;
    reg             trigger_tlp_reg0;


    assign reset_n = ~trn_lnk_up_n;

    ////////////////////////////////////////////////
    // trigger_tlp_ack & change_huge_page_ack pulse generation for 156.25 MHz domain   must be active for 3 clks in 250 MHz domain
    ////////////////////////////////////////////////
    always @( posedge trn_clk or negedge reset_n ) begin
        
        if (!reset_n ) begin  // reset
            trigger_tlp_ack <= 1'b0;
            pulse_gen_fsm1 <= s0;

            change_huge_page_ack <= 1'b0;
            pulse_gen_fsm2 <= s0;
        end
        else begin  // not reset

            case (pulse_gen_fsm1)
                s0 : begin
                    trigger_tlp_ack <= 1'b0;
                    if (trigger_tlp_ack_internal) begin
                        trigger_tlp_ack <= 1'b1;
                        pulse_gen_fsm1 <= s1;
                    end
                end
                s1 : pulse_gen_fsm1 <= s2;
                s2 : pulse_gen_fsm1 <= s0;
            endcase

            case (pulse_gen_fsm2)
                s0 : begin
                    change_huge_page_ack <= 1'b0;
                    if (return_huge_page_to_host) begin
                        change_huge_page_ack <= 1'b1;
                        pulse_gen_fsm2 <= s1;
                    end
                end
                s1 : pulse_gen_fsm2 <= s2;
                s2 : pulse_gen_fsm2 <= s0;
            endcase

        end     // not reset
    end  //always

    ////////////////////////////////////////////////
    // current_huge_page_addr
    ////////////////////////////////////////////////
    always @( posedge trn_clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            huge_page_free_1 <= 1'b0;
            huge_page_free_2 <= 1'b0;
            huge_page_available <= 1'b0;
            current_huge_page_addr <= 64'b0;
            give_huge_page_fsm <= s0;
            free_huge_page_fsm <= s0;
        end

        else begin  // not reset

            case (free_huge_page_fsm)
                s0 : begin
                    if (return_huge_page_to_host) begin
                        huge_page_free_1 <= 1'b1;
                        free_huge_page_fsm <= s1;
                    end
                end
                s1 : begin
                    huge_page_free_1 <= 1'b0;
                    free_huge_page_fsm <= s2;
                end
                s2 : begin
                    if (return_huge_page_to_host) begin
                        huge_page_free_2 <= 1'b1;
                        free_huge_page_fsm <= s3;
                    end
                end
                s3 : begin
                    huge_page_free_2 <= 1'b0;
                    free_huge_page_fsm <= s0;
                end
            endcase

            case (give_huge_page_fsm)
                s0 : begin
                    if (huge_page_status_1) begin
                        huge_page_available <= 1'b1;
                        current_huge_page_addr <= huge_page_addr_1;
                        give_huge_page_fsm <= s1;
                    end
                end

                s1 : begin
                    if (return_huge_page_to_host) begin
                        huge_page_available <= 1'b0;
                        give_huge_page_fsm <= s2;
                    end
                end

                s2 : begin
                    if (huge_page_status_2) begin
                        huge_page_available <= 1'b1;
                        current_huge_page_addr <= huge_page_addr_2;
                        give_huge_page_fsm <= s3;
                    end
                end

                s3 : begin
                    if (return_huge_page_to_host) begin
                        huge_page_available <= 1'b0;
                        give_huge_page_fsm <= s0;
                    end
                end
            endcase

        end     // not reset
    end  //always

    assign commited_rd_address = last_rd_address;
    assign rd_addr = rd_addr_extended[8:0];

    ////////////////////////////////////////////////
    // write request TLP generation to huge_page
    ////////////////////////////////////////////////
    always @( posedge trn_clk or negedge reset_n ) begin

        if (!reset_n ) begin  // reset
            
            trn_td <= 64'b0;
            trn_trem_n <= 8'b0;
            trn_tsof_n <= 1'b1;
            trn_teof_n <= 1'b1;
            trn_tsrc_rdy_n <= 1'b1;
            cfg_interrupt_n <= 1'b1;

            endpoint_not_ready_startover <= 1'b0;
            tlp_retry <= 1'b0;

            trigger_tlp_ack_internal <= 1'b0;                // must be active for 2 or 3 clks in 250 MHz domain

            last_rd_address <= 10'b0;
            next_rd_address <= 10'b0;
            rd_addr_extended <= 10'b0;
            rd_addr_extended_out <= 10'b0;

            huge_page_qword_counter <= 32'b0;
            next_huge_page_qword_counter <= 32'b0;

            next_host_mem_addr <= 64'b0;
            
            tlp_qword_counter <= 9'b0;
            tlp_number <= 32'b0;
            next_tlp_number <= 32'b0;

            trigger_tlp_reg <= 1'b0;
            trigger_tlp_reg0 <= 1'b0;
            qwords_to_send_reg <= 9'b0;
            change_huge_page_reg <= 1'b0;
            send_last_tlp_change_huge_page_reg <= 1'b0;
            send_last_tlp_change_huge_page_reg0 <= 1'b0;
            remember_to_change_huge_page <= 1'b0;
           
            return_huge_page_to_host <= 1'b0;
            state <= s0;
        end
        
        else begin  // not reset

            trigger_tlp_reg <= trigger_tlp;
            trigger_tlp_reg0 <= trigger_tlp_reg;        // delay one tick
            qwords_to_send_reg <= qwords_to_send;
            change_huge_page_reg <= change_huge_page;
            send_last_tlp_change_huge_page_reg <= send_last_tlp_change_huge_page;
            send_last_tlp_change_huge_page_reg0 <= send_last_tlp_change_huge_page_reg;      // delay one tick

            case (state)

                s0 : begin
                    host_mem_addr <= current_huge_page_addr + 8'h80;         // first 128 bytes are reserved. DW0 in the huge page contains number of QWs in the huge page
                    huge_page_qword_counter <= 32'b0;
                    next_huge_page_qword_counter <= 32'b0;

                    if (huge_page_available) begin
                        state <= s1;
                    end
                end

                s1 : begin
                    if ( (trn_tbuf_av[1]) && (!trn_tdst_rdy_n) ) begin          // credits available and endpointready
                        if (change_huge_page_reg || remember_to_change_huge_page) begin                         // previous module wants to change huge page
                            remember_to_change_huge_page <= 1'b0;
                            state <= s7;
                        end
                        else if (send_last_tlp_change_huge_page_reg0) begin
                            remember_to_change_huge_page <= 1'b1;
                            state <= s3;
                        end
                        else if ( trigger_tlp_reg0 ) begin
                            state <= s3;
                        end
                    end

                    rd_addr_extended_out <= last_rd_address;

                    rd_addr_extended <= last_rd_address;

                    qwords_in_tlp <= qwords_to_send_reg;
                    recovery_last_rd_address <= last_rd_address;
                    next_rd_address <= last_rd_address + qwords_to_send_reg;

                    endpoint_not_ready_startover <= 1'b0;
                    tlp_retry <= 1'b0;

                    trn_td <= 64'b0;
                end

                s2 : begin
                    endpoint_not_ready_startover <= 1'b0;
                    tlp_retry <= 1'b1;
                    if ( (trn_tbuf_av[1]) && (!trn_tdst_rdy_n) ) begin
                        state <= s3;
                    end
                end

                s3 : begin
                    trn_td[63:32] <= {
                                1'b0,   //reserved
                                `TX_MEM_WR64_FMT_TYPE, //memory write request 64bit addressing
                                1'b0,   //reserved
                                3'b0,   //TC (traffic class)
                                4'b0,   //reserved
                                1'b0,   //TD (TLP digest present)
                                1'b0,   //EP (poisoned data)
                                2'b00,  //Relaxed ordering, No spoon in processor cache
                                2'b0,   //reserved
                                {qwords_in_tlp, 1'b0}  //lenght in DWs. 10-bit field    // QWs x2 equals DWs
                            };
                    trn_td[31:0] <= {
                                cfg_completer_id,   //Requester ID
                                {4'b0, tlp_number[3:0] },   //Tag
                                4'hF,   //last DW byte enable
                                4'hF    //1st DW byte enable
                            };
                    trn_tsof_n <= 1'b0;
                    trn_tsrc_rdy_n <= 1'b0;
                    rd_addr_extended <= rd_addr_extended +1;      // start addressing internal memory

                    next_host_mem_addr <= host_mem_addr + {qwords_in_tlp, 3'b0};                                 // host_addr is at byte level
                    next_huge_page_qword_counter <= huge_page_qword_counter + qwords_in_tlp;
                    next_tlp_number <= tlp_number +1;

                    if (!tlp_retry) begin
                        last_rd_address <= next_rd_address;
                    end
                    state <= s4;
                end

                s4 : begin
                    rd_addr_extended <= rd_addr_extended +1;      // addressing internal memory
                    if (!trn_tdst_rdy_n) begin
                        trn_tsof_n <= 1'b1;
                        trn_td <= host_mem_addr;
                        if (!tlp_retry) begin
                            trigger_tlp_ack_internal <= 1'b1;
                        end
                        state <= s5;
                    end
                    else begin
                        endpoint_not_ready_startover <= 1'b1;
                    end
                    tlp_qword_counter <= 9'b1;
                end

                s5 : begin
                    trigger_tlp_ack_internal <= 1'b0;

                    rd_addr_extended <= rd_addr_extended +1;      // addressing internal memory
                    
                    //trn_td <= {rd_data[31:0], rd_data[63:32]};  // DW swap      // if !trn_tdst_rdy_n != 1 the data will be corrupted, so it doesn't matter if this value is incorrect
                    trn_td <= {rd_data[7:0], rd_data[15:8], rd_data[23:16], rd_data[31:24], rd_data[39:32], rd_data[47:40], rd_data[55:48] ,rd_data[63:56]};      // in vhdl use a for loop

                    if (!trn_tdst_rdy_n) begin
                        tlp_qword_counter <= tlp_qword_counter +1;
                        if (tlp_qword_counter == qwords_in_tlp) begin
                            trn_teof_n <= 1'b0;
                            state <= s6;
                        end
                    end
                    else begin
                        endpoint_not_ready_startover <= 1'b1;
                    end
                end

                s6 : begin
                    if (!trn_tdst_rdy_n) begin
                        trn_teof_n <= 1'b1;
                        trn_tsrc_rdy_n <= 1'b1;
                        if (!endpoint_not_ready_startover) begin
                            host_mem_addr <= next_host_mem_addr;
                            huge_page_qword_counter <= next_huge_page_qword_counter;
                            tlp_number <= next_tlp_number;
                            state <= s1;
                        end
                        else begin
                            rd_addr_extended <= recovery_last_rd_address;
                            state <= s2;
                        end
                    end
                end
                
                s7 : begin
                    trn_td[63:32] <= {
                                1'b0,   //reserved
                                `TX_MEM_WR64_FMT_TYPE, //memory write request 64bit addressing
                                1'b0,   //reserved
                                3'b0,   //TC (traffic class)
                                4'b0,   //reserved
                                1'b0,   //TD (TLP digest present)
                                1'b0,   //EP (poisoned data)
                                2'b00,  //Relaxed ordering, No spoon in processor cache
                                2'b0,   //reserved
                                10'h02  //lenght equal 2 DW 
                            };
                    trn_td[31:0] <= {
                                cfg_completer_id,   //Requester ID
                                {4'b0, 4'b0 },   //Tag
                                4'hF,   //last DW byte enable
                                4'hF    //1st DW byte enable
                            };
                    trn_tsof_n <= 1'b0;
                    trn_tsrc_rdy_n <= 1'b0;
                    state <= s8;
                end

                s8 : begin
                    if (!trn_tdst_rdy_n) begin
                        trn_tsof_n <= 1'b1;
                        return_huge_page_to_host <= 1'b1;
                        trn_td <= current_huge_page_addr;
                        state <= s9;
                    end
                end

                s9 : begin
                    return_huge_page_to_host <= 1'b0;
                    if (!trn_tdst_rdy_n) begin
                        //trn_td <= {huge_page_qword_counter, 32'b0};
                        trn_td <= {huge_page_qword_counter[7:0], huge_page_qword_counter[15:8], huge_page_qword_counter[23:16], huge_page_qword_counter[31:24], 32'b0};
                        trn_teof_n <= 1'b0;
                        state <= s10;
                    end
                end

                s10 : begin
                    if (!trn_tdst_rdy_n) begin
                        trn_teof_n <= 1'b1;
                        trn_tsrc_rdy_n <= 1'b1;
                        state <= s11;
                    end
                end

                s11: begin
                    cfg_interrupt_n <= 1'b0;
                    state <= s12;
                end

                s12 : begin     // see [1] below
                    if (!cfg_interrupt_rdy_n) begin                                     // write tlp pkt was sent for the interrupt
                        cfg_interrupt_n <= 1'b1;
                        state <= s0;
                    end
                end

                default : begin 
                    state <= s0;
                end

            endcase
        end     // not reset
    end  //always
   

endmodule // wr_rx_pkt_to_hugepages



//[1] the experiment shows that we cannot go too fast to send tlps again
/*
                s12 : begin
                    host_mem_addr <= current_huge_page_addr + 8'h80;         // first 128 bytes are reserved. DW0 in the huge page contains number of QWs in the huge page
                    huge_page_qword_counter <= 32'b0;
                    next_huge_page_qword_counter <= 32'b0;

                    if (!cfg_interrupt_rdy_n) begin                                     // write tlp pkt was sent for the interrupt
                        cfg_interrupt_n <= 1'b1;
                        if (huge_page_available) begin
                            state <= s1;
                        end
                        else begin
                            state <= s0;
                        end
                    end
                end
*/
