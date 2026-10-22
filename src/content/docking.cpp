module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
module phoenix.content;
namespace phoenix
{
std::vector<DockPortDefinition> compile_docking(const Catalog &catalog, const std::string &id)
{
 std::vector<DockPortDefinition> out;
 const auto &e=catalog.entities.at(id);
 if (catalog.presentation) return out;
 if(!e.contains("docking")) return out;
 auto require=[&](bool ok,const std::string &why) {if(!ok)throw std::runtime_error(id+".docking: "+why);};
 const auto combat=compile_combat(catalog,id);require(combat.has_value(),"ports require compartments");
 const auto &rows=e.at("docking").at("ports");require(rows.is_array() && rows.size()<=16,"invalid ports");
 std::set<std::string> seen;
 auto number=[&](const Json &p,const char *key,std::int64_t fallback,std::int64_t lo,std::int64_t hi)
 {const auto n=p.value(key,Json(fallback));require(n.is_number_integer()&&!n.is_boolean()&&n>=lo&&n<=hi,key);return n.get<std::int64_t>();};
 for(const auto &p:rows)
 {
  DockPortDefinition d;
  d.id=p.at("id");d.controller=p.at("controller");d.compartment=p.at("compartment");d.compatibility=p.at("compatibility");
  require(!d.id.empty()&&d.id.size()<=64&&d.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")==std::string::npos&&seen.insert(d.id).second,"duplicate/invalid port");
  require(!d.compatibility.empty()&&d.compatibility.size()<=64,"invalid compatibility");
  require(combat->compartment_ids.contains(d.compartment),"unknown port compartment");
  bool controller=false;
  for(const auto &c:e.at("control_layout").at("controllers"))controller|=c.at("id")==d.controller;
  require(controller,"unknown port Controller");
  d.x=number(p,"x_mm",0,-10000000,10000000);d.y=number(p,"y_mm",0,-10000000,10000000);d.z=number(p,"z_mm",0,-1000000,1000000);
  d.heading=number(p,"heading_mdeg",0,0,359999);
  d.capture_range=number(p,"capture_range_mm",1000,1,100000);
  d.capture_speed=number(p,"capture_speed_mm_s",500,1,10000);
  d.alignment=number(p,"alignment_mdeg",5000,1,45000);
  d.mating_ticks=number(p,"mating_ticks",20,1,36000);d.separation_ticks=number(p,"separation_ticks",20,1,36000);
  d.throughput=number(p,"units_per_second",20,1,1000000000);
  d.auto_accept_allies=p.value("auto_accept_allies",false);d.supply=p.value("supply",false);
  d.emergency_release=p.value("emergency_release",false);d.emergency_damage=number(p,"emergency_damage",0,0,1000000);
  out.push_back(d);
 }
 return out;
}
}
