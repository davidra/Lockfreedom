///////////////////////////////////////////////////////////////////////////
//
//lockfree_stack.h
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
		struct tLockFreeStackNode;
	}

	enum eLockFreeStackStorage : size_t { LFSS_SHARED = 0 };

/// <summary>
///     Lockfree implementation of a MPMC (Multiple Producers-Multiple Consumers) non-intrusive pool-based stack. Is bounded to the capacity 
///     of the pool used. Fails on pool overflow. The pool used can be provided externally (so it can be shared among different queues that 
///		use the same pool) or can be managed internally for those cases when sharing is not necessary 
///     Provides a non-atomic interface as well, for when using it in thread-safe environments
///		
///		Pros: 
///     - Simple 
///     - Flexible: Works with classes that are move-only or classes that don't have default constructor
///     - Zero-allocation: Pool based, and several containers can acquire elements from a shared pool
///
///     Cons:
///     - If the Push of an element gets interrupted (e.g., preempted) after updating mBack and before fixing up the mPrev pointer 
///		  (i.e., during construction of the pushed element) successive Pushes will work (their elements will eventually get pushed), but
///		  Pops won't be able to get past that element before the one being pushed (as if it was the last, hiding all further pushed 
///		  elements) until it is resumed and updates the first node's mPrev. I believe this is not a big problem considering the increase 
///		  on speed and flexibility, (alternatives like boost' won't work with move-only classes or classes without default constructor)
///
///		Requirements for T:
///		- T needs to support move or copy construction (the former will be chosen over the second if available), and move or copy assignment
///		- T's move or copy assignment and move or copy construction need to be thread-safe and lock-free
/// </summary>
/// <remarks>
///		Note on the choice for naming the "mPrev" pointer: many other implementations seem to prefer to use "next", as the next element that 
///		would be popped, but I personally found more helpful for the implementation to visualize the container as a linked list in which elements 
///		inserted at the top are the rightmost and the "newer" elements, mPrev means the previous last element, or the element immediately to an 
///		element's left
/// </remarks>
template <typename T, size_t storage = LFSS_SHARED, class Allocator = std::allocator<detail::tLockFreeStackNode<T>>>
class cLockFreeStack;

template <typename T, class Allocator>
class cLockFreeStack<T, LFSS_SHARED, Allocator>
{
protected:
	typedef detail::tLockFreeStackNode<T>	tElement;
	typedef typename tElement::tNodePtr		tNodePtr;

	static_assert(std::is_same<typename Allocator::value_type, tElement>::value, "The allocator provided does not allocate the right type");

public:
	typedef T										tValueType;
	typedef Allocator								tAllocatorType;
	typedef cLockFreePool<tElement, tAllocatorType>	tLockFreePool;

	// ***ATOMIC INTERFACE

	/// <summary> 
	///		Pushes a new object in the stack atomically
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
	///		Pops the next object in LIFO ordering atomically.
	/// </summary>
	/// <param name="result">
	///     (Out) the pop object will be <b>moved</b> to this argument if pop succeeds
	/// </param>
	/// <return>
	///		Returns true if the stack was not empty and an object could be popped. False otherwise.
	/// </return>
	bool Pop(T& result);

	// ***NON-ATOMIC INTERFACE
	cLockFreeStack(tLockFreePool& pool);
	~cLockFreeStack();

	/// <summary> 
	///		Queries if the stack is empty
	/// </summary>
	/// <return>
	///		Returns true if the stack was empty.
	/// </return>
	/// <remarks>
	///		Does not really have a place in a multithreaded environment, by the time you act on something that was "empty" it could be
	///		non-empty already. It is assumed logic using this method will run in serial, therefore this code is not atomic
	/// </remarks>
	bool Empty() const;

	/// <summary> 
	///		Pushes a new object in the stack non atomically
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
	///		Pops the next object in LIFO ordering non atomically
	/// </summary>
	/// <param name="result">
	///     (Out) the pop object will be <b>moved</b> to this argument if pop succeeds
	/// </param>
	/// <return>
	///		Returns true if the stack was not empty and an object could be popped. False otherwise.
	/// </return>
	bool NonAtomicPop(T& result);

private:
	// TODO: Implement copy/move 
	cLockFreeStack(const cLockFreeStack&) = delete;
	cLockFreeStack& operator=(const cLockFreeStack&) = delete;

	void LinkTopNodeAtomically(tElement* new_node);
	void LinkTopNodeNonAtomically(tElement* new_node);

	tLockFreePool&		mNodePool;
	atomic<tNodePtr>	mTop;
	_if_diagnosing(atomic<unsigned> mCount;)
};

//----------------------------------------------------------------------------
// This specialization uses a fixed-size local storage for the pool used by the stack
// TODO: In local-storage stacks, NonAtomic Pushes and Pops could be optimized to use a non-atomic version of the lockfree pool's Acquire/Release member functions. Implement this
template <typename T, size_t storage, class Allocator>
class cLockFreeStack : public cLockFreeStack<T, LFSS_SHARED, detail::local_storage_allocator<detail::tLockFreeStackNode<T>, storage>>
{
	static const constexpr size_t CAPACITY = storage;

	typedef cLockFreeStack<T, LFSS_SHARED, detail::local_storage_allocator<detail::tLockFreeStackNode<T>, storage>> tBase;
	using typename tBase::tAllocatorType;
	using typename tBase::tElement;
	using typename tBase::tLockFreePool;

public:
	cLockFreeStack() 
		: tBase(mLocalPool)
		, mLocalPool(CAPACITY, tAllocatorType(mLocalStorage))
	{}

private:

	tAlignedStorage<tElement>	mLocalStorage[CAPACITY];
	tLockFreePool				mLocalPool;
};

#include "lockfree_stack.inl"
 
}

