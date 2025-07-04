#pragma once
#include <ydb/core/tx/columnshard/blobs_action/abstract/blob_set.h>
#include <ydb/core/tx/columnshard/blob.h>
#include <ydb/core/tx/columnshard/common/tablet_id.h>
#include <ydb/core/tx/columnshard/engines/writer/write_controller.h>
#include <ydb/core/tx/columnshard/hooks/abstract/abstract.h>
#include <ydb/core/testlib/basics/runtime.h>
#include <util/string/join.h>

namespace NKikimr::NYDBTest::NColumnShard {

class TReadOnlyController: public ICSController {
private:
    YDB_READONLY(TAtomicCounter, CleanupSchemasFinishedCounter, 0);
    YDB_READONLY(TAtomicCounter, TTLFinishedCounter, 0);
    YDB_READONLY(TAtomicCounter, TTLStartedCounter, 0);
    YDB_READONLY(TAtomicCounter, CompactionFinishedCounter, 0);
    YDB_READONLY(TAtomicCounter, CompactionStartedCounter, 0);
    YDB_READONLY(TAtomicCounter, CleaningFinishedCounter, 0);
    YDB_READONLY(TAtomicCounter, CleaningStartedCounter, 0);

    YDB_READONLY(TAtomicCounter, FilteredRecordsCount, 0);

    YDB_READONLY(TAtomicCounter, HeadersSkippingOnSelect, 0);
    YDB_READONLY(TAtomicCounter, HeadersApprovedOnSelect, 0);
    YDB_READONLY(TAtomicCounter, HeadersSkippedNoData, 0);

    YDB_READONLY(TAtomicCounter, IndexesSkippingOnSelect, 0);
    YDB_READONLY(TAtomicCounter, IndexesApprovedOnSelect, 0);
    YDB_READONLY(TAtomicCounter, IndexesSkippedNoData, 0);

    YDB_READONLY(TAtomicCounter, TieringUpdates, 0);
    YDB_READONLY(TAtomicCounter, NeedActualizationCount, 0);

    YDB_READONLY(TAtomicCounter, ActualizationsCount, 0);
    YDB_READONLY(TAtomicCounter, ActualizationRefreshSchemeCount, 0);
    YDB_READONLY(TAtomicCounter, ActualizationRefreshTieringCount, 0);
    YDB_READONLY(TAtomicCounter, ShardingFiltersCount, 0);

    YDB_READONLY(TAtomicCounter, RequestTracingSnapshotsSave, 0);
    YDB_READONLY(TAtomicCounter, RequestTracingSnapshotsRemove, 0);

    YDB_ACCESSOR(TAtomicCounter, CompactionsLimit, 10000000);

protected:
    virtual void OnRequestTracingChanges(
        const std::set<NOlap::TSnapshot>& snapshotsToSave, const std::set<NOlap::TSnapshot>& snapshotsToRemove) override {
        RequestTracingSnapshotsSave.Add(snapshotsToSave.size());
        RequestTracingSnapshotsRemove.Add(snapshotsToRemove.size());
    }

    virtual void OnSelectShardingFilter() override {
        ShardingFiltersCount.Inc();
    }

    virtual void AddPortionForActualizer(const i32 portionsCount) override {
        NeedActualizationCount.Add(portionsCount);
    }

    virtual void OnPortionActualization(const NOlap::TPortionInfo& /*info*/) override {
        ActualizationsCount.Inc();
    }
    virtual void OnActualizationRefreshScheme() override {
        ActualizationRefreshSchemeCount.Inc();
    }
    virtual void OnActualizationRefreshTiering() override {
        ActualizationRefreshTieringCount.Inc();
    }

    virtual bool DoOnWriteIndexStart(const ui64 tabletId, NOlap::TColumnEngineChanges& change) override;
    virtual bool DoOnAfterFilterAssembling(const std::shared_ptr<arrow::RecordBatch>& batch) override;
    virtual bool DoOnWriteIndexComplete(const NOlap::TColumnEngineChanges& changes, const ::NKikimr::NColumnShard::TColumnShard& shard) override;
    virtual void OnTieringModified(const std::shared_ptr<NKikimr::NColumnShard::TTiersManager>& /*tiers*/) override {
        TieringUpdates.Inc();
    }
    virtual EOptimizerCompactionWeightControl GetCompactionControl() const override {
        return EOptimizerCompactionWeightControl::Force;
    }

    virtual TDuration DoGetOverridenGCPeriod(const TDuration /*def*/) const override {
        return TDuration::Zero();
    }

public:
    bool WaitCompactions(const TDuration d) const {
        TInstant start = TInstant::Now();
        ui32 compactionsStart = GetCompactionStartedCounter().Val();
        ui32 count = 0;
        while (Now() - start < d) {
            if (compactionsStart != GetCompactionStartedCounter().Val()) {
                compactionsStart = GetCompactionStartedCounter().Val();
                start = TInstant::Now();
                ++count;
            }
            Cerr << "WAIT_COMPACTION: " << GetCompactionStartedCounter().Val() << Endl;
            Sleep(std::min(TDuration::Seconds(1), d));
        }
        return count > 0;
    }

    bool WaitCleaning(const TDuration d, NActors::TTestBasicRuntime* testRuntime = nullptr) const {
        TInstant start = TInstant::Now();
        const ui32 countStart0 = GetCleaningStartedCounter().Val();
        ui32 countStart = countStart0;
        while (Now() - start < d) {
            if (countStart != GetCleaningStartedCounter().Val()) {
                countStart = GetCleaningStartedCounter().Val();
                start = TInstant::Now();
            }
            Cerr << "WAIT_CLEANING: " << GetCleaningStartedCounter().Val() << Endl;
            if (testRuntime) {
                testRuntime->SimulateSleep(TDuration::Seconds(1));
            } else {
                Sleep(TDuration::Seconds(1));
            }
        }
        return GetCleaningStartedCounter().Val() != countStart0;
    }

    bool WaitCleaningSchemas(const TDuration d, NActors::TTestBasicRuntime* testRuntime = nullptr) const {
        TInstant start = TInstant::Now();
        const ui32 countStart0 = GetCleanupSchemasFinishedCounter().Val();
        ui32 countStart = countStart0;
        while (Now() - start < d) {
            if (countStart != GetCleanupSchemasFinishedCounter().Val()) {
                countStart = GetCleanupSchemasFinishedCounter().Val();
                start = TInstant::Now();
            }
            Cerr << "WAIT_CLEANING_SCHEMAS: " << GetCleanupSchemasFinishedCounter().Val() << Endl;
            if (testRuntime) {
                testRuntime->SimulateSleep(TDuration::Seconds(1));
            } else {
                Sleep(TDuration::Seconds(1));
            }
        }
        return GetCleanupSchemasFinishedCounter().Val() != countStart0;
    }

    void WaitTtl(const TDuration d) const {
        TInstant start = TInstant::Now();
        ui32 countStart = GetTTLStartedCounter().Val();
        while (Now() - start < d) {
            if (countStart != GetTTLStartedCounter().Val()) {
                countStart = GetTTLStartedCounter().Val();
                start = TInstant::Now();
            }
            Cerr << "WAIT_TTL: " << GetTTLStartedCounter().Val() << Endl;
            Sleep(TDuration::Seconds(1));
        }
    }

    template <class TTester>
    void WaitCondition(const TDuration d, const TTester& test) const {
        const TInstant start = TInstant::Now();
        while (TInstant::Now() - start < d) {
            if (test()) {
                Cerr << "condition SUCCESS!!..." << TInstant::Now() - start << Endl;
                return;
            } else {
                Cerr << "waiting condition..." << TInstant::Now() - start << Endl;
                Sleep(TDuration::Seconds(1));
            }
        }
        AFL_VERIFY(false)("reason", "condition not reached");
    }

    void WaitActualization(const TDuration d) const {
        TInstant start = TInstant::Now();
        const i64 startVal = NeedActualizationCount.Val();
        i64 predVal = NeedActualizationCount.Val();
        while (TInstant::Now() - start < d && (!startVal || NeedActualizationCount.Val())) {
            Cerr << "waiting actualization: " << NeedActualizationCount.Val() << "/" << TInstant::Now() - start << Endl;
            if (NeedActualizationCount.Val() != predVal) {
                predVal = NeedActualizationCount.Val();
                start = TInstant::Now();
            }
            Sleep(TDuration::Seconds(1));
        }
        AFL_VERIFY(!NeedActualizationCount.Val());
    }

    virtual void OnCleanupSchemasFinished() override {
        CleanupSchemasFinishedCounter.Inc();
    }

    virtual void OnIndexSelectProcessed(const std::optional<bool> result) override {
        if (!result) {
            IndexesSkippedNoData.Inc();
        } else if (*result) {
            IndexesApprovedOnSelect.Inc();
        } else {
            IndexesSkippingOnSelect.Inc();
        }
    }

    virtual void OnHeaderSelectProcessed(const std::optional<bool> result) override {
        if (!result) {
            HeadersSkippedNoData.Inc();
        } else if (*result) {
            HeadersApprovedOnSelect.Inc();
        } else {
            HeadersSkippingOnSelect.Inc();
        }
    }

    virtual bool IsForcedGenerateInternalPathId() const override {
        return true;
    }

};

}
