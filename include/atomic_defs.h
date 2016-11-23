//////////////////////////////////////////////////////////////////////////
// atomic_defs.h
//////////////////////////////////////////////////////////////////////////
#pragma once

#define _ENABLE_ATOMIC_ALIGNMENT_FIX
#include <atomic>

namespace lockfree
{
	using std::atomic;

#define __declare_memory_order(order) \
	extern const auto memory_order_##order = std::memory_order::memory_order_##order
	// Uncomment this line and comment the one above if you suspect memory ordering problems
	//	extern const auto memory_order_##order = std::memory_order::memory_order_seq_cst

	__declare_memory_order(relaxed);
	__declare_memory_order(consume);
	__declare_memory_order(acquire);
	__declare_memory_order(release);
	__declare_memory_order(acq_rel);
	__declare_memory_order(seq_cst);
}
