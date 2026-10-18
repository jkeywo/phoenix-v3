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
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_import_selected(unsigned id, const char* name, const unsigned char* bytes, unsigned size, const char* destination) {
    if (size > 32*1024*1024) { phoenix::complete_import(id,name,{},"File exceeds 32 MiB"); return; }
    phoenix::complete_import(id,name,phoenix::Bytes(bytes,bytes+size),{},false,destination);
}
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_save_result(unsigned id, const char* name, const char* destination, const char* error, int cancelled) {
    phoenix::complete_import(id,name,{},error,cancelled != 0,destination);
}
EM_JS(void, choose_file, (unsigned id, int purpose), {
    if ((purpose === 2 || purpose === 4) && window.showOpenFilePicker && window.isSecureContext) {
        (async () => {
            let pointer = 0;
            try {
                const extension = purpose === 4 ? '.json' : '.zip';
                const [handle] = await window.showOpenFilePicker({multiple:false,types:[{description:'Phoenix content',accept:{'application/octet-stream':[extension]}}]});
                const file = await handle.getFile();
                if (file.size > 32*1024*1024) throw new Error('File exceeds 32 MiB');
                const bytes = new Uint8Array(await file.arrayBuffer());
                pointer = Module._malloc(bytes.length || 1);
                if (!pointer) throw new Error('Unable to allocate file buffer');
                HEAPU8.set(bytes,pointer);
                Module.phoenixFileHandles ??= new Map();
                Module.phoenixFileHandleSequence = (Module.phoenixFileHandleSequence || 0) + 1;
                const destination = 'browser-file/' + Module.phoenixFileHandleSequence;
                Module.phoenixFileHandles.set(destination,handle);
                Module.ccall('phoenix_import_selected',null,['number','string','number','number','string'],[id,file.name,pointer,bytes.length,destination]);
            } catch (error) {
                Module.ccall('phoenix_import_error',null,['number','string','number'],[id,error.name === 'AbortError' ? '' : String(error),error.name === 'AbortError' ? 1 : 0]);
            } finally { if (pointer) Module._free(pointer); }
        })();
        return;
    }
    const input = document.createElement('input'); input.type = 'file';
    input.hidden = true; document.body.appendChild(input);
    input.accept = purpose === 5 ? '.png' : (purpose === 4 || purpose === 6) ? '.json' : '.zip';
    let finished = false;
    const failure = (error, cancelled = false) => {
        if (finished) return;
        finished = true;
        input.remove();
        Module.ccall('phoenix_import_error',null,['number','string','number'],[id,String(error),cancelled ? 1 : 0]);
    };
    input.addEventListener('cancel', () => failure("", true));
    input.addEventListener('change', async () => {
        const file = input.files[0]; if (!file) { failure("",true); return; }
        try {
            if (file.size > 32*1024*1024) throw new Error('File exceeds 32 MiB');
            const bytes = new Uint8Array(await file.arrayBuffer());
            const pointer = Module._malloc(bytes.length || 1);
            if (!pointer) throw new Error('Unable to allocate file buffer');
            try { HEAPU8.set(bytes,pointer); if (!finished) { Module.ccall('phoenix_import_file',null,['number','string','number','number'],[id,file.name,pointer,bytes.length]); finished = true; input.remove(); } }
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
// clang-format off: embedded JavaScript must retain its operators.
EM_JS(void, save_browser_file, (unsigned id, const char* filename, const unsigned char* bytes, unsigned size, const char* destination, int save_as), {
    const suggested = UTF8ToString(filename), previous = UTF8ToString(destination);
    const data = HEAPU8.slice(bytes,bytes+size);
    const reply = (name, location, error = '', cancelled = false) => Module.ccall('phoenix_save_result',null,
        ['number','string','string','string','number'],[id,name,location,error,cancelled ? 1 : 0]);
    (async () => {
        try {
            Module.phoenixFileHandles ??= new Map();
            let location = previous;
            let handle = !save_as && previous ? Module.phoenixFileHandles.get(previous) : null;
            if (!handle && previous && !save_as) throw new Error('The saved file is no longer available. Use Save as.');
            if (!handle && window.showSaveFilePicker && window.isSecureContext) {
                const extension = suggested.endsWith('.zip') ? '.zip' : '.json';
                handle = await window.showSaveFilePicker({suggestedName:suggested,types:[{description:'Phoenix content',accept:{'application/octet-stream':[extension]}}]});
                Module.phoenixFileHandleSequence = (Module.phoenixFileHandleSequence || 0) + 1;
                location = 'browser-file/' + Module.phoenixFileHandleSequence;
            }
            if (handle) {
                if (handle.requestPermission && await handle.requestPermission({mode:'readwrite'}) !== 'granted')
                    throw new Error('Write access was refused. Use Save as to choose a different file.');
                const writer = await handle.createWritable();
                try { await writer.write(data); await writer.close(); }
                catch (error) { try { await writer.abort(); } catch {} throw error; }
                Module.phoenixFileHandles.set(location,handle);
                reply(handle.name,location);
            } else {
                const blob = new Blob([data],{type:'application/octet-stream'});
                const url = URL.createObjectURL(blob), anchor = document.createElement('a');
                anchor.href = url; anchor.download = suggested; anchor.click();
                setTimeout(() => URL.revokeObjectURL(url),60000);
                reply(suggested,'');
            }
        } catch (error) { reply('','',error.name === 'AbortError' ? '' : String(error),error.name === 'AbortError'); }
    })();
});
EM_JS(void, install_workshop_file_shortcuts, (), {
    if (Module.phoenixFileShortcutsInstalled) return;
    Module.phoenixFileShortcutsInstalled = true;
    window.addEventListener('keydown',event => {
        if (event.ctrlKey && event.code === 'KeyS') event.preventDefault();
    });
});
EM_JS(void, persist_browser_library, (unsigned id, const char* filename), {
    const name = UTF8ToString(filename);
    (async () => {
        try {
            if (!Module.phoenixPersistMenu) throw new Error('Pack storage is unavailable. Use Save as to export a file.');
            await Module.phoenixPersistMenu();
            Module.ccall('phoenix_save_result',null,['number','string','string','string','number'],[id,name,'installed-pack','',0]);
        } catch (error) {
            Module.ccall('phoenix_save_result',null,['number','string','string','string','number'],[id,'','',String(error),0]);
        }
    })();
});
// clang-format on
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
void save_file(ImportId id, const std::string &filename, const Bytes &bytes, const std::string &destination,
               bool save_as)
{
	save_browser_file(id, filename.c_str(), bytes.data(), static_cast<unsigned>(bytes.size()), destination.c_str(),
	                  save_as);
}
void enable_workshop_file_shortcuts()
{
	install_workshop_file_shortcuts();
}
void persist_library(ImportId id, const std::string &filename)
{
	persist_browser_library(id, filename.c_str());
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
