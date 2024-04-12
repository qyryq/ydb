#include "util/generic/overloaded.h"
#include "write_session.h"
#include "common.h"

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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TWriteSessionImpl

// Client method
NThreading::TFuture<ui64> TWriteSessionImpl::GetInitSeqNo() {
    return FederatedWriteSession->GetInitSeqNo();
}

NFederatedTopic::TFederatedWriteSessionSettings TWriteSessionImpl::ConvertWriteSessionSettings(TWriteSessionSettings const& pq) {
    NFederatedTopic::TFederatedWriteSessionSettings federated;
    federated.Path(pq.Path_);
    federated.ProducerId(pq.MessageGroupId_);
    federated.MessageGroupId(pq.MessageGroupId_);
    federated.Codec(ConvertCodecEnum(pq.Codec_));
    federated.CompressionLevel(pq.CompressionLevel_);
    federated.MaxMemoryUsage(pq.MaxMemoryUsage_);
    federated.MaxInflightCount(pq.MaxInflightCount_);
    federated.RetryPolicy(pq.RetryPolicy_);
    federated.BatchFlushInterval(pq.BatchFlushInterval_);
    federated.BatchFlushSizeBytes(pq.BatchFlushSizeBytes_);
    federated.ConnectTimeout(pq.ConnectTimeout_);
    federated.Counters(pq.Counters_);
    federated.CompressionExecutor(pq.CompressionExecutor_);
    if (auto h = pq.EventHandlers_.ReadyToAcceptHandler_) {
        federated.EventHandlers_.ReadyToAcceptHandler(h);
    }
    if (auto h = pq.EventHandlers_.AcksHandler_) {
        federated.EventHandlers_.AcksHandler([ctx = SelfContext, h](NTopic::TWriteSessionEvent::TAcksEvent& e) {
            if (auto self = ctx->LockShared()) {
                auto converted = self->ConvertAcksEvent(e);
                h(converted);
            }
        });
    }
    if (auto h = pq.EventHandlers_.CommonHandler_) {
        federated.EventHandlers_.CommonHandler([ctx = SelfContext, h](NTopic::TWriteSessionEvent::TEvent& e) {
            if (auto self = ctx->LockShared()) {
                auto converted = self->ConvertEvent(e);
                h(converted);
            }
        });
    }
    if (auto h = pq.EventHandlers_.SessionClosedHandler_) {
        federated.EventHandlers_.SessionClosedHandler([ctx = SelfContext, h](NTopic::TSessionClosedEvent const& e) {
            if (auto self = ctx->LockShared()) {
                auto converted = self->ConvertSessionClosedEvent(e);
                h(converted);
            }
        });
    }
    federated.ValidateSeqNo(pq.ValidateSeqNo_);
    if (pq.PartitionGroupId_ && pq.PartitionGroupId_ > 0) {
        federated.PartitionId(*pq.PartitionGroupId_ - 1);
    }
    federated.PreferredDatabase(pq.PreferredCluster_);
    federated.AllowFallback(pq.AllowFallbackToOtherClusters_);


    // TODO(qyryq) If pq.ClusterDiscoveryMode == false, then as a topic.

    // TODO(qyryq) Set to true;
    federated.DirectWriteToPartition(false);
    return federated;
}

TWriteSessionEvent::TAcksEvent TWriteSessionImpl::ConvertAcksEvent(NTopic::TWriteSessionEvent::TAcksEvent const& event) {
    TWriteSessionEvent::TAcksEvent converted;
    converted.Acks.reserve(event.Acks.size());
    for (auto const& ack : event.Acks) {
        converted.Acks.push_back(ConvertAck(ack));
    }
    return converted;
}

TSessionClosedEvent TWriteSessionImpl::ConvertSessionClosedEvent(NTopic::TSessionClosedEvent const& event) {
    auto issues = event.GetIssues();
    return TSessionClosedEvent(event.GetStatus(), std::move(issues));
}

TWriteSessionEvent::TEvent TWriteSessionImpl::ConvertEvent(NTopic::TWriteSessionEvent::TEvent& event) {
    if (auto e = std::get_if<NTopic::TWriteSessionEvent::TAcksEvent>(&event)) {
        return ConvertAcksEvent(*e);
    }
    if (auto e = std::get_if<NTopic::TSessionClosedEvent>(&event)) {
        return ConvertSessionClosedEvent(*e);
    }
    if (auto e = std::get_if<NTopic::TWriteSessionEvent::TReadyToAcceptEvent>(&event)) {
        return *e;
    }
    Y_UNREACHABLE();
}

// Client method
TMaybe<TWriteSessionEvent::TEvent> TWriteSessionImpl::GetEvent(bool block) {
    if (auto e = FederatedWriteSession->GetEvent(block)) {
        return ConvertEvent(*e);
    }
    return {};
}

// Client method
TVector<TWriteSessionEvent::TEvent> TWriteSessionImpl::GetEvents(bool block, TMaybe<size_t> maxEventsCount) {
    auto events = FederatedWriteSession->GetEvents(block, maxEventsCount);
    TVector<TWriteSessionEvent::TEvent> converted;
    converted.reserve(events.size());
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

// Client method.
void TWriteSessionImpl::WriteEncoded(
            TContinuationToken&& token, TStringBuf data, ECodec codec, ui32 originalSize, TMaybe<ui64> seqNo, TMaybe<TInstant> createTimestamp
        ) {
    FederatedWriteSession->WriteEncoded(std::move(token), data, ConvertCodecEnum(codec), originalSize, seqNo, createTimestamp);
}

void TWriteSessionImpl::Write(TContinuationToken&& token, TStringBuf data, TMaybe<ui64> seqNo, TMaybe<TInstant> createTimestamp) {
    auto msg = NTopic::TWriteMessage(data);
    msg.SeqNo(seqNo);
    msg.CreateTimestamp(createTimestamp);
    FederatedWriteSession->Write(std::move(token), std::move(msg));
}

// TStringBuilder TWriteSessionImpl::LogPrefix() const {
//     return TStringBuilder() << "MessageGroupId [" << Settings.MessageGroupId_ << "] SessionId [" << SessionId << "] ";
// }

TString DebugString(const TWriteSessionEvent::TEvent& event) {
    return std::visit([](const auto& ev) { return ev.DebugString(); }, event);
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

// Client method, no Lock
bool TWriteSessionImpl::Close(TDuration closeTimeout) {
    return FederatedWriteSession->Close(closeTimeout);
}

// Client method, no Lock
TWriterCounters::TPtr TWriteSessionImpl::GetCounters() {
    return FederatedWriteSession->GetCounters();
}

TWriteSessionImpl::~TWriteSessionImpl() {
    // LOG_LAZY(DbDriverState->Log, TLOG_DEBUG, LogPrefix() << "Write session: destroy");
}

}; // namespace NYdb::NPQTopic
