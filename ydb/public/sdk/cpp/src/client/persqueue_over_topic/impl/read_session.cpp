#include "read_session_adapter.h"

#include "common.h"

#include <library/cpp/containers/disjoint_interval_tree/disjoint_interval_tree.h>

#include <util/generic/ptr.h>
#include <util/string/builder.h>
#include <util/generic/yexception.h>
#include <util/system/mutex.h>
#include <util/system/yassert.h>

#include <deque>
#include <mutex>
#include <type_traits>
#include <unordered_map>

namespace NYdb::inline Dev::NPersQueue::NOverTopic {

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TPartitionStreamAdapter

TPartitionStreamAdapter::TPartitionStreamAdapter(NFederatedTopic::TFederatedPartitionSession::TPtr session)
    : Session_(std::move(session))
{
    PartitionStreamId = Session_->GetPartitionSessionId();
    TopicPath = Session_->GetTopicPath();
    Cluster.clear();
    PartitionId = Session_->GetPartitionId();
    PartitionGroupId = PartitionId + 1;
}

void TPartitionStreamAdapter::RequestStatus() {
    Session_->RequestStatus();
}

void TPartitionStreamAdapter::Commit(ui64 startOffset, ui64 endOffset) {
    NFederatedTopic::TDeferredCommit deferred;
    deferred.Add(Session_, startOffset, endOffset);
    deferred.Commit();
}

void TPartitionStreamAdapter::ConfirmCreate(std::optional<ui64> readOffset, std::optional<ui64> commitOffset) {
    std::function<void(std::optional<ui64>, std::optional<ui64>)> callback;
    {
        std::lock_guard guard(Mutex_);
        callback = CreateConfirmCallback_;
    }
    if (callback) {
        callback(readOffset, commitOffset);
    }
}

void TPartitionStreamAdapter::ConfirmDestroy() {
    std::function<void()> callback;
    {
        std::lock_guard guard(Mutex_);
        callback = DestroyConfirmCallback_;
    }
    if (callback) {
        callback();
    }
}

void TPartitionStreamAdapter::SetCreateConfirmCallback(std::function<void(std::optional<ui64>, std::optional<ui64>)> callback) {
    std::lock_guard guard(Mutex_);
    CreateConfirmCallback_ = std::move(callback);
}

void TPartitionStreamAdapter::SetDestroyConfirmCallback(std::function<void()> callback) {
    std::lock_guard guard(Mutex_);
    DestroyConfirmCallback_ = std::move(callback);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal event conversion helpers

class TReadEventConverter {
public:
    TReadSessionEvent::TEvent ConvertEvent(NFederatedTopic::TReadSessionEvent::TEvent&& event) {
        return std::visit([this](auto&& ev) -> TReadSessionEvent::TEvent {
            using TEventType = std::decay_t<decltype(ev)>;
            if constexpr (std::is_same_v<TEventType, NFederatedTopic::TReadSessionEvent::TDataReceivedEvent>) {
                return ConvertDataReceivedEvent(std::move(ev));
            } else if constexpr (std::is_same_v<TEventType, NFederatedTopic::TReadSessionEvent::TCommitOffsetAcknowledgementEvent>) {
                auto stream = GetOrCreateStream(ev.GetFederatedPartitionSession());
                return TReadSessionEvent::TCommitAcknowledgementEvent(std::move(stream), ev.GetCommittedOffset());
            } else if constexpr (std::is_same_v<TEventType, NFederatedTopic::TReadSessionEvent::TStartPartitionSessionEvent>) {
                auto eventPtr = std::make_shared<TEventType>(std::move(ev));
                auto stream = GetOrCreateStream(eventPtr->GetFederatedPartitionSession());
                auto* adapter = static_cast<TPartitionStreamAdapter*>(stream.Get());
                adapter->SetCreateConfirmCallback([eventPtr](std::optional<ui64> readOffset, std::optional<ui64> commitOffset) {
                    eventPtr->Confirm(readOffset, commitOffset);
                });
                return TReadSessionEvent::TCreatePartitionStreamEvent(std::move(stream), eventPtr->GetCommittedOffset(), eventPtr->GetEndOffset());
            } else if constexpr (std::is_same_v<TEventType, NFederatedTopic::TReadSessionEvent::TStopPartitionSessionEvent>) {
                auto eventPtr = std::make_shared<TEventType>(std::move(ev));
                auto stream = GetOrCreateStream(eventPtr->GetFederatedPartitionSession());
                auto* adapter = static_cast<TPartitionStreamAdapter*>(stream.Get());
                adapter->SetDestroyConfirmCallback([eventPtr]() {
                    eventPtr->Confirm();
                });
                return TReadSessionEvent::TDestroyPartitionStreamEvent(std::move(stream), eventPtr->GetCommittedOffset());
            } else if constexpr (std::is_same_v<TEventType, NFederatedTopic::TReadSessionEvent::TEndPartitionSessionEvent>) {
                auto eventPtr = std::make_shared<TEventType>(std::move(ev));
                auto stream = GetOrCreateStream(eventPtr->GetFederatedPartitionSession());
                auto* adapter = static_cast<TPartitionStreamAdapter*>(stream.Get());
                adapter->SetDestroyConfirmCallback([eventPtr]() {
                    eventPtr->Confirm();
                });
                return TReadSessionEvent::TDestroyPartitionStreamEvent(std::move(stream), 0);
            } else if constexpr (std::is_same_v<TEventType, NFederatedTopic::TReadSessionEvent::TPartitionSessionStatusEvent>) {
                auto stream = GetOrCreateStream(ev.GetFederatedPartitionSession());
                return TReadSessionEvent::TPartitionStreamStatusEvent(std::move(stream), ev.GetCommittedOffset(), ev.GetReadOffset(),
                                                                     ev.GetEndOffset(), ev.GetWriteTimeHighWatermark());
            } else if constexpr (std::is_same_v<TEventType, NFederatedTopic::TReadSessionEvent::TPartitionSessionClosedEvent>) {
                auto stream = GetOrCreateStream(ev.GetFederatedPartitionSession());
                return TReadSessionEvent::TPartitionStreamClosedEvent(std::move(stream), ConvertReason(ev.GetReason()));
            } else if constexpr (std::is_same_v<TEventType, TSessionClosedEvent>) {
                return ev;
            } else {
                return TSessionClosedEvent(EStatus::ABORTED, NYdb::NIssue::TIssues{NYdb::NIssue::TIssue{"Unsupported event"}});
            }
        }, std::move(event));
    }

private:
    TReadSessionEvent::TDataReceivedEvent ConvertDataReceivedEvent(NFederatedTopic::TReadSessionEvent::TDataReceivedEvent&& event) {
        auto stream = GetOrCreateStream(event.GetFederatedPartitionSession());
        std::vector<TReadSessionEvent::TDataReceivedEvent::TMessage> messages;
        std::vector<TReadSessionEvent::TDataReceivedEvent::TCompressedMessage> compressed;

        if (event.HasCompressedMessages()) {
            auto& topicCompressed = event.GetCompressedMessages();
            compressed.reserve(topicCompressed.size());
            for (auto& msg : topicCompressed) {
                std::vector<TReadSessionEvent::TDataReceivedEvent::TMessageInformation> infos;
                infos.emplace_back(
                    msg.GetOffset(),
                    msg.GetMessageGroupId(),
                    msg.GetSeqNo(),
                    msg.GetCreateTime(),
                    msg.GetWriteTime(),
                    std::string{},
                    msg.GetMeta(),
                    msg.GetUncompressedSize()
                );
                compressed.emplace_back(msg.GetCodec(), std::string(msg.GetData()), std::move(infos), stream, std::string{}, std::string{});
            }
        } else {
            auto& topicMessages = event.GetMessages();
            messages.reserve(topicMessages.size());
            for (auto& msg : topicMessages) {
                std::exception_ptr decompressionException;
                std::string data;
                if (msg.HasException()) {
                    try {
                        msg.GetData();
                    } catch (...) {
                        decompressionException = std::current_exception();
                    }
                    data = msg.GetBrokenData();
                } else {
                    data = msg.GetData();
                }

                TReadSessionEvent::TDataReceivedEvent::TMessageInformation info(
                    msg.GetOffset(),
                    msg.GetMessageGroupId(),
                    msg.GetSeqNo(),
                    msg.GetCreateTime(),
                    msg.GetWriteTime(),
                    std::string{},
                    msg.GetMeta(),
                    data.size()
                );
                messages.emplace_back(std::move(data), std::move(decompressionException), info, stream, std::string{}, std::string{});
            }
        }

        return TReadSessionEvent::TDataReceivedEvent(std::move(messages), std::move(compressed), std::move(stream));
    }

    static TReadSessionEvent::TPartitionStreamClosedEvent::EReason ConvertReason(
        NFederatedTopic::TReadSessionEvent::TPartitionSessionClosedEvent::EReason reason) {
        switch (reason) {
            case NFederatedTopic::TReadSessionEvent::TPartitionSessionClosedEvent::EReason::StopConfirmedByUser:
                return TReadSessionEvent::TPartitionStreamClosedEvent::EReason::DestroyConfirmedByUser;
            case NFederatedTopic::TReadSessionEvent::TPartitionSessionClosedEvent::EReason::ConnectionLost:
                return TReadSessionEvent::TPartitionStreamClosedEvent::EReason::ConnectionLost;
            case NFederatedTopic::TReadSessionEvent::TPartitionSessionClosedEvent::EReason::Lost:
            default:
                return TReadSessionEvent::TPartitionStreamClosedEvent::EReason::Lost;
        }
    }

    TPartitionStream::TPtr GetOrCreateStream(const NFederatedTopic::TFederatedPartitionSession::TPtr& session) {
        const ui64 sessionId = session->GetPartitionSessionId();
        std::lock_guard guard(Mutex_);
        auto it = Streams_.find(sessionId);
        if (it != Streams_.end()) {
            return it->second;
        }
        auto stream = MakeIntrusive<TPartitionStreamAdapter>(session);
        Streams_.emplace(sessionId, stream);
        return stream;
    }

private:
    std::mutex Mutex_;
    std::unordered_map<ui64, TPartitionStream::TPtr> Streams_;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TReadSessionAdapter

class TReadSessionAdapter final : public IReadSession {
public:
    TReadSessionAdapter(std::shared_ptr<NFederatedTopic::IFederatedReadSession> session, std::shared_ptr<TReadEventConverter> converter)
        : Session_(std::move(session))
        , Converter_(std::move(converter))
    {
    }

    NThreading::TFuture<void> WaitEvent() override {
        return Session_->WaitEvent();
    }

    std::vector<TReadSessionEvent::TEvent> GetEvents(bool block, std::optional<size_t> maxEventsCount, size_t maxByteSize) override {
        std::vector<TReadSessionEvent::TEvent> result;
        DrainBuffered(result, maxEventsCount);
        if (maxEventsCount && result.size() >= *maxEventsCount) {
            return result;
        }

        std::optional<size_t> remainingCount = maxEventsCount;
        if (remainingCount) {
            *remainingCount -= result.size();
        }
        auto topicEvents = Session_->GetEvents(block, remainingCount, maxByteSize);
        for (auto& event : topicEvents) {
            if (DataReadingSuspended_ && IsDataEvent(event)) {
                BufferDataEvent(std::move(event));
                continue;
            }
            result.emplace_back(Converter_->ConvertEvent(std::move(event)));
            if (maxEventsCount && result.size() >= *maxEventsCount) {
                break;
            }
        }
        return result;
    }

    std::optional<TReadSessionEvent::TEvent> GetEvent(bool block, size_t maxByteSize) override {
        {
            std::optional<TReadSessionEvent::TEvent> buffered = TakeBuffered();
            if (buffered) {
                return buffered;
            }
        }

        while (true) {
            auto event = Session_->GetEvent(block, maxByteSize);
            if (!event) {
                return std::nullopt;
            }
            if (DataReadingSuspended_ && IsDataEvent(*event)) {
                BufferDataEvent(std::move(*event));
                continue;
            }
            return Converter_->ConvertEvent(std::move(*event));
        }
    }

    void StopReadingData() override {
        DataReadingSuspended_ = true;
    }

    void ResumeReadingData() override {
        DataReadingSuspended_ = false;
    }

    bool Close(TDuration timeout) override {
        return Session_->Close(timeout);
    }

    TReaderCounters::TPtr GetCounters() const override {
        return Session_->GetCounters();
    }

    std::string GetSessionId() const override {
        return Session_->GetSessionId();
    }

private:
    bool IsDataEvent(const NFederatedTopic::TReadSessionEvent::TEvent& event) const {
        return std::holds_alternative<NFederatedTopic::TReadSessionEvent::TDataReceivedEvent>(event);
    }

    void BufferDataEvent(NFederatedTopic::TReadSessionEvent::TEvent&& event) {
        std::lock_guard guard(BufferMutex_);
        BufferedDataEvents_.push_back(std::move(event));
    }

    std::optional<TReadSessionEvent::TEvent> TakeBuffered() {
        if (DataReadingSuspended_) {
            return std::nullopt;
        }
        std::lock_guard guard(BufferMutex_);
        if (BufferedDataEvents_.empty()) {
            return std::nullopt;
        }
        auto event = Converter_->ConvertEvent(std::move(BufferedDataEvents_.front()));
        BufferedDataEvents_.pop_front();
        return event;
    }

    void DrainBuffered(std::vector<TReadSessionEvent::TEvent>& out, std::optional<size_t> maxEventsCount) {
        if (DataReadingSuspended_) {
            return;
        }
        std::lock_guard guard(BufferMutex_);
        while (!BufferedDataEvents_.empty()) {
            out.emplace_back(Converter_->ConvertEvent(std::move(BufferedDataEvents_.front())));
            BufferedDataEvents_.pop_front();
            if (maxEventsCount && out.size() >= *maxEventsCount) {
                break;
            }
        }
    }

private:
    std::shared_ptr<NFederatedTopic::IFederatedReadSession> Session_;
    std::shared_ptr<TReadEventConverter> Converter_;
    bool DataReadingSuspended_ = false;
    std::mutex BufferMutex_;
    std::deque<NFederatedTopic::TReadSessionEvent::TEvent> BufferedDataEvents_;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Public factory

std::shared_ptr<IReadSession> CreateReadSessionAdapter(NFederatedTopic::TFederatedTopicClient& client, const TReadSessionSettings& settings) {
    auto converter = std::make_shared<TReadEventConverter>();
    auto topicSettings = ConvertReadSessionSettings(settings);
    NFederatedTopic::TFederatedReadSessionSettings fedSettings;
    static_cast<NTopic::TReadSessionSettings&>(fedSettings) = std::move(topicSettings);

    fedSettings.FederatedEventHandlers_.MaxMessagesBytes(settings.EventHandlers_.MaxMessagesBytes_);

    if (settings.EventHandlers_.HandlersExecutor_) {
        fedSettings.FederatedEventHandlers_.HandlersExecutor(settings.EventHandlers_.HandlersExecutor_);
    }

    const auto& handlers = settings.EventHandlers_;
    if (handlers.DataReceivedHandler_ || handlers.CommonHandler_) {
        fedSettings.FederatedEventHandlers_.DataReceivedHandler([converter, handlers](NFederatedTopic::TReadSessionEvent::TDataReceivedEvent& event) mutable {
            auto pqEvent = converter->ConvertEvent(std::move(event));
            if (handlers.DataReceivedHandler_) {
                handlers.DataReceivedHandler_(std::get<TReadSessionEvent::TDataReceivedEvent>(pqEvent));
            } else if (handlers.CommonHandler_) {
                handlers.CommonHandler_(pqEvent);
            }
        });
    }
    if (handlers.CommitAcknowledgementHandler_ || handlers.CommonHandler_) {
        fedSettings.FederatedEventHandlers_.CommitOffsetAcknowledgementHandler(
            [converter, handlers](NFederatedTopic::TReadSessionEvent::TCommitOffsetAcknowledgementEvent& event) mutable {
                auto pqEvent = converter->ConvertEvent(std::move(event));
                if (handlers.CommitAcknowledgementHandler_) {
                    handlers.CommitAcknowledgementHandler_(std::get<TReadSessionEvent::TCommitAcknowledgementEvent>(pqEvent));
                } else if (handlers.CommonHandler_) {
                    handlers.CommonHandler_(pqEvent);
                }
            });
    }
    if (handlers.CreatePartitionStreamHandler_ || handlers.CommonHandler_) {
        fedSettings.FederatedEventHandlers_.StartPartitionSessionHandler(
            [converter, handlers](NFederatedTopic::TReadSessionEvent::TStartPartitionSessionEvent& event) mutable {
                auto pqEvent = converter->ConvertEvent(std::move(event));
                if (handlers.CreatePartitionStreamHandler_) {
                    handlers.CreatePartitionStreamHandler_(std::get<TReadSessionEvent::TCreatePartitionStreamEvent>(pqEvent));
                } else if (handlers.CommonHandler_) {
                    handlers.CommonHandler_(pqEvent);
                }
            });
    }
    if (handlers.DestroyPartitionStreamHandler_ || handlers.CommonHandler_) {
        fedSettings.FederatedEventHandlers_.StopPartitionSessionHandler(
            [converter, handlers](NFederatedTopic::TReadSessionEvent::TStopPartitionSessionEvent& event) mutable {
                auto pqEvent = converter->ConvertEvent(std::move(event));
                if (handlers.DestroyPartitionStreamHandler_) {
                    handlers.DestroyPartitionStreamHandler_(std::get<TReadSessionEvent::TDestroyPartitionStreamEvent>(pqEvent));
                } else if (handlers.CommonHandler_) {
                    handlers.CommonHandler_(pqEvent);
                }
            });
        fedSettings.FederatedEventHandlers_.EndPartitionSessionHandler(
            [converter, handlers](NFederatedTopic::TReadSessionEvent::TEndPartitionSessionEvent& event) mutable {
                auto pqEvent = converter->ConvertEvent(std::move(event));
                if (handlers.DestroyPartitionStreamHandler_) {
                    handlers.DestroyPartitionStreamHandler_(std::get<TReadSessionEvent::TDestroyPartitionStreamEvent>(pqEvent));
                } else if (handlers.CommonHandler_) {
                    handlers.CommonHandler_(pqEvent);
                }
            });
    }
    if (handlers.PartitionStreamStatusHandler_ || handlers.CommonHandler_) {
        fedSettings.FederatedEventHandlers_.PartitionSessionStatusHandler(
            [converter, handlers](NFederatedTopic::TReadSessionEvent::TPartitionSessionStatusEvent& event) mutable {
                auto pqEvent = converter->ConvertEvent(std::move(event));
                if (handlers.PartitionStreamStatusHandler_) {
                    handlers.PartitionStreamStatusHandler_(std::get<TReadSessionEvent::TPartitionStreamStatusEvent>(pqEvent));
                } else if (handlers.CommonHandler_) {
                    handlers.CommonHandler_(pqEvent);
                }
            });
    }
    if (handlers.PartitionStreamClosedHandler_ || handlers.CommonHandler_) {
        fedSettings.FederatedEventHandlers_.PartitionSessionClosedHandler(
            [converter, handlers](NFederatedTopic::TReadSessionEvent::TPartitionSessionClosedEvent& event) mutable {
                auto pqEvent = converter->ConvertEvent(std::move(event));
                if (handlers.PartitionStreamClosedHandler_) {
                    handlers.PartitionStreamClosedHandler_(std::get<TReadSessionEvent::TPartitionStreamClosedEvent>(pqEvent));
                } else if (handlers.CommonHandler_) {
                    handlers.CommonHandler_(pqEvent);
                }
            });
    }
    if (handlers.SessionClosedHandler_ || handlers.CommonHandler_) {
        fedSettings.FederatedEventHandlers_.SessionClosedHandler([converter, handlers](const TSessionClosedEvent& event) mutable {
            if (handlers.SessionClosedHandler_) {
                handlers.SessionClosedHandler_(event);
            }
            if (handlers.CommonHandler_) {
                TReadSessionEvent::TEvent wrapperEvent = event;
                handlers.CommonHandler_(wrapperEvent);
            }
        });
    }

    auto session = client.CreateReadSession(fedSettings);
    return std::make_shared<TReadSessionAdapter>(std::move(session), std::move(converter));
}

} // namespace NYdb::NPersQueue::NOverTopic

namespace NYdb::inline Dev::NPersQueue {

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers

static std::pair<uint64_t, uint64_t> GetMessageOffsetRange(const TReadSessionEvent::TDataReceivedEvent& dataReceivedEvent, uint64_t index) {
    if (dataReceivedEvent.IsCompressedMessages()) {
        const auto& msg = dataReceivedEvent.GetCompressedMessages()[index];
        return {msg.GetOffset(0), msg.GetOffset(msg.GetBlocksCount() - 1) + 1};
    }
    const auto& msg = dataReceivedEvent.GetMessages()[index];
    return {msg.GetOffset(), msg.GetOffset() + 1};
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NPersQueue::TReadSessionEvent

TReadSessionEvent::TCreatePartitionStreamEvent::TCreatePartitionStreamEvent(TPartitionStream::TPtr partitionStream, ui64 committedOffset, ui64 endOffset)
    : PartitionStream(std::move(partitionStream))
    , CommittedOffset(committedOffset)
    , EndOffset(endOffset)
{
}

void TReadSessionEvent::TCreatePartitionStreamEvent::Confirm(std::optional<ui64> readOffset, std::optional<ui64> commitOffset) {
    if (PartitionStream) {
        static_cast<NOverTopic::TPartitionStreamAdapter*>(PartitionStream.Get())->ConfirmCreate(readOffset, commitOffset);
    }
}

TReadSessionEvent::TDestroyPartitionStreamEvent::TDestroyPartitionStreamEvent(TPartitionStream::TPtr partitionStream, ui64 committedOffset)
    : PartitionStream(std::move(partitionStream))
    , CommittedOffset(committedOffset)
{
}

void TReadSessionEvent::TDestroyPartitionStreamEvent::Confirm() {
    if (PartitionStream) {
        static_cast<NOverTopic::TPartitionStreamAdapter*>(PartitionStream.Get())->ConfirmDestroy();
    }
}

TReadSessionEvent::TPartitionStreamClosedEvent::TPartitionStreamClosedEvent(TPartitionStream::TPtr partitionStream, EReason reason)
    : PartitionStream(std::move(partitionStream))
    , Reason(reason)
{
}

TReadSessionEvent::TDataReceivedEvent::TDataReceivedEvent(std::vector<TMessage> messages,
                                                          std::vector<TCompressedMessage> compressedMessages,
                                                          TPartitionStream::TPtr partitionStream)
    : Messages(std::move(messages))
    , CompressedMessages(std::move(compressedMessages))
    , PartitionStream(std::move(partitionStream))
{
    for (size_t i = 0; i < GetMessagesCount(); ++i) {
        auto [from, to] = GetMessageOffsetRange(*this, i);
        if (OffsetRanges.empty() || OffsetRanges.back().second != from) {
            OffsetRanges.emplace_back(from, to);
        } else {
            OffsetRanges.back().second = to;
        }
    }
}

void TReadSessionEvent::TDataReceivedEvent::Commit() {
    for (auto [from, to] : OffsetRanges) {
        static_cast<NOverTopic::TPartitionStreamAdapter*>(PartitionStream.Get())->Commit(from, to);
    }
}

TReadSessionEvent::TCommitAcknowledgementEvent::TCommitAcknowledgementEvent(TPartitionStream::TPtr partitionStream, ui64 committedOffset)
    : PartitionStream(std::move(partitionStream))
    , CommittedOffset(committedOffset)
{
}

std::string DebugString(const TReadSessionEvent::TEvent& event) {
    return std::visit([](const auto& ev) { return ev.DebugString(); }, event);
}

std::string TReadSessionEvent::TDataReceivedEvent::DebugString(bool printData) const {
    TStringBuilder ret;
    ret << "DataReceived { PartitionStreamId: " << GetPartitionStream()->GetPartitionStreamId()
        << " PartitionId: " << GetPartitionStream()->GetPartitionId();
    for (const auto& message : Messages) {
        ret << " ";
        message.DebugString(ret, printData);
    }
    for (const auto& message : CompressedMessages) {
        ret << " ";
        message.DebugString(ret, printData);
    }
    ret << " }";
    return std::move(ret);
}

std::string TReadSessionEvent::TCommitAcknowledgementEvent::DebugString() const {
    return TStringBuilder() << "CommitAcknowledgement { PartitionStreamId: " << GetPartitionStream()->GetPartitionStreamId()
                            << " PartitionId: " << GetPartitionStream()->GetPartitionId()
                            << " CommittedOffset: " << GetCommittedOffset()
                            << " }";
}

std::string TReadSessionEvent::TCreatePartitionStreamEvent::DebugString() const {
    return TStringBuilder() << "CreatePartitionStream { PartitionStreamId: " << GetPartitionStream()->GetPartitionStreamId()
                            << " PartitionId: " << GetPartitionStream()->GetPartitionId()
                            << " CommittedOffset: " << GetCommittedOffset()
                            << " EndOffset: " << GetEndOffset()
                            << " }";
}

std::string TReadSessionEvent::TDestroyPartitionStreamEvent::DebugString() const {
    return TStringBuilder() << "DestroyPartitionStream { PartitionStreamId: " << GetPartitionStream()->GetPartitionStreamId()
                            << " PartitionId: " << GetPartitionStream()->GetPartitionId()
                            << " CommittedOffset: " << GetCommittedOffset()
                            << " }";
}

std::string TReadSessionEvent::TPartitionStreamStatusEvent::DebugString() const {
    return TStringBuilder() << "PartitionStreamStatus { PartitionStreamId: " << GetPartitionStream()->GetPartitionStreamId()
                            << " PartitionId: " << GetPartitionStream()->GetPartitionId()
                            << " CommittedOffset: " << GetCommittedOffset()
                            << " ReadOffset: " << GetReadOffset()
                            << " EndOffset: " << GetEndOffset()
                            << " WriteWatermark: " << GetWriteWatermark()
                            << " }";
}

std::string TReadSessionEvent::TPartitionStreamClosedEvent::DebugString() const {
    return TStringBuilder() << "PartitionStreamClosed { PartitionStreamId: " << GetPartitionStream()->GetPartitionStreamId()
                            << " PartitionId: " << GetPartitionStream()->GetPartitionId()
                            << " Reason: " << GetReason()
                            << " }";
}

TReadSessionEvent::TPartitionStreamStatusEvent::TPartitionStreamStatusEvent(TPartitionStream::TPtr partitionStream, ui64 committedOffset, ui64 readOffset, ui64 endOffset, TInstant writeWatermark)
    : PartitionStream(std::move(partitionStream))
    , CommittedOffset(committedOffset)
    , ReadOffset(readOffset)
    , EndOffset(endOffset)
    , WriteWatermark(writeWatermark)
{
}

class TGracefulReleasingSimpleDataHandlers : public TThrRefBase {
public:
    explicit TGracefulReleasingSimpleDataHandlers(std::function<void(TReadSessionEvent::TDataReceivedEvent&)> dataHandler, bool commitAfterProcessing)
        : DataHandler(std::move(dataHandler))
        , CommitAfterProcessing(commitAfterProcessing)
    {
    }

    void OnDataReceived(TReadSessionEvent::TDataReceivedEvent& event) {
        Y_ASSERT(event.GetMessagesCount());
        TDeferredCommit deferredCommit;
        {
            std::lock_guard guard(Lock);
            auto& offsetSet = PartitionStreamToUncommittedOffsets[event.GetPartitionStream()->GetPartitionStreamId()];
            auto firstMessageOffsets = GetMessageOffsetRange(event, 0);
            auto lastMessageOffsets = GetMessageOffsetRange(event, event.GetMessagesCount() - 1);

            offsetSet.InsertInterval(firstMessageOffsets.first, lastMessageOffsets.second);

            if (CommitAfterProcessing) {
                deferredCommit.Add(event);
            }
        }
        DataHandler(event);
        deferredCommit.Commit();
    }

    void OnCommitAcknowledgement(TReadSessionEvent::TCommitAcknowledgementEvent& event) {
        std::lock_guard guard(Lock);
        const ui64 partitionStreamId = event.GetPartitionStream()->GetPartitionStreamId();
        auto& offsetSet = PartitionStreamToUncommittedOffsets[partitionStreamId];
        if (offsetSet.EraseInterval(0, event.GetCommittedOffset() + 1)) {
            if (offsetSet.Empty()) {
                auto unconfirmedDestroyIt = UnconfirmedDestroys.find(partitionStreamId);
                if (unconfirmedDestroyIt != UnconfirmedDestroys.end()) {
                    unconfirmedDestroyIt->second.Confirm();
                    UnconfirmedDestroys.erase(unconfirmedDestroyIt);
                    PartitionStreamToUncommittedOffsets.erase(partitionStreamId);
                }
            }
        }
    }

    void OnCreatePartitionStream(TReadSessionEvent::TCreatePartitionStreamEvent& event) {
        {
            std::lock_guard guard(Lock);
            Y_ABORT_UNLESS(PartitionStreamToUncommittedOffsets[event.GetPartitionStream()->GetPartitionStreamId()].Empty());
        }
        event.Confirm();
    }

    void OnDestroyPartitionStream(TReadSessionEvent::TDestroyPartitionStreamEvent& event) {
        std::lock_guard guard(Lock);
        const ui64 partitionStreamId = event.GetPartitionStream()->GetPartitionStreamId();
        Y_ABORT_UNLESS(UnconfirmedDestroys.find(partitionStreamId) == UnconfirmedDestroys.end());
        if (PartitionStreamToUncommittedOffsets[partitionStreamId].Empty()) {
            PartitionStreamToUncommittedOffsets.erase(partitionStreamId);
            event.Confirm();
        } else {
            UnconfirmedDestroys.emplace(partitionStreamId, std::move(event));
        }
    }

    void OnPartitionStreamClosed(TReadSessionEvent::TPartitionStreamClosedEvent& event) {
        std::lock_guard guard(Lock);
        const ui64 partitionStreamId = event.GetPartitionStream()->GetPartitionStreamId();
        PartitionStreamToUncommittedOffsets.erase(partitionStreamId);
        UnconfirmedDestroys.erase(partitionStreamId);
    }

private:
    TAdaptiveLock Lock;
    const std::function<void(TReadSessionEvent::TDataReceivedEvent&)> DataHandler;
    const bool CommitAfterProcessing;
    std::unordered_map<ui64, TDisjointIntervalTree<ui64>> PartitionStreamToUncommittedOffsets;
    std::unordered_map<ui64, TReadSessionEvent::TDestroyPartitionStreamEvent> UnconfirmedDestroys;
};

TReadSessionSettings::TEventHandlers& TReadSessionSettings::TEventHandlers::SimpleDataHandlers(std::function<void(TReadSessionEvent::TDataReceivedEvent&)> dataHandler,
                                                                                               bool commitDataAfterProcessing,
                                                                                               bool gracefulReleaseAfterCommit) {
    Y_ASSERT(dataHandler);

    PartitionStreamStatusHandler([](TReadSessionEvent::TPartitionStreamStatusEvent&){ });

    if (gracefulReleaseAfterCommit) {
        auto handlers = MakeIntrusive<TGracefulReleasingSimpleDataHandlers>(std::move(dataHandler), commitDataAfterProcessing);
        DataReceivedHandler([handlers](TReadSessionEvent::TDataReceivedEvent& event) {
            handlers->OnDataReceived(event);
        });
        CreatePartitionStreamHandler([handlers](TReadSessionEvent::TCreatePartitionStreamEvent& event) {
            handlers->OnCreatePartitionStream(event);
        });
        DestroyPartitionStreamHandler([handlers](TReadSessionEvent::TDestroyPartitionStreamEvent& event) {
            handlers->OnDestroyPartitionStream(event);
        });
        CommitAcknowledgementHandler([handlers](TReadSessionEvent::TCommitAcknowledgementEvent& event) {
            handlers->OnCommitAcknowledgement(event);
        });
        PartitionStreamClosedHandler([handlers](TReadSessionEvent::TPartitionStreamClosedEvent& event) {
            handlers->OnPartitionStreamClosed(event);
        });
    } else {
        if (commitDataAfterProcessing) {
            DataReceivedHandler([dataHandler = std::move(dataHandler)](TReadSessionEvent::TDataReceivedEvent& event) {
                TDeferredCommit deferredCommit;
                deferredCommit.Add(event);
                dataHandler(event);
                deferredCommit.Commit();
            });
        } else {
            DataReceivedHandler(std::move(dataHandler));
        }
        CreatePartitionStreamHandler([](TReadSessionEvent::TCreatePartitionStreamEvent& event) {
            event.Confirm();
        });
        DestroyPartitionStreamHandler([](TReadSessionEvent::TDestroyPartitionStreamEvent& event) {
            event.Confirm();
        });
        CommitAcknowledgementHandler([](TReadSessionEvent::TCommitAcknowledgementEvent&){ });
        PartitionStreamClosedHandler([](TReadSessionEvent::TPartitionStreamClosedEvent&){ });
    }
    return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TDeferredCommit

class TDeferredCommit::TImpl {
public:
    void Add(const TPartitionStream::TPtr& partitionStream, ui64 startOffset, ui64 endOffset);
    void Add(const TPartitionStream::TPtr& partitionStream, ui64 offset);

    void Add(const TReadSessionEvent::TDataReceivedEvent::TMessage& message);
    void Add(const TReadSessionEvent::TDataReceivedEvent& dataReceivedEvent);

    void Commit();

private:
    static void Add(const TPartitionStream::TPtr& partitionStream, TDisjointIntervalTree<ui64>& offsetSet, ui64 startOffset, ui64 endOffset);

private:
    std::unordered_map<TPartitionStream::TPtr, TDisjointIntervalTree<ui64>, THash<TPartitionStream::TPtr>> Offsets;
};

TDeferredCommit::TDeferredCommit() {
    Impl = std::make_unique<TImpl>();
}

TDeferredCommit::TDeferredCommit(TDeferredCommit&&) = default;

TDeferredCommit& TDeferredCommit::operator=(TDeferredCommit&&) = default;

TDeferredCommit::~TDeferredCommit() = default;

void TDeferredCommit::TImpl::Add(const TPartitionStream::TPtr& partitionStream, ui64 startOffset, ui64 endOffset) {
    Add(partitionStream, Offsets[partitionStream], startOffset, endOffset);
}

void TDeferredCommit::TImpl::Add(const TPartitionStream::TPtr& partitionStream, ui64 offset) {
    Add(partitionStream, Offsets[partitionStream], offset, offset + 1);
}

void TDeferredCommit::TImpl::Add(const TReadSessionEvent::TDataReceivedEvent::TMessage& message) {
    Add(message.GetPartitionStream(), message.GetOffset());
}

void TDeferredCommit::TImpl::Add(const TReadSessionEvent::TDataReceivedEvent& dataReceivedEvent) {
    if (dataReceivedEvent.IsCompressedMessages()) {
        for (const auto& msg : dataReceivedEvent.GetCompressedMessages()) {
            if (msg.GetBlocksCount()) {
                Add(msg.GetPartitionStream(), msg.GetOffset(0), msg.GetOffset(msg.GetBlocksCount() - 1) + 1);
            }
        }
    } else {
        for (const auto& msg : dataReceivedEvent.GetMessages()) {
            Add(msg);
        }
    }
}

void TDeferredCommit::TImpl::Add(const TPartitionStream::TPtr& partitionStream, TDisjointIntervalTree<ui64>& offsetSet, ui64 startOffset, ui64 endOffset) {
    Y_UNUSED(partitionStream);
    Y_ABORT_UNLESS(endOffset > startOffset);
    offsetSet.InsertInterval(startOffset, endOffset);
}

void TDeferredCommit::TImpl::Commit() {
    for (auto& [partitionStream, offsetSet] : Offsets) {
        for (auto range : offsetSet) {
            static_cast<NOverTopic::TPartitionStreamAdapter*>(partitionStream.Get())->Commit(range.first, range.second);
        }
    }
    Offsets.clear();
}

void TDeferredCommit::Add(const TReadSessionEvent::TDataReceivedEvent::TMessage& message) {
    Impl->Add(message);
}

void TDeferredCommit::Add(const TReadSessionEvent::TDataReceivedEvent& dataReceivedEvent) {
    Impl->Add(dataReceivedEvent);
}

void TDeferredCommit::Add(const TPartitionStream::TPtr& partitionStream, ui64 startOffset, ui64 endOffset) {
    Impl->Add(partitionStream, startOffset, endOffset);
}

void TDeferredCommit::Add(const TPartitionStream::TPtr& partitionStream, ui64 offset) {
    Impl->Add(partitionStream, offset);
}

void TDeferredCommit::Commit() {
    Impl->Commit();
}

} // namespace NYdb::NPersQueue
