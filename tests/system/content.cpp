#include <phoenix/content.hpp>
#include <iostream>
#include <stdexcept>
using namespace phoenix;
void require(bool value, const char* contract) { if (!value) throw std::runtime_error(contract); }
template<class Function> void rejected(Function function, const std::string& diagnostic) {
    try { function(); } catch (const std::exception& error) { if (std::string(error.what()).find(diagnostic) != std::string::npos) return; throw; }
    throw std::runtime_error("Expected refusal: "+diagnostic);
}
int main() {
    try {
        Pack base;
        base.manifest = {{"schema",1},{"id","test-core"}};
        base.files["definitions.json"] = read_file(std::string(PHOENIX_FIXTURE_DIR)+"/definitions.json");
        base.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR)+"/pixel.png");
        auto bytes = write_pack(base); auto loaded = read_pack(bytes);
        require(loaded.hash == sha256(bytes), "pack identity must be the SHA256 of transferred archive bytes");
        auto catalog = resolve_packs({loaded});
        auto changed_after_load = loaded;
        changed_after_load.manifest["id"] = "different";
        rejected([&]{resolve_packs({changed_after_load});},"modified since load");
        require(catalog.entities.at("test.variant")["systems"].contains("engine"), "inheritance must resolve through the production loader");
        require(catalog.entities.at("test.variant")["render"]["scale"] == 2, "child patch must preserve inherited render fields");
        auto defs = parse_json(base.files.at("definitions.json"),"fixture");
        defs["entities"][0]["systems"]["engine"]["max_speed_mm_s"] = 31000;
        base.files["definitions.json"] = json_bytes(defs);
        resolve_packs({read_pack(write_pack(base))});
        Pack mod; mod.manifest = {{"schema",1},{"id","test-mod"},{"dependencies",{"test-core"}},{"overrides",{"test.ship"}}};
        Json replacement = defs["entities"][0]; replacement["render"]["scale"] = 3;
        mod.files["definitions.json"] = json_bytes({{"schema",1},{"entities",Json::array({replacement})}});
        auto mod_loaded = read_pack(write_pack(mod));
        auto modded = resolve_packs({loaded,mod_loaded});
        require(modded.entities.at("test.variant")["render"]["scale"] == 2, "child's explicit scale must survive parent override");
        require(modded.entities.at("test.ship")["render"]["scale"] == 3, "explicit later override must win");
        rejected([&]{resolve_packs({mod_loaded,loaded});},"earlier");
        mod.manifest.erase("overrides");
        rejected([&]{resolve_packs({loaded,read_pack(write_pack(mod))});},"explicit override");
        auto broken = catalog; broken.maps.at("test.map")["ship_slots"][0]["allowed_ships"] = {"missing"};
        rejected([&]{verify_catalog(broken);},"allowed ship");
        broken = catalog; broken.consoles.at("test.console")["widgets"][0]["binding"] = "world.hidden_truth";
        rejected([&]{verify_catalog(broken);},"unsupported widget/binding");
        broken = catalog; broken.assets.at("test-core:assets/pixel.png").resize(24);
        rejected([&]{verify_catalog(broken);},"PNG");
        defs["entities"][0]["extends"] = "test.variant";
        base.files["definitions.json"] = json_bytes(defs);
        rejected([&]{resolve_packs({read_pack(write_pack(base))});},"inheritance cycle");
        base.files["../escape"] = {1}; rejected([&]{write_pack(base);},"unsafe path");
        require(sha256(Bytes{'a','b','c'}) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "content hash must match the SHA256 format contract");
        std::cout << "PASS pack round trip, inheritance, ordered overrides and runnable-content verification\n";
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
