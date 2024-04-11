#pragma once

#include "write_session_impl.h"

#include <ydb/public/sdk/cpp/client/ydb_perstopic/persqueue.h>
#include <ydb/public/sdk/cpp/client/ydb_perstopic/impl/callback_context.h>

#include <util/generic/buffer.h>


namespace NYdb::NPQTopic {


namespace NTests {
    class TSimpleWriteSessionTestAdapter;
}


class TWriteSession : public IWriteSession,
                      public TContextOwner<TWriteSessionImpl> {
private:
    friend class TSimpleBlockingWriteSession;
    friend class TPersQueueClient;
    friend class NTests::TSimpleWriteSessionTestAdapter;

public:
    TWriteSession(std::shared_ptr<NFederatedTopic::TFederatedTopicClient> client, TWriteSessionSettings settings);

    NThreading::TFuture<void> WaitEvent() override;
    TMaybe<TWriteSessionEvent::TEvent> GetEvent(bool block = false) override;
    TVector<TWriteSessionEvent::TEvent> GetEvents(bool block = false, TMaybe<size_t> maxEventsCount = Nothing()) override;

    NThreading::TFuture<ui64> GetInitSeqNo() override;

    void Write(TContinuationToken&&, TStringBuf data, TMaybe<ui64> seqNo = Nothing(), TMaybe<TInstant> createTimestamp = Nothing()) override;
    void WriteEncoded(TContinuationToken&&, TStringBuf data, ECodec codec, ui32 originalSize,
                      TMaybe<ui64> seqNo = Nothing(), TMaybe<TInstant> createTimestamp = Nothing()) override;

    bool Close(TDuration closeTimeout = TDuration::Max()) override;

    TWriterCounters::TPtr GetCounters() override;

    ~TWriteSession();
};


class TSimpleBlockingWriteSession : public ISimpleBlockingWriteSession {
    friend class NTests::TSimpleWriteSessionTestAdapter;

public:
    TSimpleBlockingWriteSession(std::shared_ptr<TWriteSession> writer);

    bool Write(TStringBuf data, TMaybe<ui64> seqNo = Nothing(), TMaybe<TInstant> createTimestamp = Nothing(),
               const TDuration& blockTimeout = TDuration::Max()) override;

    ui64 GetInitSeqNo() override;

    bool Close(TDuration closeTimeout = TDuration::Max()) override;
    bool IsAlive() const override;

    TWriterCounters::TPtr GetCounters() override;

protected:
    std::shared_ptr<TWriteSession> Writer;

private:
    TMaybe<TContinuationToken> WaitForToken(const TDuration& timeout);
    void HandleAck(TWriteSessionEvent::TAcksEvent&);
    void HandleReady(TWriteSessionEvent::TReadyToAcceptEvent&);
    void HandleClosed(const TSessionClosedEvent&);

    std::atomic_bool Closed = false;
};


}; // namespace NYdb::NPQTopic
