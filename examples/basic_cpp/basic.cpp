// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2021, Intel Corporation */

///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Lewis Baker
// Licensed under MIT license.
///////////////////////////////////////////////////////////////////////////////

/*
 * basic.cpp -- example showing miniasync integration with coroutines
 */

#include <coroutine>
#include <utility>
#include <string>
#include <iostream>
#include <queue>

#include "libminiasync.h"

/* Similar to https://github.com/lewissbaker/cppcoro/blob/master/include/cppcoro/task.hpp */
struct task {
  struct promise_type {
      struct final_awaitable
      {
          bool await_ready() const noexcept { return false; }
          void await_resume() noexcept {}

          std::coroutine_handle<> await_suspend(std::coroutine_handle<task::promise_type> h) noexcept {
			  auto &cont = h.promise().cont;
              return cont ? cont : std::noop_coroutine();
          }
      };


    task get_return_object() { return task{std::coroutine_handle<task::promise_type>::from_promise(*this)}; }
    std::suspend_always initial_suspend() { return {}; }
    auto final_suspend() noexcept { return final_awaitable{}; }
    void return_void() {}
    void unhandled_exception() {}

    std::coroutine_handle<> cont;
  };

  void wait() {
    h.resume();
  }

  bool await_ready() { return !h || h.done();}
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> aw) {
      h.promise().cont = aw;
      return h;
    }
    void await_resume() {}

  std::coroutine_handle<task::promise_type> h;
};

std::queue<std::pair<std::vector<struct vdm_memcpy_future>, std::coroutine_handle<>>> futures;

struct memcpy_task
{
	memcpy_task(void *dst, void *src, size_t n) {
		auto *pthread_mover = vdm_new(vdm_descriptor_pthreads()); // XXX - lifetime
		fut = vdm_memcpy(pthread_mover, dst, src, n);
	}

	bool await_ready()
	{
		return false;
	}

	void await_suspend(std::coroutine_handle<> h)
	{
		futures.emplace(std::vector<struct vdm_memcpy_future>{fut}, h);
	}

	// custom function
	struct vdm_memcpy_future manual_await()
	{
		return fut;
	}

	void await_resume() {}

	struct vdm_memcpy_future fut;
};

void wait(struct runtime *r)
{
	while (futures.size()) {
		auto &p = futures.front();

		std::vector<future*> futs;
		for (auto &f : p.first)
			futs.emplace_back(FUTURE_AS_RUNNABLE(&f));

		runtime_wait_multiple(r, futs.data(), futs.size());
		p.second(); // resume coroutine
		futures.pop();
	}
}

struct when_all
{
	// XXX - passing vector here leads to internal complier error...
	when_all(memcpy_task a1, memcpy_task a2, memcpy_task a3): a1(a1), a2(a2), a3(a3)
	{
	}

	bool await_ready()
	{
		return false;
	}

	void await_suspend(std::coroutine_handle<> h)
	{
		std::vector<struct vdm_memcpy_future> v;
		v.emplace_back(a1.manual_await());
		v.emplace_back(a2.manual_await());
		v.emplace_back(a3.manual_await());

		futures.emplace(v, h);
	}

	void await_resume() {}

	memcpy_task a1, a2, a3;
};

task async_mempcy(void *dst, void *src, size_t n)
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

task async_memcpy_print(std::string to_copy, char *buffer, const std::string &to_print)
{
	co_await async_mempcy(reinterpret_cast<void*>(buffer), reinterpret_cast<void*>(to_copy.data()), to_copy.size());
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
		auto future = async_memcpy_print(to_copy, buffer, to_print);

		std::cout << "inside main" << std::endl;

		future.h.resume();
		wait(r);

		std::cout << buffer << std::endl;
	}

	runtime_delete(r);

	return 0;
}
