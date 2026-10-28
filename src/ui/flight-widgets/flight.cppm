module;
#include <cstdint>
#include <map>
#include <optional>
#include <string>
export module phoenix.ui.flight_widgets;

export import phoenix.console;
export namespace phoenix
{
// Optimistic local drag feedback is reconciled against the permitted model. It
// owns no device capture or authority and receives its clock from the caller.
class ThrottleFeedback
{
public:
	double zero(const ConsoleModel &, const std::string &) const;
	double detent(const ConsoleModel &, const std::string &, double fraction) const;
	double fraction(const ConsoleModel &, const std::string &) const;
	std::int64_t request(const ConsoleModel &, const std::string &) const;
	void reconcile(const ConsoleModel &, const std::map<std::uint64_t, std::string> &contacts, double now);
	void track(const ConsoleModel &, const std::string &, double fraction, double now);
	bool differs(const std::string &, std::int64_t requested) const;
	void sent(const std::string &, std::int64_t requested);
	void settle(const std::string &, double fraction, double now, std::int64_t requested);
	void erase(const std::string &);
	void clear();

private:
	struct Preview
	{
		double fraction = 0, updated_at = 0;
		std::optional<std::int64_t> sent;
	};
	std::map<std::string, Preview> previews_;
};
} // namespace phoenix
