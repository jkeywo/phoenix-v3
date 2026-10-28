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
	void controller_palette(const Json &, WorkshopDocument &);
	void binding_map(Json &, const char *key, const Json &definition, const std::vector<std::string> &suggested = {},
	                 const std::vector<std::string> &allowed = {"integer", "boolean", "text", "reference", "parameter",
	                                                            "observation", "intent", "memory", "elapsed"});

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
