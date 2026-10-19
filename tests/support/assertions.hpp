#pragma once
#include <stdexcept>
#include <string>
namespace phoenix::test
{
inline void require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}
template <class Function> void rejects(Function function, const char *contract)
{
	try
	{
		function();
	}
	catch (const std::exception &)
	{
		return;
	}
	throw std::runtime_error(contract);
}
template <class Function> void rejected(Function function, const std::string &diagnostic)
{
	try
	{
		function();
	}
	catch (const std::exception &error)
	{
		if (std::string(error.what()).find(diagnostic) != std::string::npos)
			return;
		throw;
	}
	throw std::runtime_error("Expected refusal: " + diagnostic);
}
} // namespace phoenix::test
