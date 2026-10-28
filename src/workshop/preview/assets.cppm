module;
#include <raylib.h>
#include <memory>
#include <string>

export module phoenix.workshop.asset_preview;
export import phoenix.content.documents;

export namespace phoenix
{
class WorkshopAssetPreview
{
public:
	WorkshopAssetPreview();
	~WorkshopAssetPreview();
	WorkshopAssetPreview(const WorkshopAssetPreview &) = delete;
	WorkshopAssetPreview &operator=(const WorkshopAssetPreview &) = delete;
	Texture2D texture(const WorkshopDocument &, const std::string &reference);
	void pick(const WorkshopDocument &, const char *label, Json &, bool skybox = false);
	void clear();

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
