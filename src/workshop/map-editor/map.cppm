module;
#include <cstdint>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <set>

export module phoenix.workshop.map;
export import phoenix.content.documents;
export import phoenix.workshop.shell;

export namespace phoenix
{
struct MapEditorSelection
{
	std::string kind = "Entity";
	std::uint64_t instance = 0;
	std::set<std::uint64_t> instances;
	void select(std::uint64_t id, bool toggle = false)
	{
		if (!toggle)
			instances.clear();
		if (id && toggle && instances.contains(id))
			instances.erase(id);
		else if (id)
			instances.insert(id);
		instance = instances.contains(id) ? id : instances.empty() ? 0 : *instances.begin();
		kind = "Entity";
	}
	void clear_selection()
	{
		instance = 0;
		instances.clear();
	}
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
	bool editing() const;
	void touch_editing(bool);
	std::array<float, 4> canvas_bounds() const;
	void draw(WorkshopDocument &, MapEditorSelection &, ToolMessage &, bool saving, const ToolFrame &,
	          const std::function<void()> &save_pack);

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
