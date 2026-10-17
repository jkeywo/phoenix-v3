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
void open_file(ImportId id, FilePurpose)
{
	wchar_t path[32768]{};
	OPENFILENAMEW dialog{sizeof(dialog)};
	dialog.hwndOwner = GetActiveWindow();
	dialog.lpstrFile = path;
	dialog.nMaxFile = 32768;
	dialog.lpstrFilter = L"Phoenix content\0*.zip;*.json;*.png\0All files\0*.*\0";
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
		                read_file(utf8(path), 32 * 1024 * 1024));
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
void export_file(const std::string &filename, const Bytes &bytes)
{
	wchar_t path[32768]{};
	auto name = std::filesystem::path(std::u8string(filename.begin(), filename.end())).wstring();
	wcsncpy_s(path, name.c_str(), _TRUNCATE);
	OPENFILENAMEW dialog{sizeof(dialog)};
	dialog.hwndOwner = GetActiveWindow();
	dialog.lpstrFile = path;
	dialog.nMaxFile = 32768;
	dialog.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
	if (GetSaveFileNameW(&dialog))
		write_file(utf8(path), bytes);
}
} // namespace phoenix
