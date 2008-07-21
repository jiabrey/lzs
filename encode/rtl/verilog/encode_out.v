/******************************************************************************
 *   File Name :  encode_out.v
 *     Version :  0.1
 *        Date :  2008 02 27
 *  Description:  encode output module
 * Dependencies:
 *
 *
 *      Company:  Beijing Soul
 *
 *          BUG:
 *
 *****************************************************************************/
module encode_out(/*AUTOARG*/
   // Outputs
   m_dst_putn, m_dst, m_endn, m_dst_last,
   // Inputs
   clk, rst, ce, cnt_output_enable, cnt_finish, cnt_output,
   cnt_len
   );
   input clk, rst, ce;
   
   input cnt_output_enable, cnt_finish;
   input [12:0] cnt_output;
   input [3:0] cnt_len;

   output m_dst_putn;
   output [63:0] m_dst;

   output 	 m_endn;
   output 	 m_dst_last;
   
   /*AUTOREG*/
   
   reg 		 m_endn_reg;
   reg 		 m_dst_putn_reg;
   reg [63:0] 	 dst_reg;
   
   reg [3:0] 		din_len;
   reg [12:0] 		din_data;
   reg 			din_valid;
   always @(/*AS*/cnt_len or cnt_output or cnt_output_enable)
     begin
	/* if the end mark output done, stop drive the output data */
	din_len = cnt_len;
	din_data = cnt_output;
	din_valid = cnt_output_enable;
     end // always @ (...

   reg 			state, state_next;
   reg [4:0] 		cnt, cnt_next;
   reg [31:0] 		sreg; /* 13 + 15 = 28 */
   always @(posedge clk or posedge rst)
     begin
	if (rst)
	  sreg <= #1 0;
	else if (din_valid)
	  sreg <= #1 (sreg << din_len) | din_data;
     end
   
   always @(posedge clk or posedge rst)
     begin
	if (rst)
	  state <= #1 0;
	else
	  state <= #1 state_next;
     end

   always @(posedge clk or posedge rst) 
     begin
	if (rst)
	  cnt <= #1 0;
	else
	  cnt <= #1 cnt_next;
     end  

   always @(/*AS*/cnt or din_len or din_valid or state)
     begin
	state_next = 0;
	cnt_next = 0;

	case (state)
	  0: begin
	     if (din_valid) begin
		cnt_next = cnt + din_len;
		if (cnt_next > 15)
		  state_next = 1;
		else
		  state_next = 0;
	     end else begin
		state_next = 0;
		cnt_next = cnt;
	     end
	  end
	  
	  1: begin
	     if (din_valid) 
	       cnt_next = (cnt + din_len) - 5'h10;
	     else
	       cnt_next = cnt - 5'h10;
	     
	     if (cnt_next < 16)
	       state_next = 0;
	     else
	       state_next = 1;
	  end
	endcase // case(state)
     end // always @ (...

   /* output */
   reg [15:0] do;
   always @(/*AS*/cnt or sreg)
     begin
	do = sreg >> (cnt - 5'h10);
     end
   
   reg doe;
   always @(/*AS*/state)
     doe = state;

   /* 16 -> 64 */
   always @(posedge clk or posedge rst)
     begin
	if (rst)
	  dst_reg <= #1 0;
	else if (doe)
	  dst_reg <= #1 {do[07:00], do[15:08], dst_reg[63:16]};
     end
   
   reg [1:0] dcnt;
   always @(posedge clk or posedge rst)
     begin
	if (rst)
	  dcnt <= #1 0;
	else if (doe)
	  dcnt <= #1 dcnt + 1;
     end
   
   reg m_dst_last_reg;   
   always @(posedge clk or posedge rst)
     begin
	if (rst)
	  m_dst_putn_reg <= #1 1;
	else if (doe && (&dcnt) && m_endn_reg)
	  m_dst_putn_reg <= #1 0;
	else if (m_endn_reg == 0 && m_dst_last_reg == 0)
	  m_dst_putn_reg <= #1 0;
	else
	  m_dst_putn_reg <= #1 1;
     end

   /* I not sure using this way is safed */
   always @(posedge clk or posedge rst)
     begin
	if (rst)
	  m_endn_reg <= #1 1;
	else if (cnt_finish && state == 0 && doe == 0) /* all not busy */
	  m_endn_reg <= #1 0;
     end

   always @(posedge clk or posedge rst)
     begin
	if (rst)
	  m_dst_last_reg <= #1 0;
	else if (m_endn_reg == 0)
	  m_dst_last_reg <= #1 1;
     end
   
   /* output tri-buffer */
   assign 		m_endn     = ce ? m_endn_reg     : 1'bz;
   assign 		m_dst_putn = ce ? m_dst_putn_reg : 1'bz;
   assign 		m_dst      = ce ? dst_reg        : 64'bz;
   assign 		m_dst_last = ce ? m_dst_last_reg : 1'bz;
   
   /* for simuation */
   reg [19:0] 		tcnt;
   always @(posedge clk or posedge rst)
     begin
	if (rst)
	  tcnt <= #1 0;
	else if (!m_dst_putn_reg)
	  tcnt <= #1 tcnt + 2;
     end
endmodule // encode_out

// Local Variables:
// verilog-library-directories:("." "../../../../common/" "../../encode/src" "../../encode_ctl/src/" "../../encode_out/src/" "../../encode_dp/src/")
// verilog-library-files:("")
// verilog-library-extensions:(".v" ".h")
// End:
