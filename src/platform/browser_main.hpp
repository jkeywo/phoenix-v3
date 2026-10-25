#pragma once
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#ifdef __EMSCRIPTEN_PTHREADS__
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
#endif

namespace phoenix::browser
{
#ifdef __EMSCRIPTEN_PTHREADS__
inline em_proxying_queue *main_queue()
{
	struct Queue
	{
		em_proxying_queue *value = em_proxying_queue_create();
		~Queue()
		{
			em_proxying_queue_destroy(value);
		}
	};
	static Queue queue;
	return queue.value;
}
#endif

// A frontend awaiting a synchronous session operation must also service browser
// calls made by that operation. Ordinary event-loop turns service the same queue.
inline void pump_main_calls()
{
#ifdef __EMSCRIPTEN_PTHREADS__
	if (emscripten_is_main_runtime_thread())
		emscripten_proxy_execute_queue(main_queue());
#endif
}

template <typename Function> auto on_main(Function function)
{
#ifdef __EMSCRIPTEN_PTHREADS__
	if (emscripten_is_main_runtime_thread())
		return function();
	using Result = std::invoke_result_t<Function>;
	std::exception_ptr failure;
	const auto invoke = [&](auto &call)
	{
		if (!emscripten_proxy_sync(
		        main_queue(), emscripten_main_runtime_thread_id(),
		        [](void *argument)
		        {
			        (*static_cast<std::remove_reference_t<decltype(call)> *>(argument))();
		        },
		        &call))
			throw std::runtime_error("Browser service thread is unavailable");
		if (failure)
			std::rethrow_exception(failure);
	};
	if constexpr (std::is_void_v<Result>)
	{
		auto call = [&]
		{
			try
			{
				function();
			}
			catch (...)
			{
				failure = std::current_exception();
			}
		};
		invoke(call);
	}
	else
	{
		std::optional<Result> result;
		auto call = [&]
		{
			try
			{
				result.emplace(function());
			}
			catch (...)
			{
				failure = std::current_exception();
			}
		};
		invoke(call);
		return std::move(*result);
	}
#else
	return function();
#endif
}
} // namespace phoenix::browser
