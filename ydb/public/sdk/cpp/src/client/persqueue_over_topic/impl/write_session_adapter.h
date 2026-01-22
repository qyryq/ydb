#pragma once

#include <ydb/public/sdk/cpp/src/client/persqueue_over_topic/include/write_session.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/federated_topic/federated_topic.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/topic/write_session.h>

#include <memory>

namespace NYdb::inline Dev::NPersQueue::NOverTopic {

std::shared_ptr<IWriteSession> CreateWriteSessionAdapter(NFederatedTopic::TFederatedTopicClient& client,
                                                         const TWriteSessionSettings& settings);
std::shared_ptr<ISimpleBlockingWriteSession> CreateSimpleWriteSessionAdapter(NFederatedTopic::TFederatedTopicClient& client,
                                                                             const TWriteSessionSettings& settings);
ui64 GetAcquiredMessagesCountForTest(const ISimpleBlockingWriteSession* session);

} // namespace NYdb::NPersQueue::NOverTopic
