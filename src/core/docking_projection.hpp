#pragma once
#include "body_projection.hpp"
namespace phoenix::docking_projection
{
inline Json board(const Json &source)
{
 if(!source.is_object()||source.dump().size()>128*1024)throw std::runtime_error("Invalid docking projection");
 Json out{{"revision",projection::number(source.at("revision"),1,9007199254740991LL)},{"ports",Json::array()},{"targets",Json::array()}};
 if(!source.at("ports").is_array()||source.at("ports").size()>16||!source.at("targets").is_array()||source.at("targets").size()>128)throw std::runtime_error("Invalid docking projection rows");
 for(const auto &r:source.at("ports"))
 {
  Json v;
  for(const auto *k:{"id","controller","compatibility","phase"})v[k]=projection::text(r.at(k),64);
  for(const auto *k:{"target","progress"})v[k]=projection::number(r.at(k),0,9007199254740991LL);
  for(const auto *k:{"accepting","supply","working","emergency"})v[k]=r.at(k).get<bool>();
  out["ports"].push_back(v);
 }
 for(const auto &r:source.at("targets"))out["targets"].push_back({{"entity",projection::number(r.at("entity"),1,9007199254740991LL)},{"port",projection::text(r.at("port"),64)},{"compatibility",projection::text(r.at("compatibility"),64)},{"available",r.at("available").get<bool>()}});
 return out;
}
}
