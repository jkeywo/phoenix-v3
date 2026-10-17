#include <phoenix/files.hpp>
#include <emscripten.h>
namespace
{
std::optional<phoenix::ImportedFile> imported;
std::optional<std::string> copied;
} // namespace
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_copy_result(const char *error)
{
	copied = error;
}
// clang-format off: embedded JavaScript must retain its operators.
EM_JS(void, copy_browser_text, (const char* text), {
    const value = UTF8ToString(text);
    const reply = error => Module.ccall('phoenix_copy_result',null,['string'],[error]);
    if (!navigator.clipboard || !window.isSecureContext) { reply('Clipboard access requires HTTPS or localhost. The invitation URL is shown above.'); return; }
    navigator.clipboard.writeText(value).then(()=>reply(""),error=>reply('Could not copy the link: '+String(error)));
});
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_import_file(const char* purpose, const char* name, const unsigned char* bytes, unsigned size) {
    if (size > 32*1024*1024) { imported = phoenix::ImportedFile{purpose,name,{},"File exceeds 32 MiB"}; return; }
    imported = phoenix::ImportedFile{purpose,name,phoenix::Bytes(bytes,bytes+size),{}};
}
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_import_error(const char* message) { imported = phoenix::ImportedFile{{},{},{},message}; }
EM_JS(void, choose_file, (const char* purpose), {
    const reason = UTF8ToString(purpose);
    const input = document.createElement('input'); input.type = 'file';
    input.accept = reason === 'image' ? '.png' : reason === 'map' ? '.json' : '.zip';
    input.addEventListener('change', async () => {
        const file = input.files[0]; if (!file) return;
        try {
            if (file.size > 32*1024*1024) throw new Error('File exceeds 32 MiB');
            const bytes = new Uint8Array(await file.arrayBuffer());
            const pointer = Module._malloc(bytes.length || 1);
            if (!pointer) throw new Error('Unable to allocate file buffer');
            try { HEAPU8.set(bytes,pointer); Module.ccall('phoenix_import_file',null,['string','string','number','number'],[reason,file.name,pointer,bytes.length]); }
            finally { Module._free(pointer); }
        } catch (error) { Module.ccall('phoenix_import_error',null,['string'],[String(error)]); }
    });
    input.click();
});
EM_JS(void, download_file, (const char* filename, const unsigned char* bytes, unsigned size), {
    const blob = new Blob([HEAPU8.slice(bytes,bytes+size)],{type:'application/octet-stream'});
// clang-format on
    const url = URL.createObjectURL(blob); const anchor = document.createElement('a');
    anchor.href = url; anchor.download = UTF8ToString(filename); anchor.click();
    setTimeout(() => URL.revokeObjectURL(url),60000);
});
namespace phoenix
{
void request_file(const std::string &purpose)
{
	choose_file(purpose.c_str());
}
std::optional<ImportedFile> take_imported_file()
{
	auto result = std::move(imported);
	imported.reset();
	return result;
}
void export_file(const std::string &filename, const Bytes &bytes)
{
	download_file(filename.c_str(), bytes.data(), static_cast<unsigned>(bytes.size()));
}
void copy_text(const std::string &text)
{
	copied.reset();
	copy_browser_text(text.c_str());
}
std::optional<std::string> take_copy_result()
{
	auto result = std::move(copied);
	copied.reset();
	return result;
}
} // namespace phoenix
