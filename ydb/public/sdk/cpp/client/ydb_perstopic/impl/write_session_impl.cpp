#include "util/generic/overloaded.h"
#include "write_session.h"
#include <ydb/public/sdk/cpp/client/ydb_perstopic/impl/log_lazy.h>

#include <ydb/public/sdk/cpp/client/ydb_perstopic/persqueue.h>
#include <library/cpp/string_utils/url/url.h>

#include <util/generic/store_policy.h>
#include <util/generic/utility.h>
#include <util/stream/buffer.h>


namespace NYdb::NPQTopic {
using ::NMonitoring::TDynamicCounterPtr;
using TCounterPtr = ::NMonitoring::TDynamicCounters::TCounterPtr;


namespace NCompressionDetails {
    THolder<IOutputStream> CreateCoder(ECodec codec, TBuffer& result, int quality);
}

#define HISTOGRAM_SETUP ::NMonitoring::ExplicitHistogram({0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100})
TWriterCounters::TWriterCounters(const TIntrusivePtr<::NMonitoring::TDynamicCounters>& counters) {
    Errors = counters->GetCounter("errors", true);
    CurrentSessionLifetimeMs = counters->GetCounter("currentSessionLifetimeMs", false);
    BytesWritten = counters->GetCounter("bytesWritten", true);
    MessagesWritten = counters->GetCounter("messagesWritten", true);
    BytesWrittenCompressed = counters->GetCounter("bytesWrittenCompressed", true);
    BytesInflightUncompressed = counters->GetCounter("bytesInflightUncompressed", false);
    BytesInflightCompressed = counters->GetCounter("bytesInflightCompressed", false);
    BytesInflightTotal = counters->GetCounter("bytesInflightTotal", false);
    MessagesInflight = counters->GetCounter("messagesInflight", false);

    TotalBytesInflightUsageByTime = counters->GetHistogram("totalBytesInflightUsageByTime", HISTOGRAM_SETUP);
    UncompressedBytesInflightUsageByTime = counters->GetHistogram("uncompressedBytesInflightUsageByTime", HISTOGRAM_SETUP);
    CompressedBytesInflightUsageByTime = counters->GetHistogram("compressedBytesInflightUsageByTime", HISTOGRAM_SETUP);
}
#undef HISTOGRAM_SETUP

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
    fedSettings.DirectWriteToPartition(true);
    return fedSettings;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TWriteSessionImpl

TWriteSessionImpl::TWriteSessionImpl(
        const TWriteSessionSettings& settings,
         std::shared_ptr<TPersQueueClient::TImpl> client,
         std::shared_ptr<TGRpcConnectionsImpl> connections,
         TDbDriverStatePtr dbDriverState)
    : Settings(settings)
    , Client(std::move(client))
    , FederatedTopicClient(Client->GetFederatedTopicClient())
    , Connections(std::move(connections))
    , DbDriverState(std::move(dbDriverState))
    , PrevToken(DbDriverState->CredentialsProvider ? DbDriverState->CredentialsProvider->GetAuthInfo() : "")
    , InitSeqNoPromise(NThreading::NewPromise<ui64>())
    , WakeupInterval(
            Settings.BatchFlushInterval_.GetOrElse(TDuration::Zero()) ?
                std::min(Settings.BatchFlushInterval_.GetOrElse(TDuration::Seconds(1)) / 5, TDuration::MilliSeconds(100))
                :
                TDuration::MilliSeconds(100)
    )
{
    if (!Settings.RetryPolicy_) {
        Settings.RetryPolicy_ = IRetryPolicy::GetDefaultPolicy();
    }

    // TODO(qyryq) Safe to delete.
    if (Settings.PreferredCluster_ && !Settings.AllowFallbackToOtherClusters_) {
        TargetCluster = *Settings.PreferredCluster_;
        TargetCluster.to_lower();
    }
    if (Settings.Counters_.Defined()) {
        Counters = *Settings.Counters_;
    } else {
        Counters = MakeIntrusive<TWriterCounters>(new ::NMonitoring::TDynamicCounters());
    }

    FederatedWriteSessionSettings = ConvertToFederatedWriteSessionSettings(Settings);
    FederatedWriteSession = FederatedTopicClient->CreateWriteSession(FederatedWriteSessionSettings);
}

// Client method
NThreading::TFuture<ui64> TWriteSessionImpl::GetInitSeqNo() {
    return FederatedWriteSession->GetInitSeqNo();
}

TString DebugString(const TWriteSessionEvent::TEvent& event) {
    return std::visit([](const auto& ev) { return ev.DebugString(); }, event);
}

TWriteSessionEvent::TEvent TWriteSessionImpl::ConvertEvent(NTopic::TWriteSessionEvent::TEvent& event) {
    TWriteSessionEvent::TEvent convertedEvent;
    std::visit(TOverloaded {
        [&](NTopic::TWriteSessionEvent::TAcksEvent const& event) {
            TWriteSessionEvent::TAcksEvent converted;
            for (auto const& ack : event.Acks) {
                converted.Acks.push_back(ConvertAck(ack));
            }
            convertedEvent = converted;
        },
        [&](NTopic::TWriteSessionEvent::TReadyToAcceptEvent& event) {
            FederationContinuationTokens.push_back(std::move(event.ContinuationToken));
            convertedEvent = TWriteSessionEvent::TReadyToAcceptEvent{IssueContinuationToken()};
        },
        [&](NTopic::TSessionClosedEvent const& event) {
            auto issues = event.GetIssues();
            convertedEvent = TSessionClosedEvent(event.GetStatus(), std::move(issues));
        }
    }, event);
    return convertedEvent;
}

// Client method
TMaybe<TWriteSessionEvent::TEvent> TWriteSessionImpl::GetEvent(bool block) {
    auto ev = FederatedWriteSession->GetEvent(block);
    if (!ev) {
        return {};
    }
    return ConvertEvent(*ev);
}

// Client method
TVector<TWriteSessionEvent::TEvent> TWriteSessionImpl::GetEvents(bool block, TMaybe<size_t> maxEventsCount) {
    auto events = FederatedWriteSession->GetEvents(block, maxEventsCount);
    TVector<TWriteSessionEvent::TEvent> converted;
    for (auto& e : events) {
        converted.push_back(ConvertEvent(e));
    }
    return converted;
}

TWriteSessionEvent::TWriteAck TWriteSessionImpl::ConvertAck(NTopic::TWriteSessionEvent::TWriteAck const& ack) const {
    TWriteSessionEvent::TWriteAck converted;
    converted.SeqNo = ack.SeqNo;
    switch (ack.State) {
    case NTopic::TWriteSessionEvent::TWriteAck::EES_WRITTEN:
        converted.State = TWriteSessionEvent::TWriteAck::EES_WRITTEN;
        if (ack.Details) {
            converted.Details = TWriteSessionEvent::TWriteAck::TWrittenMessageDetails{
                .Offset = ack.Details->Offset,
                .PartitionId = ack.Details->PartitionId,
            };
        }
        break;
    case NTopic::TWriteSessionEvent::TWriteAck::EES_ALREADY_WRITTEN:
        converted.State = TWriteSessionEvent::TWriteAck::EES_ALREADY_WRITTEN;
        break;
    case NTopic::TWriteSessionEvent::TWriteAck::EES_DISCARDED:
        converted.State = TWriteSessionEvent::TWriteAck::EES_DISCARDED;
        break;
    }
    if (ack.Stat) {
        converted.Stat = MakeIntrusive<TWriteStat>();
        converted.Stat->WriteTime = ack.Stat->WriteTime;
        converted.Stat->TotalTimeInPartitionQueue = ack.Stat->MaxTimeInPartitionQueue;
        converted.Stat->PartitionQuotedTime = ack.Stat->PartitionQuotedTime;
        converted.Stat->TopicQuotedTime = ack.Stat->TopicQuotedTime;
    }
    return converted;
}

NThreading::TFuture<void> TWriteSessionImpl::WaitEvent() {
    return FederatedWriteSession->WaitEvent();
}

NTopic::ECodec ConvertCodecEnum(ECodec codec) {
    switch (codec) {
    case ECodec::RAW:
        return NTopic::ECodec::RAW;
    case ECodec::GZIP:
        return NTopic::ECodec::GZIP;
    case ECodec::LZOP:
        return NTopic::ECodec::LZOP;
    case ECodec::ZSTD:
        return NTopic::ECodec::ZSTD;
    }
}

// Client method.
void TWriteSessionImpl::WriteEncoded(
            TContinuationToken&&, TStringBuf data, ECodec codec, ui32 originalSize, TMaybe<ui64> seqNo, TMaybe<TInstant> createTimestamp
        ) {
    auto token = std::move(FederationContinuationTokens.back());
    FederationContinuationTokens.pop_back();
    FederatedWriteSession->WriteEncoded(std::move(token), data, ConvertCodecEnum(codec), originalSize, seqNo, createTimestamp);
}

void TWriteSessionImpl::Write(TContinuationToken&&, TStringBuf data, TMaybe<ui64> seqNo, TMaybe<TInstant> createTimestamp) {
    auto msg = NTopic::TWriteMessage(data);
    msg.SeqNo(seqNo);
    msg.CreateTimestamp(createTimestamp);
    auto token = std::move(FederationContinuationTokens.back());
    FederationContinuationTokens.pop_back();
    FederatedWriteSession->Write(std::move(token), std::move(msg));
}

TStringBuilder TWriteSessionImpl::LogPrefix() const {
    return TStringBuilder() << "MessageGroupId [" << Settings.MessageGroupId_ << "] SessionId [" << SessionId << "] ";
}

TString TWriteSessionEvent::TAcksEvent::DebugString() const {
    TStringBuilder res;
    res << "AcksEvent:";
    for (auto& ack : Acks) {
        res << " { seqNo : " << ack.SeqNo << ", State : " << ack.State;
        if (ack.Details) {
            res << ", offset : " << ack.Details->Offset << ", partitionId : " << ack.Details->PartitionId;
        }
        res << " }";
    }
    if (!Acks.empty() && Acks.back().Stat) {
        auto& stat = Acks.back().Stat;
        res << " write stat: Write time " << stat->WriteTime << " total time in partition queue " << stat->TotalTimeInPartitionQueue
            << " partition quoted time " << stat->PartitionQuotedTime << " topic quoted time " << stat->TopicQuotedTime;
    }
    return res;
}

TString TWriteSessionEvent::TReadyToAcceptEvent::DebugString() const {
    return "ReadyToAcceptEvent";
}

// Client method, no Lock
bool TWriteSessionImpl::Close(TDuration closeTimeout) {
    return FederatedWriteSession->Close(closeTimeout);
}

TWriteSessionImpl::~TWriteSessionImpl() {
    LOG_LAZY(DbDriverState->Log, TLOG_DEBUG, LogPrefix() << "Write session: destroy");
}

}; // namespace NYdb::NPQTopic
