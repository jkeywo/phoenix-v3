module;
#include <memory>

export module phoenix.workshop.material_preview;
export import phoenix.content.documents;
export import phoenix.workshop.shell;

export namespace phoenix
{
void draw_reference_grid_properties(Json &);
class MaterialPreview
{
public:
	MaterialPreview();
	~MaterialPreview();
	MaterialPreview(const MaterialPreview &) = delete;
	MaterialPreview &operator=(const MaterialPreview &) = delete;
	void clear();
	void draw(WorkshopDocument &, int definition_index, ToolMessage &);

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
