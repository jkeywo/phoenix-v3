#include "../support/assertions.hpp"
#include "../support/craft_fixture.hpp"
#include <iostream>
import phoenix.simulation;
using namespace phoenix;
using namespace phoenix::test;
namespace
{
void ticks(World &w,int n){while(n--)w.tick();}
Json fixture()
{
 auto s=craft_fixture();
 s["maps"][0]["factions"]=Json::array({{{"id","crew"},{"label","Crew"}}});
 for(auto &e:s["entities"])
 {
  e["faction"]="crew";const bool craft=e.contains("craft");
  e["logistics"]={{"controller",craft?"flight":"team"},{"units_per_second",100},{"batch_size",10},
  {"materials",Json::array({{{"id","parts"}}})},
  {"stores",Json::array({{{"id","hold"},{"compartment","bridge"},{"capacity",20},{"accepts",{"parts"}},{"main",true},{"initial",{{"parts",craft?0:4}}}}})}};
  e["docking"]={{"ports",Json::array({{{"id","port"},{"controller",craft?"flight":"engine"},{"compartment","bridge"},{"compatibility","standard"},{"x_mm",craft?1000:-5000},{"heading_mdeg",craft?0:180000},{"capture_range_mm",1000},{"mating_ticks",3},{"separation_ticks",3},{"auto_accept_allies",true},{"supply",true}}})}};
 }
 return s;
}
OperatingCommand command(const World &w,std::uint64_t claim,const std::string &verb,const std::string &system,const std::string &text,const std::string &controller)
{
 const auto o=*w.observe(claim);OperatingCommand c{o.own.id,"logistics."+verb,o.gameplay.at("logistics").at("revision"),system};c.console=o.console;c.controller=controller;c.text=text;
 for(const auto &ctrl:o.gameplay.at("controllers"))if(ctrl.at("id")==controller)c.generation=ctrl.at("generation");return c;
}
}
int main(int argc,char **argv)
{
 try
 {
  const auto source=fixture();const auto catalog=load(source);World w;w.load(catalog,"test.map",{{"crew","test.away"}});
  require(w.claim(101,2,"drive")&&w.claim(102,2,"observer"),"Cargo crew claims");ticks(w,5);
  const auto craft=w.observe(102)->gameplay.at("logistics").at("cargo_craft")[0].at("id").get<EntityId>();
  const auto text=std::to_string(craft)+"/hold/1/hold/parts/3";
  require(!w.submit(102,command(w,102,"deliver","hold",text,"team")),"Unapproved craft cannot be taken for cargo");
  require(w.submit(101,command(w,101,"craft_allow",std::to_string(craft),"1","bay")),"Tactical grants standing supply permission");ticks(w,1);
  require(w.submit(102,command(w,102,"deliver","hold",text,"team")),"Engineering requests real cargo delivery");ticks(w,4);
  require(!w.observe(102)->gameplay.at("logistics").at("cargo").empty(),"Cargo commitment is visible");
  World resumed;resumed.restore(catalog,w.save());
  for(int i=0;i<4000;++i)
  {
   w.tick();resumed.tick();
   if(w.save()!=resumed.save()){std::cerr<<"tick "<<i<<" diff "<<Json::diff(w.save(),resumed.save()).dump()<<"\n";break;}
   if(w.save().at("logistics").at("cargo")[0].at("phase")=="complete")break;
  }
  require(w.save()==resumed.save(),"Cargo flight and docking resume exactly");
  if(w.save().at("logistics").at("cargo")[0].at("phase")!="complete")
  {
   const auto debug=w.save();std::cerr<<debug.at("logistics").dump()<<"\n";
   for(const auto &b:debug.at("bodies"))if(b.at("body").at("id")==craft)std::cerr<<b.dump()<<"\n";
   std::cerr<<debug.at("craft").dump()<<"\n";
  }
  require(w.save().at("logistics").at("cargo")[0].at("phase")=="complete","Physical cargo sortie returns after unloading");
  require(w.observe(102)->gameplay.at("logistics").at("stores")[0].at("quantities").at("parts")==1,"Cargo leaves source stock");
  const auto save=w.save();std::int64_t received=-1;
  for(const auto &body:save.at("bodies"))if(body.at("body").at("id")==1)received=body.at("inventory").at("stores")[0].at("quantities").at("parts");
  require(received==7,"Cargo arrives at destination once");
  if(argc>1&&std::string(argv[1])=="--trace"){auto trace=save;trace.erase("packs");std::cout<<trace.dump()<<'\n';}else std::cout<<"Physical cargo integration passed\n";
 }
 catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
