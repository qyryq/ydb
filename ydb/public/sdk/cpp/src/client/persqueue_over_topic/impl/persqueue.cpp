#include "common.h"
#include "read_session_adapter.h"
#include "write_session_adapter.h"

#include <ydb/public/sdk/cpp/src/client/persqueue_over_topic/include/client.h>

#include <util/string/cast.h>

namespace NYdb::inline Dev::NPersQueue {

class TCommonCodecsProvider {
public:
    TCommonCodecsProvider() {
        TCodecMap::GetTheCodecMap().Set((ui32)NYdb::NPersQueue::ECodec::GZIP, std::make_unique<TGzipCodec>());
        TCodecMap::GetTheCodecMap().Set((ui32)NYdb::NPersQueue::ECodec::ZSTD, std::make_unique<TZstdCodec>());
    }
};
TCommonCodecsProvider COMMON_CODECS_PROVIDER;

const std::vector<ECodec>& GetDefaultCodecs() {
    static const std::vector<ECodec> codecs = {};
    return codecs;
}

TCredentials::TCredentials(const Ydb::PersQueue::V1::Credentials& settings)
    : Credentials_(settings)
{
    switch (Credentials_.credentials_case()) {
        case Ydb::PersQueue::V1::Credentials::kOauthToken: {
            Mode_ = EMode::OAUTH_TOKEN;
            break;
        }
        case Ydb::PersQueue::V1::Credentials::kJwtParams: {
            Mode_ = EMode::JWT_PARAMS;
            break;
        }
        case Ydb::PersQueue::V1::Credentials::kIam: {
            Mode_ = EMode::IAM;
            break;
        }
        case Ydb::PersQueue::V1::Credentials::CREDENTIALS_NOT_SET: {
            Mode_ = EMode::NOT_SET;
            break;
        }
        default: {
            ythrow yexception() << "unsupported credentials type " << Credentials_.ShortDebugString();
        }
    }
}

TCredentials::EMode TCredentials::GetMode() const {
    return Mode_;
}

std::string TCredentials::GetOauthToken() const {
    Y_ENSURE(GetMode() == EMode::OAUTH_TOKEN);
    return Credentials_.oauth_token();
}

std::string TCredentials::GetJwtParams() const {
    Y_ENSURE(GetMode() == EMode::JWT_PARAMS);
    return Credentials_.jwt_params();
}

std::string TCredentials::GetIamEndpoint() const {
    Y_ENSURE(GetMode() == EMode::IAM);
    return Credentials_.iam().endpoint();
}

std::string TCredentials::GetIamServiceAccountKey() const {
    Y_ENSURE(GetMode() == EMode::IAM);
    return Credentials_.iam().service_account_key();
}

TDescribeTopicResult::TDescribeTopicResult(TStatus status, const Ydb::PersQueue::V1::DescribeTopicResult& result)
    : TStatus(std::move(status))
    , TopicSettings_(result.settings())
    , Proto_(result)
{
}

TDescribeTopicResult::TTopicSettings::TTopicSettings(const Ydb::PersQueue::V1::TopicSettings& settings) {
    RetentionPeriod_ = TDuration::MilliSeconds(settings.retention_period_ms());
    SupportedFormat_ = static_cast<EFormat>(settings.supported_format());

    if (settings.has_auto_partitioning_settings()) {
        PartitionsCount_ = settings.auto_partitioning_settings().min_active_partitions();
        MaxPartitionsCount_ = settings.auto_partitioning_settings().max_active_partitions();
        StabilizationWindow_ = TDuration::Seconds(settings.auto_partitioning_settings().partition_write_speed().stabilization_window().seconds());
        UpUtilizationPercent_ = settings.auto_partitioning_settings().partition_write_speed().up_utilization_percent();
        DownUtilizationPercent_ = settings.auto_partitioning_settings().partition_write_speed().down_utilization_percent();
        AutoPartitioningStrategy_ = settings.auto_partitioning_settings().strategy();
    } else {
        PartitionsCount_ = settings.partitions_count();
    }

    for (const auto& codec : settings.supported_codecs()) {
        SupportedCodecs_.push_back(static_cast<ECodec>(codec));
    }
    MaxPartitionStorageSize_ = settings.max_partition_storage_size();
    MaxPartitionWriteSpeed_ = settings.max_partition_write_speed();
    MaxPartitionWriteBurst_ = settings.max_partition_write_burst();
    ClientWriteDisabled_ = settings.client_write_disabled();
    AllowUnauthenticatedRead_ = AllowUnauthenticatedWrite_ = false;
    if (settings.has_metrics_level()) {
        MetricsLevel_ = settings.metrics_level();
    }

    for (auto& pair : settings.attributes()) {
        if (pair.first == "_partitions_per_tablet") {
            PartitionsPerTablet_ = FromString<ui32>(pair.second);
        } else if (pair.first == "_allow_unauthenticated_read") {
            AllowUnauthenticatedRead_ = FromString<bool>(pair.second);
        } else if (pair.first == "_allow_unauthenticated_write") {
            AllowUnauthenticatedWrite_ = FromString<bool>(pair.second);
        } else if (pair.first == "_abc_id") {
            AbcId_ = FromString<ui32>(pair.second);
        } else if (pair.first == "_abc_slug") {
            AbcSlug_ = pair.second;
        } else if (pair.first == "_federation_account") {
            FederationAccount_ = pair.second;
        }
    }
    for (const auto& readRule : settings.read_rules()) {
        ReadRules_.emplace_back(readRule);
    }
    if (settings.has_remote_mirror_rule()) {
        RemoteMirrorRule_ = settings.remote_mirror_rule();
    }
}

TDescribeTopicResult::TTopicSettings::TReadRule::TReadRule(const Ydb::PersQueue::V1::TopicSettings::ReadRule& settings) {
    ConsumerName_ = settings.consumer_name();
    Important_ = settings.important();
    AvailabilityPeriod_ = TDuration::Seconds(settings.availability_period().seconds());
    StartingMessageTimestamp_ = TInstant::MilliSeconds(settings.starting_message_timestamp_ms());

    SupportedFormat_ = static_cast<EFormat>(settings.supported_format());
    for (const auto& codec : settings.supported_codecs()) {
        SupportedCodecs_.push_back(static_cast<ECodec>(codec));
    }
    Version_ = settings.version();
    ServiceType_ = settings.service_type();
}

TDescribeTopicResult::TTopicSettings::TRemoteMirrorRule::TRemoteMirrorRule(const Ydb::PersQueue::V1::TopicSettings::RemoteMirrorRule& settings)
    : Credentials_(settings.credentials())
{
    Endpoint_ = settings.endpoint();
    TopicPath_ = settings.topic_path();
    ConsumerName_ = settings.consumer_name();
    StartingMessageTimestamp_ = TInstant::MilliSeconds(settings.starting_message_timestamp_ms());
    Database_ = settings.database();
}

class TPersQueueClient::TImpl {
public:
    class TFederatedTopicClientWithOverride final : public NFederatedTopic::TFederatedTopicClient {
    public:
        using NFederatedTopic::TFederatedTopicClient::TFederatedTopicClient;
        using NFederatedTopic::TFederatedTopicClient::OverrideCodec;
    };

    TImpl(const TDriver& driver, const TPersQueueClientSettings& settings)
        : ControlPlaneClient(driver, NOverTopic::ConvertClientSettings(settings))
        , DataPlaneClient(driver, NOverTopic::ConvertFederatedClientSettings(settings))
    {
    }

    NTopic::TTopicClient ControlPlaneClient;
    TFederatedTopicClientWithOverride DataPlaneClient;
};

TPersQueueClient::TPersQueueClient(const TDriver& driver, const TPersQueueClientSettings& settings)
    : Impl_(std::make_shared<TImpl>(driver, settings))
{
}

void TPersQueueClient::ProvideCodec(ECodec codecId, std::unique_ptr<NTopic::ICodec>&& codecImpl) {
    class TSharedCodec final : public NTopic::ICodec {
    public:
        explicit TSharedCodec(std::shared_ptr<NTopic::ICodec> inner)
            : Inner_(std::move(inner))
        {
        }

        std::string Decompress(const std::string& data) const override {
            return Inner_->Decompress(data);
        }

        std::unique_ptr<IOutputStream> CreateCoder(TBuffer& result, int quality) const override {
            return Inner_->CreateCoder(result, quality);
        }

    private:
        std::shared_ptr<NTopic::ICodec> Inner_;
    };

    auto shared = std::shared_ptr<NTopic::ICodec>(std::move(codecImpl));
    TCodecMap::GetTheCodecMap().Set((ui32)codecId, std::make_unique<TSharedCodec>(shared));
    Impl_->DataPlaneClient.ProvideCodec(codecId, std::make_unique<TSharedCodec>(std::move(shared)));
}

void TPersQueueClient::OverrideCodec(ECodec codecId, std::unique_ptr<NTopic::ICodec>&& codecImpl) {
    class TSharedCodec final : public NTopic::ICodec {
    public:
        explicit TSharedCodec(std::shared_ptr<NTopic::ICodec> inner)
            : Inner_(std::move(inner))
        {
        }

        std::string Decompress(const std::string& data) const override {
            return Inner_->Decompress(data);
        }

        std::unique_ptr<IOutputStream> CreateCoder(TBuffer& result, int quality) const override {
            return Inner_->CreateCoder(result, quality);
        }

    private:
        std::shared_ptr<NTopic::ICodec> Inner_;
    };

    auto shared = std::shared_ptr<NTopic::ICodec>(std::move(codecImpl));
    TCodecMap::GetTheCodecMap().Set((ui32)codecId, std::make_unique<TSharedCodec>(shared));
    Impl_->DataPlaneClient.OverrideCodec(codecId, std::make_unique<TSharedCodec>(std::move(shared)));
}

TAsyncStatus TPersQueueClient::CreateTopic(const std::string& path, const TCreateTopicSettings& settings) {
    return Impl_->ControlPlaneClient.CreateTopic(path, NOverTopic::ConvertCreateTopicSettings(settings));
}

TAsyncStatus TPersQueueClient::AlterTopic(const std::string& path, const TAlterTopicSettings& settings) {
    return Impl_->ControlPlaneClient.AlterTopic(path, NOverTopic::ConvertAlterTopicSettings(settings));
}

TAsyncStatus TPersQueueClient::DropTopic(const std::string& path, const TDropTopicSettings& settings) {
    return Impl_->ControlPlaneClient.DropTopic(path, NOverTopic::ConvertDropTopicSettings(settings));
}

TAsyncStatus TPersQueueClient::AddReadRule(const std::string& path, const TAddReadRuleSettings& settings) {
    NTopic::TAlterTopicSettings alterSettings;
    auto& consumer = alterSettings.BeginAddConsumer(settings.ReadRule_.ConsumerName_);
    consumer.Important(settings.ReadRule_.Important_);
    consumer.AvailabilityPeriod(settings.ReadRule_.AvailabilityPeriod_);
    consumer.ReadFrom(settings.ReadRule_.StartingMessageTimestamp_);
    consumer.SetSupportedCodecs(settings.ReadRule_.SupportedCodecs_);
    consumer.EndAddConsumer();
    return Impl_->ControlPlaneClient.AlterTopic(path, alterSettings);
}

TAsyncStatus TPersQueueClient::RemoveReadRule(const std::string& path, const TRemoveReadRuleSettings& settings) {
    NTopic::TAlterTopicSettings alterSettings;
    alterSettings.AppendDropConsumers(settings.ConsumerName_);
    return Impl_->ControlPlaneClient.AlterTopic(path, alterSettings);
}

TAsyncDescribeTopicResult TPersQueueClient::DescribeTopic(const std::string& path, const TDescribeTopicSettings& settings) {
    (void)settings;
    return Impl_->ControlPlaneClient.DescribeTopic(path, NTopic::TDescribeTopicSettings())
        .Apply([](const NThreading::TFuture<NTopic::TDescribeTopicResult>& future) {
            auto result = future.GetValue();
            Ydb::PersQueue::V1::DescribeTopicResult proto;
            NOverTopic::FillPersQueueDescribeResult(result, proto);
            return TDescribeTopicResult(TStatus(result), proto);
        });
}

std::shared_ptr<IReadSession> TPersQueueClient::CreateReadSession(const TReadSessionSettings& settings) {
    return NOverTopic::CreateReadSessionAdapter(Impl_->DataPlaneClient, settings);
}

std::shared_ptr<IWriteSession> TPersQueueClient::CreateWriteSession(const TWriteSessionSettings& settings) {
    return NOverTopic::CreateWriteSessionAdapter(Impl_->DataPlaneClient, settings);
}

std::shared_ptr<ISimpleBlockingWriteSession> TPersQueueClient::CreateSimpleBlockingWriteSession(const TWriteSessionSettings& settings) {
    return NOverTopic::CreateSimpleWriteSessionAdapter(Impl_->DataPlaneClient, settings);
}

} // namespace NYdb::NPersQueue
