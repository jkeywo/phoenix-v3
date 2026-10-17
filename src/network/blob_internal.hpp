#pragma once
namespace phoenix::blobs
{
inline constexpr std::size_t max_bytes = 32 * 1024 * 1024;
void verify_key(const std::string &key);
Bytes encode(const std::string &key, const Bytes &bytes);
Bytes decode(const std::string &key, const Bytes &encoded);
} // namespace phoenix::blobs
