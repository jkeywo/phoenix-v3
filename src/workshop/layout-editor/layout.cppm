module;
#include <map>
#include <memory>
#include <string>

export module phoenix.workshop.layout;
export import phoenix.content.documents;
export import phoenix.files;
export import phoenix.workshop.shell;

export namespace phoenix
{
struct LayoutPreviewSettings
{
	int width = 1280, height = 720, text_scale = 100;
	int sample_state = 0, sample_stars = 3;
	bool header = false;
	std::map<std::string, std::string> pages;
};
class LayoutEditor
{
public:
	LayoutEditor();
	~LayoutEditor();
	LayoutEditor(const LayoutEditor &) = delete;
	LayoutEditor &operator=(const LayoutEditor &) = delete;
	void draw(WorkshopDocument &, int console_index, bool pseudo, FileImportInbox &, ToolMessage &);
	void focus();
	LayoutPreviewSettings &preview();
	const std::string &error() const;

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
