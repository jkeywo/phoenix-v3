module;
#include <RmlUi/Core.h>
#include <raylib.h>
#include <memory>
#include <set>
#include <string>
export module phoenix.ui.rml;

export import phoenix.data;
export namespace phoenix
{
std::string register_menu_image(const Bytes &);
const Bytes *menu_image_data(const std::string &);
void register_theme_font(const Bytes &, const std::string &family, bool fallback = false);
void retain_menu_images(const std::set<std::string> &, const void *owner = nullptr);
class UiRenderer;
void initialise_rml_runtime(UiRenderer &);
void shutdown_rml_runtime();
} // namespace phoenix
export namespace phoenix
{
class UiSystem final : public Rml::SystemInterface
{
public:
	double GetElapsedTime() override;

	void JoinPath(Rml::String &translated, const Rml::String &document, const Rml::String &path) override;

	bool LogMessage(Rml::Log::Type type, const Rml::String &message) override;
};
// Basic flat UI backend. CSS filters, masks and transformed clipping are not
// supported yet; keep authorable widgets within this renderer's vocabulary.
class UiRenderer final : public Rml::RenderInterface
{
	struct State;
	std::unique_ptr<State> state_;
	void clip();

public:
	UiRenderer();
	~UiRenderer() override;
	UiRenderer(const UiRenderer &) = delete;
	UiRenderer &operator=(const UiRenderer &) = delete;
	void viewport(Rectangle area);

	void finish();

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v, Rml::Span<const int> i) override;

	void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override;

	void SetTransform(const Rml::Matrix4f *transform) override;

	void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f offset, Rml::TextureHandle texture) override;

	Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &path) override;

	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> pixels, Rml::Vector2i size) override;

	void ReleaseTexture(Rml::TextureHandle handle) override;

	void EnableScissorRegion(bool enabled) override;

	void SetScissorRegion(Rml::Rectanglei region) override;
};
} // namespace phoenix
