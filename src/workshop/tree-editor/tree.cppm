module;
#include <memory>
#include <string>
#include <vector>

export module phoenix.workshop.tree;
export import phoenix.content.documents;

export namespace phoenix
{
class TreeEditor
{
public:
	TreeEditor();
	~TreeEditor();
	TreeEditor(const TreeEditor &) = delete;
	TreeEditor &operator=(const TreeEditor &) = delete;
	void presentation(bool pseudo, bool probe = false);
	void set_vocabulary(const Json &);
	bool has_vocabulary() const;
	void runtime_feedback(const Json &, const std::string &controller);
	void draw(WorkshopDocument &, bool inspector, std::string &selection);
	void database(WorkshopDocument &, std::string &selection);
	bool tree_binding(WorkshopDocument &, Json &owner, const char *key, const char *context, bool optional = true);
	void controller_palette(const Json &, WorkshopDocument &);
	void binding_map(Json &, const char *key, const Json &definition, const std::vector<std::string> &suggested = {},
	                 const std::vector<std::string> &allowed = {});

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
