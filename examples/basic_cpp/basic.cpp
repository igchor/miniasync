// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2021, Intel Corporation */

/*
 * basic.cpp -- example showing miniasync integration with coroutines
 */

#include <coroutine>
#include <utility>
#include <string>
#include <iostream>
#include <vector>

#include "libminiasync.h"

struct simple_future {
	struct promise_type;
	using handle_type = std::coroutine_handle<promise_type>;

	simple_future(handle_type h): coroutine(h) {}

	bool is_ready();

	void await_resume() {};
	bool await_ready() { return is_ready(); }
	// std::coroutine_handle<> await_suspend(std::coroutine_handle<> h);
	std::coroutine_handle<> await_suspend(std::coroutine_handle<> h);

	handle_type coroutine;
};

struct simple_future::promise_type {
	simple_future get_return_object() { return simple_future(handle_type::from_promise(*this)); }
	std::suspend_always initial_suspend() { return {}; }


	struct final_awaitable
	{
		bool await_ready() const noexcept { return false; }

		template<typename Promise>
		std::coroutine_handle<> await_suspend(
			std::coroutine_handle<Promise> coro) noexcept
		{
			return coro.promise().continuation;
		}

		void await_resume() noexcept {}
	};


	auto final_suspend() noexcept { return final_awaitable{}; }
	void return_void() {}
	void unhandled_exception() {}

	std::coroutine_handle<> continuation;
};

bool simple_future::is_ready()
{
	return !coroutine || coroutine.done();
}

std::coroutine_handle<> simple_future::await_suspend(std::coroutine_handle<> h) {
	coroutine.promise().continuation = h;
	return coroutine;
}

template <typename Operation>
struct async_operation
{
	void await_resume() {}
	bool await_ready() { return false; }
	bool await_suspend(std::coroutine_handle<> h) {
		static_assert(std::is_base_of_v<async_operation, Operation>);

		awaitingCoroutine = h;
		return static_cast<Operation*>(this)->try_start();
	}

	std::coroutine_handle<> awaitingCoroutine;
};

struct async_memcpy_operation : public async_operation<async_memcpy_operation>
{
	async_memcpy_operation(struct vdm *v, void *dst, void *src, size_t n)
	{
		// XXX- v lifetime
		fut = vdm_memcpy(v, dst, src, n);
	}

	bool try_start()
	{
		return true;
	}

	struct vdm_memcpy_future fut;
};

template <typename T>
void wait_single(struct runtime *r, T&& awaitable)
{
	static_assert(std::is_base_of_v<async_operation, T>); // XXX - concept
	runtime_wait(r, FUTURE_AS_RUNNABLE(&awaitable.fut));
}

struct when_all_operation : public async_operation<when_all_operation>
{
	template <typename T>
	when_all_operation(struct runtime *r, std::vector<T>&& awaitables): r(r)
	{
		for (auto &a : awaitables)
			v.emplace_back(FUTURE_AS_RUNNABLE(&a.fut));
	}

	bool try_start()
	{
		runtime_wait_multiple(r, v.data(), v.size());
		return false;
	}

	struct runtime *r;
	std::vector<decltype(FUTURE_AS_RUNNABLE(std::add_pointer_t<vdm_memcpy_future>()))> v;
};

template <typename T>
auto wait_all(struct runtime *r, std::vector<T>&& awaitables)
{
	return when_all_operation(r, std::move(awaitables));
}


simple_future async_memcpy_print(std::string to_copy, char *buffer, const std::string &to_print, struct vdm *v)
{
	co_await async_memcpy_operation(v, buffer, to_copy.data(), to_copy.size());
	std::cout << to_print << std::endl;
}

simple_future task(std::string to_copy, char *buffer, const std::string &to_print, struct vdm *v)
{
	co_await async_memcpy_print(to_copy, buffer, to_print, v);
}

int
main(int argc, char *argv[])
{
	auto *r = runtime_new();
	auto *pthread_mover = vdm_new(vdm_descriptor_pthreads());

	static constexpr auto buffer_size = 10;
	static constexpr auto to_copy = "something";
	static constexpr auto to_print = "async print!";

	char buffer[buffer_size];
	for (auto &c : buffer)
		c = 0;

	{
		auto future = task(to_copy, buffer, to_print, pthread_mover);
		//wait_single(r, future);
	}

	runtime_delete(r);

	return 0;
}
