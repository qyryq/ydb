#pragma once

#include <ydb/public/sdk/cpp/client/ydb_driver/driver.h>
#include <ydb/public/sdk/cpp/client/ydb_table/table.h>
#include <ydb/public/sdk/cpp/client/ydb_perstopic/persqueue.h>

namespace NKikimr::NPersQueueTests {

    std::shared_ptr<NYdb::NPQTopic::IWriteSession> CreateWriter(
        NYdb::TDriver& driver,
        const NYdb::NPQTopic::TWriteSessionSettings& settings,
        std::shared_ptr<NYdb::ICredentialsProviderFactory> creds = nullptr
    );

    std::shared_ptr<NYdb::NPQTopic::IWriteSession> CreateWriter(
        NYdb::TDriver& driver,
        const TString& topic,
        const TString& sourceId,
        std::optional<ui32> partitionGroup = {},
        std::optional<TString> codec = {},
        std::optional<bool> reconnectOnFailure = {},
        std::shared_ptr<NYdb::ICredentialsProviderFactory> creds = nullptr
    );

    std::shared_ptr<NYdb::NPQTopic::ISimpleBlockingWriteSession> CreateSimpleWriter(
        NYdb::TDriver& driver,
        const NYdb::NPQTopic::TWriteSessionSettings& settings
    );

    std::shared_ptr<NYdb::NPQTopic::ISimpleBlockingWriteSession> CreateSimpleWriter(
        NYdb::TDriver& driver,
        const TString& topic,
        const TString& sourceId,
        std::optional<ui32> partitionGroup = {},
        std::optional<TString> codec = {},
        std::optional<bool> reconnectOnFailure = {},
        THashMap<TString, TString> sessionMeta = {}
    );

    std::shared_ptr<NYdb::NPQTopic::IReadSession> CreateReader(
        NYdb::TDriver& driver,
        const NYdb::NPQTopic::TReadSessionSettings& settings,
        std::shared_ptr<NYdb::ICredentialsProviderFactory> creds = nullptr

    );

    TMaybe<NYdb::NPQTopic::TReadSessionEvent::TDataReceivedEvent> GetNextMessageSkipAssignment(std::shared_ptr<NYdb::NPQTopic::IReadSession>& reader, TDuration timeout = TDuration::Max());

}
