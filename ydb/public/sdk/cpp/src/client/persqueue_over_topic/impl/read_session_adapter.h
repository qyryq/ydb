#pragma once

#include <ydb/public/sdk/cpp/src/client/persqueue_over_topic/include/read_session.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/federated_topic/federated_topic.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/topic/read_session.h>

#include <memory>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace NYdb::inline Dev::NPersQueue::NOverTopic {

class TPartitionStreamAdapter final : public TPartitionStream {
public:
    explicit TPartitionStreamAdapter(NFederatedTopic::TFederatedPartitionSession::TPtr session);

    void RequestStatus() override;

    void Commit(ui64 startOffset, ui64 endOffset);
    void ConfirmCreate(std::optional<ui64> readOffset, std::optional<ui64> commitOffset);
    void ConfirmDestroy();
    void SetCreateConfirmCallback(std::function<void(std::optional<ui64>, std::optional<ui64>)> callback);
    void SetDestroyConfirmCallback(std::function<void()> callback);

private:
    NFederatedTopic::TFederatedPartitionSession::TPtr Session_;
    std::mutex Mutex_;
    std::function<void(std::optional<ui64>, std::optional<ui64>)> CreateConfirmCallback_;
    std::function<void()> DestroyConfirmCallback_;
};

std::shared_ptr<IReadSession> CreateReadSessionAdapter(NFederatedTopic::TFederatedTopicClient& client,
                                                       const TReadSessionSettings& settings);

} // namespace NYdb::NPersQueue::NOverTopic
