#include "write_session.h"

#include <ydb/public/sdk/cpp/client/ydb_perstopic/persqueue.h>

#include <library/cpp/string_utils/url/url.h>

#include <util/generic/store_policy.h>
#include <util/generic/utility.h>
#include <util/stream/buffer.h>


namespace NYdb::NPQTopic {

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TWriteSession

// TWriteSession::TWriteSession(
//         const TWriteSessionSettings& settings,
//          std::shared_ptr<TPersQueueClient::TImpl> client,
//          std::shared_ptr<TGRpcConnectionsImpl> connections)
//     : TContextOwner(settings, std::move(client), std::move(connections)) {
// }

TWriteSession::TWriteSession(std::shared_ptr<NFederatedTopic::TFederatedTopicClient> client, TWriteSessionSettings settings)
    : TContextOwner(settings, client) {}

// TWriteSession::TWriteSession(std::shared_ptr<NTopic::IWriteSession> session)
//     : TContextOwner(std::move(session)) {
// }

NThreading::TFuture<ui64> TWriteSession::GetInitSeqNo() {
    return TryGetImpl()->GetInitSeqNo();
}

TMaybe<TWriteSessionEvent::TEvent> TWriteSession::GetEvent(bool block) {
    return TryGetImpl()->GetEvent(block);
}

TVector<TWriteSessionEvent::TEvent> TWriteSession::GetEvents(bool block, TMaybe<size_t> maxEventsCount) {
    return TryGetImpl()->GetEvents(block, maxEventsCount);
}

NThreading::TFuture<void> TWriteSession::WaitEvent() {
    return TryGetImpl()->WaitEvent();
}

void TWriteSession::WriteEncoded(TContinuationToken&& token, TStringBuf data, ECodec codec, ui32 originalSize,
                                 TMaybe<ui64> seqNo, TMaybe<TInstant> createTimestamp) {
    TryGetImpl()->WriteEncoded(std::move(token), data, codec, originalSize, seqNo, createTimestamp);
}

void TWriteSession::Write(TContinuationToken&& token, TStringBuf data, TMaybe<ui64> seqNo,
                          TMaybe<TInstant> createTimestamp) {
    TryGetImpl()->Write(std::move(token), data, seqNo, createTimestamp);
}

bool TWriteSession::Close(TDuration closeTimeout) {
    return TryGetImpl()->Close(closeTimeout);
}

TWriteSession::~TWriteSession() {
    TryGetImpl()->Close(TDuration::Zero());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TSimpleBlockingWriteSession

TSimpleBlockingWriteSession::TSimpleBlockingWriteSession(std::shared_ptr<TWriteSession> writer)
    : Writer(writer) {}

ui64 TSimpleBlockingWriteSession::GetInitSeqNo() {
    return Writer->GetInitSeqNo().GetValueSync();
}

bool TSimpleBlockingWriteSession::Write(
        TStringBuf data, TMaybe<ui64> seqNo, TMaybe<TInstant> createTimestamp, const TDuration& blockTimeout
) {
    auto continuationToken = WaitForToken(blockTimeout);
    if (continuationToken.Defined()) {
        Writer->Write(std::move(*continuationToken), std::move(data), seqNo, createTimestamp);
        return true;
    }
    return false;
}

TMaybe<TContinuationToken> TSimpleBlockingWriteSession::WaitForToken(const TDuration& timeout) {
    TInstant startTime = TInstant::Now();
    TDuration remainingTime = timeout;

    TMaybe<TContinuationToken> token = Nothing();

    while (IsAlive() && remainingTime > TDuration::Zero()) {
        Writer->WaitEvent().Wait(remainingTime);

        for (auto event : Writer->GetEvents()) {
            if (auto* readyEvent = std::get_if<TWriteSessionEvent::TReadyToAcceptEvent>(&event)) {
                Y_ABORT_UNLESS(token.Empty());
                token = std::move(readyEvent->ContinuationToken);
            } else if (auto* ackEvent = std::get_if<TWriteSessionEvent::TAcksEvent>(&event)) {
                // discard
            } else if (auto* closeSessionEvent = std::get_if<TSessionClosedEvent>(&event)) {
                Closed.store(true);
                return Nothing();
            }
        }

        if (token.Defined()) {
            return token;
        }

        remainingTime = timeout - (TInstant::Now() - startTime);
    }

    return Nothing();
}

TWriterCounters::TPtr TSimpleBlockingWriteSession::GetCounters() {
    return Writer->GetCounters();
}


bool TSimpleBlockingWriteSession::IsAlive() const {
    return !Closed.load();
}

bool TSimpleBlockingWriteSession::Close(TDuration closeTimeout) {
    Closed.store(true);
    return Writer->Close(std::move(closeTimeout));
}

}; // namespace NYdb::NPQTopic
