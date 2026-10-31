module;
#include <algorithm>
#include <cstdint>
#include <exception>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>
module phoenix.workshop.authoring_core;

namespace phoenix
{
WorkshopAuthoring::WorkshopAuthoring()
{
	history_.reset(document_);
}
const WorkshopDocument &WorkshopAuthoring::document() const
{
	return document_;
}
void WorkshopAuthoring::commit(const WorkshopDocument &candidate, bool checkpoint)
{
	candidate.validate_draft();
	if (candidate.definitions() == document_.definitions() && candidate.pack().manifest == document_.pack().manifest &&
	    candidate.resource_revision() == document_.resource_revision())
	{
		// Map selection is not an undo step, but changes the scope of subsequent commands.
		bool selection_changed = false;
		if (!candidate.definitions().at("maps").empty())
			for (std::size_t i = 0; i < candidate.definitions().at("maps").size(); ++i)
				if (candidate.definitions().at("maps")[i].at("id") == candidate.map().at("id"))
				{
					selection_changed = document_.map().at("id") != candidate.map().at("id");
					document_.select_map(i);
				}
		if (selection_changed)
			++revision_;
		return;
	}
	document_ = candidate;
	++revision_;
	if (checkpoint)
		history_.checkpoint(document_);
}
void WorkshopAuthoring::checkpoint()
{
	history_.checkpoint(document_);
}
void WorkshopAuthoring::reset(const WorkshopDocument &document)
{
	document_ = document;
	++revision_;
	history_.reset(document_);
}
bool WorkshopAuthoring::undo()
{
	if (!history_.undo(document_))
		return false;
	++revision_;
	return true;
}
bool WorkshopAuthoring::redo()
{
	if (!history_.redo(document_))
		return false;
	++revision_;
	return true;
}
bool WorkshopAuthoring::can_undo() const
{
	return history_.can_undo();
}
bool WorkshopAuthoring::can_redo() const
{
	return history_.can_redo();
}
Json WorkshopAuthoring::execute(const std::string &operation, const Json &arguments)
{
	if (operation == "capabilities" || operation == "help")
		return {
		    {"protocol", 1}, {"operations", workshop_operations()}, {"definition_kinds", workshop_definition_kinds()}};
	if (operation == "describe")
	{
		if (arguments.value("seeds", false))
			return workshop_seeds();
		if (arguments.contains("record"))
			return workshop_record(arguments.at("record"));
		if (arguments.value("records", false))
			return workshop_records();
		if (arguments.contains("field"))
			return workshop_field(arguments.at("field").get<std::string>());
		if (arguments.contains("operation"))
		{
			for (const auto &row : workshop_operations())
				if (row.at("id") == arguments.at("operation"))
					return row;
			throw std::runtime_error("Unknown operation");
		}
		return workshop_fields();
	}
	if (operation == "undo" || operation == "redo")
		return {{"changed", operation == "undo" ? undo() : redo()}};
	if (operation == "verify")
	{
		(void)document_.verify();
		return {{"valid", true}};
	}
	if (workshop_operation(operation).at("execution") == "query")
	{
		auto candidate = document_;
		return workshop_apply(candidate, operation, arguments);
	}
	if (operation == "batch")
	{
		if (!arguments.at("requests").is_array())
			throw std::runtime_error("requests: expected array");
		auto candidate = document_;
		Json results = Json::array();
		for (const auto &edit : arguments.at("requests"))
		{
			const auto op = edit.at("operation").get<std::string>();
			if (op == "batch" ||
			    (workshop_operation(op).at("execution") != "edit" && workshop_operation(op).at("execution") != "query"))
				throw std::runtime_error("Batches contain document operations only");
			const auto args = edit.value("arguments", Json::object());
			results.push_back(workshop_apply(candidate, op, args));
			if (args.value("verify", false))
				(void)candidate.verify();
		}
		if (arguments.value("verify", false))
			(void)candidate.verify();
		commit(candidate);
		return results;
	}
#ifndef __EMSCRIPTEN__
	if (operation == "save")
	{
		workshop_save_source(document_, arguments.at("path").get<std::string>());
		return {{"path", arguments.at("path")}};
	}
	if (operation == "map.export" || operation == "source.export")
	{
		const auto path = arguments.at("path").get<std::string>();
		if (operation == "map.export")
			workshop_export_map(document_, path);
		else
			workshop_export_source(document_, path);
		return {{"path", path}};
	}
	if (operation == "export")
	{
		workshop_export(document_, arguments.at("path").get<std::string>(), arguments.value("scenario_only", false));
		return {{"path", arguments.at("path")}};
	}
	if (operation == "open")
	{
		auto candidate = document_;
		const auto path = arguments.at("path").get<std::string>();
		if (arguments.value("source", false))
			candidate.load_draft(workshop_read_source(path));
		else
			candidate.load_pack(read_file(path));
		reset(candidate);
		return {{"path", path}};
	}
	if (operation == "import")
	{
		auto candidate = document_;
		auto args = arguments;
		const auto path = arguments.at("path").get<std::string>();
		args.erase("path");
		args["bytes"] = read_file(path);
		if (!args.contains("filename"))
			args["filename"] =
			    path.substr(path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\") + 1);
		auto result = workshop_apply(candidate, "import.bytes", args);
		if (arguments.value("verify", false))
			(void)candidate.verify();
		commit(candidate);
		return result;
	}
#endif
	auto candidate = document_;
	auto result = workshop_apply(candidate, operation, arguments);
	if (arguments.value("verify", false))
		(void)candidate.verify();
	if (workshop_operation(operation).at("execution") == "load")
		reset(candidate);
	else
		commit(candidate);
	return result;
}
Json WorkshopAuthoring::request(const Json &request)
{
	Json response{{"version", 1}, {"id", request.is_object() ? request.value("id", Json()) : Json()}};
	std::string operation;
	try
	{
		if (!request.is_object() || request.value("version", 1) != 1)
			throw std::invalid_argument("Expected a version 1 request object");
		operation = request.at("operation").get<std::string>();
		if (request.contains("expected_revision") && request.at("expected_revision") != revision_)
		{
			response["ok"] = false;
			response["error"] = {{"code", "stale_revision"},
			                     {"operation", operation},
			                     {"message", "Document revision changed; inspect it before retrying"}};
		}
		else
		{
			const auto args = request.value("arguments", Json::object());
			if (!args.is_object())
				throw std::invalid_argument("arguments: expected object");
			workshop_validate_arguments(operation, args);
			response["result"] = execute(operation, args);
			response["ok"] = true;
		}
	}
	catch (const std::exception &error)
	{
		response["ok"] = false;
		response["error"] = {
		    {"code", "operation_failed"},
		    {"operation", operation},
		    {"target", request.is_object() && request.contains("arguments") && request.at("arguments").is_object()
		                   ? request.at("arguments").value("target", Json())
		                   : Json()},
		    {"message", error.what()}};
	}
	response["revision"] = revision_;
	return response;
}
} // namespace phoenix
