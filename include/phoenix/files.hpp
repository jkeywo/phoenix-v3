#pragma once
#include <phoenix/content.hpp>
#include <optional>
namespace phoenix {
struct ImportedFile { std::string purpose, filename; Bytes bytes; std::string error; };
void request_file(const std::string& purpose);
std::optional<ImportedFile> take_imported_file();
void export_file(const std::string& filename, const Bytes& bytes);
}
