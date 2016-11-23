//-------------------------------------------------------------------------
template<class T, class tPoolAllocator>
cLockFreePool<T, tPoolAllocator>::cLockFreePool(unsigned n, tPoolAllocator&& allocator)
	: mHead(tIndexTag(NULL_IDX, 0))
	, mCapacity(0)
	, mAlloc(move(allocator))
	, mStorage(nullptr)
{
	AllocateStorage(n);
}

//-------------------------------------------------------------------------
template<class T, class tPoolAllocator>
cLockFreePool<T, tPoolAllocator>::cLockFreePool(unsigned n, const tPoolAllocator& allocator = tPoolAllocator()) : cLockFreePool(n, tPoolAllocator(allocator)) {}

//-------------------------------------------------------------------------
template<class T, class tPoolAllocator>
cLockFreePool<T, tPoolAllocator>::cLockFreePool(cLockFreePool&& rhs)
{
	*this = move(rhs);
}

//-------------------------------------------------------------------------
template<class T, class tPoolAllocator>
auto cLockFreePool<T, tPoolAllocator>::operator=(cLockFreePool&& rhs) -> cLockFreePool&
{
	mHead = rhs.mHead.exchange(tIndexTag(NULL_IDX, 0), memory_order_relaxed);
	mCapacity = exchange(rhs.mCapacity, 0);
	mAlloc = move(rhs.mAlloc);
	mStorage = exchange(rhs.mStorage, nullptr);

	return *this;
}

//-------------------------------------------------------------------------
template<class T, class tPoolAllocator>
cLockFreePool<T, tPoolAllocator>::~cLockFreePool()
{
	mAlloc.deallocate(mStorage, mCapacity);
}

//-------------------------------------------------------------------------
template<class T, class tPoolAllocator>
bool cLockFreePool<T, tPoolAllocator>::Empty() const
{
	return IsNull(mHead.load(memory_order_relaxed).mIdx);
}

//-------------------------------------------------------------------------
template<class T, class tPoolAllocator>
unsigned cLockFreePool<T, tPoolAllocator>::GetCapacity() const
{
	return mCapacity;
}

//-------------------------------------------------------------------------
template<class T, class tPoolAllocator>
T* cLockFreePool<T, tPoolAllocator>::AcquirePtr()
{
	T* ptr = nullptr;

	const tIndex idx = AcquireIdx();
	if (idx != NULL_IDX)
	{
		ptr = mStorage + idx;
	}

	return ptr;
}

//-------------------------------------------------------------------------
template<class T, class tPoolAllocator>
template <typename... Args>
T* cLockFreePool<T, tPoolAllocator>::Acquire(Args&&... args)
{
	T* const ptr = AcquirePtr();
	if (ptr)
	{
		new (ptr) T(forward<Args>(args)...);
	}
	return ptr;
}

//-------------------------------------------------------------------------
template<class T, class tPoolAllocator>
void cLockFreePool<T, tPoolAllocator>::ReleasePtr(const T* ptr)
{
	const ptrdiff_t ptr_to_storage_diff = ptr - mStorage;
	LF_assert((ptr_to_storage_diff >= 0) && (ptr_to_storage_diff < GetCapacity()), "Trying to release an object not managed by this pool!");
	const tIndex idx = static_cast<tIndex>(ptr_to_storage_diff);
	ReleaseIdx(idx);
}

//-------------------------------------------------------------------------
template<class T, class tPoolAllocator>
void cLockFreePool<T, tPoolAllocator>::Release(T const* ptr)
{
	if (!std::is_trivially_destructible<T>::value && ptr)
	{
		ptr->~T();
	}
	ReleasePtr(ptr);
}

//-------------------------------------------------------------------------
template<class T, class tPoolAllocator>
void cLockFreePool<T, tPoolAllocator>::Release(T& element)
{
	Release(&element);
}

//-------------------------------------------------------------------------
template<class T, class tPoolAllocator>
bool cLockFreePool<T, tPoolAllocator>::Full() const
{
	tIndexTag cur = mHead.load(memory_order_relaxed);
	for (unsigned int i = 0; i != mCapacity; ++i)
	{
		if (IsNull(cur.mIdx))
		{
			return false;
		}
		cur = GetNode(cur.mIdx)->mNext;
	}
	return true;
}

//-------------------------------------------------------------------------
template<class T, class tPoolAllocator>
bool cLockFreePool<T, tPoolAllocator>::Manages(const T* ptr) const
{
	return (ptr >= mStorage) && (ptr < (mStorage + mCapacity));
}
