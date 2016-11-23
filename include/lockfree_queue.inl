namespace detail
{
	//----------------------------------------------------------------------------
	template <typename T>
	struct tLockFreeQueueNode
	{
		typedef tTaggedPtr<tLockFreeQueueNode> tNodePtr;

		tLockFreeQueueNode()
			: mPrev(nullptr)
		{
		}

		~tLockFreeQueueNode()
		{
			if (!std::is_trivially_destructible<T>::value)
			{
				reinterpret_cast<T&>(mData).~T();
			}
		}

		tLockFreeQueueNode(const tLockFreeQueueNode& other)
			: mPrev(other.mPrev.load(memory_order_acquire))
		{
			SetData(other.GetData());
		}

		tLockFreeQueueNode(tLockFreeQueueNode&& other)
			: mPrev(other.mPrev.load(memory_order_acquire))
		{
			SetData(move(other.GetData()));
		}

		tLockFreeQueueNode& operator = (const tLockFreeQueueNode& other)
		{
			GetData() = other.GetData();
			mPrev = other.mPrev.load(memory_order_acquire);

			return *this;
		}

		tLockFreeQueueNode& operator = (tLockFreeQueueNode&& other)
		{
			GetData() = move(other.GetData());
			mPrev = other.mPrev.load(memory_order_acquire);

			return *this;
		}

		template <typename... Args>
		void SetData(Args&&... args)
		{
			new (&mData) T(forward<Args>(args)...);
		}

		T& GetData() { return reinterpret_cast<T&>(mData); }
		const T& GetData() const { return reinterpret_cast<const T&>(mData); }

		tAlignedStorage<T>	mData;
		atomic<tNodePtr>	mPrev;
	};

	//----------------------------------------------------------------------------
	template <typename T>
	struct tMPSCLockFreeQueueNode
	{
		template <typename... Args>
		tMPSCLockFreeQueueNode(Args&&... args)
			: mPrev(nullptr)
		{
			new (&mData) T(forward<Args>(args)...);
		}

		~tMPSCLockFreeQueueNode()
		{
			if (!std::is_trivially_destructible<T>::value)
			{
				reinterpret_cast<T&>(mData).~T();
			}
		}

		T& GetData() { return reinterpret_cast<T&>(mData); }
		const T& GetData() const { return reinterpret_cast<const T&>(mData); }

		tAlignedStorage<T>				mData;
		atomic<tMPSCLockFreeQueueNode*>	mPrev;
	};

	//----------------------------------------------------------------------------
	template <size_t N, class Allocator>
	class cLockFreeQueueLocalStorage
	{
		typedef typename Allocator::value_type		tElement;
		typedef cLockFreePool<tElement, Allocator>	tLockFreePool;
	protected:
		cLockFreeQueueLocalStorage()
			: mLocalPool(N, Allocator(mLocalStorage))
		{}

		tAlignedStorage<tElement>	mLocalStorage[N];
		tLockFreePool				mLocalPool;
	};
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
template <typename... Args>
bool cLockFreeQueue<T, LFQS_SHARED, Allocator>::Push(Args&&... args)
{
	return LinkBackNodeAtomically(forward<Args>(args)...);
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
bool cLockFreeQueue<T, LFQS_SHARED, Allocator>::Pop(T& result)
{
	// Explanation for memory ordering:
	// we only need to synchronize-with the writing to the mPrev pointer of the node we are going to pop so 
	// the construction of its data happens-before this. mFront ordering can be relaxed, then, since no other
	// thread will access the element that has been popped after mFront is CAS'ed

	tNodePtr old_front(mFront.load(memory_order_relaxed));
	tNodePtr old_front_prev(old_front->mPrev.load(memory_order_acquire));

	while (old_front_prev)
	{
		tNodePtr new_front(old_front_prev.GetPtr(), old_front.GetTag() + 1);
		if (mFront.compare_exchange_weak(old_front, new_front, memory_order_relaxed, memory_order_relaxed))
		{
			// If you get a compilation error here T's move assignment is deleted/private AND T's copy assignment
			// parameter is non-const T& (so it can't bind to a r-value reference), so fix that. If there is a good
			// reason for it to be that way this code can be changed to selectively copy instead of move data in 
			// those situations (but I don't think there is a good reason for that)
			result = move(old_front->GetData());
			mNodePool.Release(*old_front);

			_if_diagnosing(mCount.fetch_sub(1, memory_order_relaxed);)

				return true;
		}
		else
		{
			old_front_prev = old_front->mPrev.load(memory_order_acquire);
		}
	}

	return false;
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
cLockFreeQueue<T, LFQS_SHARED, Allocator>::cLockFreeQueue(tLockFreePool& pool)
	: mNodePool(pool)
	, mFront(nullptr)
	, mBack(nullptr)
{
	tElement* const dummy_node = AcquireNewNode();
	mFront.store(tNodePtr(dummy_node, 0), memory_order_relaxed);
	mBack.store(tNodePtr(dummy_node, 0), memory_order_release);

	_if_diagnosing(mCount.store(0, memory_order_relaxed);)
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
cLockFreeQueue<T, LFQS_SHARED, Allocator>::~cLockFreeQueue()
{
	T dummy = T();
	while (NonAtomicPop(dummy));

	// TODO: Find a better way to do this. This is intentional so we don't invoke the tNode destructor
	// on the sentinel node, which will try to destroy the data (that is still not instantiated there)
	LF_assert(mFront.load(memory_order_relaxed), "Front should not be nullptr");
	mNodePool.ReleasePtr(mFront.load(memory_order_relaxed).GetPtr());
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
bool cLockFreeQueue<T, LFQS_SHARED, Allocator>::Empty() const
{
	return !mFront.load(memory_order_relaxed)->mPrev.load(memory_order_relaxed);
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
template <typename... Args>
bool cLockFreeQueue<T, LFQS_SHARED, Allocator>::NonAtomicPush(Args&&... args)
{
	return LinkBackNodeNonAtomically(forward<Args>(args)...);
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
bool cLockFreeQueue<T, LFQS_SHARED, Allocator>::NonAtomicPop(T& result)
{
	tNodePtr old_front(mFront.load(memory_order_relaxed));
	tNodePtr old_front_prev(old_front->mPrev.load(memory_order_relaxed));
	if (old_front_prev)
	{
		mFront.store(tNodePtr(old_front_prev.GetPtr(), old_front.GetTag() + 1), memory_order_relaxed);

		// If you get a compilation error here T's move assignment is deleted/private AND T's copy assignment
		// parameter is non-const T& (so it can't bind to a r-value reference), so fix that. If there is a good
		// reason for it to be that way this code can be changed to selectively copy instead of move data in 
		// those situations (but I don't think there is a good reason for that)
		result = move(old_front->GetData());
		mNodePool.Release(*old_front);

		_if_diagnosing(mCount.fetch_sub(1, memory_order_relaxed);)

		return true;
	}

	return false;
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
template <typename... Args>
bool cLockFreeQueue<T, LFQS_SHARED, Allocator>::LinkBackNodeAtomically(Args&&... args)
{
	tElement* const new_node = AcquireNewNode();
	if (!new_node)
	{
		return false;
	}

	LF_assert(new_node->mPrev.load(memory_order_relaxed).GetPtr() == nullptr, "Previous must be nullptr.");

	// 1. Move back to the new (sentinel) node
	tNodePtr new_back(new_node);
	tNodePtr old_back = mBack.exchange(new_back, memory_order_acq_rel);

	// 2. Construct the pushed object in the old back node
	old_back->SetData(forward<Args>(args)...);

	// 3. Point the old node's prev pointer to the new node
	old_back->mPrev.store(new_back, memory_order_release);

	_if_diagnosing(mCount.fetch_add(1, memory_order_relaxed);)
	return true;
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
template <typename... Args>
bool cLockFreeQueue<T, LFQS_SHARED, Allocator>::LinkBackNodeNonAtomically(Args&&... args)
{
	tElement* const new_node = AcquireNewNode();
	if (!new_node)
	{
		return false;
	}

	LF_assert(new_node->mPrev.load(memory_order_relaxed).GetPtr() == nullptr, "Previous must be nullptr.");

	// 1. Move back to the new (sentinel) node
	tNodePtr new_back(new_node);
	tNodePtr old_back = mBack.exchange(new_back, memory_order_relaxed);

	// 2. Construct the pushed object in the old back node
	old_back->SetData(forward<Args>(args)...);

	// 3. Point the old node's prev pointer to the new node
	old_back->mPrev.store(new_back, memory_order_relaxed);

	_if_diagnosing(mCount.fetch_add(1, memory_order_relaxed);)
		return true;
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
auto cLockFreeQueue<T, LFQS_SHARED, Allocator>::AcquireNewNode() -> tElement*
{
	tElement* const new_mem = mNodePool.AcquirePtr();
	return new_mem ? new (new_mem) tElement() : nullptr;
}

//----------------------------------------------------------------------------
// cMPSCLockFreeQueue section
template <typename T, class Allocator>
template <typename... Args>
bool cMPSCLockFreeQueue<T, LFQS_SHARED, Allocator>::Push(Args&&... args)
{
	return LinkBackNodeAtomically(forward<Args>(args)...);
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
bool cMPSCLockFreeQueue<T, LFQS_SHARED, Allocator>::Pop(T& result)
{
	tElement* const old_front = mFront;
	tElement* const node_to_pop = old_front->mPrev.load(memory_order_acquire);
	if (node_to_pop)
	{
		mFront = node_to_pop;
		result = move(node_to_pop->GetData());

		// Release the old mFront
		mNodePool.Release(*old_front);

		_if_diagnosing(mCount.fetch_sub(1, memory_order_relaxed);)

			return true;
	}

	return false; // empty
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
cMPSCLockFreeQueue<T, LFQS_SHARED, Allocator>::cMPSCLockFreeQueue(tLockFreePool& pool)
	: mNodePool(pool)
{
	tElement* const sentinel_node = AcquireNewNode();

	mFront = sentinel_node;
	mBack.store(sentinel_node, memory_order_relaxed);

	_if_diagnosing(mCount.store(0, memory_order_relaxed);)
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
template <typename... Args>
bool cMPSCLockFreeQueue<T, LFQS_SHARED, Allocator>::NonAtomicPush(Args&&... args)
{
	return LinkBackNodeNonAtomically(forward<Args>(args)...);
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
bool cMPSCLockFreeQueue<T, LFQS_SHARED, Allocator>::Empty() const
{
	return !mFront->mPrev.load(memory_order_relaxed);
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
bool cMPSCLockFreeQueue<T, LFQS_SHARED, Allocator>::NonAtomicPop(T& result)
{
	tElement* const old_front = mFront;
	tElement* const node_to_pop = old_front->mPrev.load(memory_order_relaxed);
	if (node_to_pop)
	{
		mFront = node_to_pop;
		result = move(node_to_pop->GetData());

		// Release the old mFront
		mNodePool.Release(*old_front);

		_if_diagnosing(mCount.fetch_sub(1, memory_order_relaxed);)

			return true;
	}

	return false; // empty
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
template <typename... Args>
auto cMPSCLockFreeQueue<T, LFQS_SHARED, Allocator>::AcquireNewNode(Args&&... args) -> tElement*
{
	return mNodePool.Acquire(forward<Args>(args)...);
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
template <typename... Args>
bool cMPSCLockFreeQueue<T, LFQS_SHARED, Allocator>::LinkBackNodeAtomically(Args&&... args)
{
	tElement* const new_node = AcquireNewNode(forward<Args>(args)...);
	if (new_node)
	{
		tElement* const old_back = mBack.exchange(new_node, memory_order_acq_rel);
		old_back->mPrev.store(new_node, memory_order_release);

		_if_diagnosing(mCount.fetch_add(1, memory_order_relaxed);)

			return true;
	}

	return false;
}

//----------------------------------------------------------------------------
template <typename T, class Allocator>
template <typename... Args>
bool cMPSCLockFreeQueue<T, LFQS_SHARED, Allocator>::LinkBackNodeNonAtomically(Args&&... args)
{
	tElement* const new_node = AcquireNewNode(forward<Args>(args)...);
	if (new_node)
	{
		tElement* const old_back = mBack.exchange(new_node, memory_order_relaxed);
		old_back->mPrev.store(new_node, memory_order_relaxed);

		_if_diagnosing(mCount.fetch_add(1, memory_order_relaxed);)

			return true;
	}

	return false;
}
