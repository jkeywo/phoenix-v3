#pragma once
namespace phoenix::blobs
{
inline constexpr std::size_t max_bytes = content_archive_limit;
void verify_key(const std::string &key);
Bytes encode(const std::string &key, const Bytes &bytes);
Bytes decode(const std::string &key, const Bytes &encoded);
} // namespace phoenix::blobs
