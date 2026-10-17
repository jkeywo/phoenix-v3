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
	Rml::Rectanglei region_{};

public:
	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v, Rml::Span<const int> i) override
	{
		return reinterpret_cast<Rml::CompiledGeometryHandle>(new Geometry{v, i});
	}
	void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override
	{
		delete reinterpret_cast<Geometry *>(handle);
	}
	void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f offset, Rml::TextureHandle texture) override
	{
		const auto &geometry = *reinterpret_cast<Geometry *>(handle);
		rlDrawRenderBatchActive();
		BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
		rlSetTexture(texture ? reinterpret_cast<Texture2D *>(texture)->id : rlGetTextureIdDefault());
		rlBegin(RL_TRIANGLES);
		for (int index : geometry.indices)
		{
			const auto &v = geometry.vertices[index];
			rlColor4ub(v.colour.red, v.colour.green, v.colour.blue, v.colour.alpha);
			rlTexCoord2f(v.tex_coord.x, v.tex_coord.y);
			rlVertex2f(v.position.x + offset.x, v.position.y + offset.y);
		}
		rlEnd();
		rlSetTexture(0);
		EndBlendMode();
	}
	Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &path) override
	{
		Image image = LoadImage(path.c_str());
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
			BeginScissorMode(region_.Left(), region_.Top(), region_.Width(), region_.Height());
	}
	void SetScissorRegion(Rml::Rectanglei region) override
	{
		region_ = region;
		if (scissor_)
		{
			EndScissorMode();
			BeginScissorMode(region.Left(), region.Top(), region.Width(), region.Height());
		}
	}
};
} // namespace phoenix
