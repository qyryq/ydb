#pragma once

#include "persqueue_impl.h"

#include <ydb/public/sdk/cpp/client/ydb_perstopic/persqueue.h>
#include <ydb/public/sdk/cpp/client/ydb_perstopic/impl/callback_context.h>

#include <util/generic/buffer.h>

namespace NYdb::NPQTopic {

namespace NTests {
    class TSimpleWriteSessionTestAdapter;
}

class TWriteSessionImpl : public TContinuationTokenIssuer,
                          public TEnableSelfContext<TWriteSessionImpl> {
private:
    friend class TWriteSession;
    friend class TSimpleBlockingWriteSession;

public:

    TWriteSessionImpl(TWriteSessionSettings settings, std::shared_ptr<NFederatedTopic::TFederatedTopicClient> client)
        : FederatedTopicClient(std::move(client))
        , FederatedWriteSession(FederatedTopicClient->CreateWriteSession(ConvertWriteSessionSettings(settings))) {
    }

    NThreading::TFuture<void> WaitEvent();
    TMaybe<TWriteSessionEvent::TEvent> GetEvent(bool block = false);
    TVector<TWriteSessionEvent::TEvent> GetEvents(bool block = false, TMaybe<size_t> maxEventsCount = Nothing());

    NThreading::TFuture<ui64> GetInitSeqNo();

    void Write(TContinuationToken&&, TStringBuf data, TMaybe<ui64> seqNo = Nothing(), TMaybe<TInstant> createTimestamp = Nothing());
    void WriteEncoded(TContinuationToken&&, TStringBuf data, ECodec codec, ui32 originalSize,
                      TMaybe<ui64> seqNo = Nothing(), TMaybe<TInstant> createTimestamp = Nothing());


    bool Close(TDuration closeTimeout = TDuration::Max());

    TWriterCounters::TPtr GetCounters();

    ~TWriteSessionImpl(); // will not call close - destroy everything without acks

private:

    // TStringBuilder LogPrefix() const;

    TWriteSessionEvent::TWriteAck ConvertAck(NTopic::TWriteSessionEvent::TWriteAck const& ack) const;
    TWriteSessionEvent::TEvent ConvertEvent(NTopic::TWriteSessionEvent::TEvent& event);
    TWriteSessionEvent::TAcksEvent ConvertAcksEvent(NTopic::TWriteSessionEvent::TAcksEvent const& event);
    TWriteSessionEvent::TReadyToAcceptEvent ConvertReadyToAcceptEvent(NTopic::TWriteSessionEvent::TReadyToAcceptEvent const& event);
    TSessionClosedEvent ConvertSessionClosedEvent(NTopic::TSessionClosedEvent const& event);
    NFederatedTopic::TFederatedWriteSessionSettings ConvertWriteSessionSettings(TWriteSessionSettings const& pqSettings);

private:
    std::shared_ptr<NFederatedTopic::TFederatedTopicClient> FederatedTopicClient;
    std::shared_ptr<NTopic::IWriteSession> FederatedWriteSession;
    TAdaptiveLock Lock;

    TString SessionId;

    TInstant SessionStartedTs;

protected:
    ui64 MessagesAcquired = 0;
};

}; // namespace NYdb::NPQTopic
