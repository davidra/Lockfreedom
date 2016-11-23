namespace detail
{
	//----------------------------------------------------------------------------
	template <typename T>
	struct tLockFreeStackNode
	{
		typedef tTaggedPtr<tLockFreeStackNode> tNodePtr;

		tLockFreeStackNode()
		{
		}

		tLockFreeStackNode(const T& data)
			: mData(data)
		{
		}

		tLockFreeStackNode(T&& data)
			: mData(move(data))
		{
		}

		T			mData;
		tNodePtr	mPrev;
	};
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
cLockFreeStack<T, LFSS_SHARED, Allocator>::cLockFreeStack(tLockFreePool& pool)
	: mNodePool(pool)
	, mTop(tNodePtr(nullptr, 0))
{
	mTop.store(tNodePtr(nullptr, 0), memory_order_relaxed);

	_if_diagnosing(mCount.store(0, memory_order_relaxed);)
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
cLockFreeStack<T, LFSS_SHARED, Allocator>::~cLockFreeStack()
{
	T dummy = T();
	while (NonAtomicPop(dummy));
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
bool cLockFreeStack<T, LFSS_SHARED, Allocator>::Empty() const
{
	return (mTop.load(memory_order_relaxed).GetPtr() == nullptr);
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
template <typename... Args>
bool cLockFreeStack<T, LFSS_SHARED, Allocator>::Push(Args&&... args)
{
	tElement* const new_node = mNodePool.Acquire(forward<Args>(args)...);
	if (new_node)
	{
		LinkTopNodeAtomically(new_node);
		return true;
	}

	return false;
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
bool cLockFreeStack<T, LFSS_SHARED, Allocator>::Pop(T& result)
{
	tNodePtr old_top(mTop.load(memory_order_acquire));
	for (bool empty = (old_top.GetPtr() == nullptr); !empty; empty = (old_top.GetPtr() == nullptr))
	{
		// ABA gets solved by just changing the tag on pop calls, no need to do this in push too
		// Note that old_top at this point could have been released from the pool already (by a concurrent Pop call). 
		// But because it is pool-managed we can still do old_top->mPrev "safely" (the memory is still allocated). 
		// *old_top can contain anything at this point, though. Is the compare_exchange (with ABA tag) on mTop below 
		// which will tell us if the object is still the one we want... juggling with razor blades indeed
		tNodePtr new_top(old_top->mPrev.GetPtr(), old_top.GetTag() + 1);

		if (mTop.compare_exchange_weak(old_top, new_top, memory_order_acq_rel, memory_order_acquire))
		{
			result = move(old_top->mData);

			mNodePool.Release(*old_top);

			_if_diagnosing(mCount.fetch_sub(1, memory_order_relaxed);)

				return true;
		}
	}

	return false;
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
template <typename... Args>
bool cLockFreeStack<T, LFSS_SHARED, Allocator>::NonAtomicPush(Args&&... args)
{
	tElement* const new_node = mNodePool.Acquire(forward<Args>(args)...);
	if (new_node)
	{
		LinkTopNodeNonAtomically(new_node);
		return true;
	}

	return false;
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
bool cLockFreeStack<T, LFSS_SHARED, Allocator>::NonAtomicPop(T& result)
{
	tNodePtr old_top(mTop.load(memory_order_relaxed));
	const bool empty = (old_top.GetPtr() == nullptr);
	if (!empty)
	{
		tNodePtr new_top(old_top->mPrev.GetPtr(), old_top.GetTag() + 1);
		mTop.store(new_top, memory_order_relaxed);

		result = move(old_top->mData);

		mNodePool.Release(*old_top);

		_if_diagnosing(mCount.fetch_sub(1, memory_order_relaxed);)
	}

	return !empty;
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
void cLockFreeStack<T, LFSS_SHARED, Allocator>::LinkTopNodeAtomically(tElement* new_node)
{
	LF_assert(new_node, "Invalid new_node.");

	new_node->mPrev = mTop.load(memory_order_relaxed);

	tNodePtr new_node_ptr;
	do
	{
		new_node_ptr.Set(new_node, new_node->mPrev.GetTag());
	} while (!mTop.compare_exchange_weak(new_node->mPrev, new_node_ptr, memory_order_acq_rel, memory_order_acquire));

	_if_diagnosing(mCount.fetch_add(1, memory_order_relaxed);)
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
void cLockFreeStack<T, LFSS_SHARED, Allocator>::LinkTopNodeNonAtomically(tElement* new_node)
{
	LF_assert(new_node, "Invalid new_node.");

	new_node->mPrev = mTop.load(memory_order_relaxed);
	mTop.store(tNodePtr(new_node, new_node->mPrev.GetTag()), memory_order_relaxed);

	_if_diagnosing(mCount.fetch_add(1, memory_order_relaxed);)
}
