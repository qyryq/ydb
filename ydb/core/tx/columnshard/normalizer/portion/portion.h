#pragma once

#include "normalizer.h"
#include <ydb/core/tx/columnshard/normalizer/abstract/abstract.h>
#include <ydb/core/tx/columnshard/defs.h>


namespace NKikimr::NColumnShard {
    class TTablesManager;
}

namespace NKikimr::NOlap {

class TPortionsNormalizer : public TPortionsNormalizerBase {
public:
    static TString GetClassNameStatic() {
        return "PortionsMetadata";
    }

private:
    static inline TFactory::TRegistrator<TPortionsNormalizer> Registrator = TFactory::TRegistrator<TPortionsNormalizer>(
        GetClassNameStatic());
public:
    class TNormalizerResult;
    class TTask;

public:
    virtual std::optional<ENormalizerSequentialId> DoGetEnumSequentialId() const override {
        return std::nullopt;
    }

    virtual TString GetClassName() const override {
        return GetClassNameStatic();
    }

    TPortionsNormalizer(const TNormalizationController::TInitContext& info)
        : TPortionsNormalizerBase(info)
    {}

    virtual INormalizerTask::TPtr BuildTask(std::vector<TPortionDataAccessor>&& portions, std::shared_ptr<THashMap<ui64, ISnapshotSchema::TPtr>> schemas) const override;
    virtual TConclusion<bool> DoInitImpl(const TNormalizationController& controller, NTabletFlatExecutor::TTransactionContext& txc) override;

    virtual bool CheckPortion(const NColumnShard::TTablesManager& tablesManager, const TPortionDataAccessor& portionInfo) const override;

private:
    THashSet<TPortionAddress> KnownPortions;

};

}
