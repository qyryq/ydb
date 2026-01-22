#include "common.h"

#include <util/string/cast.h>
#include <util/generic/size_literals.h>

namespace NYdb::inline Dev::NPersQueue::NOverTopic {

namespace {

Ydb::PersQueue::V1::AutoPartitioningStrategy ConvertAutoPartitioningStrategy(NTopic::EAutoPartitioningStrategy strategy) {
    switch (strategy) {
        case NTopic::EAutoPartitioningStrategy::Disabled:
            return Ydb::PersQueue::V1::AutoPartitioningStrategy::AUTO_PARTITIONING_STRATEGY_DISABLED;
        case NTopic::EAutoPartitioningStrategy::ScaleUp:
            return Ydb::PersQueue::V1::AutoPartitioningStrategy::AUTO_PARTITIONING_STRATEGY_SCALE_UP;
        case NTopic::EAutoPartitioningStrategy::ScaleUpAndDown:
            return Ydb::PersQueue::V1::AutoPartitioningStrategy::AUTO_PARTITIONING_STRATEGY_SCALE_UP_AND_DOWN;
        case NTopic::EAutoPartitioningStrategy::Paused:
            return Ydb::PersQueue::V1::AutoPartitioningStrategy::AUTO_PARTITIONING_STRATEGY_PAUSED;
        case NTopic::EAutoPartitioningStrategy::Unspecified:
        default:
            return Ydb::PersQueue::V1::AutoPartitioningStrategy::AUTO_PARTITIONING_STRATEGY_UNSPECIFIED;
    }
}

template <class TSettings>
void ApplyPersQueueAttributes(const TSettings& settings, std::map<std::string, std::string>& attributes) {
    if (settings.PartitionsPerTablet_) {
        attributes["_partitions_per_tablet"] = ToString(*settings.PartitionsPerTablet_);
    }
    if (settings.AllowUnauthenticatedRead_) {
        attributes["_allow_unauthenticated_read"] = ToString(settings.AllowUnauthenticatedRead_);
    }
    if (settings.AllowUnauthenticatedWrite_) {
        attributes["_allow_unauthenticated_write"] = ToString(settings.AllowUnauthenticatedWrite_);
    }
    if (settings.AbcId_) {
        attributes["_abc_id"] = ToString(*settings.AbcId_);
    }
    if (settings.AbcSlug_) {
        attributes["_abc_slug"] = *settings.AbcSlug_;
    }
    if (settings.FederationAccount_) {
        attributes["_federation_account"] = *settings.FederationAccount_;
    }
    if (settings.ClientWriteDisabled_) {
        attributes["_client_write_disabled"] = ToString(settings.ClientWriteDisabled_);
    }
    if (settings.MetricsLevel_) {
        attributes["_metrics_level"] = ToString(*settings.MetricsLevel_);
    }
}

std::optional<bool> GetBoolAttribute(const std::map<std::string, std::string>& attributes, const std::string& key) {
    auto it = attributes.find(key);
    if (it == attributes.end()) {
        return std::nullopt;
    }
    return FromString<bool>(it->second);
}

} // namespace

NTopic::TTopicClientSettings ConvertClientSettings(const TPersQueueClientSettings& settings) {
    NTopic::TTopicClientSettings result;
    result.DefaultCompressionExecutor(settings.DefaultCompressionExecutor_);
    result.DefaultHandlersExecutor(settings.DefaultHandlersExecutor_);
    return result;
}

NFederatedTopic::TFederatedTopicClientSettings ConvertFederatedClientSettings(const TPersQueueClientSettings& settings) {
    NFederatedTopic::TFederatedTopicClientSettings result;
    result.DefaultCompressionExecutor(settings.DefaultCompressionExecutor_);
    result.DefaultHandlersExecutor(settings.DefaultHandlersExecutor_);
    // Use no-retry policy so that if FederationDiscovery service is not available,
    // the SDK immediately falls back to single-database mode instead of retrying forever.
    result.RetryPolicy(NTopic::IRetryPolicy::GetNoRetryPolicy());
    return result;
}

NTopic::TReadSessionSettings ConvertReadSessionSettings(const TReadSessionSettings& settings) {
    NTopic::TReadSessionSettings result;
    if (!settings.ConsumerName_.empty()) {
        result.ConsumerName(settings.ConsumerName_);
    }

    if (settings.MaxMemoryUsageBytes_) {
        result.MaxMemoryUsageBytes(settings.MaxMemoryUsageBytes_);
    }
    if (settings.MaxTimeLag_) {
        result.MaxLag(settings.MaxTimeLag_);
    }
    if (settings.StartingMessageTimestamp_) {
        result.ReadFromTimestamp(settings.StartingMessageTimestamp_);
    }

    result.RetryPolicy(settings.RetryPolicy_);
    result.Decompress(settings.Decompress_);
    if (settings.DecompressionExecutor_) {
        result.DecompressionExecutor(settings.DecompressionExecutor_);
    }
    if (settings.Counters_) {
        result.Counters(settings.Counters_);
    }
    result.ConnectTimeout(settings.ConnectTimeout_);
    if (settings.Log_) {
        result.Log(settings.Log_);
    }

    for (const auto& topic : settings.Topics_) {
        NTopic::TTopicReadSettings topicSettings;
        topicSettings.Path(topic.Path_);
        if (topic.StartingMessageTimestamp_) {
            topicSettings.ReadFromTimestamp(*topic.StartingMessageTimestamp_);
        }
        if (!topic.PartitionGroupIds_.empty()) {
            std::vector<uint64_t> partitionIds;
            partitionIds.reserve(topic.PartitionGroupIds_.size());
            for (ui64 groupId : topic.PartitionGroupIds_) {
                if (groupId > 0) {
                    partitionIds.push_back(groupId - 1);
                }
            }
            topicSettings.PartitionIds_ = std::move(partitionIds);
        }
        result.Topics_.push_back(std::move(topicSettings));
    }

    return result;
}

NTopic::TWriteSessionSettings ConvertWriteSessionSettings(const TWriteSessionSettings& settings) {
    NTopic::TWriteSessionSettings result;
    result.Path(settings.Path_);
    if (!settings.MessageGroupId_.empty()) {
        result.ProducerId(settings.MessageGroupId_);
        result.MessageGroupId(settings.MessageGroupId_);
    }
    if (settings.PartitionGroupId_) {
        if (*settings.PartitionGroupId_ > 0) {
            result.PartitionId(std::optional<uint32_t>(*settings.PartitionGroupId_ - 1));
        }
    }
    result.Codec(settings.Codec_);
    result.CompressionLevel(settings.CompressionLevel_);
    result.MaxMemoryUsage(settings.MaxMemoryUsage_);
    result.MaxInflightCount(settings.MaxInflightCount_);
    result.RetryPolicy(settings.RetryPolicy_);
    result.Meta_ = settings.Meta_;
    if (settings.BatchFlushInterval_) {
        result.BatchFlushInterval(settings.BatchFlushInterval_);
    }
    if (settings.BatchFlushSizeBytes_) {
        result.BatchFlushSizeBytes(settings.BatchFlushSizeBytes_);
    }
    result.ConnectTimeout(settings.ConnectTimeout_);
    if (settings.Counters_) {
        result.Counters(settings.Counters_);
    }
    if (settings.CompressionExecutor_) {
        result.CompressionExecutor(settings.CompressionExecutor_);
    }
    result.ValidateSeqNo(settings.ValidateSeqNo_);
    // Disable DirectWriteToPartition since PersQueue topic paths are different from
    // Topic SDK paths. DirectWrite uses DescribePartition with Topic SDK path format
    // which fails with SCHEME_ERROR for PersQueue topics.
    result.DirectWriteToPartition(false);
    return result;
}

NTopic::TCreateTopicSettings ConvertCreateTopicSettings(const TCreateTopicSettings& settings) {
    NTopic::TCreateTopicSettings result;

    ui64 minPartitions = settings.PartitionsCount_;
    result.PartitioningSettings(minPartitions, minPartitions, NTopic::TAutoPartitioningSettings());
    result.RetentionPeriod(settings.RetentionPeriod_);
    result.SetSupportedCodecs(settings.SupportedCodecs_);
    if (settings.MaxPartitionStorageSize_) {
        result.RetentionStorageMb(settings.MaxPartitionStorageSize_ / (1_MB));
    }
    result.PartitionWriteSpeedBytesPerSecond(settings.MaxPartitionWriteSpeed_);
    result.PartitionWriteBurstBytes(settings.MaxPartitionWriteBurst_);

    if (settings.MetricsLevel_) {
        result.MetricsLevel(*settings.MetricsLevel_);
    }

    for (const auto& rule : settings.ReadRules_) {
        auto& consumer = result.BeginAddConsumer(rule.ConsumerName_);
        consumer.SetImportant(rule.Important_);
        consumer.SetAvailiabilityPeriod(rule.AvailabilityPeriod_);
        consumer.ReadFrom(rule.StartingMessageTimestamp_);
        consumer.SetSupportedCodecs(rule.SupportedCodecs_);
        consumer.EndAddConsumer();
    }

    ApplyPersQueueAttributes(settings, result.Attributes_);

    return result;
}

NTopic::TAlterTopicSettings ConvertAlterTopicSettings(const TAlterTopicSettings& settings) {
    NTopic::TAlterTopicSettings result;

    result.SetRetentionPeriod(std::optional<TDuration>(settings.RetentionPeriod_));
    result.SetSupportedCodecs(settings.SupportedCodecs_);

    if (settings.MaxPartitionStorageSize_) {
        result.SetRetentionStorageMb(std::optional<uint64_t>(settings.MaxPartitionStorageSize_ / (1_MB)));
    }
    result.SetPartitionWriteSpeedBytesPerSecond(std::optional<uint64_t>(settings.MaxPartitionWriteSpeed_));
    result.SetPartitionWriteBurstBytes(std::optional<uint64_t>(settings.MaxPartitionWriteBurst_));

    ui64 minPartitions = settings.PartitionsCount_;
    result.AlterPartitioningSettings(minPartitions, minPartitions);

    if (settings.MetricsLevel_) {
        result.SetMetricsLevel(*settings.MetricsLevel_);
    }

    for (const auto& rule : settings.ReadRules_) {
        auto& consumer = result.BeginAddConsumer(rule.ConsumerName_);
        consumer.Important(rule.Important_);
        consumer.AvailabilityPeriod(rule.AvailabilityPeriod_);
        consumer.ReadFrom(rule.StartingMessageTimestamp_);
        consumer.SetSupportedCodecs(rule.SupportedCodecs_);
        consumer.EndAddConsumer();
    }

    ApplyPersQueueAttributes(settings, result.AlterAttributes_);

    return result;
}

NTopic::TDropTopicSettings ConvertDropTopicSettings(const TDropTopicSettings& settings) {
    NTopic::TDropTopicSettings result;
    result.OperationTimeout(settings.OperationTimeout_);
    result.ClientTimeout(settings.ClientTimeout_);
    return result;
}

void FillPersQueueDescribeResult(const NTopic::TDescribeTopicResult& topicResult,
                                 Ydb::PersQueue::V1::DescribeTopicResult& outProto) {
    const auto& desc = topicResult.GetTopicDescription();

    auto* settings = outProto.mutable_settings();
    settings->set_retention_period_ms(desc.GetRetentionPeriod().MilliSeconds());
    settings->set_supported_format(static_cast<Ydb::PersQueue::V1::TopicSettings_Format>(EFormat::BASE));

    const auto& partitioning = desc.GetPartitioningSettings();
    if (partitioning.GetMaxActivePartitions() != 0 && partitioning.GetMaxActivePartitions() != partitioning.GetMinActivePartitions()) {
        auto* autoSettings = settings->mutable_auto_partitioning_settings();
        autoSettings->set_min_active_partitions(partitioning.GetMinActivePartitions());
        autoSettings->set_max_active_partitions(partitioning.GetMaxActivePartitions());
        autoSettings->set_strategy(ConvertAutoPartitioningStrategy(partitioning.GetAutoPartitioningSettings().GetStrategy()));
        autoSettings->mutable_partition_write_speed()->set_up_utilization_percent(partitioning.GetAutoPartitioningSettings().GetUpUtilizationPercent());
        autoSettings->mutable_partition_write_speed()->set_down_utilization_percent(partitioning.GetAutoPartitioningSettings().GetDownUtilizationPercent());
        autoSettings->mutable_partition_write_speed()->mutable_stabilization_window()->set_seconds(partitioning.GetAutoPartitioningSettings().GetStabilizationWindow().Seconds());
    } else {
        settings->set_partitions_count(partitioning.GetMinActivePartitions());
    }

    for (auto codec : desc.GetSupportedCodecs()) {
        settings->add_supported_codecs(static_cast<Ydb::PersQueue::V1::Codec>(codec));
    }

    if (desc.GetRetentionStorageMb()) {
        settings->set_max_partition_storage_size(*desc.GetRetentionStorageMb() * 1_MB);
    }
    settings->set_max_partition_write_speed(desc.GetPartitionWriteSpeedBytesPerSecond());
    settings->set_max_partition_write_burst(desc.GetPartitionWriteBurstBytes());

    for (const auto& consumer : desc.GetConsumers()) {
        auto* readRule = settings->add_read_rules();
        readRule->set_consumer_name(consumer.GetConsumerName());
        readRule->set_important(consumer.GetImportant());
        readRule->mutable_availability_period()->set_seconds(consumer.GetAvailabilityPeriod().Seconds());
        readRule->set_starting_message_timestamp_ms(consumer.GetReadFrom().MilliSeconds());
        readRule->set_supported_format(static_cast<Ydb::PersQueue::V1::TopicSettings_Format>(EFormat::BASE));
        for (auto codec : consumer.GetSupportedCodecs()) {
            readRule->add_supported_codecs(static_cast<Ydb::PersQueue::V1::Codec>(codec));
        }
    }

    for (const auto& [key, value] : desc.GetAttributes()) {
        (*settings->mutable_attributes())[key] = value;
    }

    if (auto val = desc.GetMetricsLevel()) {
        settings->set_metrics_level(*val);
    }

    auto allowRead = GetBoolAttribute(desc.GetAttributes(), "_allow_unauthenticated_read");
    if (allowRead) {
        (*settings->mutable_attributes())["_allow_unauthenticated_read"] = ToString(*allowRead);
    }
    auto allowWrite = GetBoolAttribute(desc.GetAttributes(), "_allow_unauthenticated_write");
    if (allowWrite) {
        (*settings->mutable_attributes())["_allow_unauthenticated_write"] = ToString(*allowWrite);
    }
}

} // namespace NYdb::NPersQueue::NOverTopic
