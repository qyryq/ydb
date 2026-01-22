#include "write_session_adapter.h"

#include "common.h"

#include <util/generic/utility.h>
#include <util/generic/ptr.h>
#include <util/datetime/base.h>

#include <type_traits>
#include <atomic>
#include <mutex>

namespace NYdb::inline Dev::NPersQueue::NOverTopic {

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal event conversion

class TWriteEventConverter {
public:
    TWriteSessionEvent::TEvent ConvertEvent(NTopic::TWriteSessionEvent::TEvent&& event) {
        return std::visit([](auto&& ev) -> TWriteSessionEvent::TEvent {
            using TEventType = std::decay_t<decltype(ev)>;
            if constexpr (std::is_same_v<TEventType, NTopic::TWriteSessionEvent::TAcksEvent>) {
                TWriteSessionEvent::TAcksEvent result;
                result.Acks.reserve(ev.Acks.size());
                for (auto& ack : ev.Acks) {
                    result.Acks.push_back(ConvertAck(std::move(ack)));
                }
                return result;
            } else if constexpr (std::is_same_v<TEventType, NTopic::TWriteSessionEvent::TReadyToAcceptEvent>) {
                return TWriteSessionEvent::TReadyToAcceptEvent{std::move(ev.ContinuationToken)};
            } else if constexpr (std::is_same_v<TEventType, TSessionClosedEvent>) {
                return ev;
            } else {
                return TSessionClosedEvent(EStatus::ABORTED, NYdb::NIssue::TIssues{NYdb::NIssue::TIssue{"Unsupported event"}});
            }
        }, std::move(event));
    }

private:
    static TWriteSessionEvent::TWriteAck ConvertAck(NTopic::TWriteSessionEvent::TWriteAck&& ack) {
        TWriteSessionEvent::TWriteAck result;
        result.SeqNo = ack.SeqNo;
        result.State = ConvertState(ack.State);
        if (ack.Details) {
            result.Details = TWriteSessionEvent::TWriteAck::TWrittenMessageDetails{ack.Details->Offset, ack.Details->PartitionId};
        }
        if (ack.Stat) {
            auto stat = MakeIntrusive<TWriteStat>();
            stat->WriteTime = ack.Stat->WriteTime;
            stat->TotalTimeInPartitionQueue = ack.Stat->MaxTimeInPartitionQueue;
            stat->PartitionQuotedTime = ack.Stat->PartitionQuotedTime;
            stat->TopicQuotedTime = ack.Stat->TopicQuotedTime;
            result.Stat = std::move(stat);
        }
        return result;
    }

    static TWriteSessionEvent::TWriteAck::EEventState ConvertState(NTopic::TWriteSessionEvent::TWriteAck::EEventState state) {
        switch (state) {
            case NTopic::TWriteSessionEvent::TWriteAck::EES_ALREADY_WRITTEN:
                return TWriteSessionEvent::TWriteAck::EES_ALREADY_WRITTEN;
            case NTopic::TWriteSessionEvent::TWriteAck::EES_DISCARDED:
                return TWriteSessionEvent::TWriteAck::EES_DISCARDED;
            case NTopic::TWriteSessionEvent::TWriteAck::EES_WRITTEN_IN_TX:
                return TWriteSessionEvent::TWriteAck::EES_WRITTEN;
            case NTopic::TWriteSessionEvent::TWriteAck::EES_WRITTEN:
            default:
                return TWriteSessionEvent::TWriteAck::EES_WRITTEN;
        }
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TWriteSessionAdapter

class TWriteSessionAdapter final : public IWriteSession {
public:
    TWriteSessionAdapter(std::shared_ptr<NTopic::IWriteSession> session, std::shared_ptr<TWriteEventConverter> converter)
        : Session_(std::move(session))
        , Converter_(std::move(converter))
    {
    }

    NThreading::TFuture<void> WaitEvent() override {
        return Session_->WaitEvent();
    }

    std::optional<TWriteSessionEvent::TEvent> GetEvent(bool block) override {
        auto event = Session_->GetEvent(block);
        if (!event) {
            return std::nullopt;
        }
        return Converter_->ConvertEvent(std::move(*event));
    }

    std::vector<TWriteSessionEvent::TEvent> GetEvents(bool block, std::optional<size_t> maxEventsCount) override {
        auto events = Session_->GetEvents(block, maxEventsCount);
        std::vector<TWriteSessionEvent::TEvent> result;
        result.reserve(events.size());
        for (auto& event : events) {
            result.emplace_back(Converter_->ConvertEvent(std::move(event)));
        }
        return result;
    }

    NThreading::TFuture<ui64> GetInitSeqNo() override {
        return Session_->GetInitSeqNo();
    }

    void Write(TContinuationToken&& continuationToken, std::string_view data, std::optional<ui64> seqNo,
               std::optional<TInstant> createTimestamp) override {
        NTopic::TWriteMessage message(data);
        if (seqNo) {
            message.SeqNo(*seqNo);
        }
        if (createTimestamp) {
            message.CreateTimestamp(*createTimestamp);
        }
        Session_->Write(std::move(continuationToken), std::move(message), nullptr);
    }

    void WriteEncoded(TContinuationToken&& continuationToken, std::string_view data, ECodec codec, ui32 originalSize,
                      std::optional<ui64> seqNo, std::optional<TInstant> createTimestamp) override {
        auto message = NTopic::TWriteMessage::CompressedMessage(data, codec, originalSize);
        if (seqNo) {
            message.SeqNo(*seqNo);
        }
        if (createTimestamp) {
            message.CreateTimestamp(*createTimestamp);
        }
        Session_->WriteEncoded(std::move(continuationToken), std::move(message), nullptr);
    }

    bool Close(TDuration closeTimeout) override {
        return Session_->Close(closeTimeout);
    }

    TWriterCounters::TPtr GetCounters() override {
        return Session_->GetCounters();
    }

private:
    std::shared_ptr<NTopic::IWriteSession> Session_;
    std::shared_ptr<TWriteEventConverter> Converter_;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TSimpleBlockingWriteSessionAdapter

class TFederatedSimpleBlockingWriteSessionAdapter final : public ISimpleBlockingWriteSession {
public:
    explicit TFederatedSimpleBlockingWriteSessionAdapter(std::shared_ptr<NTopic::IWriteSession> session)
        : Session_(std::move(session))
    {
    }

    bool Write(std::string_view data, std::optional<ui64> seqNo, std::optional<TInstant> createTimestamp,
               const TDuration& blockTimeout) override {
        auto token = WaitForToken(blockTimeout);
        if (!token) {
            return false;
        }
        NTopic::TWriteMessage message(data);
        if (seqNo) {
            message.SeqNo(*seqNo);
        }
        if (createTimestamp) {
            message.CreateTimestamp(*createTimestamp);
        }
        ++AcquiredMessages_;
        Session_->Write(std::move(*token), std::move(message), nullptr);
        return true;
    }

    ui64 GetInitSeqNo() override {
        return Session_->GetInitSeqNo().GetValueSync();
    }

    bool Close(TDuration closeTimeout) override {
        const bool ok = Session_->Close(closeTimeout);
        Closed_.store(true);
        return ok;
    }

    bool IsAlive() const override {
        return !Closed_.load();
    }

    TWriterCounters::TPtr GetCounters() override {
        return Session_->GetCounters();
    }

    ui64 GetAcquiredMessagesCountForTest() const {
        return AcquiredMessages_;
    }

private:
    std::optional<NTopic::TContinuationToken> WaitForToken(const TDuration& timeout) {
        const TInstant deadline = timeout == TDuration::Max() ? TInstant::Max() : (TInstant::Now() + timeout);
        while (true) {
            if (auto event = Session_->GetEvent(false)) {
                if (std::holds_alternative<NTopic::TWriteSessionEvent::TReadyToAcceptEvent>(*event)) {
                    return std::move(std::get<NTopic::TWriteSessionEvent::TReadyToAcceptEvent>(*event).ContinuationToken);
                }
                if (std::holds_alternative<TSessionClosedEvent>(*event)) {
                    Closed_.store(true);
                    return std::nullopt;
                }
                continue;
            }
            if (deadline != TInstant::Max()) {
                const TInstant now = TInstant::Now();
                if (now >= deadline) {
                    return std::nullopt;
                }
                if (!Session_->WaitEvent().Wait(deadline - now)) {
                    return std::nullopt;
                }
            } else {
                Session_->WaitEvent().Wait(TDuration::Seconds(1));
            }
        }
    }

private:
    std::shared_ptr<NTopic::IWriteSession> Session_;
    ui64 AcquiredMessages_ = 0;
    std::atomic<bool> Closed_{false};
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Public factory

std::shared_ptr<IWriteSession> CreateWriteSessionAdapter(NFederatedTopic::TFederatedTopicClient& client, const TWriteSessionSettings& settings) {
    auto converter = std::make_shared<TWriteEventConverter>();
    auto topicSettings = ConvertWriteSessionSettings(settings);
    NFederatedTopic::TFederatedWriteSessionSettings fedSettings(std::move(topicSettings));

    if (settings.EventHandlers_.HandlersExecutor_) {
        fedSettings.EventHandlers_.HandlersExecutor(settings.EventHandlers_.HandlersExecutor_);
    }

    const auto& handlers = settings.EventHandlers_;
    if (handlers.AcksHandler_ || handlers.CommonHandler_) {
        fedSettings.EventHandlers_.AcksHandler([converter, handlers](NTopic::TWriteSessionEvent::TAcksEvent& event) mutable {
            auto pqEvent = converter->ConvertEvent(std::move(event));
            if (handlers.AcksHandler_) {
                handlers.AcksHandler_(std::get<TWriteSessionEvent::TAcksEvent>(pqEvent));
            } else if (handlers.CommonHandler_) {
                handlers.CommonHandler_(pqEvent);
            }
        });
    }
    if (handlers.ReadyToAcceptHandler_ || handlers.CommonHandler_) {
        fedSettings.EventHandlers_.ReadyToAcceptHandler([converter, handlers](NTopic::TWriteSessionEvent::TReadyToAcceptEvent& event) mutable {
            auto pqEvent = converter->ConvertEvent(std::move(event));
            if (handlers.ReadyToAcceptHandler_) {
                handlers.ReadyToAcceptHandler_(std::get<TWriteSessionEvent::TReadyToAcceptEvent>(pqEvent));
            } else if (handlers.CommonHandler_) {
                handlers.CommonHandler_(pqEvent);
            }
        });
    }
    if (handlers.SessionClosedHandler_ || handlers.CommonHandler_) {
        fedSettings.EventHandlers_.SessionClosedHandler([handlers](const TSessionClosedEvent& event) mutable {
            if (handlers.SessionClosedHandler_) {
                handlers.SessionClosedHandler_(event);
            }
            if (handlers.CommonHandler_) {
                TWriteSessionEvent::TEvent wrapperEvent = event;
                handlers.CommonHandler_(wrapperEvent);
            }
        });
    }
    if (handlers.CommonHandler_ && !handlers.AcksHandler_ && !handlers.ReadyToAcceptHandler_ && !handlers.SessionClosedHandler_) {
        fedSettings.EventHandlers_.CommonHandler([converter, handlers](NTopic::TWriteSessionEvent::TEvent& event) mutable {
            auto pqEvent = converter->ConvertEvent(std::move(event));
            handlers.CommonHandler_(pqEvent);
        });
    }

    auto session = client.CreateWriteSession(fedSettings);
    return std::make_shared<TWriteSessionAdapter>(std::move(session), std::move(converter));
}

std::shared_ptr<ISimpleBlockingWriteSession> CreateSimpleWriteSessionAdapter(NFederatedTopic::TFederatedTopicClient& client, const TWriteSessionSettings& settings) {
    auto topicSettings = ConvertWriteSessionSettings(settings);
    NFederatedTopic::TFederatedWriteSessionSettings fedSettings(std::move(topicSettings));
    auto session = client.CreateWriteSession(fedSettings);
    return std::make_shared<TFederatedSimpleBlockingWriteSessionAdapter>(std::move(session));
}

ui64 GetAcquiredMessagesCountForTest(const ISimpleBlockingWriteSession* session) {
    if (!session) {
        return 0;
    }
    const auto* adapter = dynamic_cast<const TFederatedSimpleBlockingWriteSessionAdapter*>(session);
    if (!adapter) {
        return 1;
    }
    return adapter->GetAcquiredMessagesCountForTest();
}

} // namespace NYdb::NPersQueue::NOverTopic
