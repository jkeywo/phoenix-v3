#pragma once
#include <phoenix/content.hpp>
#include <optional>
namespace phoenix
{
struct ImportedFile
{
	std::string purpose, filename;
	Bytes bytes;
	std::string error;
};
void request_file(const std::string &purpose);
std::optional<ImportedFile> take_imported_file();
void export_file(const std::string &filename, const Bytes &bytes);
void copy_text(const std::string &text);
// An empty error is a completed platform copy, not merely an enqueued request.
std::optional<std::string> take_copy_result();
} // namespace phoenix
