#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>
namespace phoenix {
using Json = nlohmann::json;
using Bytes = std::vector<std::uint8_t>;
struct Catalog;
struct Pack {
    Json manifest;
    std::map<std::string, Bytes> files;
    std::string hash;
private:
    Bytes archive_;
    friend Pack read_pack(std::span<const std::uint8_t> bytes);
    friend Catalog resolve_packs(const std::vector<Pack>& ordered);
};
struct Catalog {
    std::map<std::string, Json> entities, maps, consoles, trees;
    std::map<std::string, Bytes> assets;
    std::vector<std::pair<std::string, std::string>> ordered_packs;
};
// Throws an actionable exception. ZIP is read into bounded memory, never
// extracted to caller-controlled paths on disk.
Pack read_pack(std::span<const std::uint8_t> bytes);
Bytes write_pack(const Pack& pack);
Catalog resolve_packs(const std::vector<Pack>& ordered);
void verify_catalog(const Catalog& catalog);
Bytes read_file(const std::string& path);
void write_file(const std::string& path, std::span<const std::uint8_t> bytes);
std::string sha256(std::span<const std::uint8_t> bytes);
Bytes json_bytes(const Json& json);
Json parse_json(std::span<const std::uint8_t> bytes, const std::string& source);
}
