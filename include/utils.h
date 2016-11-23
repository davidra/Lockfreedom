#pragma once

#include "debug.h"
#include <utility>

#if !defined _MSC_VER || (_MSC_VER < 1900)
	#pragma message("WARNING: This code has not really been tested under compilers different than MSVC 14.0")
#endif

#if !defined LF_DIAGNOSTICS_ENABLED
	#define LF_DIAGNOSTICS_ENABLED 1
#endif

#if defined LF_DIAGNOSTICS_ENABLED && LF_DIAGNOSTICS_ENABLED
	#define _if_diagnosing(...) __VA_ARGS__
#else
	#define _if_diagnosing(...)
#endif

namespace lockfree
{
	using std::move;
	using std::forward;

	//-------------------------------------------------------------------------
	// See http://www.open-std.org/JTC1/sc22/WG21/docs/papers/2013/n3668.html
	template<class T, class U = T>
	T exchange(T& current, U&& replacement)
	{
		T previous = move(current);
		current = forward<U>(replacement);
		return previous;
	}

	//-------------------------------------------------------------------------
	template <typename T>
	using tAlignedStorage = std::aligned_storage_t<sizeof(T), alignof(T)>;

	//-------------------------------------------------------------------------
	namespace detail
	{
		// Simple allocator that simulates allocations from some specified storage
		// For situations where we know the allocator is going to be used once to allocate a buffer of some size and we want to provide the storage for it (on the stack, or in some object's space)
		// The storage is still owned externally
		template <typename T, size_t N>
		struct local_storage_allocator
		{
			typedef T value_type;

			// this is so we can pass arrays of std::aligned_storage objects too
			template <typename U>
			local_storage_allocator(U (&storage)[N])
				: mStorage(reinterpret_cast<T*>(storage))
			{
				static_assert((sizeof(T) == sizeof(U)) && (alignof(T) == alignof(U)), "storage should an array of elements with same size and alignment than T");
			}

			T* allocate(std::size_t n, ...)
			{
				LF_assert(n <= N, "Trying to allocate more than the size storage this allocator is managing");
				return mStorage;
			}

			void deallocate(T*, std::size_t)
			{
			}

		private:
			T* const mStorage;
		};
	}
}