#pragma once
#include <chrono>
#include <stdexcept>
#include <thread>
namespace phoenix::test
{
template <class Store> auto wait(Store &store, const char *message)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	do
	{
		if (auto value = store.poll())
			return *value;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	} while (std::chrono::steady_clock::now() < deadline);
	throw std::runtime_error(message);
}
} // namespace phoenix::test
