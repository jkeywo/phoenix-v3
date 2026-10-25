#pragma once

namespace phoenix
{
// Keep the pinned HarfBuzz renderer and cache only its pure width query.
// RmlUi can measure the same runs repeatedly while laying out sibling panels.
class CachedFontMeasurements final : public FontEngineInterfaceHarfBuzz
{
	using Key = std::tuple<Rml::FontFaceHandle, Rml::String, Rml::String, Rml::Style::Direction,
	                       Rml::Style::FontKerning, float, Rml::Character>;
	std::list<Key> recent_;
	std::map<Key, std::pair<int, std::list<Key>::iterator>> widths_;
	std::uint64_t verified_hits_ = 0;
	void clear_measurements()
	{
		widths_.clear();
		recent_.clear();
	}

public:
	int GetStringWidth(Rml::FontFaceHandle face, Rml::StringView text, const Rml::TextShapingContext &context,
	                   Rml::Character prior) override
	{
		if (text.empty() || text.size() > 4096)
			return FontEngineInterfaceHarfBuzz::GetStringWidth(face, text, context, prior);
		Key key{face,
		        Rml::String(text.begin(), text.end()),
		        context.language,
		        context.text_direction,
		        context.font_kerning,
		        context.letter_spacing,
		        prior};
		if (auto found = widths_.find(key); found != widths_.end())
		{
			// Explicit graphical smoke mode compares real layout queries against
			// HarfBuzz; it is disabled for performance acceptance captures.
			static const bool verify = std::getenv("PHOENIX_VERIFY_FONT_WIDTHS") != nullptr;
			if (verify)
			{
				if (found->second.first != FontEngineInterfaceHarfBuzz::GetStringWidth(face, text, context, prior))
					throw std::runtime_error("Cached font measurement differs from HarfBuzz");
				if (++verified_hits_ == 100)
					std::cout << "PHOENIX_FONT_PROFILE {\"verified_hits\":100}" << std::endl;
			}
			recent_.splice(recent_.end(), recent_, found->second.second);
			return found->second.first;
		}
		const auto width = FontEngineInterfaceHarfBuzz::GetStringWidth(face, text, context, prior);
		if (widths_.size() >= 4096)
		{
			widths_.erase(recent_.front());
			recent_.pop_front();
		}
		recent_.push_back(key);
		widths_.emplace(std::move(key), std::pair{width, std::prev(recent_.end())});
		return width;
	}
	bool LoadFontFace(const Rml::String &file, int index, bool fallback, Rml::Style::FontWeight weight) override
	{
		clear_measurements();
		return FontEngineInterfaceHarfBuzz::LoadFontFace(file, index, fallback, weight);
	}
	bool LoadFontFace(Rml::Span<const Rml::byte> data, int index, const Rml::String &family,
	                  Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallback) override
	{
		clear_measurements();
		return FontEngineInterfaceHarfBuzz::LoadFontFace(data, index, family, style, weight, fallback);
	}
	void RegisterLanguage(const Rml::String &language, const Rml::String &script, TextFlowDirection direction)
	{
		clear_measurements();
		FontEngineInterfaceHarfBuzz::RegisterLanguage(language, script, direction);
	}
	void ReleaseFontResources() override
	{
		clear_measurements();
		FontEngineInterfaceHarfBuzz::ReleaseFontResources();
	}
	void Shutdown() override
	{
		clear_measurements();
		FontEngineInterfaceHarfBuzz::Shutdown();
	}
};
} // namespace phoenix
