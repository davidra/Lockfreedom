///////////////////////////////////////////////////////////////////////////
//
// Simple battery of unit tests for showcasing and testing the lockfree containers
//
// TODO: Add relacy (http://www.1024cores.net/home/relacy-race-detector) tests for proving 
// the containers are 100% race-condition-proof
//
/////////////////////////////////////////////////////////////////////////////

#define CATCH_CONFIG_MAIN
#include "external\catch.hpp"

#define _ENABLE_ATOMIC_ALIGNMENT_FIX
#include <atomic>
#include <future>
#include <numeric>

#include "lockfree_pool.h"
#include "lockfree_stack.h"
#include "lockfree_queue.h"

//-------------------------------------------------------------------------
template <typename Fnc, typename... Args>
auto LaunchParallelTask(Fnc&& fnc, Args&&... args)
{
	return std::async(std::launch::async, std::forward<Fnc>(fnc), std::forward<Args>(args)...);
};

//-------------------------------------------------------------------------
template <typename tContainer>
void WaitForAll(const tContainer& future_container)
{
	for (const auto& future : future_container)
	{
		future.wait();
	}
}

//-------------------------------------------------------------------------
TEST_CASE("cLockfreePool single thread test", "[lockfreepool]") 
{
	typedef lockfree::cLockFreePool<int> tTestLockFreePool;
	tTestLockFreePool test_lockfreepool(3);

	REQUIRE(test_lockfreepool.Full());

	tTestLockFreePool::tElement* const element1 = test_lockfreepool.Acquire(42);
	tTestLockFreePool::tElement* const element2 = test_lockfreepool.Acquire(666);
	tTestLockFreePool::tElement* const element3 = test_lockfreepool.AcquirePtr();

	REQUIRE(element3 != nullptr);
	REQUIRE(*element1 == 42);
	REQUIRE(*element2 == 666);

	REQUIRE(test_lockfreepool.Empty());

	tTestLockFreePool::tElement* const element4 = test_lockfreepool.Acquire(1138);
	REQUIRE(element4 == nullptr);

	test_lockfreepool.Release(*element2);
	test_lockfreepool.Release(*element1);
	test_lockfreepool.ReleasePtr(element3);

	REQUIRE(test_lockfreepool.Full());
}

//-------------------------------------------------------------------------
TEST_CASE("cLockfreePool concurrent test", "[lockfreepool]")
{
	typedef lockfree::cLockFreePool<int> tTestLockFreePool;
	static constexpr const size_t TEST_LOCKFREEPOOL_CAPACITY = 500;
	tTestLockFreePool test_lockfreepool(TEST_LOCKFREEPOOL_CAPACITY);

	std::atomic<bool> release_signal = false;
	std::atomic<unsigned> acquire_count = 0;

	const auto acquire_then_release_elements = 
		[&test_lockfreepool, &release_signal, &acquire_count]
		{
			typedef tTestLockFreePool::tElement tElement;

			std::vector<tElement*> elements;
			for (unsigned count = acquire_count.fetch_add(1, std::memory_order_acq_rel)
				; count < TEST_LOCKFREEPOOL_CAPACITY
				; count = acquire_count.fetch_add(1, std::memory_order_acq_rel))
			{
				tElement* const acqr_element = test_lockfreepool.AcquirePtr();
				elements.push_back(acqr_element);
			}

			const size_t elements_acquired = elements.size();

			// Block until told to start releasing
			acquire_count.fetch_add(1, std::memory_order_acq_rel);
			while (!release_signal.load(std::memory_order_acquire))
				std::this_thread::yield();

			// Now release the elements acquired
			for (auto* element_to_remove : elements)
			{
				test_lockfreepool.ReleasePtr(element_to_remove);
			}

			return elements_acquired;
		};

	static constexpr const int NUM_TASKS = 16;
	std::vector<decltype(LaunchParallelTask(acquire_then_release_elements))> parallel_tasks;
	parallel_tasks.reserve(NUM_TASKS);

	std::generate_n(std::back_inserter(parallel_tasks), parallel_tasks.capacity(), [&acquire_then_release_elements] { return LaunchParallelTask(acquire_then_release_elements); });

	// Wait until all tasks have acquired their elements,
	while (acquire_count.load(std::memory_order_acquire) < TEST_LOCKFREEPOOL_CAPACITY);

	// At this point all tasks are waiting, and the lockfree pool should be empty
	REQUIRE(test_lockfreepool.Empty());

	// Signal the tasks so they can start releasing all elements
	release_signal.store(true, std::memory_order_release);

	const size_t total_elements_acquired = std::accumulate(parallel_tasks.begin(), parallel_tasks.end(), 0ULL, [](size_t total, decltype(*parallel_tasks.begin())& task) { return total + task.get(); });
	REQUIRE(total_elements_acquired == test_lockfreepool.GetCapacity());

	REQUIRE(test_lockfreepool.Full());
}

//-------------------------------------------------------------------------
TEST_CASE("cLockfreeStack single thread test", "[lockfreestack]")
{
	const auto test_stack = [] (auto& test_lockfreestack)
	{
		REQUIRE(test_lockfreestack.Empty());

		REQUIRE(test_lockfreestack.NonAtomicPush(42));
		REQUIRE(test_lockfreestack.NonAtomicPush(666));
		REQUIRE(test_lockfreestack.NonAtomicPush(1337));

		REQUIRE(test_lockfreestack.NonAtomicPush(1138) == false);

		int result = 0;
		REQUIRE(test_lockfreestack.NonAtomicPop(result));
		REQUIRE(result == 1337);
		REQUIRE(test_lockfreestack.NonAtomicPop(result));
		REQUIRE(result == 666);
		REQUIRE(test_lockfreestack.NonAtomicPop(result));
		REQUIRE(result == 42);
		REQUIRE(test_lockfreestack.NonAtomicPop(result) == false);
	};

	SECTION("cLockFreeStack using a shared pool")
	{
		typedef lockfree::cLockFreeStack<int> tTestLockFreeStack;
		tTestLockFreeStack::tLockFreePool pool(3);
		tTestLockFreeStack test_lockfreestack(pool);

		test_stack(test_lockfreestack);
	}

	SECTION("cLockFreeStack using local storage")
	{
		typedef lockfree::cLockFreeStack<int, 3> tTestLockFreeStack;
		tTestLockFreeStack test_lockfreestack;

		test_stack(test_lockfreestack);
	}
}

//-------------------------------------------------------------------------
TEST_CASE("cLockfreeStack concurrent test", "[lockfreestack]")
{
	static constexpr const int LOCKFREE_STACK_CAPACITY = 300;
	typedef lockfree::cLockFreeStack<int, LOCKFREE_STACK_CAPACITY> tLockFreeStack;
	tLockFreeStack test_lockfree_stack;

	std::vector<std::future<void>> futures;
	futures.reserve(LOCKFREE_STACK_CAPACITY * 2);

	// Insert a number of tasks that randomly insert/pop random values from the lock-free stack and then check things are coherent
	int total_pushes = 0;
	int pops = 0;
	std::generate_n(std::back_inserter(futures), futures.capacity(),
		[&total_pushes, &pops, &test_lockfree_stack]
		{
			const bool can_push = (total_pushes < LOCKFREE_STACK_CAPACITY);
			const bool can_pop = (total_pushes > pops);
			assert((can_pop || can_push) && "Well, this is embarrassing. Logic fail.");

			const int random_val = std::rand();
			const bool do_pop = can_pop && ((random_val & 1) || !can_push);
			if (do_pop)
			{
				++pops;
				return LaunchParallelTask(
					[&test_lockfree_stack]
					{
						int result = 0;
						while (!test_lockfree_stack.Pop(result));
						{
							std::this_thread::yield();
						}
					});
			}
			else
			{
				++total_pushes;
				return LaunchParallelTask(
					[&test_lockfree_stack, random_val]
				{
					test_lockfree_stack.Push(random_val);
				});
			}
		});

	WaitForAll(futures);

	int dummy = 0;
	REQUIRE(test_lockfree_stack.Empty());
	REQUIRE(!test_lockfree_stack.Pop(dummy));
}

//-------------------------------------------------------------------------
TEST_CASE("cLockfreeQueue single thread test", "[lockfreequeue]")
{
	auto test_queue = [](auto& test_lockfreequeue)
	{
		REQUIRE(test_lockfreequeue.Empty());

		REQUIRE(test_lockfreequeue.NonAtomicPush(42));
		REQUIRE(test_lockfreequeue.NonAtomicPush(666));
		REQUIRE(test_lockfreequeue.NonAtomicPush(1337));

		REQUIRE(test_lockfreequeue.NonAtomicPush(1138) == false);

		int result = 0;
		REQUIRE(test_lockfreequeue.NonAtomicPop(result));
		REQUIRE(result == 42);
		REQUIRE(test_lockfreequeue.NonAtomicPop(result));
		REQUIRE(result == 666);
		REQUIRE(test_lockfreequeue.NonAtomicPop(result));
		REQUIRE(result == 1337);

		REQUIRE(test_lockfreequeue.NonAtomicPop(result) == false);
	};

	SECTION("cLockFreeQueue using a shared pool")
	{
		typedef lockfree::cLockFreeQueue<int> tTestLockFreeQueue;
		tTestLockFreeQueue::tLockFreePool pool(3 + 1);
		tTestLockFreeQueue test_lockfreequeue(pool);

		test_queue(test_lockfreequeue);
	}

	SECTION("cLockFreeQueue using local storage")
	{
		typedef lockfree::cLockFreeQueue<int, 3> tTestLockFreeQueue;
		tTestLockFreeQueue test_lockfreequeue;

		test_queue(test_lockfreequeue);
	}
}

//-------------------------------------------------------------------------
TEST_CASE("cLockfreeQueue concurrent test", "[lockfreequeue]")
{
	static constexpr const size_t LOCKFREE_QUEUE_MAX_ELEMENTS = 300;
	typedef lockfree::cLockFreeQueue<int, LOCKFREE_QUEUE_MAX_ELEMENTS> tLockFreeQueue;

	unsigned int count = 0;
	tLockFreeQueue test_lockfree_queue;

	std::vector<std::future<void>> futures;
	futures.reserve(LOCKFREE_QUEUE_MAX_ELEMENTS * 2);

	// Insert a number of tasks that randomly insert/pop random values from the lock-free queue and then check things are coherent

	int total_pushes = 0;
	int pops = 0;
	std::generate_n(std::back_inserter(futures), futures.capacity(),
		[&total_pushes, &pops, &test_lockfree_queue]
		{
			const bool can_push = (total_pushes < LOCKFREE_QUEUE_MAX_ELEMENTS);
			const bool can_pop = (total_pushes > pops);
			assert((can_pop || can_push) && "Well, this is embarrassing. Logic fail.");

			const int random_val = std::rand();
			const bool do_pop = can_pop && ((random_val & 1) || !can_push);
			if (do_pop)
			{
				++pops;
				return LaunchParallelTask(
					[&test_lockfree_queue]
					{
						int result = 0;
						while (!test_lockfree_queue.Pop(result))
						{
							std::this_thread::yield();
						}
					});
			}
			else
			{
				++total_pushes;
				return LaunchParallelTask(
					[&test_lockfree_queue, random_val]
					{
						test_lockfree_queue.Push(random_val);
					});
			}
		});

	WaitForAll(futures);

	int dummy = 0;
	REQUIRE(!test_lockfree_queue.Pop(dummy));
}

//-------------------------------------------------------------------------
TEST_CASE("cMPSCLockFreeQueue single thread test", "[mpsclockfreequeue]")
{
	auto test_queue = [](auto& test_lockfreequeue)
	{
		REQUIRE(test_lockfreequeue.Empty());

		REQUIRE(test_lockfreequeue.NonAtomicPush(42));
		REQUIRE(test_lockfreequeue.NonAtomicPush(666));
		REQUIRE(test_lockfreequeue.NonAtomicPush(1337));

		REQUIRE(test_lockfreequeue.NonAtomicPush(1138) == false);

		int result = 0;
		REQUIRE(test_lockfreequeue.NonAtomicPop(result));
		REQUIRE(result == 42);
		REQUIRE(test_lockfreequeue.NonAtomicPop(result));
		REQUIRE(result == 666);
		REQUIRE(test_lockfreequeue.NonAtomicPop(result));
		REQUIRE(result == 1337);

		REQUIRE(test_lockfreequeue.NonAtomicPop(result) == false);
	};

	SECTION("cMPSCLockFreeQueue using a shared pool")
	{
		typedef lockfree::cMPSCLockFreeQueue<int> tTestLockFreeQueue;
		tTestLockFreeQueue::tLockFreePool pool(3 + 1);
		tTestLockFreeQueue test_lockfreequeue(pool);

		test_queue(test_lockfreequeue);
	}

	SECTION("cMPSCLockFreeQueue using local storage")
	{
		typedef lockfree::cMPSCLockFreeQueue<int, 3> tTestLockFreeQueue;
		tTestLockFreeQueue test_lockfreequeue;

		test_queue(test_lockfreequeue);
	}
}

//-------------------------------------------------------------------------
TEST_CASE("cMPSCLockFreeQueue concurrent test", "[mpsclockfreequeue]")
{
	static constexpr const size_t LOCKFREE_QUEUE_MAX_ELEMENTS = 300;
	typedef lockfree::cLockFreeQueue<unsigned, LOCKFREE_QUEUE_MAX_ELEMENTS> tLockFreeQueue;

	tLockFreeQueue test_lockfree_queue;

	static const constexpr unsigned PARALLEL_TASKS = 16;
	std::vector<std::future<void>> futures;
	futures.reserve(PARALLEL_TASKS);

	// Insert a number of tasks that insert values while we pop them from the main thread
	std::atomic<unsigned> total_pushes = 0;
	std::generate_n(std::back_inserter(futures), futures.capacity(),
		[&total_pushes, &test_lockfree_queue]
		{
			return LaunchParallelTask(
				[&total_pushes, &test_lockfree_queue]
				{
					for ( unsigned this_push = total_pushes.fetch_add(1, std::memory_order_acq_rel)
						; this_push < LOCKFREE_QUEUE_MAX_ELEMENTS
						; this_push = total_pushes.fetch_add(1, std::memory_order_acq_rel))
					{
						test_lockfree_queue.Push(this_push);
					}
				});
		});

	// Concurrently we pop (consume) from the Main Thread
	std::set<unsigned> pushed_elements;
	for (int pops = 0; pops < LOCKFREE_QUEUE_MAX_ELEMENTS; )
	{
		unsigned value = 0;
		if (test_lockfree_queue.Pop(value))
		{
			++pops;

			auto insert_result = pushed_elements.insert(value);
			REQUIRE(insert_result.second);
		}
	}

	REQUIRE(test_lockfree_queue.Empty());
	unsigned dummy = 0;
	REQUIRE(!test_lockfree_queue.Pop(dummy));
}
