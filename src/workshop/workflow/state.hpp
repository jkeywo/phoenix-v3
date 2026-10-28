#pragma once
// Private workspace state; other modules receive explicit document/view inputs.
struct WorkshopWorkspace::State
{
	FileImportInbox imports;
	FileImportInbox saves;
	FileImportInbox packages;
	std::map<std::string, Bytes> package_files;
	struct SaveLocation
	{
		std::string filename, destination;
	};
	SaveLocation pack_location;
	std::string library_pack;
	bool saving_library = false;
	std::map<std::string, SaveLocation> map_locations;
	std::string saving_map;
	bool saving = false;
	WorkshopDocument document;
	WorkshopHistory history;
	MapEditorSelection selection;
	int console = -1;
	ToolMessage status = "Import a PNG, create an entity definition, then place instances on the map.";
	std::string pack_name = "workshop", library_parent;
	std::string mod_tree_identity;
	std::map<std::string, Json> mod_manifests;
	std::map<std::string, std::string> mod_hashes;
	std::string rename_from, template_id;
	std::string console_rename_from, console_id;
	WorkshopAssetPreview assets;
	MaterialPreview preview;
	Json pending_sidecar;
	std::map<std::string, std::string> sidecar_images;
	bool import_engines = false;
	bool started = false;
	ImVec2 dock_size{};
	void reset_save_locations();
	void clear_textures();

	MapEditor map_editor;
	LayoutEditor layout_editor;
	GameplayEditors gameplay;

	void attempt(const std::function<void()> &action);
	void save_pack(WorkshopEnvironment &environment, bool save_as);
	std::pair<std::string, Bytes> installed_base_pack(WorkshopEnvironment &environment);
	void open_base_pack(WorkshopEnvironment &environment);
	void save_map(bool save_as);
	void add_map();
	void pack_parent_properties(WorkshopEnvironment &environment);
	void string_field(const char *label, Json &object, const char *key);
	void integer_field(const char *label, Json &object, const char *key);
	void transform_fields(Json &value);
	Json *selected_instance();
	void radar_icon_picker(Json &material);
	void material_properties(Json &sprite, bool with_radar_icon = true);
	void sprite_import(Json &render);
	void entity_properties(Json &entity);
	void console_editor(WorkshopEnvironment &environment, bool inspector);
	void slots_editor();
	bool probe_workshop_layout(unsigned stage);
	bool probe_workshop_controllers(unsigned stage);
	void close_editor();
	void open_workshop_pack(const std::string &path);
	void render_editor(WorkshopEnvironment &environment);
};
