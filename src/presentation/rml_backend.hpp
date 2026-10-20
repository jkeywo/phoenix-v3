#pragma once

namespace phoenix
{
class UiSystem final : public Rml::SystemInterface
{
public:
	double GetElapsedTime() override
	{
		return GetTime();
	}
	void JoinPath(Rml::String &translated, const Rml::String &document, const Rml::String &path) override
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
	bool LogMessage(Rml::Log::Type type, const Rml::String &message) override
	{
		auto &stream = type <= Rml::Log::LT_WARNING ? std::cerr : std::cout;
		stream << "RmlUi: " << message << '\n';
		return true;
	}
};
// Basic flat UI backend. CSS filters, masks and transformed clipping are not
// supported yet; keep authorable widgets within this renderer's vocabulary.
class UiRenderer final : public Rml::RenderInterface
{
	struct Geometry
	{
		Rml::Span<const Rml::Vertex> vertices;
		Rml::Span<const int> indices;
	};
	bool scissor_ = false;
	std::optional<Rml::Matrix4f> transform_;
	Rml::Rectanglei region_{};
	Vector2 origin_{};
	Rectangle viewport_{0, 0, 100000, 100000};
	void clip()
	{
		auto x = std::max(viewport_.x, origin_.x + region_.Left());
		auto y = std::max(viewport_.y, origin_.y + region_.Top());
		auto right = std::min(viewport_.x + viewport_.width, origin_.x + region_.Right());
		auto bottom = std::min(viewport_.y + viewport_.height, origin_.y + region_.Bottom());
		BeginScissorMode(static_cast<int>(x), static_cast<int>(y), static_cast<int>(std::max(0.f, right - x)),
		                 static_cast<int>(std::max(0.f, bottom - y)));
	}

public:
	void viewport(Rectangle area)
	{
		origin_ = {area.x, area.y};
		transform_.reset();
		viewport_ = area;
		scissor_ = false;
		BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
		BeginScissorMode(static_cast<int>(area.x), static_cast<int>(area.y), static_cast<int>(area.width),
		                 static_cast<int>(area.height));
	}
	void finish()
	{
		EndScissorMode();
		EndBlendMode();
		scissor_ = false;
	}
	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v, Rml::Span<const int> i) override
	{
		return reinterpret_cast<Rml::CompiledGeometryHandle>(new Geometry{v, i});
	}
	void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override
	{
		delete reinterpret_cast<Geometry *>(handle);
	}
	void SetTransform(const Rml::Matrix4f *transform) override
	{
		if (transform)
			transform_ = *transform;
		else
			transform_.reset();
	}
	void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f offset, Rml::TextureHandle texture) override
	{
		const auto &geometry = *reinterpret_cast<Geometry *>(handle);
		rlSetTexture(texture ? reinterpret_cast<Texture2D *>(texture)->id : rlGetTextureIdDefault());
		rlBegin(RL_TRIANGLES);
		for (int index : geometry.indices)
		{
			const auto &v = geometry.vertices[index];
			rlColor4ub(v.colour.red, v.colour.green, v.colour.blue, v.colour.alpha);
			rlTexCoord2f(v.tex_coord.x, v.tex_coord.y);
			auto point = Rml::Vector4f(v.position.x + offset.x, v.position.y + offset.y, 0, 1);
			if (transform_)
				point = *transform_ * point;
			const float w = point.w != 0 ? point.w : 1.f;
			rlVertex2f(point.x / w + origin_.x, point.y / w + origin_.y);
		}
		rlEnd();
		rlSetTexture(0);
	}
	Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &path) override
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
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> pixels, Rml::Vector2i size) override
	{
		Image image{const_cast<Rml::byte *>(pixels.data()), size.x, size.y, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
		return reinterpret_cast<Rml::TextureHandle>(new Texture2D(LoadTextureFromImage(image)));
	}
	void ReleaseTexture(Rml::TextureHandle handle) override
	{
		auto *texture = reinterpret_cast<Texture2D *>(handle);
		UnloadTexture(*texture);
		delete texture;
	}
	void EnableScissorRegion(bool enabled) override
	{
		if (scissor_)
			EndScissorMode();
		scissor_ = enabled;
		if (enabled)
			clip();
		else
			BeginScissorMode(static_cast<int>(viewport_.x), static_cast<int>(viewport_.y),
			                 static_cast<int>(viewport_.width), static_cast<int>(viewport_.height));
	}
	void SetScissorRegion(Rml::Rectanglei region) override
	{
		region_ = region;
		if (scissor_)
		{
			EndScissorMode();
			clip();
		}
	}
};
} // namespace phoenix
