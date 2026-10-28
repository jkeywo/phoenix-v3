module;
#include <gtk/gtk.h>
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

namespace phoenix
{
namespace
{
std::optional<std::string> copied;
void desktop()
{
	if (!gtk_init_check(nullptr, nullptr))
		throw std::runtime_error("Cannot connect to the Linux desktop for file selection or clipboard");
}
std::optional<std::string> pick(bool save, const std::string &suggested = {})
{
	desktop();
	auto *dialog =
	    gtk_file_chooser_dialog_new(save ? "Save Phoenix file" : "Open Phoenix file", nullptr,
	                                save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel",
	                                GTK_RESPONSE_CANCEL, save ? "_Save" : "_Open", GTK_RESPONSE_ACCEPT, nullptr);
	if (save)
	{
		gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), true);
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),
		                                  std::filesystem::path(suggested).filename().c_str());
	}
	std::optional<std::string> path;
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		auto *name = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		if (name)
		{
			path = name;
			g_free(name);
		}
	}
	gtk_widget_destroy(dialog);
	while (gtk_events_pending())
		gtk_main_iteration_do(false);
	return path;
}
} // namespace
void open_file(ImportId id, FilePurpose purpose)
{
	try
	{
		auto path = pick(false);
		if (!path)
			complete_import(id, {}, {}, {}, true);
		else
			complete_import(id, std::filesystem::path(*path).filename().string(),
			                read_file(*path, file_import_limit(purpose)), {}, false, *path);
	}
	catch (const std::exception &error)
	{
		complete_import(id, {}, {}, error.what());
	}
}
void copy_text(const std::string &text)
{
	copied = "Could not copy the invitation link";
	if (text.find('\0') != std::string::npos || text.size() > 8192)
		return;
	try
	{
		desktop();
		auto *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
		gtk_clipboard_set_text(clipboard, text.c_str(), static_cast<int>(text.size()));
		gtk_clipboard_store(clipboard);
		copied = "";
	}
	catch (const std::exception &error)
	{
		copied = error.what();
	}
}
std::optional<std::string> take_copy_result()
{
	while (gtk_events_pending())
		gtk_main_iteration_do(false);
	auto result = std::move(copied);
	copied.reset();
	return result;
}
void save_file(ImportId id, const std::string &filename, const Bytes &bytes, const std::string &destination,
               bool save_as)
{
	const auto path = destination.empty() || save_as ? pick(true, filename) : std::optional<std::string>(destination);
	if (!path)
	{
		if (id)
			complete_import(id, {}, {}, {}, true);
		return;
	}
	StagedFileReplacement(*path, bytes, FileWriteMode::Buffered, "Saved file").commit();
	if (id)
		complete_import(id, std::filesystem::path(*path).filename().string(), {}, {}, false, *path);
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
