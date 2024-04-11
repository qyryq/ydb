#include "persqueue_impl.h"
#include "read_session.h"
#include "write_session.h"

// #include <ydb/public/sdk/cpp/client/ydb_topic/topic_settings.h>

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

std::shared_ptr<IWriteSession> TPersQueueClient::TImpl::CreateWriteSession(const TWriteSessionSettings& settings) {
    return CreateWriteSessionInternal(settings);
}

std::shared_ptr<TWriteSession> TPersQueueClient::TImpl::CreateWriteSessionInternal(const TWriteSessionSettings& settings) {
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
    auto writerSettings = maybeSettings.GetOrElse(settings);
    writerSettings.Path(topic);
    auto clientSettings = FederatedTopicClientSettings;
    clientSettings.Database(database);
    if (writerSettings.RetryPolicy_) {
        clientSettings.RetryPolicy(writerSettings.RetryPolicy_);
    }
    auto client = std::make_shared<NFederatedTopic::TFederatedTopicClient>(*Driver, clientSettings);
    return std::make_shared<TWriteSession>(client, writerSettings);
}

std::shared_ptr<ISimpleBlockingWriteSession> TPersQueueClient::TImpl::CreateSimpleWriteSession(
        const TWriteSessionSettings& settings
) {
    auto subSettings = settings;
    // with_lock (Lock) {
    //     subSettings.EventHandlers_.HandlersExecutor(Settings.DefaultHandlersExecutor_);
    //     if (!settings.CompressionExecutor_) {
    //         subSettings.CompressionExecutor(Settings.DefaultCompressionExecutor_);
    //     }
    // }
    if (settings.EventHandlers_.AcksHandler_) {
        // LOG_LAZY(dbDriverState->Log, TLOG_WARNING, "TSimpleBlockingWriteSession: Cannot use AcksHandler, resetting.");
        subSettings.EventHandlers_.AcksHandler({});
    }
    if (settings.EventHandlers_.ReadyToAcceptHandler_) {
        // LOG_LAZY(dbDriverState->Log, TLOG_WARNING, "TSimpleBlockingWriteSession: Cannot use ReadyToAcceptHandler, resetting.");
        subSettings.EventHandlers_.ReadyToAcceptHandler({});
    }
    if (settings.EventHandlers_.SessionClosedHandler_) {
        // LOG_LAZY(dbDriverState->Log, TLOG_WARNING, "TSimpleBlockingWriteSession: Cannot use SessionClosedHandler, resetting.");
        subSettings.EventHandlers_.SessionClosedHandler({});
    }
    if (settings.EventHandlers_.CommonHandler_) {
        // LOG_LAZY(dbDriverState->Log, TLOG_WARNING, "TSimpleBlockingWriteSession: Cannot use CommonHandler, resetting.");
        subSettings.EventHandlers_.CommonHandler({});
    }
    return std::make_shared<TSimpleBlockingWriteSession>(CreateWriteSessionInternal(subSettings));
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

NFederatedTopic::TFederatedTopicClientSettings ConvertClientSettings(TPersQueueClientSettings const& pq) {
    NFederatedTopic::TFederatedTopicClientSettings federated;

    federated.DefaultCompressionExecutor(pq.DefaultCompressionExecutor_);
    federated.DefaultHandlersExecutor(pq.DefaultHandlersExecutor_);

    // TODO(qyryq) pq.ClusterDiscoveryMode_

    if (pq.Database_) {
        federated.Database(*pq.Database_);
    }
    if (pq.DiscoveryEndpoint_) {
        federated.DiscoveryEndpoint(*pq.DiscoveryEndpoint_);
    }
    if (pq.CredentialsProviderFactory_) {
        federated.CredentialsProviderFactory(*pq.CredentialsProviderFactory_);
    }
    if (pq.DiscoveryMode_) {
        federated.DiscoveryMode(*pq.DiscoveryMode_);
    }
    if (pq.SslCredentials_) {
        federated.SslCredentials(*pq.SslCredentials_);
    }
    return federated;
}

} // namespace NYdb::NPQTopic
