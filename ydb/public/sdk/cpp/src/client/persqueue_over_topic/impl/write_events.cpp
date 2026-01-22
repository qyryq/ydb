#include <ydb/public/sdk/cpp/src/client/persqueue_over_topic/include/write_events.h>

#include <util/string/builder.h>

namespace NYdb::inline Dev::NPersQueue {

std::string DebugString(const TWriteSessionEvent::TEvent& event) {
    return std::visit([](const auto& ev) { return ev.DebugString(); }, event);
}

std::string TWriteSessionEvent::TAcksEvent::DebugString() const {
    TStringBuilder res;
    res << "AcksEvent:";
    for (const auto& ack : Acks) {
        res << " { seqNo : " << ack.SeqNo << ", State : " << ack.State;
        if (ack.Details) {
            res << ", offset : " << ack.Details->Offset << ", partitionId : " << ack.Details->PartitionId;
        }
        res << " }";
    }
    if (!Acks.empty() && Acks.back().Stat) {
        const auto& stat = Acks.back().Stat;
        res << " write stat: Write time " << stat->WriteTime
            << " total time in partition queue " << stat->TotalTimeInPartitionQueue
            << " partition quoted time " << stat->PartitionQuotedTime
            << " topic quoted time " << stat->TopicQuotedTime;
    }
    return res;
}

std::string TWriteSessionEvent::TReadyToAcceptEvent::DebugString() const {
    return "ReadyToAcceptEvent";
}

} // namespace NYdb::NPersQueue
