// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2021, Intel Corporation */

/*
 * basic.cpp -- example showing miniasync integration with coroutines
 */

#include "libminiasync.h"

#include <iostream>
#include <queue>

#include "coroutine_helpers.hpp"

struct memcpy_task
{
	memcpy_task(char *dst, char *src, size_t n) {
		auto *pthread_mover = vdm_new(vdm_descriptor_pthreads());
		fut = vdm_memcpy(pthread_mover, (void*)dst, (void*)src, n);
	}

	bool await_ready()
	{
		return false;
	}

	void await_suspend(std::coroutine_handle<task::promise_type> h)
	{
		auto &futures = h.promise().futures;
		futures.emplace_back();

		future_poll(&fut, &futures.back().first);
		h.promise().futures.back.second = h;
	}

	void await_resume() {}

	struct vdm_memcpy_future fut;
};

/* Executor loop */
void wait(task& t)
{
	t.h.resume();

	auto &futures = t.h.promise().futures;
	while (futures.size()) {
		// XXX - optimize this for single future case,
		// it's not optimal to allocate new vector each time
		using futures_set = std::unordered_set<task::promise_type::future_type>;
		futures_set f_set = futures_set(futures.begin(), futures.end());
		
		int completed = 0;
		while (f_set.size()) {
			// XXX: can use umwait

			for (auto f_it = f_set.begin(); f_it != f_set.end();) {
				if (*f_it->first.ptr_to_monitor) {
					f_it->second.resume();
					f_it = f_set.erase(f_it);
				} else {
					f_it++;
				}
			}
		}
	}
}

task async_mempcy(char *dst, char *src, size_t n)
{
	std::cout << "Before memcpy" << std::endl;
	co_await memcpy_task{dst, src, n/2};
	std::cout << "After memcpy " << ((char*) dst) << std::endl;
	co_await memcpy_task{dst + n/2, src + n/2, n - n/2};
	std::cout << "After second memcpy " << ((char*) dst) << std::endl;

	auto a1 = memcpy_task{dst, src, 1};
	auto a2 = memcpy_task{dst + 1, src, 1};
	auto a3 = memcpy_task{dst + 2, src, 1};

	co_await when_all(a1, a2, a3);
	std::cout << "After 3 concurrent memcopies " << ((char*) dst) << std::endl;
}

task async_memcpy_print(char *dst, char *src, size_t n, std::string to_print)
{
	co_await async_mempcy(dst, src, n/2);
	// auto a2 = async_mempcy(dst + n/2, src + n/2, n - n/2);

	// co_await when_all(a1, a2);

	std::cout << to_print << std::endl;
}

int
main(int argc, char *argv[])
{
	auto r = runtime_new();

	static constexpr auto buffer_size = 10;
	static constexpr auto to_copy = "something";
	static constexpr auto to_print = "async print!";

	char buffer[buffer_size] = {0};
	{
		auto future = async_memcpy_print(buffer, std::string(to_copy).data(), buffer_size, to_print);

		std::cout << "inside main" << std::endl;

		wait(future);

		std::cout << buffer << std::endl;
	}

	runtime_delete(r);

	return 0;
}
