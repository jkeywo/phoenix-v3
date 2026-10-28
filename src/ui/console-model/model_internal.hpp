#pragma once
namespace phoenix::console_detail
{
const Json &borrowed_member(const Json &, const std::string &, bool array = false);
const Json &controller_rows(const Json &);
std::string bearing_readout(std::int64_t);
std::optional<std::int64_t> console_integer(const Json &);
std::optional<std::uint64_t> console_unsigned(const Json &);
std::string console_string(const Json &, const char *);
std::string console_scalar(const Json &);
std::string console_label(const std::string &);
std::string console_execution_prefix(const Json &);
std::string console_encoded(const std::string &);
std::optional<std::string> console_decoded(const std::string &);
std::string console_message(const Json &);
bool console_descriptor(const Json &);
std::string console_evidence(const Json &, std::uint64_t now = 0);
std::string console_property_readout(const Json &);
std::string console_objective_status(const Json &);
std::vector<std::pair<std::string, std::string>> board_readouts(const Json &, std::uint64_t);
} // namespace phoenix::console_detail
