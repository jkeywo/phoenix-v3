#include <phoenix/content.hpp>
#include <miniz.h>
#include <picosha2.h>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <set>
namespace phoenix {
namespace {
constexpr std::size_t max_archive = 32 * 1024 * 1024;
constexpr std::size_t max_file = 16 * 1024 * 1024;
constexpr std::size_t max_total = 64 * 1024 * 1024;
void path_check(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find_first_of("\\:\0",0,3) != std::string::npos) throw std::runtime_error("pack: unsafe path '"+path+"'");
    std::size_t start = 0;
    while (start < path.size()) {
        auto end = path.find('/', start); if (end == std::string::npos) end = path.size();
        auto segment = path.substr(start,end-start);
        if (segment.empty() || segment == "." || segment == "..") throw std::runtime_error("pack: unsafe path '"+path+"'");
        start = end+1;
    }
}
void validate_manifest(const Json& manifest) {
    if (!manifest.is_object() || manifest.value("schema",0) != 1 || !manifest.contains("id") || !manifest["id"].is_string()) throw std::runtime_error("manifest.json: expected schema 1 and string id");
    auto id = manifest["id"].get<std::string>();
    if (id.empty() || id.size() > 128 || id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos) throw std::runtime_error("manifest.json: invalid pack id '"+id+"'");
    for (auto key : {"dependencies","overrides"}) {
        if (manifest.contains(key)) {
            if (!manifest[key].is_array()) throw std::runtime_error(std::string("manifest.json: ")+key+" must be an array");
            std::set<std::string> seen;
            for (const auto& item : manifest[key]) if (!item.is_string() || !seen.insert(item.get<std::string>()).second) throw std::runtime_error(std::string("manifest.json: ")+key+" contains a duplicate/non-string id");
        }
    }
}
}
std::string sha256(std::span<const std::uint8_t> bytes) { return picosha2::hash256_hex_string(bytes.begin(),bytes.end()); }
Bytes json_bytes(const Json& json) { auto text = json.dump(2); return Bytes(text.begin(),text.end()); }
Json parse_json(std::span<const std::uint8_t> bytes, const std::string& source) {
    try { return Json::parse(bytes.begin(),bytes.end()); }
    catch (const std::exception& error) { throw std::runtime_error(source+": "+error.what()); }
}
Bytes read_file(const std::string& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open '"+path+"'");
    auto length = input.tellg();
    if (length < 0 || length > max_archive) throw std::runtime_error(path+": file exceeds 32 MiB limit");
    Bytes result(static_cast<std::size_t>(length)); input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(result.data()),static_cast<std::streamsize>(result.size()))) throw std::runtime_error(path+": read failed");
    return result;
}
void write_file(const std::string& path, std::span<const std::uint8_t> bytes) {
    std::ofstream output(path,std::ios::binary|std::ios::trunc);
    if (!output || !output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))) throw std::runtime_error(path+": write failed");
}
Pack read_pack(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > max_archive) throw std::runtime_error("pack: archive exceeds 32 MiB limit");
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip,bytes.data(),bytes.size(),0)) throw std::runtime_error("pack: invalid ZIP");
    struct End { mz_zip_archive* zip; ~End(){mz_zip_reader_end(zip);} } end{&zip};
    Pack result; result.hash = sha256(bytes); result.archive_.assign(bytes.begin(),bytes.end());
    auto count = mz_zip_reader_get_num_files(&zip);
    if (count > 1024) throw std::runtime_error("pack: more than 1024 entries");
    std::size_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip,i,&stat)) throw std::runtime_error("pack: cannot read ZIP directory");
        if (mz_zip_reader_get_filename(&zip,i,nullptr,0) > sizeof(stat.m_filename)) throw std::runtime_error("pack: entry path exceeds supported length");
        if (stat.m_is_directory) continue;
        std::string path = stat.m_filename; path_check(path);
        if (stat.m_is_encrypted || stat.m_uncomp_size > max_file || stat.m_uncomp_size > max_total-total) throw std::runtime_error(path+": encrypted or oversized pack entry");
        if (result.files.contains(path)) throw std::runtime_error(path+": duplicate ZIP entry");
        Bytes file(static_cast<std::size_t>(stat.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(&zip,i,file.data(),file.size(),0)) throw std::runtime_error(path+": extraction/CRC failed");
        total += file.size(); result.files.emplace(path,std::move(file));
    }
    if (!result.files.contains("manifest.json") || !result.files.contains("definitions.json")) throw std::runtime_error("pack: missing manifest.json or definitions.json");
    result.manifest = parse_json(result.files.at("manifest.json"),"manifest.json"); validate_manifest(result.manifest);
    return result;
}
Bytes write_pack(const Pack& pack) {
    validate_manifest(pack.manifest);
    auto files = pack.files; files["manifest.json"] = json_bytes(pack.manifest);
    if (!files.contains("definitions.json")) throw std::runtime_error("pack: missing definitions.json");
    if (files.size() > 1024) throw std::runtime_error("pack: more than 1024 entries");
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip,0,0)) throw std::runtime_error("pack: cannot initialise ZIP writer");
    struct End { mz_zip_archive* zip; ~End(){mz_zip_writer_end(zip);} } end{&zip};
    std::size_t total = 0;
    for (const auto& [path, bytes] : files) {
        path_check(path);
        if (bytes.size() > max_file || bytes.size() > max_total-total) throw std::runtime_error(path+": oversized pack entry");
        total += bytes.size();
        if (!mz_zip_writer_add_mem(&zip,path.c_str(),bytes.data(),bytes.size(),MZ_BEST_SPEED)) throw std::runtime_error(path+": ZIP write failed");
    }
    void* output = nullptr; std::size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip,&output,&size)) throw std::runtime_error("pack: ZIP finalise failed");
    std::unique_ptr<void,decltype(&mz_free)> allocation(output,&mz_free);
    if (size > max_archive) throw std::runtime_error("pack: archive exceeds 32 MiB limit");
    auto* begin = static_cast<std::uint8_t*>(output); return Bytes(begin,begin+size);
}
}
