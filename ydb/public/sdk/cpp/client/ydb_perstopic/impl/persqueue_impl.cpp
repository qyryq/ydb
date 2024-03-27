#include "persqueue_impl.h"
#include "read_session.h"
#include "write_session.h"

#include <ydb/public/sdk/cpp/client/ydb_topic/topic_settings.h>

namespace NYdb::NPQTopic {

std::shared_ptr<IReadSession> TPersQueueClient::TImpl::CreateReadSession(const TReadSessionSettings& settings) {
    TMaybe<TReadSessionSettings> maybeSettings;
    if (!settings.DecompressionExecutor_ || !settings.EventHandlers_.HandlersExecutor_) {
        maybeSettings = settings;
        with_lock (Lock) {
            if (!settings.DecompressionExecutor_) {
                maybeSettings->DecompressionExecutor(Settings.DefaultCompressionExecutor_);
            }
            if (!settings.EventHandlers_.HandlersExecutor_) {
                maybeSettings->EventHandlers_.HandlersExecutor(Settings.DefaultHandlersExecutor_);
            }
        }
    }
    auto session = std::make_shared<TReadSession>(maybeSettings.GetOrElse(settings), shared_from_this(), Connections_, DbDriverState_);
    session->Start();
    return std::move(session);
}

NFederatedTopic::TFederatedWriteSessionSettings ConvertToFederatedWriteSessionSettings(TWriteSessionSettings const& pqSettings) {
    NFederatedTopic::TFederatedWriteSessionSettings fedSettings;
    fedSettings.Path(pqSettings.Path_);
    fedSettings.ProducerId(pqSettings.MessageGroupId_);
    fedSettings.MessageGroupId(pqSettings.MessageGroupId_);
    // TODO(qyryq) fedSettings.Codec(pqSettings.Codec_);
    fedSettings.CompressionLevel(pqSettings.CompressionLevel_);
    fedSettings.MaxMemoryUsage(pqSettings.MaxMemoryUsage_);
    fedSettings.MaxInflightCount(pqSettings.MaxInflightCount_);
    fedSettings.RetryPolicy(pqSettings.RetryPolicy_);  // TODO(qyryq) Maybe convert?
    fedSettings.BatchFlushInterval(pqSettings.BatchFlushInterval_);
    fedSettings.BatchFlushSizeBytes(pqSettings.BatchFlushSizeBytes_);
    fedSettings.ConnectTimeout(pqSettings.ConnectTimeout_);
    // TODO(qyryq) fedSettings.Counters(pqSettings.Counters_);
    // TODO(qyryq) fedSettings.CompressionExecutor(pqSettings.CompressionExecutor_);
    // TODO(qyryq) fedSettings.EventHandlers(pqSettings.EventHandlers_);
    fedSettings.ValidateSeqNo(pqSettings.ValidateSeqNo_);
    if (pqSettings.PartitionGroupId_ && pqSettings.PartitionGroupId_ > 0) {
        fedSettings.PartitionId(*pqSettings.PartitionGroupId_ - 1);
    }
    fedSettings.PreferredDatabase(pqSettings.PreferredCluster_);  // TODO(qyryq) Any conversions?
    fedSettings.AllowFallback(pqSettings.AllowFallbackToOtherClusters_);
    // TODO(qyryq) If ClusterDiscoveryMode == false, then as a topic.
    fedSettings.DirectWriteToPartition(false);
    return fedSettings;
}

std::pair<TString, TString> PrepareDatabaseTopicPathsForFederatedClient(TStringBuf endpoint, TStringBuf database, TStringBuf topic) {
    TString dbPath(database);
    TString topicPath(topic);
    if (database != "/Root") {
        return {dbPath, topicPath};
    }
    topic.SkipPrefix("/");
    TString accountName(topic.NextTok('/'));
    if (endpoint == "logbroker.yandex.net" || endpoint == "logbroker-prestable.yandex.net") {
        // topic should be in format <account-name>/<directory>/<topic-name> (directory is optional).
        // Federated Topic SDK expects /logbroker-federation/<account-name> database path, and <directory>/<topic-name> topic path.
        // database is /Root here, but we ignore it, as TFederationDiscoveryServiceActor (kikimr only)
        // prepends it on the server side and Federated Topic SDK receives full path in a ListFederationDatabasesResponse.
        dbPath = "/logbroker-federation/" + accountName;
        topicPath = topic;
    } else if (endpoint.ends_with(".logbroker.yandex.net")) {
        dbPath = "/Root/logbroker-federation/" + accountName;
        topicPath = topic;
    }
    return {dbPath, topicPath};
}

std::shared_ptr<IWriteSession> TPersQueueClient::TImpl::CreateWriteSession(
        const TWriteSessionSettings& settings
) {
    TMaybe<TWriteSessionSettings> maybeSettings;
    // if (!settings.CompressionExecutor_ || !settings.EventHandlers_.HandlersExecutor_) {
    //     maybeSettings = settings;
    //     with_lock (Lock) {
    //         if (!settings.CompressionExecutor_) {
    //             maybeSettings->CompressionExecutor(Settings.DefaultCompressionExecutor_);
    //         }
    //         if (!settings.EventHandlers_.HandlersExecutor_) {
    //             maybeSettings->EventHandlers_.HandlersExecutor(Settings.DefaultHandlersExecutor_);
    //         }
    //     }
    // }
    // if (!Settings.RetryPolicy_) {
    //     Settings.RetryPolicy_ = IRetryPolicy::GetDefaultPolicy();
    // }
    // // TODO(qyryq) Safe to delete.
    // if (Settings.PreferredCluster_ && !Settings.AllowFallbackToOtherClusters_) {
    //     TargetCluster = *Settings.PreferredCluster_;
    //     TargetCluster.to_lower();
    // }
    // if (Settings.Counters_.Defined()) {
    //     Counters = *Settings.Counters_;
    // } else {
    //     Counters = MakeIntrusive<TWriterCounters>(new ::NMonitoring::TDynamicCounters());
    // }

    auto [database, topic] = PrepareDatabaseTopicPathsForFederatedClient(DbDriverState_->GetEndpoint(), DbDriverState_->Database, settings.Path_);
    // Cerr << "XXXXX DbDriverState_->GetEndpoint()=" << DbDriverState_->GetEndpoint()
    //      << " DbDriverState_->Database=" << DbDriverState_->Database
    //      << " settings.Path_=" << settings.Path_ << Endl;
    // Cerr << "XXXXX dbPath=" << database << " topic=" << topic << Endl;
    auto clientSettings = FederatedTopicClientSettings;
    clientSettings.Database(database);
    auto sessionSettings = maybeSettings.GetOrElse(settings);
    sessionSettings.Path(topic);
    auto client = std::make_shared<NFederatedTopic::TFederatedTopicClient>(*Driver, clientSettings);
    return std::make_shared<TWriteSession>(client, sessionSettings);
}

std::shared_ptr<NFederatedTopic::TFederatedTopicClient> TPersQueueClient::TImpl::GetFederatedTopicClient() {
    return FederatedTopicClient;
}

std::shared_ptr<ISimpleBlockingWriteSession> TPersQueueClient::TImpl::CreateSimpleWriteSession(
        const TWriteSessionSettings& settings
) {
    auto alteredSettings = settings;
    // with_lock (Lock) {
    //     alteredSettings.EventHandlers_.HandlersExecutor(Settings.DefaultHandlersExecutor_);
    //     if (!settings.CompressionExecutor_) {
    //         alteredSettings.CompressionExecutor(Settings.DefaultCompressionExecutor_);
    //     }
    // }

    auto session = std::make_shared<TSimpleBlockingWriteSession>(
            alteredSettings, shared_from_this(), Connections_, DbDriverState_
    );
    return std::move(session);
}

std::shared_ptr<TPersQueueClient::TImpl> TPersQueueClient::TImpl::GetClientForEndpoint(const TString& clusterEndoint) {
    with_lock (Lock) {
        Y_ABORT_UNLESS(!CustomEndpoint);
        std::shared_ptr<TImpl>& client = Subclients[clusterEndoint];
        if (!client) {
            client = std::make_shared<TImpl>(clusterEndoint, Connections_, Settings);
        }
        return client;
    }
}

std::shared_ptr<TPersQueueClient::TImpl::IReadSessionConnectionProcessorFactory> TPersQueueClient::TImpl::CreateReadSessionConnectionProcessorFactory() {
    using TService = Ydb::PersQueue::V1::PersQueueService;
    using TRequest = Ydb::PersQueue::V1::MigrationStreamingReadClientMessage;
    using TResponse = Ydb::PersQueue::V1::MigrationStreamingReadServerMessage;
    return CreateConnectionProcessorFactory<TService, TRequest, TResponse>(&TService::Stub::AsyncMigrationStreamingRead, Connections_, DbDriverState_);
}

std::shared_ptr<TPersQueueClient::TImpl::IWriteSessionConnectionProcessorFactory> TPersQueueClient::TImpl::CreateWriteSessionConnectionProcessorFactory() {
    using TService = Ydb::PersQueue::V1::PersQueueService;
    using TRequest = Ydb::PersQueue::V1::StreamingWriteClientMessage;
    using TResponse = Ydb::PersQueue::V1::StreamingWriteServerMessage;
    return CreateConnectionProcessorFactory<TService, TRequest, TResponse>(&TService::Stub::AsyncStreamingWrite, Connections_, DbDriverState_);
}

NFederatedTopic::TFederatedTopicClientSettings ConvertToFederatedTopicClientSettings(TPersQueueClientSettings const& pqSettings) {
    NFederatedTopic::TFederatedTopicClientSettings fedSettings;
    // TODO(qyryq) fedSettings.DefaultCompressionExecutor(pqSettings.DefaultCompressionExecutor_);
    // TODO(qyryq) fedSettings.DefaultHandlersExecutor(pqSettings.DefaultHandlersExecutor_);

    if (pqSettings.Database_) {
        auto db = *pqSettings.Database_;
        // if (db == "/Root") {
        //     TStringBuf path = settings->Topic;
        //     path.SkipPrefix("/");
        //     TString accountName(path.NextTok('/'));
        //     topicPath = path;
        //     if (IsFederation(GetHost(Config().Endpoint()))) {
        //         // settings->Topic should be in the format <account-name>/<directory>/<topic-name> (directory is optional).
        //         // Federated Topic SDK expects /logbroker-federation/<account-name> database path, and <directory>/<topic-name> topic path.
        //         // Config().Database() is equal to /Root here, but we ignore it, as TFederationDiscoveryServiceActor (kikimr only)
        //         // prepends it on the server side and the Federated Topic SDK receives the full path in a ListFederationDatabasesResponse.
        //         dbPath = "/logbroker-federation/" + accountName;
        //     } else {
        //         // Special case for lbkx[t].
        //         dbPath = "/Root/logbroker-federation/" + accountName;
        //     }
        // } else {
        //     // If the endpoint + database-path combination does not point to a federation,
        //     // we pass the database and topic paths to Federated Topic SDK unchanged.
        //     topicPath = settings->Topic;
        //     dbPath = Config().Database();
        // }
        // YLOG_DEBUG(TStringBuilder() << "dbPath=" << dbPath << " topicPath=" << topicPath);
        fedSettings.Database(db);
    }
    if (pqSettings.DiscoveryEndpoint_) {
        fedSettings.DiscoveryEndpoint(*pqSettings.DiscoveryEndpoint_);
    }
    if (pqSettings.CredentialsProviderFactory_) {
        fedSettings.CredentialsProviderFactory(*pqSettings.CredentialsProviderFactory_);
    }
    if (pqSettings.DiscoveryMode_) {
        fedSettings.DiscoveryMode(*pqSettings.DiscoveryMode_);
    }
    if (pqSettings.SslCredentials_) {
        fedSettings.SslCredentials(*pqSettings.SslCredentials_);
    }
    return fedSettings;
}

} // namespace NYdb::NPQTopic
