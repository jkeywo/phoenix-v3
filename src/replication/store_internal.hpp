#pragma once
#include <phoenix/peer_store.hpp>
namespace phoenix::storage {
constexpr std::size_t max_journal = 128*1024*1024;
struct Recovered { WireJson state; std::size_t complete_bytes = 0; };
std::string encode(const WireJson& transaction);
Recovered replay(const std::string& journal, bool atomic_records);
}
