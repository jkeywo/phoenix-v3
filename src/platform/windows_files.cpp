module;
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <filesystem>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

module phoenix.files;
import phoenix.content;

namespace phoenix
{
namespace
{
std::optional<std::string> copied;
std::string utf8(const std::wstring &value)
{
	auto length =
	    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	std::string result(length, '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr,
	                    nullptr);
	return result;
}
} // namespace
void open_file(ImportId id, FilePurpose purpose)
{
	wchar_t path[32768]{};
	OPENFILENAMEW dialog{sizeof(dialog)};
	dialog.hwndOwner = GetActiveWindow();
	dialog.lpstrFile = path;
	dialog.nMaxFile = 32768;
	dialog.lpstrFilter = purpose == FilePurpose::PlayerAvatar
	                         ? L"Avatar image\0*.png;*.jpg;*.jpeg\0"
	                         : L"Phoenix content\0*.zip;*.json;*.png\0All files\0*.*\0";
	dialog.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&dialog))
	{
		const auto error = CommDlgExtendedError();
		complete_import(id, {}, {}, error ? "File picker failed: " + std::to_string(error) : "", !error);
		return;
	}
	try
	{
		complete_import(id, utf8(std::filesystem::path(path).filename().wstring()),
		                read_file(utf8(path), 32 * 1024 * 1024), {}, false, utf8(path));
	}
	catch (const std::exception &error)
	{
		complete_import(id, {}, {}, error.what());
	}
}
void copy_text(const std::string &text)
{
	copied = "Could not copy the invitation link";
	if (text.find('\0') != std::string::npos || text.size() > 8192 || !OpenClipboard(GetActiveWindow()))
		return;
	const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, nullptr, 0);
	HGLOBAL memory = length ? GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(length) * sizeof(wchar_t)) : nullptr;
	if (memory)
	{
		auto *buffer = static_cast<wchar_t *>(GlobalLock(memory));
		if (buffer)
		{
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, buffer, length);
			GlobalUnlock(memory);
			if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory))
			{
				memory = nullptr;
				copied = "";
			}
		}
		if (memory)
			GlobalFree(memory);
	}
	CloseClipboard();
}
std::optional<std::string> take_copy_result()
{
	auto result = std::move(copied);
	copied.reset();
	return result;
}
void save_file(ImportId id, const std::string &filename, const Bytes &bytes, const std::string &destination,
               bool save_as)
{
	wchar_t path[32768]{};
	const auto suggested = destination.empty() ? filename : destination;
	auto name = std::filesystem::u8path(suggested).wstring();
	wcsncpy_s(path, name.c_str(), _TRUNCATE);
	OPENFILENAMEW dialog{sizeof(dialog)};
	dialog.hwndOwner = GetActiveWindow();
	dialog.lpstrFile = path;
	dialog.nMaxFile = 32768;
	dialog.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
	dialog.lpstrFilter =
	    filename.ends_with(".zip") ? L"Phoenix pack\0*.zip\0All files\0*.*\0" : L"Map JSON\0*.json\0All files\0*.*\0";
	dialog.lpstrDefExt = filename.ends_with(".zip") ? L"zip" : L"json";
	if ((destination.empty() || save_as) && !GetSaveFileNameW(&dialog))
	{
		const auto error = CommDlgExtendedError();
		if (id)
			complete_import(id, {}, {}, error ? "Save picker failed: " + std::to_string(error) : "", !error);
		return;
	}
	const auto target = std::filesystem::path(path);
	StagedFileReplacement(utf8(target.wstring()), bytes, FileWriteMode::Buffered, "Saved file").commit();
	if (id)
		complete_import(id, utf8(target.filename().wstring()), {}, {}, false, utf8(target.wstring()));
}
void export_file(const std::string &filename, const Bytes &bytes)
{
	save_file(0, filename, bytes, {}, true);
}
void persist_library(ImportId id, const std::string &filename)
{
	complete_import(id, filename, {}, {}, false, "installed-pack");
}
void enable_workshop_file_shortcuts()
{
}
} // namespace phoenix
