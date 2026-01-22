#pragma once

#include <ydb/public/sdk/cpp/src/client/persqueue_over_topic/include/client.h>
#include <ydb/public/sdk/cpp/src/client/persqueue_over_topic/include/control_plane.h>
#include <ydb/public/sdk/cpp/src/client/persqueue_over_topic/include/read_session.h>
#include <ydb/public/sdk/cpp/src/client/persqueue_over_topic/include/write_session.h>

#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/topic/client.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/topic/control_plane.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/topic/read_session.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/topic/write_session.h>

#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/federated_topic/federated_topic.h>

#include <ydb/public/api/grpc/draft/ydb_persqueue_v1.grpc.pb.h>

namespace NYdb::inline Dev::NPersQueue::NOverTopic {

NTopic::TTopicClientSettings ConvertClientSettings(const TPersQueueClientSettings& settings);
NFederatedTopic::TFederatedTopicClientSettings ConvertFederatedClientSettings(const TPersQueueClientSettings& settings);

NTopic::TReadSessionSettings ConvertReadSessionSettings(const TReadSessionSettings& settings);
NTopic::TWriteSessionSettings ConvertWriteSessionSettings(const TWriteSessionSettings& settings);

NTopic::TCreateTopicSettings ConvertCreateTopicSettings(const TCreateTopicSettings& settings);
NTopic::TAlterTopicSettings ConvertAlterTopicSettings(const TAlterTopicSettings& settings);
NTopic::TDropTopicSettings ConvertDropTopicSettings(const TDropTopicSettings& settings);

void FillPersQueueDescribeResult(const NTopic::TDescribeTopicResult& topicResult,
                                 Ydb::PersQueue::V1::DescribeTopicResult& outProto);

} // namespace NYdb::NPersQueue::NOverTopic
