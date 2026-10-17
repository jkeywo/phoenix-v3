#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <phoenix/files.hpp>
#include <filesystem>
namespace phoenix {
namespace {
std::optional<ImportedFile> imported;
std::string utf8(const std::wstring& value) {
    auto length = WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string result(length,'\0'); WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),length,nullptr,nullptr); return result;
}
}
void request_file(const std::string& purpose) {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = GetActiveWindow(); dialog.lpstrFile = path; dialog.nMaxFile = 32768;
    dialog.lpstrFilter = L"Phoenix content\0*.zip;*.json;*.png\0All files\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return;
    try { imported = ImportedFile{purpose,utf8(std::filesystem::path(path).filename().wstring()),read_file(utf8(path)),{}}; }
    catch (const std::exception& error) { imported = ImportedFile{purpose,{},{},error.what()}; }
}
std::optional<ImportedFile> take_imported_file() { auto result = std::move(imported); imported.reset(); return result; }
void export_file(const std::string& filename, const Bytes& bytes) {
    wchar_t path[32768]{};
    auto name = std::filesystem::path(std::u8string(filename.begin(),filename.end())).wstring();
    wcsncpy_s(path,name.c_str(),_TRUNCATE);
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = GetActiveWindow(); dialog.lpstrFile = path; dialog.nMaxFile = 32768;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&dialog)) write_file(utf8(path),bytes);
}
}
