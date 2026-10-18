#pragma once
namespace phoenix
{
class NativeMenuScreen
{
public:
	NativeMenuScreen();
	~NativeMenuScreen();
	void poll(Application &app);
	bool prepare(Application &app, Rml::ElementDocument &document);
	void draw_backdrop(const Application &app, Rml::ElementDocument &document);
	void draw(const Application &app, Rml::ElementDocument &document);
	std::string style_sheet(const std::string &asset_directory);
	void position_labels(Rml::ElementDocument &document);

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
