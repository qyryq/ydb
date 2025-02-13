#pragma once

#include "replication.h"

#include <optional>

namespace NKikimr::NReplication::NController {

IActor* CreateStreamCreator(TReplication* replication, ui64 targetId, const TActorContext& ctx);
IActor* CreateStreamCreator(const TActorId& parent, const TActorId& proxy, ui64 rid, ui64 tid,
    TReplication::ETargetKind kind, const TString& srcPath, const TString& dstPath,
    const TString& streamName, const TDuration& streamRetentionPeriod,
    const std::optional<TDuration>& resolvedTimestamps = std::nullopt,
    bool supportsTopicAutopartitioning = false);

}
