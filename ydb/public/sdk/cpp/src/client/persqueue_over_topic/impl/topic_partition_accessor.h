#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <util/system/types.h>

namespace NYdb::inline Dev::NTopic {
struct TPartitionSession;
}

namespace NYdb::inline Dev::NPersQueue::NOverTopic {

void CommitPartitionSession(NTopic::TPartitionSession* session, ui64 startOffset, ui64 endOffset);
void ConfirmCreatePartitionSession(NTopic::TPartitionSession* session, std::optional<ui64> readOffset, std::optional<ui64> commitOffset);
void ConfirmDestroyPartitionSession(NTopic::TPartitionSession* session);
void ConfirmEndPartitionSession(NTopic::TPartitionSession* session, const std::vector<ui32>& childIds);

} // namespace NYdb::NPersQueue::NOverTopic
