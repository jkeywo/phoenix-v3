module;
#include <emscripten.h>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

module phoenix.files;

namespace
{
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
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_import_file(unsigned id, const char* name, const unsigned char* bytes, unsigned size) {
    if (size > 32*1024*1024) { phoenix::complete_import(id,name,{},"File exceeds 32 MiB"); return; }
    phoenix::complete_import(id,name,phoenix::Bytes(bytes,bytes+size));
}
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_import_error(unsigned id, const char* message, int cancelled) { phoenix::complete_import(id,{},{},message,cancelled != 0); }
EM_JS(void, choose_file, (unsigned id, int purpose), {
    const input = document.createElement('input'); input.type = 'file';
    input.accept = purpose === 5 ? '.png' : purpose === 4 ? '.json' : '.zip';
    let finished = false;
    const failure = (error, cancelled = false) => {
        if (finished) return;
        finished = true;
        Module.ccall('phoenix_import_error',null,['number','string','number'],[id,String(error),cancelled ? 1 : 0]);
    };
    input.addEventListener('cancel', () => failure('', true));
    input.addEventListener('change', async () => {
        const file = input.files[0]; if (!file) { failure('',true); return; }
        try {
            if (file.size > 32*1024*1024) throw new Error('File exceeds 32 MiB');
            const bytes = new Uint8Array(await file.arrayBuffer());
            const pointer = Module._malloc(bytes.length || 1);
            if (!pointer) throw new Error('Unable to allocate file buffer');
            try { HEAPU8.set(bytes,pointer); if (!finished) { Module.ccall('phoenix_import_file',null,['number','string','number','number'],[id,file.name,pointer,bytes.length]); finished = true; } }
            finally { Module._free(pointer); }
        } catch (error) { failure(error); }
    });
    try { input.click(); } catch (error) { failure(error); }
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
void open_file(ImportId id, FilePurpose purpose)
{
	choose_file(id, static_cast<int>(purpose));
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
