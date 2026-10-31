#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

import phoenix.workshop.authoring_core;

int workshop_cli(int argc, char **argv)
{
	using namespace phoenix;
	try
	{
		std::string operation = argc > 1 ? argv[1] : "help", input, output, requests;
		if (operation == "--help" || operation == "-h")
			operation = "help";
		Json arguments = Json::object();
		std::vector<std::string> dependencies;
		for (int i = 2; i < argc; ++i)
		{
			const std::string flag = argv[i];
			if (operation == "describe" && i == 2 && !flag.starts_with("-"))
			{
				arguments["operation"] = flag;
				continue;
			}
			if (i + 1 >= argc)
				throw std::runtime_error("Missing value after " + flag);
			const std::string value = argv[++i];
			if (flag == "--input")
				input = value;
			else if (flag == "--output")
				output = value;
			else if (flag == "--requests")
				requests = value;
			else if (flag == "--dependency")
				dependencies.push_back(value);
			else if (flag == "--args")
				arguments = Json::parse(value);
			else if (flag == "--args-file")
			{
				if (value == "-")
					std::cin >> arguments;
				else
					arguments = parse_json(read_file(value), value);
			}
			else
				throw std::runtime_error("Unknown option: " + flag);
		}
		WorkshopAuthoring session;
		bool known = operation == "session";
		for (const auto &op : workshop_operations())
			known = known || op.at("id") == operation;
		if (!known)
			throw std::runtime_error("Unknown operation: " + operation);
		if (!arguments.is_object())
			throw std::runtime_error("Arguments must be a JSON object");
		WorkshopDocument draft;
		for (const auto &path : dependencies)
			draft.load_dependency(read_pack_path(path).archive());
		if (!input.empty())
		{
			if (std::filesystem::is_directory(std::filesystem::u8path(input)))
				draft.load_draft(workshop_read_source(input));
			else
				draft.load_pack(read_file(input));
		}
		session.reset(draft);
		auto submit = [&](const Json &request)
		{
			auto response = session.request(request);
			std::cout << response.dump() << '\n';
			std::cout.flush();
			return response.value("ok", false);
		};
		if (operation == "session")
		{
			std::string line;
			while (std::getline(std::cin, line))
			{
				if (line.empty())
					continue;
				try
				{
					submit(Json::parse(line));
				}
				catch (const std::exception &error)
				{
					auto failed = session.request(Json());
					failed["error"] = {{"code", "invalid_json"}, {"message", error.what()}};
					std::cout << failed.dump() << '\n';
					std::cout.flush();
				}
			}
			return std::cin.bad() ? 2 : 0;
		}
		if (operation == "describe" && arguments.empty() && argc == 2)
			arguments = Json::object();
		if (operation == "batch")
		{
			if (requests.empty())
				throw std::runtime_error("batch requires --requests <file|->");
			Json edits;
			if (requests == "-")
				std::cin >> edits;
			else
				edits = parse_json(read_file(requests), requests);
			arguments["requests"] = edits;
		}
		if (operation == "export" || operation == "save")
		{
			if (output.empty())
				throw std::runtime_error(operation + " requires --output");
			arguments["path"] = output;
		}
		bool read_only = operation == "help" || operation == "capabilities" || operation == "describe" ||
		                 operation == "list" || operation == "get" || operation == "choices" || operation == "target" ||
		                 operation == "verify" || operation == "vocabulary";
		if (!read_only && operation != "export" && operation != "save" && output.empty())
			throw std::runtime_error("Editing commands require --output; use session for unsaved multi-step work");
		auto response = session.request({{"version", 1}, {"operation", operation}, {"arguments", arguments}});
		if (response.value("ok", false) && !read_only && operation != "export" && operation != "save")
		{
			auto saved = session.request({{"operation", std::filesystem::path(output).has_extension() &&
			                                                    (std::filesystem::path(output).extension() == ".zip" ||
			                                                     std::filesystem::path(output).extension() == ".dat")
			                                                ? "export"
			                                                : "save"},
			                              {"arguments", {{"path", output}}}});
			if (!saved.value("ok", false))
				response = saved;
			else
				response["output"] = output;
		}
		std::cout << response.dump() << '\n';
		return response.value("ok", false) ? 0 : 1;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		std::cout << phoenix::Json{{"version", 1},
		                           {"id", nullptr},
		                           {"ok", false},
		                           {"error", {{"code", "invocation_failed"}, {"message", error.what()}}}}
		                 .dump()
		          << '\n';
		return 2;
	}
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
{
	std::vector<std::string> arguments;
	for (int i = 0; i < argc; ++i)
	{
		const int length =
		    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr);
		if (!length)
			return 2;
		std::string argument(static_cast<std::size_t>(length), '\0');
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, argument.data(), length, nullptr, nullptr);
		argument.pop_back();
		arguments.push_back(std::move(argument));
	}
	std::vector<char *> pointers;
	for (auto &argument : arguments)
		pointers.push_back(argument.data());
	return workshop_cli(argc, pointers.data());
}
#else
int main(int argc, char **argv)
{
	return workshop_cli(argc, argv);
}
#endif
