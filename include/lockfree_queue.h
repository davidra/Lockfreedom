///////////////////////////////////////////////////////////////////////////
//
//lockfree_queue.h
//
/////////////////////////////////////////////////////////////////////////////
#pragma once

#include "lockfree_pool.h"
#include "tagged_ptr.h"
#include "utils.h"

namespace lockfree {

	namespace detail 
	{
		template <typename T>
		struct tLockFreeQueueNode;

		template <size_t N, class Allocator>
		class cLockFreeQueueLocalStorage;
	}

	enum eLockFreeQueueStorage : size_t { LFQS_SHARED = 0 };

/// <summary>
///     Lockfree implementation of a MPMC (Multiple Producers-Multiple Consumers) non-intrusive pool-based queue. Is bounded to the capacity 
///     of the pool used. Fails on pool overflow. The pool's type is specified by the container and needs to be instantiated externally and 
///		provided by reference on construction, to allow sharing it among other queues.
///     Provides a non-atomic interface as well, for when using it in thread-safe environments
///		
///		Pros: 
///     - Simple 
///     - Flexible: Works with classes that are move-only or classes that don't have default constructor
///		- Fast: Producers are wait-free. Consumers only need one acquire load and one relaxed CAS per iteration
///     - Zero-allocation: Pool based, and several containers can acquire elements from a shared pool
///
///     Cons:
///     - If the Push of an element gets interrupted (e.g., preempted) after updating mBack and before fixing up the mPrev pointer 
///		  (i.e., during construction of the pushed element) successive Pushes will work (their elements will eventually get pushed), but
///		  Pops won't be able to get past that element before the one being pushed (as if it was the last, hiding all further pushed 
///		  elements) until it is resumed and updates the first node's mPrev. I believe this is not a big problem considering the increase 
///		  on speed and flexibility, (alternatives like boost' won't work with move-only classes or classes without default constructor)
///		- Each lockfree queue allocates one sentinel node from the lockfree pool. This makes choosing the size of a shared pool a little bit 
///		  tricky, since it needs to account not only for the number of objects contained, but for the number of instances of queues using 
///		  the same pool in the application. Queues using local storage don't have this problem (they account for the extra node internally)
///
///		Requirements for T:
///		- T needs to support move or copy construction (the former will be chosen over the second if available), and move or copy assignment
///		- T's move or copy assignment and move or copy construction need to be thread-safe and lock-free
/// <summary>
template <typename T, size_t storage = LFQS_SHARED, class Allocator = std::allocator<detail::tLockFreeQueueNode<T>>>
class cLockFreeQueue;

template <typename T, class Allocator>
class cLockFreeQueue<T, LFQS_SHARED, Allocator>
{
protected:
	typedef detail::tLockFreeQueueNode<T>	tElement;
	typedef typename tElement::tNodePtr		tNodePtr;

	static_assert(std::is_same<typename Allocator::value_type, tElement>::value, "The allocator provided does not allocate the right type");

public:
	typedef T										tValueType;
	typedef Allocator								tAllocatorType;
	typedef cLockFreePool<tElement, tAllocatorType> tLockFreePool;

	// ***ATOMIC INTERFACE

	/// <summary> 
	///		Pushes a new object in the queue atomically
	/// </summary>
	/// <return>
	///		Returns true if object has been pushed successfully. False when an error occurs (like the pool being full, for example)
	/// </return>
	/// <remarks>
	///		The object will be emplaced with the variadic arguments passed. An empty argument list will push a default-constructed item
	/// </remarks>
	template <typename... Args>
	bool Push(Args&&... args);

	/// <summary> 
	///		Pops the next object in FIFO ordering atomically
	/// </summary>
	/// <param name="result">
	///     (Out) the pop object will be <b>moved</b> to this argument if pop succeeds
	/// </param>
	/// <return>
	///		Returns true if the queue was not empty and an object could be pop. False otherwise.
	/// </return>
	bool Pop(T& result);

	// ***NON-ATOMIC INTERFACE

	cLockFreeQueue(tLockFreePool& pool);
	~cLockFreeQueue();

	/// <summary> 
	///		Queries if the queue is empty
	/// </summary>
	/// <return>
	///		Returns true if the queue was empty.
	/// </return>
	/// <remarks>
	///		Does not really have a place in a multithreaded environment, by the time you act on something that was "empty" it could be
	///		non-empty already. It is assumed logic using this method will run in serial, therefore this code is not atomic
	/// </remarks>
	bool Empty() const;

	/// <summary> 
	///		Pushes a new object in the queue non atomically
	/// </summary>
	/// <return>
	///		Returns true if object has been pushed successfully. False when an error occurs (like the pool being full, for example)
	/// </return>
	/// <remarks>
	///		The object will be emplaced with the variadic arguments passed. An empty argument list will push a default-constructed item
	/// </remarks>
	template <typename... Args>
	bool NonAtomicPush(Args&&... args);

	/// <summary> 
	///		Pops the next object in FIFO ordering atomically
	/// </summary>
	/// <param name="result">
	///     (Out) the pop object will be <b>moved</b> to this argument if pop succeeds
	/// </param>
	/// <return>
	///		Returns true if the queue was not empty and an object could be pop. False otherwise.
	/// </return>
	bool NonAtomicPop(T& result);

private:
	// TODO: Implement copy/move
	cLockFreeQueue(const cLockFreeQueue&) = delete;
	cLockFreeQueue& operator=(const cLockFreeQueue&) = delete;

	template <typename... Args>
	bool LinkBackNodeAtomically(Args&&... args);

	template <typename... Args>
	bool LinkBackNodeNonAtomically(Args&&... args);

	tElement* AcquireNewNode();

	tLockFreePool&		mNodePool;
	atomic<tNodePtr>	mFront;
	atomic<tNodePtr>	mBack;

	_if_diagnosing(atomic<unsigned> mCount;)
};

//----------------------------------------------------------------------------
// This specialization uses a fixed-size local storage for the pool used by the stack
// TODO: In local-storage queues, NonAtomic Pushes and Pops could be optimized to use a non-atomic version of the lockfree pool's Acquire/Release member functions. Implement this
template <typename T, size_t storage, class Allocator>
class cLockFreeQueue 
	// the order in which we inherit from these is important, don't change it
	: protected detail::cLockFreeQueueLocalStorage<storage + 1, detail::local_storage_allocator<detail::tLockFreeQueueNode<T>, storage + 1>>
	, public cLockFreeQueue<T, LFSS_SHARED, detail::local_storage_allocator<detail::tLockFreeQueueNode<T>, storage + 1>>
{
	typedef detail::cLockFreeQueueLocalStorage<storage + 1, detail::local_storage_allocator<detail::tLockFreeQueueNode<T>, storage + 1>> tStorage;
	typedef cLockFreeQueue<T, LFSS_SHARED, detail::local_storage_allocator<detail::tLockFreeQueueNode<T>, storage + 1>> tBaseQueue;

public:
	cLockFreeQueue()
		: tStorage()
		, tBaseQueue(mLocalPool)
	{}
};

namespace detail
{
	template <typename T>
	struct tMPSCLockFreeQueueNode;
}

/// <summary>
///     Lockfree implementation of a MPSC (Multiple Producers-Single Consumer) non-intrusive pool-based queue. Is bounded to the capacity 
///     of the pool used. Fails on pool overflow. The pool's type is specified by the container and needs to be instantiated externally and 
///		provided by reference on construction, to allow sharing it among other queues.
///     Provides a non-atomic interface as well, for when using it in thread-safe environments
///		Based on http://www.1024cores.net/home/lock-free-algorithms/queues/non-intrusive-mpsc-node-based-queue (some points below extracted from there)
///		
///		Pros: 
///     - Simple 
///     - Flexible: Works with classes that are move-only or classes that don't have default constructor
///		- Waitfree and fast producers. One XCHG is the maximum that one can get with a multi-producer non-distributed queue.
/// 	- Extremely fast consumer. On fast - path it's atomic-free, XCHG executed per node batch, in order to grab 'last item'.
/// 	- No need for node order reversion. So pop operation is always O(1).
///     - Zero-allocation: Pool based, and several containers can acquire elements from a shared pool
/// 	- ABA - free.
///
///     Cons:
///     - Push function is blocking wrt consumer. I.e. if producer gets blocked, then consumer is blocked too. Fortunately the 'window of 
///		  inconsistency' is extremely small. The producer must be blocked exactly in (*).Actually it's disadvantage only as compared with totally 
///		  lockfree algorithm. It's still much better than a lock-based algorithm. 
///		- Each lockfree queue allocates one sentinel node from the lockfree pool. This makes choosing the size of a shared pool a little bit 
///		  tricky, since it needs to account not only for the number of objects contained, but for the number of instances of queues using 
///		  the same pool in the application. Queues using local storage don't have this problem (they account for the extra node internally)
///
///		Requirements for T:
///		- T needs to support move or copy construction (the former will be chosen over the second if available), and move or copy assignment
///		- T's move or copy assignment and move or copy construction need to be thread-safe and lock-free
/// <summary>
template <typename T, size_t storage = LFQS_SHARED, class Allocator = std::allocator<detail::tMPSCLockFreeQueueNode<T>>>
class cMPSCLockFreeQueue;

template <typename T, class Allocator>
class cMPSCLockFreeQueue<T, LFQS_SHARED, Allocator>
{
protected:
	typedef detail::tMPSCLockFreeQueueNode<T> tElement;

	static_assert(std::is_same<typename Allocator::value_type, tElement>::value, "The allocator provided does not allocate the right type");

public:
	typedef T										tValueType;
	typedef Allocator								tAllocatorType;
	typedef cLockFreePool<tElement, tAllocatorType> tLockFreePool;

	// ***ATOMIC INTERFACE

	/// <summary> 
	///		Pushes a new object in the queue atomically
	/// </summary>
	/// <return>
	///		Returns true if object has been pushed successfully. False when an error occurs (like the pool being full, for example)
	/// </return>
	/// <remarks>
	///		The object will be emplaced with the variadic arguments passed. An empty argument list will push a default-constructed item
	/// </remarks>
	template <typename... Args>
	bool Push(Args&&... args);

	/// <summary> 
	///		Pops the next object in FIFO ordering atomically
	/// </summary>
	/// <param name="result">
	///     (Out) the pop object will be <b>moved</b> to this argument if pop succeeds
	/// </param>
	/// <return>
	///		Returns true if the queue was not empty and an object could be pop. False otherwise.
	/// </return>
	bool Pop(T& result);

	// ***NON-ATOMIC INTERFACE
	cMPSCLockFreeQueue(tLockFreePool& pool);

	/// <summary> 
	///		Queries if the queue is empty
	/// </summary>
	/// <return>
	///		Returns true if the queue was empty.
	/// </return>
	/// <remarks>
	///		Does not really have a place in a multithreaded environment, by the time you act on something that was "empty" it could be
	///		non-empty already. It is assumed logic using this method will run in serial, therefore this code is not atomic
	/// </remarks>
	bool Empty() const;

	/// <summary> 
	///		Pushes a new object in the queue non atomically
	/// </summary>
	/// <return>
	///		Returns true if object has been pushed successfully. False when an error occurs (like the pool being full, for example)
	/// </return>
	/// <remarks>
	///		The object will be emplaced with the variadic arguments passed. An empty argument list will push a default-constructed item
	/// </remarks>
	template <typename... Args>
	bool NonAtomicPush(Args&&... args);

	/// <summary> 
	///		Pops the next object in FIFO ordering atomically
	/// </summary>
	/// <param name="result">
	///     (Out) the pop object will be <b>moved</b> to this argument if pop succeeds
	/// </param>
	/// <return>
	///		Returns true if the queue was not empty and an object could be pop. False otherwise.
	/// </return>
	bool NonAtomicPop(T& result);

private:
	// TODO: Implement copy/move
	cMPSCLockFreeQueue(const cMPSCLockFreeQueue&) = delete;
	cMPSCLockFreeQueue& operator=(const cMPSCLockFreeQueue&) = delete;

	template <typename... Args>
	tElement* AcquireNewNode(Args&&... args);

	template <typename... Args>
	bool LinkBackNodeAtomically(Args&&... args);

	template <typename... Args>
	bool LinkBackNodeNonAtomically(Args&&... args);


	tLockFreePool&		mNodePool;
	atomic<tElement*>	mBack;
	tElement*			mFront;

	_if_diagnosing(atomic<unsigned> mCount;)
};

//----------------------------------------------------------------------------
// This specialization uses a fixed-size local storage for the pool used by the stack
// TODO: In local-storage queues, NonAtomic Pushes and Pops could be optimized to use a non-atomic version of the lockfree pool's Acquire/Release member functions. Implement this
template <typename T, size_t storage, class Allocator>
class cMPSCLockFreeQueue 
	// the order in which we inherit from these is important, don't change it
	: protected detail::cLockFreeQueueLocalStorage<storage + 1, detail::local_storage_allocator<detail::tMPSCLockFreeQueueNode<T>, storage + 1>>
	, public cMPSCLockFreeQueue<T, LFSS_SHARED, detail::local_storage_allocator<detail::tMPSCLockFreeQueueNode<T>, storage + 1>>
{
	typedef detail::cLockFreeQueueLocalStorage<storage + 1, detail::local_storage_allocator<detail::tMPSCLockFreeQueueNode<T>, storage + 1>> tStorage;
	typedef cMPSCLockFreeQueue<T, LFSS_SHARED, detail::local_storage_allocator<detail::tMPSCLockFreeQueueNode<T>, storage + 1>> tBaseQueue;

public:
	cMPSCLockFreeQueue()
		: tStorage()
		, tBaseQueue(mLocalPool)
	{}
};

#include "lockfree_queue.inl"

}
