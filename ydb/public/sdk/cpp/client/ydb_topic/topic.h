#pragma once

#include <ydb/public/sdk/cpp/client/ydb_topic/codecs/codecs.h>
#include <ydb/public/sdk/cpp/client/ydb_topic/common/counters.h>
#include <ydb/public/sdk/cpp/client/ydb_topic/common/executor.h>
#include <ydb/public/sdk/cpp/client/ydb_topic/common/retry_policy.h>

#include <ydb/public/api/grpc/ydb_topic_v1.grpc.pb.h>
#include <ydb/public/sdk/cpp/client/ydb_driver/driver.h>
#include <ydb/public/sdk/cpp/client/ydb_scheme/scheme.h>
#include <ydb/public/sdk/cpp/client/ydb_types/exceptions/exceptions.h>

#include <library/cpp/logger/log.h>

#include <util/datetime/base.h>
#include <util/generic/hash.h>
#include <util/generic/maybe.h>
#include <util/generic/ptr.h>
#include <util/generic/size_literals.h>
#include <util/string/builder.h>
#include <util/thread/pool.h>

#include "topic_settings.h"

namespace NYdb::NTopic {

// Topic client.
class TTopicClient {
public:
    class TImpl;

    TTopicClient(const TDriver& driver, const TTopicClientSettings& settings = TTopicClientSettings());

    void ProvideCodec(ECodec codecId, THolder<ICodec>&& codecImpl);

    // Create a new topic.
    TAsyncStatus CreateTopic(const TString& path, const TCreateTopicSettings& settings = {});

    // Update a topic.
    TAsyncStatus AlterTopic(const TString& path, const TAlterTopicSettings& settings = {});

    // Delete a topic.
    TAsyncStatus DropTopic(const TString& path, const TDropTopicSettings& settings = {});

    // Describe a topic.
    TAsyncDescribeTopicResult DescribeTopic(const TString& path, const TDescribeTopicSettings& settings = {});

    // Describe a topic consumer.
    TAsyncDescribeConsumerResult DescribeConsumer(const TString& path, const TString& consumer, const TDescribeConsumerSettings& settings = {});

    // Describe a topic partition
    TAsyncDescribePartitionResult DescribePartition(const TString& path, i64 partitionId, const TDescribePartitionSettings& settings = {});

    //! Create read session.
    std::shared_ptr<IReadSession> CreateReadSession(const TReadSessionSettings& settings);

    //! Create write session.
    std::shared_ptr<ISimpleBlockingWriteSession> CreateSimpleBlockingWriteSession(const TWriteSessionSettings& settings);
    std::shared_ptr<IWriteSession> CreateWriteSession(const TWriteSessionSettings& settings);

    // Commit offset
    TAsyncStatus CommitOffset(const TString& path, ui64 partitionId, const TString& consumerName, ui64 offset,
        const TCommitOffsetSettings& settings = {});

protected:
    void OverrideCodec(ECodec codecId, THolder<ICodec>&& codecImpl);

private:
    std::shared_ptr<TImpl> Impl_;
};

}  // namespace NYdb::NTopic
