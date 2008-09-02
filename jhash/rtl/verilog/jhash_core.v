/******************************************************************************
 *   File Name :  jhash_core.v
 *     Version :  0.1
 *        Date :  2008 08 29
 *  Description:  jash core module
 * Dependencies:
 *
 *
 *      Company:  Beijing Soul
 *
 *          BUG:
 *
 *****************************************************************************/
module jhash_core(/*AUTOARG*/
   // Outputs
   stream_ack, hash_out, hash_done,
   // Inputs
   clk, rst, stream_data0, stream_data1, stream_data2,
   stream_valid, stream_done, stream_left
   );
   input clk, rst;
   
   input [31:0] stream_data0,
		stream_data1,
		stream_data2;
   input 	stream_valid;
   input 	stream_done;
   input [1:0] 	stream_left;
   
   output 	stream_ack;

   output [31:0] hash_out;
   output 	 hash_done;
   
   /*AUTOREG*/
   // Beginning of automatic regs (for this module's undeclared outputs)
   reg			hash_done;
   reg [31:0]		hash_out;
   reg			stream_ack;
   // End of automatics
   /*AUTOWIRE*/
   // Beginning of automatic wires (for undeclared instantiated-module outputs)
   wire [31:0]		OA;			// From mix of mix.v
   wire [31:0]		OB;			// From mix of mix.v
   wire [31:0]		OC;			// From mix of mix.v
   // End of automatics
   
   parameter [1:0]
		S_IDLE = 2'b00,
		S_LOAD = 2'b01,
		S_RUN  = 2'b10,
		S_DONE = 2'b11;
   
   reg [1:0] 	
		state, state_n;
   always @(posedge clk or posedge rst)
     begin
	if (rst)
	  state <= #1 S_IDLE;
	else
	  state <= #1 state_n;
     end
   
   reg [2:0] round;
   reg 	     round_rst, round_inc;
   always @(posedge clk)
     if (round_rst)
       round <= #1 3'b000;
     else if (round_inc)
       round <= #1 round + 1'b1;

   reg [31:0] a, a_n,
	      b, b_n,
	      c, c_n;
   reg [4:0]  shift, shift_n;
   reg 	      final_r, final_n;
   
   mix mix (/*AUTOINST*/
	    // Outputs
	    .OA				(OA[31:0]),
	    .OB				(OB[31:0]),
	    .OC				(OC[31:0]),
	    // Inputs
	    .a				(a[31:0]),
	    .b				(b[31:0]),
	    .c				(c[31:0]),
	    .clk			(clk),
	    .shift			(shift[4:0]));
   
   always @(posedge clk)
     begin
	a <= #1 a_n;
	b <= #1 b_n;
	c <= #1 c_n;
	shift <= #1 shift_n;
	final_r<=#1 final_n;
     end

   always @(/*AS*/OA or OB or OC or a or b or c or final_r
	    or round or shift or state or stream_data0
	    or stream_data1 or stream_data2 or stream_done
	    or stream_left or stream_valid)
     begin
	a_n = a;
	b_n = b;
	c_n = c;
	state_n = state;
	stream_ack = 1'b0;
	shift_n = shift;
	round_inc = 1'b0;
	round_rst = 1'b0;
	final_n = final_r;
	
	case (state)
	  S_IDLE: begin
	     a_n = 32'h0;
	     b_n = 32'h0;
	     c_n = 32'h0;
	     final_n = 1'b0;
	     if (stream_valid) begin
		state_n = S_LOAD;
	     end
	  end
	  
	  S_LOAD:  if (stream_done) begin
	     /* a -= c;  a ^= rot(c, 4);  c += b; */
	     round_rst = 1'b1;
	     shift_n = 4;
	     state_n = S_RUN;
	     final_n = 1'b1;
	     case (stream_left)
	       2'b00: state_n = S_DONE;
	       2'b01: begin
		  a_n = a + stream_data0;
	       end
	       2'b10: begin
		  a_n = a + stream_data0;
		  b_n = b + stream_data1;
	       end
	       2'b11: begin
		  a_n = a + stream_data0;
		  b_n = b + stream_data1;
		  c_n = c + stream_data2;
	       end
	     endcase // case(stream_left)
	  end else // if (stream_done)
	    if (stream_valid) begin
	       /* a -= c;  a ^= rot(c, 4);  c += b; */
	       a_n = a + stream_data0;
	       b_n = b + stream_data1;
	       c_n = c + stream_data2;
	     state_n = S_RUN;
	       shift_n = 4;
	       stream_ack = 1'b1;
	       round_rst  = 1'b1;
	    end
	  
	  S_RUN: begin
	     a_n = OB;
	     b_n = OC;
	     c_n = OA;
	     round_inc = 1'b1;
	     
	     case (round)
	       3'b000:       /* b -= a;  b ^= rot(a, 6);  a += c; */
		 shift_n = 6;
	       3'b001:       /* c -= b;  c ^= rot(b, 8);  b += a; */
		 shift_n = 8;
	       3'b010:       /* a -= c;  a ^= rot(c,16);  c += b; */
		 shift_n = 16;
	       3'b011:       /* b -= a;  b ^= rot(a,19);  a += c */
		 shift_n = 19;
	       3'b100:       /* c -= b;  c ^= rot(b, 4);  b += a; */
		  shift_n = 4;
	       3'b101: begin
		  if (/*final_r*/stream_done)
		    state_n = S_DONE;
		  else
		    state_n = S_LOAD;
	       end
	     endcase // case(round)
	  end // case: S_RUN

	  S_DONE: ;
	endcase // case(state)
     end // always @ (...

   always @(posedge clk)
     hash_out <= #1 OA;
   
   always @(posedge clk)
     hash_done <= state == S_DONE;
   
endmodule // jhash_core