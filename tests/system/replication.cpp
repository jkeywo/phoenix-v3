#include <phoenix/consensus.hpp>
#include <phoenix/world.hpp>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace phoenix;
void require(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
struct Cluster {
    std::map<std::string,std::unique_ptr<PeerConsensus>> nodes;
    std::map<std::string,WireJson> disk;
    std::map<std::string,std::vector<WireJson>> applied;
    std::map<std::string,std::unique_ptr<World>> worlds;
    std::set<std::string> stalled;
    std::set<std::pair<std::string,std::string>> blocked;
    std::vector<std::string> roster;
    Catalog content;
    explicit Cluster(std::vector<std::string> peers) : roster(std::move(peers)) {
        Pack pack; pack.manifest = {{"schema",1},{"id","test-core"}};
        pack.files["definitions.json"] = read_file(std::string(PHOENIX_FIXTURE_DIR)+"/definitions.json");
        pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR)+"/pixel.png");
        content = resolve_packs({read_pack(write_pack(pack))});
        for (const auto& id : roster) {
            nodes[id] = std::make_unique<PeerConsensus>(PeerRoster{"synthetic-session",id,roster});
            worlds[id] = std::make_unique<World>(); worlds[id]->load(content,"test.map"); worlds[id]->claim(1,1);
        }
        pump();
    }
    void pump() {
        for (int round = 0; round < 1000; ++round) {
            bool progress = false;
            std::vector<std::pair<std::string,PeerDelivery>> deliveries;
            for (auto& [id,node] : nodes) {
                if (!stalled.contains(id)) if (auto write = node->pending_write()) { disk[id] = write->state; node->persisted(write->generation,true); progress = true; }
                for (auto message : node->take_deliveries()) deliveries.emplace_back(id,std::move(message));
                for (const auto& entry : node->take_committed()) {
                    if (entry.value.value("kind",std::string{}) == "consensus-noop") continue;
                    applied[id].push_back(entry.value);
                    require(entry.value.at("kind") == "tick","Only synthetic committed tick frames reach the production world");
                    for (const auto& command : entry.value.at("commands")) require(worlds[id]->submit(1,{1,"speed_mm_s",command.get<std::int64_t>()}),"Replicated inputs must use normal World admission");
                    worlds[id]->tick();
                }
            }
            for (auto& [sender,message] : deliveries) if (!blocked.contains({sender,message.peer})) {
                require(nodes.at(message.peer)->receive(sender,message.packet),"Production peer must accept well-formed authenticated messages"); progress = true;
            }
            if (!progress) return;
        }
        throw std::runtime_error("Synthetic message loop failed to settle");
    }
    void advance(unsigned ms) { for (unsigned elapsed = 0; elapsed < ms; elapsed += 50) { for (auto& [id,node] : nodes) node->elapse(50); pump(); } }
    std::string leader(const std::set<std::string>& exclude = {}) {
        std::string result;
        for (const auto& [id,node] : nodes) if (!exclude.contains(id) && node->role() == PeerRole::leader) { require(result.empty(),"Connected majority must elect only one leader"); result = id; }
        return result;
    }
    void isolate(const std::string& id) { for (const auto& peer : roster) if (peer != id) { blocked.insert({id,peer}); blocked.insert({peer,id}); } }
};
WireJson frame(int id, int speed) { return {{"kind","tick"},{"id",id},{"commands",WireJson::array({speed})}}; }
int main() {
    try {
        PeerConsensus delayed({"storage-probe","a",{"a"}});
        auto initial = delayed.pending_write(); require(initial.has_value(),"New identity/roster must become durable before election");
        delayed.elapse(1000); require(delayed.take_deliveries().empty() && delayed.take_committed().empty(),"Initial storage delay must expose no election effects");
        delayed.persisted(initial->generation+1,true); require(delayed.pending_write().has_value(),"Stale storage completion must have no effect");
        delayed.persisted(initial->generation,true); for (int i = 0; i < 3; ++i) delayed.elapse(1000);
        auto election = delayed.pending_write(); require(election && election->state.at("voted") == "a","Election must first persist its term, own vote and current-term no-op");
        require(delayed.commit_index() == 0 && delayed.take_committed().empty(),"A volatile singleton commit must not reach gameplay");
        delayed.persisted(election->generation,true); require(delayed.has_quorum() && delayed.take_committed().size() == 1,"Durable singleton can commit its election barrier");
        require(delayed.propose(frame(1,2000)),"Elected singleton can propose a bounded tick frame");
        auto failed = delayed.pending_write(); delayed.persisted(failed->generation,false);
        require(delayed.role() == PeerRole::failed && delayed.take_committed().empty() && !delayed.propose(frame(2,2000)),"Failed durable storage must stop authority instead of acknowledging volatile work");

        Cluster fleet({"a","b","c"}); fleet.advance(4000); auto first = fleet.leader(); require(!first.empty(),"Three available peers must elect a leader");
        std::vector<std::string> followers; for (const auto& peer : fleet.roster) if (peer != first) { followers.push_back(peer); fleet.stalled.insert(peer); }
        require(fleet.nodes.at(first)->propose(frame(1,2000)),"Leader can stage the first synthetic frame"); fleet.pump();
        require(fleet.applied[first].empty(),"Follower acknowledgements must wait for durable log writes");
        fleet.stalled.erase(followers[0]); fleet.pump(); require(fleet.applied[first].size() == 1,"One durable follower plus leader must form a three-peer majority");
        fleet.stalled.clear(); fleet.pump(); fleet.advance(300);
        for (const auto& peer : fleet.roster) require(fleet.applied[peer] == fleet.applied[first],"Connected replicas must apply the same committed frames");
        auto forged = WireJson{{"schema",1},{"session","synthetic-session"},{"from",followers[0]},{"to",first},{"term",9000},{"type","vote"},{"last",0},{"last_term",0}};
        auto before_term = fleet.nodes.at(first)->term();
        require(!fleet.nodes.at(first)->receive("outsider",forged) && fleet.nodes.at(first)->term() == before_term,"Unbound/spoofed sender must not raise a term or affect membership");
        forged["from"] = followers[0]; forged["term"] = -1;
        require(!fleet.nodes.at(first)->receive(followers[0],forged),"Negative or malformed counters must be refused before mutation");

        fleet.isolate(first);
        require(fleet.nodes.at(first)->propose(frame(2,9000)),"A newly partitioned leader may stage work before failure detection"); fleet.pump();
        require(fleet.applied[first].size() == 1,"Uncommitted isolated-leader frame must never advance its simulation");
        fleet.advance(5000); auto replacement = fleet.leader({first}); require(!replacement.empty(),"Two surviving peers must elect a replacement without the signalling service deciding");
        require(fleet.nodes.at(replacement)->propose(frame(3,4000)),"Surviving majority must continue accepting committed frames"); fleet.pump(); fleet.advance(300);
        require(fleet.worlds[first]->ticks() == 1 && fleet.worlds[replacement]->ticks() == 2,"Minority must pause while the majority continues full-world execution");
        fleet.blocked.clear(); fleet.advance(4000);
        for (const auto& peer : fleet.roster) {
            require(fleet.applied[peer] == fleet.applied[replacement],"Returning peer must discard its conflicting uncommitted suffix and catch up");
            require(fleet.worlds[peer]->save() == fleet.worlds[replacement]->save(),"Caught-up production simulation must match majority motion and continuation state");
            for (const auto& value : fleet.applied[peer]) require(value.at("id") != 2,"Discarded minority input must never execute after rejoin");
        }
        auto returning = followers[0]; auto durable = fleet.disk[returning];
        PeerConsensus restarted({"synthetic-session",returning,fleet.roster},durable);
        std::vector<WireJson> replay; for (const auto& entry : restarted.take_committed()) if (entry.value.at("kind") != "consensus-noop") replay.push_back(entry.value);
        require(replay == fleet.applied[returning],"Restart must replay exactly the durable committed prefix");
        bool refused = false; try { PeerConsensus wrong({"different-session",returning,fleet.roster},durable); } catch (const std::exception&) { refused = true; }
        require(refused,"Durable state must be bound to the fixed roster, local identity and session epoch");

        Cluster pair({"x","y"}); pair.advance(4000); auto pair_leader = pair.leader(); require(!pair_leader.empty(),"Two connected peers must elect normally");
        pair.isolate(pair_leader); pair.nodes.at(pair_leader)->propose(frame(1,1000)); pair.pump(); pair.advance(5000);
        for (const auto& [id,world] : pair.worlds) require(world->ticks() == 0,"A two-peer session must pause after losing either peer");
        std::cout << "PASS durable elections, delayed acknowledgements, quorum-only simulation, partition recovery and two-peer pause\n";
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
