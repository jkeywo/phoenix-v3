#include "game.hpp"
#include <phoenix/session.hpp>
#include <phoenix/files.hpp>
#include <raylib.h>
#include <cmath>
#include <algorithm>
#include <sstream>
namespace phoenix {
namespace {
std::string mission, slot, ship, notice, radar_reading;
std::int64_t desired_speed = 0, desired_heading = 0;
std::map<std::string,Texture2D> textures;
RenderTexture2D scene{};
float azimuth = 0.7f, elevation = 0.8f, distance = 160;
Vector3 focus{};
std::optional<std::uint64_t> save_after;
std::optional<std::uint64_t> requested_at;
constexpr float pi = 3.14159265358979323846f;
std::string escape(std::string value) {
    std::string result;
    for (char c : value) { if (c == '&') result += "&amp;"; else if (c == '<') result += "&lt;"; else if (c == '>') result += "&gt;"; else if (c == '"') result += "&quot;"; else result += c; }
    return result;
}
std::string label(const Application& app, const std::string& text) { return app.pseudo_locale ? "[ "+text+" ~~~ ]" : text; }
void next(std::string& value, const std::vector<std::string>& choices) {
    auto found = std::find(choices.begin(),choices.end(),value);
    value = choices.empty() ? "" : (found == choices.end() || ++found == choices.end() ? choices.front() : *found);
}
std::vector<std::string> missions(const Catalog& catalog) { std::vector<std::string> ids; for (const auto& [id,data] : catalog.maps) ids.push_back(id); return ids; }
std::vector<std::string> slots(const Catalog& catalog) {
    std::vector<std::string> ids;
    if (catalog.maps.contains(mission)) for (const auto& value : catalog.maps.at(mission).at("ship_slots")) ids.push_back(value.at("id"));
    return ids;
}
std::vector<std::string> ships(const Catalog& catalog) {
    if (catalog.maps.contains(mission)) for (const auto& value : catalog.maps.at(mission).at("ship_slots")) if (value.at("id") == slot) return value.at("allowed_ships").get<std::vector<std::string>>();
    return {};
}
void normalise(const Catalog& catalog) {
    auto maps = missions(catalog); if (std::find(maps.begin(),maps.end(),mission) == maps.end()) mission = maps.empty() ? "" : maps.front();
    auto available_slots = slots(catalog); if (std::find(available_slots.begin(),available_slots.end(),slot) == available_slots.end()) slot = available_slots.empty() ? "" : available_slots.front();
    auto available_ships = ships(catalog); if (std::find(available_ships.begin(),available_ships.end(),ship) == available_ships.end()) ship = available_ships.empty() ? "" : available_ships.front();
}
std::string contacts_text(const Observation& observation) {
    std::ostringstream text; text << "Radar: " << observation.contacts.size() << " contacts.";
    for (const auto& contact : observation.contacts) {
        auto x = static_cast<double>(contact.x-observation.own.x), y = static_cast<double>(contact.y-observation.own.y);
        auto bearing = std::fmod(std::atan2(y,x)*180/pi+360,360.0);
        text << " " << contact.definition << " #" << contact.id << ", " << static_cast<int>(std::hypot(x,y)/1000) << " m, bearing " << static_cast<int>(bearing) << " degrees.";
    }
    return text.str();
}
bool action(Application& app, const std::string& id) {
    if (!app.session || id.rfind("game.",0) != 0) return false;
    try {
        auto& session = *app.session;
        if (id == "game.import" && !session.running()) request_file("game-pack");
        else if (id == "game.load") request_file("game-save");
        else if (id == "game.save" && session.running()) { save_after = session.ticks(); notice = "Saving after the next local tick boundary."; }
        else if (id == "game.resume" && session.recovery_pending()) {
            session.resume_local(); auto observation = *session.observation(); desired_heading = observation.requested_heading;
            desired_speed = observation.requested_speed; notice = "Local console reclaimed. Saved engine requests remain in effect until changed."; app.screen = Screen::console;
        }
        else if (id == "game.clear") { session.clear_packs(); close_game_scene(); mission.clear(); slot.clear(); ship.clear(); radar_reading.clear(); save_after.reset(); notice = "Session ended; content selection cleared."; }
        else if (id == "game.mission" && !session.running()) { next(mission,missions(session.catalog())); slot.clear(); ship.clear(); }
        else if (id == "game.slot" && !session.running()) { next(slot,slots(session.catalog())); ship.clear(); }
        else if (id == "game.ship" && !session.running()) next(ship,ships(session.catalog()));
        else if (id == "game.start" && !session.running()) {
            session.start({mission,{{slot,ship}},slot}); desired_speed = 0; radar_reading.clear();
            auto own = session.observation()->own; desired_heading = own.heading;
            focus = {own.x/1000.f,0,-own.y/1000.f}; notice = "Local session running. Network peers are not connected."; app.screen = Screen::viewscreen;
        }
        else if (id == "game.console" && session.running()) app.screen = Screen::console;
        else if (id == "game.scene" && session.running()) app.screen = Screen::viewscreen;
        else if (session.running()) {
            const auto observation = session.observation(); if (!observation) return false;
            const auto& systems = session.catalog().entities.at(observation->own.definition).at("systems");
            OperatingCommand command{observation->own.id,"",0};
            auto speed = desired_speed, heading = desired_heading;
            const auto control = id.substr(0,id.find('/'));
            if ((control == "game.faster" || control == "game.slower") && systems.contains("engine")) {
                auto maximum = systems.at("engine").at("max_speed_mm_s").get<std::int64_t>();
                speed = std::clamp(speed+(control == "game.faster" ? 1 : -1)*std::max<std::int64_t>(1,maximum/20),std::int64_t{0},maximum);
                command.action = "speed_mm_s"; command.value = speed;
            } else if (control == "game.left" || control == "game.right") {
                heading = (heading+(control == "game.left" ? 5000 : 355000))%360000; command.action = "heading_mdeg"; command.value = heading;
            } else if (control == "game.contacts") { radar_reading = contacts_text(*observation); notice = radar_reading; return true; }
            else return false;
            if (!session.command(command)) throw std::runtime_error("The server refused that control request");
            desired_speed = speed; desired_heading = heading;
            requested_at = session.ticks();
            notice = "Requested speed "+std::to_string(speed/1000.0)+" m/s; heading "+std::to_string(heading/1000)+" degrees.";
        } else return false;
        normalise(session.catalog()); return true;
    } catch (const std::exception& error) { notice = error.what(); return false; }
}
Rectangle area() { return {20,300,static_cast<float>(GetScreenWidth()-40),std::max(80.f,static_cast<float>(GetScreenHeight()-320))}; }
Rectangle widget_rect(const Json& widget) {
    auto a = area(); return {a.x+a.width*widget.at("x").get<float>()/1000, a.y+a.height*widget.at("y").get<float>()/1000,
        a.width*widget.at("width").get<float>()/1000,a.height*widget.at("height").get<float>()/1000};
}
const Json* console_definition(const Application& app) {
    auto observation = app.session->observation(); if (!observation) return nullptr;
    const auto& catalog = app.session->catalog(); const auto& entity = catalog.entities.at(observation->own.definition);
    return &catalog.consoles.at(entity.at("console").get<std::string>());
}
Texture2D texture(const Catalog& catalog, const std::string& asset) {
    auto found = textures.find(asset); if (found != textures.end()) return found->second;
    const auto& bytes = catalog.assets.at(asset);
    auto image = LoadImageFromMemory(".png",bytes.data(),static_cast<int>(bytes.size()));
    auto result = LoadTextureFromImage(image); UnloadImage(image); textures[asset] = result; return result;
}
}
void prepare_game_screen(Application& app, Rml::ElementDocument& document) {
    app.screen_controls.clear();
    app.screen_action = [&app](const std::string& id) { return action(app,id); };
    std::string markup, description = "Choose a mode to begin.";
    if (app.screen == Screen::client_lobby) description = "Console lobby. Remote connection and authorised station claims are not implemented yet.";
    if (app.screen == Screen::viewscreen || app.screen == Screen::console) description = "No local mission is running.";
    auto button = [&](const std::string& id,const std::string& text) {
        app.screen_controls.emplace_back(id,text);
        return "<button id=\""+escape(id)+"\">"+escape(label(app,text))+"</button>";
    };
    if (app.session) {
        // The Workshop owns its file responses while active. Game imports have an explicit purpose.
        if (app.screen != Screen::workshop && app.screen != Screen::gm) if (auto file = take_imported_file()) {
            try {
                if (!file->error.empty()) throw std::runtime_error(file->error);
                if (file->purpose == "game-pack") { app.session->add_pack(file->bytes); normalise(app.session->catalog()); notice = "Verified pack added: "+file->filename; }
                if (file->purpose == "game-save") {
                    app.session->restore(file->bytes); close_game_scene(); save_after.reset(); radar_reading.clear();
                    app.screen = Screen::server_lobby; notice = "Verified runtime save loaded. Simulation paused; reclaim the local console to resume.";
                }
            } catch (const std::exception& error) { notice = error.what(); }
        }
        if (save_after && app.session->running() && app.session->ticks() > *save_after) {
            try { export_file("phoenix-session.zip",app.session->save()); notice = "Runtime save exported with its exact selected packs."; }
            catch (const std::exception& error) { notice = error.what(); }
            save_after.reset();
        }
        const auto& catalog = app.session->catalog(); normalise(catalog);
        static bool was_running = false;
        if (app.session->running() && !was_running) {
            const auto own = app.session->observation()->own;
            focus = {own.x/1000.f,0,-own.y/1000.f}; requested_at.reset();
        }
        was_running = app.session->running();
        if (app.session->running() && (!requested_at || app.session->ticks() > *requested_at)) {
            const auto observation = *app.session->observation();
            desired_speed = observation.requested_speed; desired_heading = observation.requested_heading; requested_at.reset();
        }
        if (app.screen == Screen::home || app.screen == Screen::server_lobby) {
            description = "Single local ship server. Import base packs before dependent mods. ";
            for (const auto& [id,hash] : catalog.ordered_packs) description += id+" ["+hash.substr(0,12)+"] ";
            if (app.session->recovery_pending()) {
                description = "Recovery lobby / tick "+std::to_string(app.session->ticks())+". No live station claim. ";
                markup = button("game.resume","Reclaim local console and resume")+button("game.load","Load runtime save")+button("game.clear","Discard recovery and clear packs");
            } else if (!app.session->running()) {
                markup = button("game.import","Add ZIP pack")+button("game.clear","Clear packs")+button("game.load","Load runtime save");
                if (!catalog.maps.empty()) markup += button("game.mission","Mission: "+mission)+button("game.slot","Ship slot: "+slot)+button("game.ship","Ship: "+ship);
                if (!ship.empty()) markup += button("game.start","Start local session");
            } else markup = button("game.scene","Open viewscreen")+button("game.console","Open ship console")+button("game.save","Save runtime ZIP")+button("game.load","Load runtime save")+button("game.clear","End session and clear packs");
        } else if ((app.screen == Screen::viewscreen || app.screen == Screen::console) && app.session->running()) {
            const auto own = app.session->observation()->own;
            description = "Local tick "+std::to_string(app.session->ticks())+" / position "+std::to_string(own.x/1000)+", "+std::to_string(own.y/1000)+" m / speed "+std::to_string(static_cast<int>(std::hypot(static_cast<double>(own.vx),static_cast<double>(own.vy))/1000))+" m/s / heading "+std::to_string(own.heading/1000)+" degrees.";
            if (app.screen == Screen::viewscreen) description += " Right drag: orbit; middle drag: pan; wheel: zoom.";
            if (app.screen == Screen::viewscreen) markup = button("game.console","Open ship console");
            else if (const auto* console = console_definition(app)) {
                for (const auto& widget : console->at("widgets")) {
                    const auto percent = [&](const char* name) { return std::to_string(widget.at(name).get<double>()/10)+"%"; };
                    markup += "<div class=\"widget\" style=\"left:"+percent("x")+";top:"+percent("y")+";width:"+percent("width")+";height:"+percent("height")+"\"><h3>"+escape(label(app,widget.at("label")))+"</h3>";
                    const std::string kind = widget.at("kind");
                    const auto suffix = "/"+widget.at("id").get<std::string>();
                    if (kind == "radar") {
                        markup += button("game.contacts"+suffix,"Read current radar contacts");
                        if (!radar_reading.empty()) app.screen_controls.back().second += ". "+radar_reading;
                    }
                    if (kind == "throttle") markup += button("game.slower"+suffix,"Decrease desired speed")+button("game.faster"+suffix,"Increase desired speed");
                    if (kind == "heading") markup += button("game.left"+suffix,"Turn heading +5 degrees")+button("game.right"+suffix,"Turn heading -5 degrees");
                    if (kind == "status") markup += "<p>"+escape(notice)+"</p>";
                    markup += "</div>";
                }
            }
        }
    }
    if (!notice.empty()) description += " "+notice;
    auto* game = document.GetElementById("game");
    game->SetProperty("width",std::to_string(std::max(40,GetScreenWidth()-40))+"px");
    game->SetProperty("height",std::to_string(std::max(80,GetScreenHeight()-320))+"px");
    static std::string previous_markup, previous_description;
    if (markup != previous_markup) { game->SetInnerRML(markup); previous_markup = markup; }
    const auto displayed = escape(label(app,description));
    if (displayed != previous_description) document.GetElementById("description")->SetInnerRML(displayed);
    previous_description = displayed;
}
void draw_game_scene(const Application& app) {
    if (!app.session || !app.session->running()) return;
    if (app.screen == Screen::viewscreen) {
        if (GetMouseY() >= 300) {
            auto delta = GetMouseDelta();
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) { azimuth -= delta.x*0.005f; elevation = std::clamp(elevation+delta.y*0.005f,0.15f,1.5f); }
            if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) { focus.x -= delta.x*distance/700; focus.z -= delta.y*distance/700; }
            distance = std::clamp(distance*std::pow(0.9f,GetMouseWheelMove()),5.f,2000000.f);
        }
        Camera3D camera{{focus.x+distance*std::cos(elevation)*std::cos(azimuth),distance*std::sin(elevation),focus.z+distance*std::cos(elevation)*std::sin(azimuth)},focus,{0,1,0},50,CAMERA_PERSPECTIVE};
        auto region = area();
        int width = std::max(1,static_cast<int>(region.width)), height = std::max(1,static_cast<int>(region.height));
        if (!scene.id || scene.texture.width != width || scene.texture.height != height) {
            if (scene.id) UnloadRenderTexture(scene);
            scene = LoadRenderTexture(width,height);
        }
        BeginTextureMode(scene); ClearBackground({8,15,25,255});
        BeginMode3D(camera); DrawGrid(40,10);
        const auto& catalog = app.session->catalog();
        for (const auto& body : app.session->viewscreen()) {
            const auto& render = catalog.entities.at(body.definition).at("render");
            auto position = Vector3{body.x/1000.f,0,-body.y/1000.f};
            const auto& images = render.at("images"); std::size_t frame = 0;
            if (render.at("mode") == "eight") {
                auto bearing = std::atan2(-(camera.position.z-position.z),camera.position.x-position.x)*180/pi-body.heading/1000.0;
                frame = static_cast<std::size_t>(static_cast<int>(std::floor((std::fmod(bearing+720,360.0)+22.5)/45))%8);
            }
            auto image = texture(catalog,images.at(frame)); auto scale = render.at("scale").get<float>();
            DrawBillboard(camera,image,position,scale,WHITE);
        }
        EndMode3D(); EndTextureMode();
        DrawTextureRec(scene.texture,{0,0,static_cast<float>(width),-static_cast<float>(height)},{region.x,region.y},WHITE);
    } else if (app.screen == Screen::console) {
        auto observation = app.session->observation(); const auto* console = console_definition(app);
        if (!observation || !console) return;
        const auto& systems = app.session->catalog().entities.at(observation->own.definition).at("systems");
        for (const auto& widget : console->at("widgets")) if (widget.at("kind") == "radar") {
            auto rect = widget_rect(widget); rect.y += 100; rect.height -= 110;
            if (rect.width < 10 || rect.height < 10) continue;
            BeginScissorMode(static_cast<int>(rect.x),static_cast<int>(rect.y),static_cast<int>(rect.width),static_cast<int>(rect.height));
            Vector2 centre{rect.x+rect.width/2,rect.y+rect.height/2}; float radius = std::min(rect.width,rect.height)*0.45f;
            for (int ring = 1; ring <= 4; ++ring) DrawCircleLinesV(centre,radius*ring/4,Color{45,91,111,255});
            DrawLineV({centre.x-radius,centre.y},{centre.x+radius,centre.y},Color{45,91,111,255});
            DrawLineV({centre.x,centre.y-radius},{centre.x,centre.y+radius},Color{45,91,111,255});
            const float heading = static_cast<float>(observation->own.heading)*pi/180000;
            auto rotate = [&](float x,float y) { return Vector2{centre.x+x*std::cos(heading)-y*std::sin(heading),centre.y-x*std::sin(heading)-y*std::cos(heading)}; };
            DrawTriangle(rotate(6,0),rotate(-5,4),rotate(-5,-4),WHITE);
            const double range = systems.contains("radar") ? systems.at("radar").at("range_mm").get<double>() : 1;
            for (const auto& contact : observation->contacts) {
                Vector2 point{centre.x+static_cast<float>((contact.x-observation->own.x)/range)*radius,centre.y-static_cast<float>((contact.y-observation->own.y)/range)*radius};
                DrawCircleV(point,4,SKYBLUE); DrawText(TextFormat("#%llu",static_cast<unsigned long long>(contact.id)),static_cast<int>(point.x+6),static_cast<int>(point.y),14,WHITE);
            }
            EndScissorMode();
        }
    }
}
void close_game_scene() { for (auto [id,value] : textures) UnloadTexture(value); textures.clear(); if (scene.id) UnloadRenderTexture(scene); scene = {}; }
}
