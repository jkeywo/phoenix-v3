#pragma once
// Private to phoenix.workshop.gameplay; each source provides global includes.
struct GameplayEditors::State : AuthoringControls
{

	std::string entity;
	WorkshopTestClient test_client;
	std::string selected_system, system_entity, message_tree;
	TreeEditor tree_editor;
	Json composition = Json::object();
	Catalog composition_catalog;
	Json *select_entity(WorkshopDocument &document);
	std::vector<std::string> fitted_ids(const WorkshopDocument &document, const Json &ship);
	void system_details(Json &spec, const std::string &fallback_kind = "engine");
	void fitted(WorkshopDocument &document);
	void placements(WorkshopDocument &document);
	void services(Json &parent);
	std::string equipment_kind(const WorkshopDocument &document, const Json &instance) const;
	void destruction_fields(Json &d);
	void projectile_fields(Json &p);
	void combat(WorkshopDocument &document);
	void personnel(Json &ship, WorkshopDocument &document);
	void resources(WorkshopDocument &document);
	void network_editor(Json &network, const std::vector<std::string> &external = {});
	void stock_fields(Json &stock);
	void processor_fields(Json &processor);
	void property_value_field(Json &owner, const char *field, const Json &definition);
	Json property_template(WorkshopDocument &document, const std::string &id);
	void owner_processes_fields(Json &owner, WorkshopDocument &document);
	void owner_properties_fields(Json &owner, WorkshopDocument &document);
	void rule_bindings_fields(Json &source, WorkshopDocument &document, bool officer = false);
	void rule_expression_field(Json &source, const char *key, Json initial);
	void effect_costs_fields(Json &group, const char *key, const char *quantity, WorkshopDocument &document);
	void effect_operations_fields(Json &group, WorkshopDocument &document);
	void templates(WorkshopDocument &document);
	void knowledge(WorkshopDocument &document);
	void knowledge_bindings(WorkshopDocument &document);
	void telemetry_records(Json &spec, WorkshopDocument &document);
	void messages(WorkshopDocument &document);
	void actor(Json &parent, const char *key, const std::vector<std::string> &slots);
	void scenario(WorkshopDocument &document);
	void scenario_selectors(Json &bindings, const Json &scenario, const Json &map,
	                        const std::vector<std::string> &slots);
	void control_layout(WorkshopDocument &document);
	void console_properties(WorkshopDocument &document, bool pseudo_language, Json &console);
	void properties(WorkshopDocument &document, bool pseudo_language, ToolMessage &status, const std::string &selection,
	                const std::string &selected_entity);
	void areas(WorkshopDocument &document, bool pseudo_language, ToolMessage &status, std::string &selection);
};
