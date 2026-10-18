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

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
