module;
#include <cstdint>
#include <set>
#include <tuple>
module phoenix.knowledge;
namespace phoenix
{
KnowledgeStreamIdentity knowledge_stream_identity(const KnowledgeRecord &record)
{
	return {record.scope,
	        record.owner,
	        record.value.schema,
	        record.value.subject,
	        record.value.provenance.source,
	        record.publication,
	        record.recipient_actor};
}
KnowledgeEvidenceQuality knowledge_evidence_quality(KnowledgeTick tick, std::uint64_t revision,
                                                    std::int64_t uncertainty)
{
	return {tick, revision, -uncertainty};
}
KnowledgeEvidenceQuality knowledge_evidence_quality(const EvidenceProvenance &value)
{
	return knowledge_evidence_quality(value.observed_tick, value.revision, value.uncertainty);
}
bool KnowledgeStreamSelection::take_latest(const KnowledgeRecord &record)
{
	return seen_.insert(knowledge_stream_identity(record)).second;
}
} // namespace phoenix
