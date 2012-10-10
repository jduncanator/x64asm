#include "src/tracer/tracer.h"

#include "src/assembler/assembler.h"
#include "src/tracer/state.h"
#include "src/tracer/trace.h"

#include <iostream> // DEBUG
using namespace std;

#define offset(type, mem) ((size_t)(&((type*)0)->mem))

namespace x64 {

Function& Tracer::trace(Function& fxn, Trace& t, const Code& code) {
	assm_.start(fxn);

	for ( size_t i = 0, ie = code.size(); i < ie; ++i ) {
		const auto& instr = code.get(i);
		
		auto before = befores_.find(i) != befores_.end();
		auto after = afters_.find(i) != afters_.end();

		if ( before )
			trace_gp(t, true);
		if ( before || after )
			finish_state(t, i);
		assm_.assemble(instr);
		if ( after )
			trace_gp(t, false);
	}

	assm_.finish();

	return fxn;
}

void Tracer::finish_state(Trace& t, size_t line) {
	assm_.pushq_64r(rax);
	assm_.pushq_64r(rbx);
	assm_.lahf();
	assm_.pushw_16r(rax);

	// Record the line number of the current instruction
	assm_.movabsq_64rax_64o(rax, (Operand) &t.next_elem_);
	assm_.movq_64r_64i(rbx, (Operand) sizeof(State));
	assm_.imulq_64r_64r_rm1(rbx, rax);
	assm_.movq_64r_64i(rax, (Operand) &t.trace_);
	assm_.movq_64m_32i_rm0(Addr(rax, rbx), (Operand) line);

	// Increment the trace's next elem pointer
	assm_.movq_64r_64i(rax, (Operand) &t.next_elem_);
	assm_.incq_64m_rm0(Addr(rax));

	assm_.popw_16r(rax);
	assm_.sahf();
	assm_.popq_64r(rbx);
	assm_.popq_64r(rax);
}

void Tracer::trace_gp(Trace& t, bool is_before) {
	// Backup original rax and rbx
	assm_.pushq_64r(rax); 
	assm_.pushq_64r(rbx); 
	// Backup condition regiters
	assm_.lahf();         
	assm_.pushw_16r(rax); 
	// We've lost ax, get it back
	assm_.movq_64r_64m_rm1(rax, Addr(rsp, Imm(10)));

	// Push everything we want to write out to the stack
	for ( auto gp = gps_.begin(), gpe = gps_.end(); gp != gpe; ++gp )
		assm_.pushq_64r(*gp);

	// Find the address of the current State
	// If this is an after trace, you'll have to subtract 1
	assm_.movabsq_64rax_64o(rax, (Operand) &t.next_elem_);
	if ( !is_before )
		assm_.decq_64r_rm0(rax);

	assm_.movq_64r_64i(rbx, (Operand) sizeof(State));
	assm_.imulq_64r_64r_rm1(rbx, rax);
	assm_.movq_64r_64i(rax, (Operand) &t.trace_);
	assm_.addq_64r_64r_rm0(rbx, rax);
	
	// Pop the registers of the stack (reverse order!) and write to State
	for ( auto gp = gps_.rbegin(), gpe = gps_.rend(); gp != gpe; ++gp ) {
		const auto disp = is_before ? 
			Imm(offset(State, gp_before_) + *gp * sizeof(State::gp_reg_val_type)) :
			Imm(offset(State, gp_after_)  + *gp * sizeof(State::gp_reg_val_type));
		assm_.popq_64r(rax);
		assm_.movq_64m_64r_rm0(Addr(rbx, disp), rax);
	}

	// Put everything back the way we found it
	assm_.popw_16r(rax);
	assm_.sahf();
	assm_.popq_64r(rbx);
	assm_.popq_64r(rax);
}

} // namespace x64