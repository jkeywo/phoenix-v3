module;
#include <RmlUi/Core.h>
#include <raylib.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <RmlUi/Core/StreamMemory.h>
#include <RmlUi/Core/StyleSheetContainer.h>
#include <FontEngineInterfaceHarfBuzz.h>
#include <list>
#include <map>
#include <tuple>
#include <iterator>
#include <cstdlib>
#include <stdexcept>
module phoenix.ui.rml;

namespace phoenix
{
struct UiRenderer::State
{
	struct Geometry
	{
		Rml::Span<const Rml::Vertex> vertices;
		Rml::Span<const int> indices;
		float left = 0, top = 0, right = 0, bottom = 0;
	};
	bool scissor_ = false;
	std::optional<Rml::Matrix4f> transform_;
	Rml::Rectanglei region_{};
	Vector2 origin_{};
	Rectangle viewport_{0, 0, 100000, 100000};
};
UiRenderer::UiRenderer() : state_(std::make_unique<State>())
{
}
UiRenderer::~UiRenderer() = default;

double UiSystem::GetElapsedTime()
{
	return GetTime();
}
void UiSystem::JoinPath(Rml::String &translated, const Rml::String &document, const Rml::String &path)
{
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
	// RmlUi's default web-style resolver strips the root slash. Native
	// Linux resources use absolute filesystem paths and must retain it.
	if (path.starts_with('/'))
	{
		translated = path;
		return;
	}
#endif
	Rml::SystemInterface::JoinPath(translated, document, path);
}
bool UiSystem::LogMessage(Rml::Log::Type type, const Rml::String &message)
{
	auto &stream = type <= Rml::Log::LT_WARNING ? std::cerr : std::cout;
	stream << "RmlUi: " << message << '\n';
	return true;
}
void UiRenderer::clip()
{
	auto x = std::max(state_->viewport_.x, state_->origin_.x + state_->region_.Left());
	auto y = std::max(state_->viewport_.y, state_->origin_.y + state_->region_.Top());
	auto right = std::min(state_->viewport_.x + state_->viewport_.width, state_->origin_.x + state_->region_.Right());
	auto bottom =
	    std::min(state_->viewport_.y + state_->viewport_.height, state_->origin_.y + state_->region_.Bottom());
	BeginScissorMode(static_cast<int>(x), static_cast<int>(y), static_cast<int>(std::max(0.f, right - x)),
	                 static_cast<int>(std::max(0.f, bottom - y)));
}
void UiRenderer::viewport(Rectangle area)
{
	state_->origin_ = {area.x, area.y};
	state_->transform_.reset();
	state_->viewport_ = area;
	state_->scissor_ = false;
	BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
	BeginScissorMode(static_cast<int>(area.x), static_cast<int>(area.y), static_cast<int>(area.width),
	                 static_cast<int>(area.height));
}
void UiRenderer::finish()
{
	EndScissorMode();
	EndBlendMode();
	state_->scissor_ = false;
}
Rml::CompiledGeometryHandle UiRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> v, Rml::Span<const int> i)
{
	auto geometry = std::make_unique<State::Geometry>(State::Geometry{v, i});
	if (!v.empty())
	{
		geometry->left = geometry->right = v[0].position.x;
		geometry->top = geometry->bottom = v[0].position.y;
		for (const auto &vertex : v)
		{
			geometry->left = std::min(geometry->left, vertex.position.x);
			geometry->right = std::max(geometry->right, vertex.position.x);
			geometry->top = std::min(geometry->top, vertex.position.y);
			geometry->bottom = std::max(geometry->bottom, vertex.position.y);
		}
	}
	return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
}
void UiRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle handle)
{
	delete reinterpret_cast<State::Geometry *>(handle);
}
void UiRenderer::SetTransform(const Rml::Matrix4f *transform)
{
	if (transform)
		state_->transform_ = *transform;
	else
		state_->transform_.reset();
}
void UiRenderer::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f offset, Rml::TextureHandle texture)
{
	const auto &geometry = *reinterpret_cast<State::Geometry *>(handle);
	// Avoid expanding vertices that the GPU scissor would discard. Perspective
	// crossings retain the existing path; positive-w rectangles have bounded projections.
	const float x = state_->scissor_ ? std::max(state_->viewport_.x, state_->origin_.x + state_->region_.Left())
	                                 : state_->viewport_.x;
	const float y = state_->scissor_ ? std::max(state_->viewport_.y, state_->origin_.y + state_->region_.Top())
	                                 : state_->viewport_.y;
	const float right = state_->scissor_ ? std::min(state_->viewport_.x + state_->viewport_.width,
	                                                state_->origin_.x + state_->region_.Right())
	                                     : state_->viewport_.x + state_->viewport_.width;
	const float bottom = state_->scissor_ ? std::min(state_->viewport_.y + state_->viewport_.height,
	                                                 state_->origin_.y + state_->region_.Bottom())
	                                      : state_->viewport_.y + state_->viewport_.height;
	const int width = static_cast<int>(std::max(0.f, right - x)), height = static_cast<int>(std::max(0.f, bottom - y));
	if (geometry.indices.empty() || !width || !height)
		return;
	float left_bound = geometry.left + offset.x, right_bound = geometry.right + offset.x;
	float top_bound = geometry.top + offset.y, bottom_bound = geometry.bottom + offset.y;
	bool bounded = true;
	if (state_->transform_)
	{
		const std::array corners{
		    Rml::Vector4f(left_bound, top_bound, 0, 1), Rml::Vector4f(right_bound, top_bound, 0, 1),
		    Rml::Vector4f(left_bound, bottom_bound, 0, 1), Rml::Vector4f(right_bound, bottom_bound, 0, 1)};
		bool first = true;
		for (auto corner : corners)
		{
			corner = *state_->transform_ * corner;
			if (!(corner.w > 0) || !std::isfinite(corner.w) || !std::isfinite(corner.x) || !std::isfinite(corner.y))
			{
				bounded = false;
				break;
			}
			const float px = corner.x / corner.w, py = corner.y / corner.w;
			if (!std::isfinite(px) || !std::isfinite(py))
			{
				bounded = false;
				break;
			}
			if (first)
			{
				left_bound = right_bound = px;
				top_bound = bottom_bound = py;
				first = false;
			}
			else
			{
				left_bound = std::min(left_bound, px);
				right_bound = std::max(right_bound, px);
				top_bound = std::min(top_bound, py);
				bottom_bound = std::max(bottom_bound, py);
			}
		}
	}
	if (bounded && (right_bound + state_->origin_.x <= static_cast<int>(x) ||
	                left_bound + state_->origin_.x >= static_cast<int>(x) + width ||
	                bottom_bound + state_->origin_.y <= static_cast<int>(y) ||
	                top_bound + state_->origin_.y >= static_cast<int>(y) + height))
		return;
	rlSetTexture(texture ? reinterpret_cast<Texture2D *>(texture)->id : rlGetTextureIdDefault());
	rlBegin(RL_TRIANGLES);
	for (int index : geometry.indices)
	{
		const auto &v = geometry.vertices[index];
		rlColor4ub(v.colour.red, v.colour.green, v.colour.blue, v.colour.alpha);
		rlTexCoord2f(v.tex_coord.x, v.tex_coord.y);
		auto point = Rml::Vector4f(v.position.x + offset.x, v.position.y + offset.y, 0, 1);
		if (state_->transform_)
			point = *state_->transform_ * point;
		const float w = point.w != 0 ? point.w : 1.f;
		rlVertex2f(point.x / w + state_->origin_.x, point.y / w + state_->origin_.y);
	}
	rlEnd();
	rlSetTexture(0);
}
Rml::TextureHandle UiRenderer::LoadTexture(Rml::Vector2i &dimensions, const Rml::String &path)
{
	const auto *memory = menu_image_data(path);
	Image image = memory ? LoadImageFromMemory(".png", memory->data(), static_cast<int>(memory->size()))
	              : path.starts_with("phoenix-menu://image/") ? Image{}
	                                                          : LoadImage(path.c_str());
	if (!image.data)
		return 0;
	ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	ImageAlphaPremultiply(&image);
	auto *texture = new Texture2D(LoadTextureFromImage(image));
	dimensions = {image.width, image.height};
	UnloadImage(image);
	return reinterpret_cast<Rml::TextureHandle>(texture);
}
Rml::TextureHandle UiRenderer::GenerateTexture(Rml::Span<const Rml::byte> pixels, Rml::Vector2i size)
{
	Image image{const_cast<Rml::byte *>(pixels.data()), size.x, size.y, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
	return reinterpret_cast<Rml::TextureHandle>(new Texture2D(LoadTextureFromImage(image)));
}
void UiRenderer::ReleaseTexture(Rml::TextureHandle handle)
{
	auto *texture = reinterpret_cast<Texture2D *>(handle);
	UnloadTexture(*texture);
	delete texture;
}
void UiRenderer::EnableScissorRegion(bool enabled)
{
	if (state_->scissor_)
		EndScissorMode();
	state_->scissor_ = enabled;
	if (enabled)
		clip();
	else
		BeginScissorMode(static_cast<int>(state_->viewport_.x), static_cast<int>(state_->viewport_.y),
		                 static_cast<int>(state_->viewport_.width), static_cast<int>(state_->viewport_.height));
}
void UiRenderer::SetScissorRegion(Rml::Rectanglei region)
{
	state_->region_ = region;
	if (state_->scissor_)
	{
		EndScissorMode();
		clip();
	}
}
} // namespace phoenix
#include "font_measurements.hpp"
namespace phoenix
{
namespace
{
std::map<std::string, Bytes> menu_images, theme_fonts;
std::map<const void *, std::set<std::string>> menu_image_owners;
} // namespace
std::string register_menu_image(const Bytes &png)
{
	const auto source = "phoenix-menu://image/" + sha256(png) + ".png";
	menu_images.try_emplace(source, png);
	return source;
}
void register_theme_font(const Bytes &font, const std::string &family, bool fallback)
{
	const auto key = sha256(font) + family + (fallback ? "fallback" : "primary");
	if (theme_fonts.contains(key))
		return;
	auto &owned = theme_fonts[key] = font;
	if (!Rml::LoadFontFace(Rml::Span<const Rml::byte>(owned.data(), owned.size()), family,
	                       Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto, fallback))
	{
		theme_fonts.erase(key);
		throw std::runtime_error("Unable to load theme font " + family);
	}
}
const Bytes *menu_image_data(const std::string &source)
{
	const auto found = menu_images.find(source);
	return found == menu_images.end() ? nullptr : &found->second;
}
void retain_menu_images(const std::set<std::string> &sources, const void *owner)
{
	if (sources.empty())
		menu_image_owners.erase(owner);
	else
		menu_image_owners[owner] = sources;
	std::set<std::string> retained;
	for (const auto &[identity, owned] : menu_image_owners)
		retained.insert(owned.begin(), owned.end());
	for (auto it = menu_images.begin(); it != menu_images.end();)
		if (!retained.contains(it->first))
		{
			Rml::ReleaseTexture(it->first);
			it = menu_images.erase(it);
		}
		else
			++it;
}
namespace
{
UiSystem runtime_system;
CachedFontMeasurements shaping;
} // namespace
void initialise_rml_runtime(UiRenderer &renderer)
{
	Rml::SetSystemInterface(&runtime_system);
	Rml::SetRenderInterface(&renderer);
	shaping.RegisterLanguage("en", "Latn", TextFlowDirection::LeftToRight);
	shaping.RegisterLanguage("ar", "Arab", TextFlowDirection::RightToLeft);
	shaping.RegisterLanguage("hi", "Deva", TextFlowDirection::LeftToRight);
	Rml::SetFontEngineInterface(&shaping);
	if (!Rml::Initialise())
		throw std::runtime_error("RmlUi initialisation failed");
}
void shutdown_rml_runtime()
{
	Rml::Shutdown();
	theme_fonts.clear();
	menu_images.clear();
	menu_image_owners.clear();
}
} // namespace phoenix
