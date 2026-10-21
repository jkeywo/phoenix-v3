module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
export module phoenix.conversations;
export import phoenix.contracts;
export import phoenix.state_tree;
export namespace phoenix
{
using ConversationId = std::uint64_t;
struct CommsParticipant
{
	std::string id, name, portrait, default_tree;
	EntityId host = 0;
	bool character = false, public_directory = true;
};
struct CommsEndpoint
{
	EntityId entity = 0;
	std::int64_t x = 0, y = 0, range = 0;
	bool working = false, human = false;
	std::int64_t altitude = 0;
};
struct ConversationLine
{
	std::string speaker, text;
	std::uint64_t delay = 0, incoming = 0, expiry = 0;
};
struct ConversationChoice
{
	std::string id, text;
};
struct ConversationPromptDefinition
{
	std::string speaker, text, default_choice, ai_choice;
	std::vector<ConversationChoice> choices;
	std::uint64_t timeout = 0, ai_delay = 20;
};
struct ConversationProgram
{
	state_tree::Program tree;
	std::string title;
	std::map<std::string, ConversationLine> lines;
	std::map<std::string, ConversationPromptDefinition> prompts;
};
state_tree::Registry conversation_registry();
ConversationProgram compile_conversation(const Json &, const state_tree::Catalog &);
struct ConversationMessage
{
	ConversationId id = 0;
	std::string sender, name, portrait, text;
	std::uint64_t sent = 0, due = 0, incoming = 0, expires = 0;
	bool literal = false;
	std::set<std::string> recipients, delivered, expired;
};
struct ConversationPrompt
{
	ConversationId id = 0, message = 0;
	std::string definition, answer, respondent;
	std::uint64_t deadline = 0, opened = 0;
	std::set<std::string> eligible;
};
struct Conversation
{
	ConversationId id = 0;
	std::string title, program;
	std::map<std::string, std::string> bindings;
	std::set<std::string> members, former;
	std::vector<ConversationMessage> messages;
	ConversationPrompt prompt;
	state_tree::Instance continuation;
	std::vector<std::string> events;
	std::uint64_t hails = 0;
	bool finished = false;
};
class ConversationRuntime
{
public:
	std::map<std::string, ConversationProgram> programs;
	std::map<std::string, CommsParticipant> participants;
	std::map<EntityId, CommsEndpoint> endpoints;
	std::map<ConversationId, Conversation> conversations;
	std::map<EntityId, std::set<std::string>> known;
	std::map<EntityId, ConversationId> selected;
	std::map<EntityId, std::map<ConversationId, ConversationId>> read_through;
	std::vector<std::pair<ConversationId, std::string>> emitted;
	std::uint64_t tick = 0;
	void disclose(EntityId viewer, const std::string &participant);
	bool linked(const std::string &, const std::string &) const;
	ConversationId open(const std::string &program, std::map<std::string, std::string> bindings,
	                    const std::vector<std::string> &members, const std::string &title = {});
	ConversationId hail(EntityId source, const std::string &target, bool execute = true);
	bool text_allowed(const Conversation &) const;
	bool send_text(EntityId source, ConversationId conversation, const std::string &text, bool execute = true);
	bool answer(EntityId source, ConversationId conversation, ConversationId prompt, const std::string &choice,
	            bool execute = true);
	void event(ConversationId conversation, const std::string &event);
	bool report(ConversationId conversation, const std::string &line);
	void advance(std::uint64_t now);
	Json project(EntityId viewer) const;
	Json save() const;
	void restore(const Json &);

private:
	ConversationId next_id_ = 1;
	std::map<std::string, ConversationId> direct_;
	ConversationId message(Conversation &, const std::string &sender, const std::string &text, std::uint64_t delay = 0,
	                       std::uint64_t incoming = 0, std::uint64_t expiry = 0, bool literal = false);
	void resolve(Conversation &, const std::string &participant, const std::string &choice);
	void step(Conversation &);
};
} // namespace phoenix
