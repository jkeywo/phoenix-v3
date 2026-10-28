module;
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

export module phoenix.workshop.map;
export import phoenix.content.documents;
export import phoenix.workshop.shell;

export namespace phoenix
{
struct MapEditorSelection
{
	std::string kind = "Entity";
	std::uint64_t instance = 0;
	int definition = -1;
};
void add_workshop_map(WorkshopDocument &, MapEditorSelection &, ToolMessage &);
class MapEditor
{
public:
	MapEditor();
	~MapEditor();
	MapEditor(const MapEditor &) = delete;
	MapEditor &operator=(const MapEditor &) = delete;
	void clear();
	void draw(WorkshopDocument &, MapEditorSelection &, ToolMessage &, bool saving, const ToolFrame &,
	          const std::function<void()> &save_pack);

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
