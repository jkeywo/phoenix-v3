#pragma once
namespace phoenix
{
class GameScreen
{
public:
	GameScreen();
	~GameScreen();
	void prepare(Application &app, Rml::ElementDocument &document);
	void draw(const Application &app);

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
