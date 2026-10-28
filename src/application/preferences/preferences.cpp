module;
#include <cstdint>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

module phoenix.application.preferences;

namespace phoenix
{
namespace
{
Json translations = Json::object();
std::string language = "en";
bool pseudo_language = false;
std::uint64_t translation_revision = 0;
} // namespace
void configure_translations(const Json &catalog)
{
	if (catalog.at("schema") != 1 || !catalog.at("languages").is_object() || catalog.at("languages").size() > 64 ||
	    !catalog.at("languages").contains("en"))
		throw std::runtime_error("Invalid translation catalog");
	for (const auto &[locale, labels] : catalog.at("languages").items())
	{
		if (locale.empty() || locale.size() > 32 || !labels.is_object() || labels.size() > 4096)
			throw std::runtime_error("Invalid language resource");
		for (const auto &[key, text] : labels.items())
			if (key.empty() || key.size() > 256 || !text.is_string() ||
			    text.get_ref<const std::string &>().size() > 4096)
				throw std::runtime_error("Invalid translated label");
	}
	translations = catalog.at("languages");
	++translation_revision;
}
void select_language(const std::string &locale, bool pseudo)
{
	if (locale != "en" && !translations.contains(locale))
		throw std::runtime_error("Language is not installed: " + locale);
	language = locale;
	pseudo_language = pseudo;
}
std::string translate(const std::string &key, bool pseudo)
{
	if (key.empty())
		return {};
	struct Cache
	{
		std::string locale;
		bool pseudo = false;
		std::uint64_t revision = 0;
		std::unordered_map<std::string, std::string> labels;
	};
	thread_local Cache cache;
	if (cache.locale != language || cache.pseudo != pseudo || cache.revision != translation_revision)
	{
		cache.labels.clear();
		cache.locale = language;
		cache.pseudo = pseudo;
		cache.revision = translation_revision;
	}
	const bool cacheable = key.size() <= 1024;
	if (cacheable)
		if (const auto found = cache.labels.find(key); found != cache.labels.end())
			return found->second;

	static constexpr std::pair<const char *, const char *> labels[] = {
	    {"home", "Home"},         {"host", "New game"}, {"join", "Join console"},
	    {"view", "Viewscreen"},   {"edit", "Workshop"}, {"locale", "Toggle pseudo-localisation"},
	    {"settings", "Settings"}, {"lobby", "Lobby"}};
	std::string result = key;
	for (const auto &[id, text] : labels)
		if (key == id)
			result = text;
	if (translations.contains(language))
	{
		const auto &labels = translations.at(language);
		if (labels.contains(key))
			result = labels.at(key);
		else if (labels.contains(result))
			result = labels.at(result);
	}
	if (pseudo)
		result = "[ " + result + " ~~~ ]";
	if (cacheable)
	{
		if (cache.labels.size() >= 4096)
			cache.labels.clear();
		cache.labels.emplace(key, result);
	}
	return result;
}
std::string localise(const std::string &key)
{
	return translate(key, pseudo_language);
}
std::vector<std::string> installed_languages()
{
	std::vector<std::string> result;
	for (const auto &[id, labels] : translations.items())
		result.push_back(id);
	if (result.empty())
		result.push_back("en");
	return result;
}
} // namespace phoenix
