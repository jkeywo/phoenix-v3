#include <phoenix/world.hpp>
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace phoenix;
void require(bool value, const char* contract) { if (!value) throw std::runtime_error(contract); }
Catalog load(Json definitions) {
    Pack pack; pack.manifest = {{"schema",1},{"id","test-core"}};
    pack.files["definitions.json"] = json_bytes(definitions);
    pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR)+"/pixel.png");
    return resolve_packs({read_pack(write_pack(pack))});
}
Json instance(int id, const std::string& definition, int x, int heading = 0) { return {{"id",id},{"definition",definition},{"x_mm",x},{"y_mm",0},{"heading_mdeg",heading}}; }
int main(int argc, char**) {
    try {
        auto fixture = parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR)+"/definitions.json"),"synthetic fixture");
        fixture["maps"][0]["entities"] = Json::array({instance(1,"test.ship",0),instance(2,"test.ship",10000),instance(3,"test.ship",200000)});
        auto catalog = load(fixture);
        World engine; engine.load(catalog,"test.map");
        require(engine.claim(101,1),"a vacant ship must admit one console claim");
        require(!engine.claim(102,1) && !engine.submit(102,{1,"speed_mm_s",1000}),"unclaimed/other-console commands must have no effect");
        require(!engine.submit(101,{2,"speed_mm_s",1000}) && !engine.submit(101,{1,"speed_mm_s",999999}),"wrong ship and requests beyond capability must be refused");
        require(engine.observe(101)->contacts.size() == 1 && engine.observe(101)->contacts[0].id == 2,"radar must expose the in-range contact and omit the out-of-range entity");
        engine.submit(101,{1,"speed_mm_s",5000}); for (int i = 0; i < 20; ++i) engine.tick();
        require(engine.inspect()[0].vx > 0 && engine.inspect()[0].x > 0,"admitted engine request must accelerate and move through Flecs execution");
        engine.submit(101,{1,"engine_enabled",0}); engine.submit(101,{1,"heading_mdeg",90000}); engine.tick();
        auto coast = engine.inspect()[0]; for (int i = 0; i < 5; ++i) engine.tick();
        require(engine.inspect()[0].vx == coast.vx && engine.inspect()[0].heading > coast.heading,"engine disable must preserve velocity while steering continues");
        engine.submit(101,{1,"radar_enabled",0}); engine.tick(); require(engine.observe(101)->contacts.empty(),"radar disable must remove its live observations");
        require(!engine.observe(999),"no console claim means no permitted ship projection");
        require(engine.release(101) && engine.claim(303,1) && !engine.submit(101,{1,"speed_mm_s",1000}),"release/reclaim must invalidate commands from the previous console");
        for (int shape = 0; shape < 3; ++shape) {
            auto data = fixture;
            data["entities"][1].erase("extends"); data["entities"][1] = data["entities"][0]; data["entities"][1]["id"] = "test.other";
            if (shape >= 1) data["entities"][1]["collision"] = {{"shape","box"},{"half_width_mm",1000},{"half_length_mm",1000},{"mass_grams",10000}};
            if (shape == 2) data["entities"][0]["collision"] = data["entities"][1]["collision"];
            data["maps"][0]["entities"] = Json::array({instance(1,"test.ship",-3000),instance(2,"test.other",3000,180000)});
            World collision; collision.load(load(data),"test.map"); collision.claim(1,1); collision.claim(2,2);
            collision.submit(1,{1,"speed_mm_s",10000}); collision.submit(2,{2,"speed_mm_s",10000});
            for (int tick = 0; tick < 80; ++tick) collision.tick();
            auto bodies = collision.inspect();
            require(bodies[0].x < bodies[1].x && bodies[1].x-bodies[0].x >= 1998,"circle-circle/circle-box/box-box contacts must prevent crossing");
            require(std::abs(bodies[0].vx+bodies[1].vx) <= 1,"equal opposing finite masses must conserve net momentum in a controlled axial collision");
        }
        for (bool moving_box : {false,true}) {
            auto data = fixture;
            data["entities"][1] = data["entities"][0]; data["entities"][1]["id"] = "test.wall";
            data["entities"][1]["collision"] = {{"shape","box"},{"half_width_mm",2000},{"half_length_mm",500},{"mass_grams",0}};
            if (moving_box) data["entities"][0]["collision"] = {{"shape","box"},{"half_width_mm",1000},{"half_length_mm",1000},{"mass_grams",10000}};
            data["maps"][0]["entities"] = Json::array({instance(1,"test.ship",-5000),instance(2,"test.wall",0,45000)});
            World angled; angled.load(load(data),"test.map"); angled.claim(1,1); angled.submit(1,{1,"speed_mm_s",4000});
            for (int tick = 0; tick < 35; ++tick) angled.tick();
            auto bodies = angled.inspect();
            require(bodies[0].y != 0 && bodies[1].x == 0 && bodies[1].y == 0,"oriented box normals must deflect both circles and boxes while static geometry stays anchored");
        }
        auto ai_data = fixture;
        ai_data["entities"][0]["controller"] = "test.patrol";
        ai_data["trees"] = Json::array({{{"id","test.patrol"},{"initial","advance"},{"states",{
            {"advance",{{"ticks",9},{"next","turn"},{"speed_mm_s",4000},{"heading_mdeg",45000},{"radar_enabled",true}}},
            {"turn",{{"ticks",7},{"next","advance"},{"speed_mm_s",1000},{"heading_mdeg",135000},{"radar_enabled",false}}}
        }}}});
        auto ai_content = load(ai_data);
        World radar_tree; radar_tree.load(ai_content,"test.map"); for (int tick = 0; tick < 10; ++tick) radar_tree.tick();
        require(radar_tree.claim(7,1) && radar_tree.observe(7)->contacts.empty(),"a delegated tree's radar output must affect the same permitted observation path");
        World continuous; continuous.load(ai_content,"test.map"); for (int tick = 0; tick < 7; ++tick) continuous.tick();
        auto checkpoint = continuous.save(); World resumed; resumed.restore(ai_content,checkpoint);
        for (int tick = 0; tick < 100; ++tick) { continuous.tick(); resumed.tick(); }
        require(continuous.save() == resumed.save(),"interrupted state-tree work, integer remainders and motion must resume identically to an uninterrupted run");
        require(continuous.inspect()[0].x != 0 && continuous.inspect()[0].heading != 0,"delegated tree must actually drive engine and steering");
        auto changed = continuous.save(); changed["bodies"][0]["body"]["remainder"]["x"] = std::numeric_limits<std::int64_t>::min();
        bool refused = false; try { resumed.restore(ai_content,changed); } catch (const std::exception&) { refused = true; }
        require(refused && continuous.save() == resumed.save(),"invalid restoration must preserve the previous live state");
        if (argc > 1) std::cout << continuous.save().at("bodies").dump() << '\n';
        else std::cout << "PASS motion, admission, radar, collision pairs and state-tree save/resume\n";
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
