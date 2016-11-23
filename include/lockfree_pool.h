///////////////////////////////////////////////////////////////////////////
//
//lockfree_pool.h
//
/////////////////////////////////////////////////////////////////////////////
#pragma once

#include "atomic_defs.h"
#include "utils.h"
#include "debug.h"

namespace lockfree
{
/// <summary>
///     Lock-free implementation of a generic pool. It allocates storage on construction and does not resize it during its lifecycle. It
///     uses an allocator provided on construction for allocating said storage. Its type can be specified optionally as the second template argument
/// </summary>
/// <remarks>
///		<list type="bullet">
///			<item><description>		
///				Since we reuse the space of each element to store the empty freelist nodes, we need T to be at least as big as the node size, which minimum 
///				size if 4 bytes. Therefore, T can not be smaller than 4 bytes.
///			</item></description>		
///			<item><description>		
///				The maximum number of elements contained by the pool depends on the size of its nodes, which in turn depends on the size of T. When T is bigger 
///				or equal than 8 bytes, its max capacity would be 2^32. Anything smaller than that will render a max capacity of 2^16
///			</item></description>		
///		</list>
///		
/// </remarks>
template<class T, class tPoolAllocator = std::allocator<T>>
class cLockFreePool
{
public:
	//-------------------------------------------------------------------------
	typedef T tElement;

	// ***ATOMIC INTERFACE

	/// <summary> 
	///		Acquires a T-sized block from the pool
	/// </summary>
	/// <return>
	///		Returns a pointer to a T-sized block of memory that has not yet been constructed, ready for the user to use placement new to construct it
	/// </return>
	T* AcquirePtr();

	/// <summary> 
	///		Acquires and constructs one element from the pool
	/// </summary>
	/// <param name=args>
	///		Variadic list of arguments that will be passed as arguments to the construction of the new element
	/// </param>
	/// <return>
	///		Returns a pointer to the newly acquired and constructed element
	/// </return>
	// acquire and construct a T
	template <typename... Args>
	T* Acquire(Args&&... args);

	/// <summary> 
	///		Releases a T-sized block from the pool without destructing it
	/// </summary>
	/// <param name=ptr>
	///		Pointer to the memory acquired by the pool that we want to release
	/// </param>
	/// <remarks>
	///		The object must be managed by the pool or the function will fail
	/// </remarks>
	void ReleasePtr(const T* ptr);

	/// <summary> 
	///		Releases and destructs a pool element
	/// </summary>
	/// <param name=ptr>
	///		Pointer to the memory acquired by the pool that we want to release and destruct
	/// </param>
	/// <remarks>
	///		The object must be managed by the pool or the function will fail
	/// </remarks>
	void Release(const T* ptr);

	/// <summary> 
	///		Releases and destructs a pool element
	/// </summary>
	/// <param name=element>
	///		Reference to the element we want to release and destruct
	/// </param>
	/// <remarks>
	///		The object must be managed by the pool or the function will fail
	/// </remarks>
	void Release(T& element);

	// ***NON-ATOMIC INTERFACE

	// TODO: Implement non-atomic versions of the above functions for situations where we know the pool is being used in a serial manner

	//-------------------------------------------------------------------------
	cLockFreePool(unsigned n, tPoolAllocator&& allocator);
	cLockFreePool(unsigned n, const tPoolAllocator& allocator = tPoolAllocator());

	//-------------------------------------------------------------------------
	// non copyable
	cLockFreePool(const cLockFreePool& rhs) = delete;
	cLockFreePool& operator=(const cLockFreePool& rhs) = delete;

	cLockFreePool(cLockFreePool&& rhs);
	cLockFreePool& operator=(cLockFreePool&& rhs);
	
	~cLockFreePool();

	/// <summary> 
	///		Queries if the pool has no elements left
	/// </summary>
	bool		Empty() const;

	/// <summary> 
	///		Queries the maximum number of elements the pool can contain
	/// </summary>
	unsigned	GetCapacity() const;

	/// <summary> 
	///		Queries if the pool has all elements available
	/// </summary>
	/// <remarks> 
	///		This function's complexity is O(N)
	/// </remarks> 
	bool		Full() const;

	/// <summary> 
	///		Queries if some memory is managed by (i.e., part of) the pool
	/// </summary>
	bool		Manages(const T* ptr) const;

private:
	static_assert(sizeof(T) >= sizeof(uint32_t), "Elements smaller than 4 bytes are not supported");
	static_assert(std::is_same<typename tPoolAllocator::value_type, T>::value, "The tPoolAllocator type argument does not allocate elements of type T");

	typedef std::conditional_t<sizeof(T) >= sizeof(uint64_t), uint32_t, uint16_t> tIndex;
	typedef tIndex tTag;

	//-------------------------------------------------------------------------
	struct tIndexTag
	{
		tIndexTag()
		{
			mIdx = 0;
			mTag = 0;
		}

		tIndexTag(tIndex idx, tTag tag)
		{
			mIdx = idx;
			mTag = tag;
		}

		bool operator==(const tIndexTag& rhs) const
		{
			return (mIdx == rhs.mIdx) && (mTag == rhs.mTag);
		}

		tIndex	mIdx;   // ptr offset into storage
		tTag	mTag;   // tag to avoid ABA problem
	};

	//-------------------------------------------------------------------------
	struct tNode
	{
		tIndexTag mNext;
	};

	//-------------------------------------------------------------------------
	enum { NULL_IDX = std::numeric_limits<tIndex>::max() };

	//-------------------------------------------------------------------------
	bool IsNull(tIndex index) const
	{
		return index >= mCapacity;
	}

	//-------------------------------------------------------------------------
	tNode* GetNode(tIndex index)
	{
		return reinterpret_cast<tNode*>(mStorage + index);
	}

	//-------------------------------------------------------------------------
	const tNode* GetNode(tIndex index) const
	{
		return reinterpret_cast<const tNode*>(mStorage + index);
	}

	//-------------------------------------------------------------------------
	void ReleaseAllPtrs()
	{
		mHead.store(tIndexTag(NULL_IDX, 0), memory_order_relaxed);
		for (int i = 0; i != mCapacity; ++i)
		{
			tNode* const node = GetNode(i);
			node->mNext.mIdx = NULL_IDX;
			node->mNext.mTag = 0;
			ReleasePtr(mStorage + i);
		}
	}

	//-------------------------------------------------------------------------
	void AllocateStorage(unsigned requested_capacity)
	{
		LF_assert(mStorage == nullptr, "Pool already in use.");
		if (mStorage != nullptr)
		{
			return;
		}

		static constexpr const unsigned max_capacity = std::numeric_limits<tIndex>::max() - 1;
		mCapacity = (std::min)(requested_capacity, max_capacity);
		mStorage = mAlloc.allocate(mCapacity);

		ReleaseAllPtrs();
	}

	//-------------------------------------------------------------------------
	tIndex AcquireIdx()
	{
		tIndexTag head_tmp = mHead.load(memory_order_relaxed);

		for(;;)
		{
			if (IsNull(head_tmp.mIdx))
			{
				return NULL_IDX;
			}

			const tNode* const node = GetNode(head_tmp.mIdx);
			const tIndexTag next = node->mNext;
			
			const tIndexTag tmp(next.mIdx, head_tmp.mTag + 1);	// increment tag to avoid ABA problem
			if (mHead.compare_exchange_weak(head_tmp, tmp, memory_order_acq_rel, memory_order_acquire))
			{
				return head_tmp.mIdx;
			}
		}
	}

	//-------------------------------------------------------------------------
	void ReleaseIdx(tIndex index)
	{
		if (IsNull(index))
		{
			return;
		}

		tNode* const node = GetNode(index);

		tIndexTag head_tmp = mHead.load(memory_order_relaxed);

		do
		{
			node->mNext.mIdx = head_tmp.mIdx;
		} while (!mHead.compare_exchange_weak(head_tmp, tIndexTag(index, head_tmp.mTag), memory_order_acq_rel, memory_order_acquire));
	}

	//-------------------------------------------------------------------------
	atomic<tIndexTag>	mHead;
	unsigned int		mCapacity;
	tPoolAllocator		mAlloc;
	T*					mStorage;
};   

#include "lockfree_pool.inl"
}

