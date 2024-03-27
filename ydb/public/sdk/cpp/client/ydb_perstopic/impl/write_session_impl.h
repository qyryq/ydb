#pragma once

#include "common.h"
#include "persqueue_impl.h"
#include "ydb/public/sdk/cpp/client/ydb_federated_topic/impl/federated_write_session.h"

#include <ydb/public/sdk/cpp/client/ydb_perstopic/persqueue.h>
#include <ydb/public/sdk/cpp/client/ydb_perstopic/impl/callback_context.h>

#include <util/generic/buffer.h>

namespace NYdb::NPQTopic {

inline const TString& GetCodecId(const ECodec codec) {
    static THashMap<ECodec, TString> idByCodec{
        {ECodec::RAW, TString(1, '\0')},
        {ECodec::GZIP, "\1"},
        {ECodec::LZOP, "\2"},
        {ECodec::ZSTD, "\3"}
    };
    Y_ABORT_UNLESS(idByCodec.contains(codec));
    return idByCodec[codec];
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TWriteSessionEventsQueue

// TODO(qyryq) Delete it?
namespace NTests {
    class TSimpleWriteSessionTestAdapter;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TWriteSessionImpl

class TWriteSessionImpl : public TContinuationTokenIssuer,
                          public TEnableSelfContext<TWriteSessionImpl> {
private:
    friend class TWriteSession;
    friend class TSimpleBlockingWriteSession;
    friend class NTests::TSimpleWriteSessionTestAdapter;

private:
    using TClientMessage = Ydb::PersQueue::V1::StreamingWriteClientMessage;
    using TServerMessage = Ydb::PersQueue::V1::StreamingWriteServerMessage;
    using IWriteSessionConnectionProcessorFactory =
            TPersQueueClient::TImpl::IWriteSessionConnectionProcessorFactory;
    using IProcessor = IWriteSessionConnectionProcessorFactory::IProcessor;

    struct TMessage {
        ui64 Id;
        TInstant CreatedAt;
        TStringBuf DataRef;
        TMaybe<ECodec> Codec;
        ui32 OriginalSize; // only for coded messages
        TMessage(ui64 id, const TInstant& createdAt, TStringBuf data, TMaybe<ECodec> codec = {}, ui32 originalSize = 0)
            : Id(id)
            , CreatedAt(createdAt)
            , DataRef(data)
            , Codec(codec)
            , OriginalSize(originalSize)
        {}
    };

    struct TMessageBatch {
        TBuffer Data;
        TVector<TMessage> Messages;
        ui64 CurrentSize = 0;
        TInstant StartedAt = TInstant::Zero();
        bool Acquired = false;
        bool FlushRequested = false;
        void Add(ui64 id, const TInstant& createdAt, TStringBuf data, TMaybe<ECodec> codec, ui32 originalSize) {
            if (StartedAt == TInstant::Zero())
                StartedAt = TInstant::Now();
            CurrentSize += codec ? originalSize : data.size();
            Messages.emplace_back(id, createdAt, data, codec, originalSize);
            Acquired = false;
        }

        bool HasCodec() const {
            return Messages.empty() ? false : Messages.front().Codec.Defined();
        }

        bool Acquire() {
            if (Acquired || Messages.empty())
                return false;
            auto currSize = Data.size();
            Data.Append(Messages.back().DataRef.data(), Messages.back().DataRef.size());
            Messages.back().DataRef = TStringBuf(Data.data() + currSize, Data.size() - currSize);
            Acquired = true;
            return true;
        }

        bool Empty() const noexcept {
            return CurrentSize == 0 && Messages.empty();
        }

        void Reset() {
            StartedAt = TInstant::Zero();
            Messages.clear();
            Data.Clear();
            Acquired = false;
            CurrentSize = 0;
            FlushRequested = false;
        }
    };

    struct TBlock {
        size_t Offset = 0; //!< First message sequence number in the block
        size_t MessageCount = 0;
        size_t PartNumber = 0;
        size_t OriginalSize = 0;
        size_t OriginalMemoryUsage = 0;
        TString CodecID = GetCodecId(ECodec::RAW);
        mutable TVector<TStringBuf> OriginalDataRefs;
        mutable TBuffer Data;
        bool Compressed = false;
        mutable bool Valid = true;

        TBlock& operator=(TBlock&&) = default;
        TBlock(TBlock&&) = default;
        TBlock() = default;

        //For taking ownership by copying from const object, f.e. lambda -> std::function, priority_queue
        void Move(const TBlock& rhs) {
            Offset = rhs.Offset;
            MessageCount = rhs.MessageCount;
            PartNumber = rhs.PartNumber;
            OriginalSize = rhs.OriginalSize;
            OriginalMemoryUsage = rhs.OriginalMemoryUsage;
            CodecID = rhs.CodecID;
            OriginalDataRefs.swap(rhs.OriginalDataRefs);
            Data.Swap(rhs.Data);
            Compressed = rhs.Compressed;

            rhs.Data.Clear();
            rhs.OriginalDataRefs.clear();
        }
    };

    struct TOriginalMessage {
        ui64 Id;
        TInstant CreatedAt;
        size_t Size;
        TOriginalMessage(const ui64 id, const TInstant createdAt, const size_t size)
                : Id(id)
                , CreatedAt(createdAt)
                , Size(size)
        {}
    };

    //! Block comparer, makes block with smallest offset (first sequence number) appear on top of the PackedMessagesToSend priority queue
    struct Greater {
        bool operator() (const TBlock& lhs, const TBlock& rhs) {
            return lhs.Offset > rhs.Offset;
        }
    };

    struct THandleResult {
        bool DoRestart = false;
        TDuration StartDelay = TDuration::Zero();
        bool DoStop = false;
        bool DoSetSeqNo = false;
    };
    struct TProcessSrvMessageResult {
        THandleResult HandleResult;
        TMaybe<ui64> InitSeqNo;
        TVector<TWriteSessionEvent::TEvent> Events;
        bool Ok = true;
    };

public:
    // TWriteSessionImpl(const TWriteSessionSettings& settings,
    //         std::shared_ptr<TPersQueueClient::TImpl> client);

    TWriteSessionImpl(TWriteSessionSettings settings, std::shared_ptr<NFederatedTopic::TFederatedTopicClient> client)
        : FederatedTopicClient(std::move(client))
        , FederatedWriteSession(FederatedTopicClient->CreateWriteSession(ConvertToFederatedWriteSessionSettings(settings))) {
    }

    TMaybe<TWriteSessionEvent::TEvent> GetEvent(bool block = false);
    TVector<TWriteSessionEvent::TEvent> GetEvents(bool block = false,
                                                  TMaybe<size_t> maxEventsCount = Nothing());
    NThreading::TFuture<ui64> GetInitSeqNo();

    void Write(TContinuationToken&& continuationToken, TStringBuf data,
               TMaybe<ui64> seqNo = Nothing(), TMaybe<TInstant> createTimestamp = Nothing());

    void WriteEncoded(TContinuationToken&& continuationToken, TStringBuf data, ECodec codec, ui32 originalSize,
               TMaybe<ui64> seqNo = Nothing(), TMaybe<TInstant> createTimestamp = Nothing());


    NThreading::TFuture<void> WaitEvent();

    // Empty maybe - block till all work is done. Otherwise block at most at closeTimeout duration.
    bool Close(TDuration closeTimeout = TDuration::Max());

    TWriterCounters::TPtr GetCounters() {Y_ABORT("Unimplemented"); } //ToDo - unimplemented;

    ~TWriteSessionImpl(); // will not call close - destroy everything without acks

private:

    // TStringBuilder LogPrefix() const;

    TWriteSessionEvent::TWriteAck ConvertAck(NTopic::TWriteSessionEvent::TWriteAck const& ack) const;
    TWriteSessionEvent::TEvent ConvertEvent(NTopic::TWriteSessionEvent::TEvent& event);

private:
    std::shared_ptr<NFederatedTopic::TFederatedTopicClient> FederatedTopicClient;
    std::shared_ptr<NTopic::IWriteSession> FederatedWriteSession;
    TString TargetCluster;
    TString InitialCluster;
    TString CurrentCluster;
    size_t ConnectionGeneration = 0;
    size_t ConnectionAttemptsDone = 0;
    TAdaptiveLock Lock;
    IProcessor::TPtr Processor;
    IRetryPolicy::IRetryState::TPtr RetryState; // Current retry state (if now we are (re)connecting).
    std::shared_ptr<TServerMessage> ServerMessage; // Server message to write server response to.

    TString SessionId;
    IExecutor::TPtr Executor;
    IExecutor::TPtr CompressionExecutor;
    size_t MemoryUsage = 0; //!< Estimated amount of memory used
    bool FirstTokenSent = false;

    TMessageBatch CurrentBatch;

    const size_t MaxBlockSize = std::numeric_limits<size_t>::max();
    const size_t MaxBlockMessageCount = 1; //!< Max message count that can be packed into a single block. In block version 0 is equal to 1 for compatibility
    bool Connected = false;
    bool Started = false;
    TAtomic Aborting = 0;
    bool SessionEstablished = false;
    ui32 PartitionId = 0;
    ui64 NextId = 0;
    ui64 MinUnsentId = 1;
    std::map<TString, ui64> InitSeqNo;
    TMaybe<bool> AutoSeqNoMode;
    bool ValidateSeqNoMode = false;

    NThreading::TPromise<ui64> InitSeqNoPromise;
    bool InitSeqNoSetDone = false;
    TInstant SessionStartedTs;
    TInstant LastCountersUpdateTs = TInstant::Zero();
    TInstant LastCountersLogTs;
    TWriterCounters::TPtr Counters;
    TDuration WakeupInterval;

    TString StateStr;

protected:
    ui64 MessagesAcquired = 0;
};

}; // namespace NYdb::NPQTopic
