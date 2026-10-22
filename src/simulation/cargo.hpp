// Cargo missions reuse ordinary craft, inventory and docking; no virtual delivery.
std::vector<CargoDelivery> cargo_deliveries;
std::uint64_t next_cargo_delivery = 1;
bool cargo_committed(EntityId craft) const
{
 for(const auto &job:cargo_deliveries)if(job.carrier==craft && job.phase!="complete" && job.phase!="lost")return true;
 return false;
}
std::int64_t cargo_reservation(EntityId target,std::size_t store,const std::string &) const
{
 std::int64_t volume=0;
 for(const auto &job:cargo_deliveries)if(job.target==target && job.to==store && job.reserved>0)
  volume+=job.reserved*definition(target).logistics.materials.at(job.material).volume;
 return volume;
}
std::int64_t material_available(EntityId id,std::size_t store,const std::string &material,std::uint64_t exempt_cargo=0) const
{
 auto available=material_quantity(id,store,material);
 for(const auto &job:cargo_deliveries)
  if(job.id!=exempt_cargo && job.carrier==id && job.cargo==store && job.material==material && (job.phase=="loading"||job.phase=="outbound"||job.phase=="unloading"))available-=std::min(available,job.amount);
 return available;
}
bool cargo_authority(const ConsoleAddress &a,const OperatingCommand &c,EntityId craft,bool ai) const
{
 const auto &state=entities.at(craft).get<CraftState>();
 if(state.home!=a.ship || enabled_system_owner(a.ship,state.bay)!=a)return false;
 for(const auto &ctrl:entities.at(a.ship).get<EntityControls>().controllers)
  if(material_authority(a,c,ctrl.definition.id,ai))for(const auto &ch:ctrl.definition.commands)for(auto consumer:ch.consumers)
   if(definition(a.ship).fitted->values[consumer].id==state.bay)return true;
 return false;
}
bool cargo_command(const ConsoleAddress &a,const OperatingCommand &c,bool execute,bool ai)
{
 const auto e=entities.at(a.ship);if(!e.has<MaterialInventory>())return false;
 auto &state=e.get_mut<MaterialInventory>();
 if(c.action=="logistics.craft_allow")
 {
  const auto id=parse_material_number(c.system);if(id<1 || !entities.contains(id)||!entities.at(id).has<CraftState>()||(c.text!="0"&&c.text!="1")||!cargo_authority(a,c,id,ai))return false;
  if(execute){if(c.text=="1")state.authorised_craft.insert(id);else state.authorised_craft.erase(id);++state.revision;}return true;
 }
 if(c.action=="logistics.cargo_cancel")
 {
  const auto id=parse_material_number(c.system);
  auto it=std::find_if(cargo_deliveries.begin(),cargo_deliveries.end(),[&](const auto &job){return job.id==id && job.source==a.ship;});
  if(it==cargo_deliveries.end()||!material_authority(a,c,definition(a.ship).logistics.controller,ai))return false;
  if(execute)
  {
   auto &job=*it;job.phase="returning";job.reserved=0;
   std::erase_if(material_requests,[&](const auto &r){return r.cargo==job.id;});
   if(entities.at(job.carrier).has<DockConnection>())
   {
    auto &dock=entities.at(job.carrier).get_mut<DockConnection>();
    if(dock.phase=="approach")detach_dock(job.carrier,false);else {dock.phase="separating";dock.progress=0;}
   }
   for(auto &batch:material_batches)
    if(batch.source==job.carrier && batch.destination==job.target && batch.location==job.carrier && !batch.crossing && material_space(job.carrier,job.cargo,batch.material)>=batch.quantity)
    {batch.destination=job.carrier;batch.to=job.cargo;batch.returning=true;}
   craft_return(entities.at(job.carrier));++state.revision;
  }
  return true;
 }
 if(c.action!="logistics.deliver" || material_requests.size()>=4096)return false;
 const auto &d=definition(a.ship).logistics;if(!material_authority(a,c,d.controller,ai)||cargo_deliveries.size()>=128)return false;
 const auto fields=material_fields(c.text);if(fields.size()!=6)return false;
 const auto craft_number=parse_material_number(fields[0]),target_number=parse_material_number(fields[2]),amount=parse_material_number(fields[5]);
 if(craft_number<1||target_number<1||amount<1||amount>1000000000)return false;
 const auto craft=static_cast<EntityId>(craft_number),target=static_cast<EntityId>(target_number);
 if(!entities.contains(craft)||!entities.contains(target)||!entities.at(craft).has<CraftState>()||!entities.at(craft).has<MaterialInventory>()||!entities.at(target).has<MaterialInventory>()||target==a.ship||!dock_known(a,target))return false;
 const auto &cs=entities.at(craft).get<CraftState>();
 if(cs.home!=a.ship||definition(craft).craft->role!="shuttle"||!craft_available(craft)||!state.authorised_craft.contains(craft))return false;
 auto store=[&](EntityId id,const std::string &key){const auto &stores=definition(id).logistics.stores;for(std::size_t i=0;i<stores.size();++i)if(stores[i].id==key)return i;return stores.size();};
 const auto from=store(a.ship,c.system),cargo=store(craft,fields[1]),to=store(target,fields[3]);const auto &m=fields[4];
 if(from==d.stores.size()||cargo==definition(craft).logistics.stores.size()||to==definition(target).logistics.stores.size()||!material_accepts(a.ship,from,m)||!material_accepts(craft,cargo,m)||!material_accepts(target,to,m)||d.materials.at(m).volume!=definition(craft).logistics.materials.at(m).volume||d.materials.at(m).volume!=definition(target).logistics.materials.at(m).volume||material_space(craft,cargo,m)<amount)return false;
 for(const auto &type:material_store(craft,cargo).accepts)if(material_quantity(craft,cargo,type))return false;
 for(const auto &request:material_requests)if(request.destination==craft && request.to==cargo)return false;
 for(const auto &batch:material_batches)if(batch.destination==craft && batch.to==cargo)return false;
 bool port=false;
 for(std::size_t p=0;p<definition(craft).docking.size();++p)for(std::size_t q=0;q<definition(target).docking.size();++q)
  if(definition(craft).docking[p].compatibility==definition(target).docking[q].compatibility)port=true;
 if(!port)return false;
 if(execute)
 {
  CargoDelivery job;job.id=next_cargo_delivery++;job.source=a.ship;job.carrier=craft;job.target=target;job.from=from;job.cargo=cargo;job.to=to;job.material=m;job.amount=amount;
  request_material(a.ship,from,craft,cargo,m,amount,false);material_requests.back().cargo=job.id;
  cargo_deliveries.push_back(job);++state.revision;
 }
 return true;
}
void step_cargo()
{
 for(auto &job:cargo_deliveries)
 {
  const auto e=entities.at(job.carrier);auto &cs=e.get_mut<CraftState>();
  if(combat_dead(e)||cs.phase==CraftPhase::Lost){job.phase="lost";job.reserved=0;}
  if(craft_link(job.carrier)){job.report=job.phase;job.report_tick=ecs.get<Clock>().tick;}
  if(job.phase=="complete"||job.phase=="lost")continue;
  if(job.phase=="loading")
  {
   if(material_quantity(job.carrier,job.cargo,job.material)<job.amount)continue;
   const auto *owner=comms_owner(job.source).has_value()?&definition(job.source):nullptr;
   if(material_space(job.target,job.to,job.material)<job.amount||!craft_link(job.carrier)||ftl_preparing(job.source)||!owner)continue;
   const auto address=*comms_owner(job.source);
   if(!dock_report_available(address,job.target))continue;
   job.reserved=job.amount;job.phase="outbound";
   cs.order="transport";cs.target=job.target;cs.target_position=entities.at(job.target).get<Pose>().position;
   cs.target_velocity=entities.at(job.target).get<Motion>().velocity;cs.target_altitude=altitude(job.target);launch_craft(e);
  }
  if(job.phase=="outbound")
  {
   if(cs.order=="return" || ftl_recall_started(job.source)){job.phase="returning";job.reserved=0;continue;}
   if(cs.phase!=CraftPhase::Flying)continue;
   if(!e.has<DockConnection>())
   {
    for(std::size_t p=0;p<definition(job.carrier).docking.size()&&!e.has<DockConnection>();++p)
     for(std::size_t q=0;q<definition(job.target).docking.size()&&!e.has<DockConnection>();++q)
     {
      const auto &port=definition(job.carrier).docking[p];
      for(const auto &ctrl:e.get<EntityControls>().controllers)if(ctrl.definition.id==port.controller)
      {
       OperatingCommand c{job.carrier,"dock.assist",static_cast<std::int64_t>(e.get<DockingState>().revision),port.id};
       c.controller=ctrl.definition.id;c.generation=ctrl.generation;c.text=std::to_string(job.target)+"/"+definition(job.target).docking[q].id;
       docking_command({job.carrier,ctrl.definition.console},c,true,true);
      }
     }
   }
   if(dock_ready(job.carrier,job.target))
   {
    if(material_requests.size()>=4096)continue;
    job.phase="unloading";request_material(job.carrier,job.cargo,job.target,job.to,job.material,job.amount,false);
    material_requests.back().cargo=job.id;
   }
  }
  if(job.phase=="unloading")
  {
   bool pending=false;
   for(const auto &r:material_requests)pending|=r.cargo==job.id;
   for(const auto &b:material_batches)pending|=b.source==job.carrier&&b.destination==job.target;
   if(!pending)
   {
    job.phase="returning";job.reserved=0;
    if(e.has<DockConnection>()){auto &c=e.get_mut<DockConnection>();c.phase="separating";c.progress=0;}
    craft_return(e);
   }
  }
  if(job.phase=="returning"&&cs.phase==CraftPhase::Docked)
  {
   // Unused cargo remains physically aboard; a later transfer can unload it.
   bool pending=false;for(const auto &b:material_batches)pending|=b.destination==job.carrier;
   if(!pending)job.phase="complete";
  }
 }
}
void validate_cargo() const
{
 if(cargo_deliveries.size()>128 || !next_cargo_delivery || next_cargo_delivery>9007199254740991ULL)throw std::runtime_error("Invalid cargo continuation count");
 std::set<EntityId> committed;std::uint64_t previous=0;
 for(const auto &job:cargo_deliveries)
 {
  if(!job.id||job.id<=previous||job.id>=next_cargo_delivery||!entities.contains(job.source)||!entities.contains(job.carrier)||!entities.contains(job.target)||!entities.at(job.carrier).has<CraftState>()||entities.at(job.carrier).get<CraftState>().home!=job.source||job.source==job.target||job.amount<=0||job.amount>1000000000||job.reserved<0||job.reserved>job.amount||job.report_tick>ecs.get<Clock>().tick)throw std::runtime_error("Invalid cargo commitment");
  previous=job.id;
  if(job.phase!="loading"&&job.phase!="outbound"&&job.phase!="unloading"&&job.phase!="returning"&&job.phase!="complete"&&job.phase!="lost")throw std::runtime_error("Invalid cargo phase");
  if(job.reserved && job.phase!="outbound" && job.phase!="unloading")throw std::runtime_error("Invalid cargo reservation phase");
  for(const auto &[id,index]:std::vector<std::pair<EntityId,std::size_t>>{{job.source,job.from},{job.carrier,job.cargo},{job.target,job.to}})
   if(!entities.at(id).has<MaterialInventory>()||index>=definition(id).logistics.stores.size()||!material_accepts(id,index,job.material))throw std::runtime_error("Invalid cargo store");
  if(job.phase!="complete"&&job.phase!="lost" && (!committed.insert(job.carrier).second || !entities.at(job.carrier).get<CraftState>().mission.empty()))throw std::runtime_error("Double-booked cargo craft");
 }
 for(const auto &r:material_requests)if(r.cargo)
 {
  bool found=false;for(const auto &job:cargo_deliveries)found|=job.id==r.cargo;
  if(!found)throw std::runtime_error("Cargo request without commitment");
 }
 for(const auto &[id,e]:inventory_rows())
  for(const auto craft:e.get<MaterialInventory>().authorised_craft)
   if(!entities.contains(craft)||!entities.at(craft).has<CraftState>()||entities.at(craft).get<CraftState>().home!=id)throw std::runtime_error("Invalid standing craft authorisation");
}
