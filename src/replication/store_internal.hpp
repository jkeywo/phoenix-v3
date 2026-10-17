#pragma once
namespace phoenix::storage
{
inline constexpr std::size_t max_journal = 128 * 1024 * 1024;
struct Recovered
{
	WireJson state;
	std::size_t complete_bytes = 0;
};
std::string encode(const WireJson &transaction);
Recovered replay(const std::string &journal, bool atomic_records);
} // namespace phoenix::storage
