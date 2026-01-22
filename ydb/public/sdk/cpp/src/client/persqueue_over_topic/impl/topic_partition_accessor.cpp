#include "topic_partition_accessor.h"

#include <ydb/public/sdk/cpp/src/client/topic/impl/read_session_impl.ipp>

namespace NYdb::inline Dev::NPersQueue::NOverTopic {

void CommitPartitionSession(NTopic::TPartitionSession* session, ui64 startOffset, ui64 endOffset) {
    static_cast<NTopic::TPartitionStreamImpl<false>*>(session)->Commit(startOffset, endOffset);
}

void ConfirmCreatePartitionSession(NTopic::TPartitionSession* session, std::optional<ui64> readOffset, std::optional<ui64> commitOffset) {
    static_cast<NTopic::TPartitionStreamImpl<false>*>(session)->ConfirmCreate(readOffset, commitOffset);
}

void ConfirmDestroyPartitionSession(NTopic::TPartitionSession* session) {
    static_cast<NTopic::TPartitionStreamImpl<false>*>(session)->ConfirmDestroy();
}

void ConfirmEndPartitionSession(NTopic::TPartitionSession* session, const std::vector<ui32>& childIds) {
    static_cast<NTopic::TPartitionStreamImpl<false>*>(session)->ConfirmEnd(childIds);
}

} // namespace NYdb::NPersQueue::NOverTopic
