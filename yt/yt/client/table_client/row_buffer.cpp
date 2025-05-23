#include "row_buffer.h"

#include "schema.h"
#include "unversioned_row.h"
#include "versioned_row.h"

namespace NYT::NTableClient {

////////////////////////////////////////////////////////////////////////////////

TRowBuffer::TRowBuffer(
    TRefCountedTypeCookie tagCookie,
    IMemoryChunkProviderPtr chunkProvider,
    size_t startChunkSize,
    IMemoryUsageTrackerPtr tracker,
    bool allowMemoryOvercommit)
    : MemoryTracker_(std::move(tracker))
    , AllowMemoryOvercommit_(allowMemoryOvercommit)
    , Pool_(
        tagCookie,
        std::move(chunkProvider),
        startChunkSize)
    , MemoryGuard_(TMemoryUsageTrackerGuard::Build(MemoryTracker_))
{ }

TChunkedMemoryPool* TRowBuffer::GetPool()
{
    return &Pool_;
}

TMutableUnversionedRow TRowBuffer::AllocateUnversioned(int valueCount)
{
    auto result = TMutableUnversionedRow::Allocate(&Pool_, valueCount);
    UpdateMemoryUsage();
    return result;
}

TMutableVersionedRow TRowBuffer::AllocateVersioned(
    int keyCount,
    int valueCount,
    int writeTimestampCount,
    int deleteTimestampCount)
{
    auto result = TMutableVersionedRow::Allocate(
        &Pool_,
        keyCount,
        valueCount,
        writeTimestampCount,
        deleteTimestampCount);
    UpdateMemoryUsage();
    return result;
}

void TRowBuffer::CaptureValue(TUnversionedValue* value)
{
    if (IsStringLikeType(value->Type) && value->Data.String != nullptr) {
        char* dst = Pool_.AllocateUnaligned(value->Length);
        memcpy(dst, value->Data.String, value->Length);
        value->Data.String = dst;
    }

    UpdateMemoryUsage();
}

TVersionedValue TRowBuffer::CaptureValue(const TVersionedValue& value)
{
    auto capturedValue = value;
    CaptureValue(&capturedValue);
    return capturedValue;
}

TUnversionedValue TRowBuffer::CaptureValue(const TUnversionedValue& value)
{
    auto capturedValue = value;
    CaptureValue(&capturedValue);
    return capturedValue;
}

TMutableUnversionedRow TRowBuffer::CaptureRow(TUnversionedRow row, bool captureValues)
{
    if (!row) {
        return TMutableUnversionedRow();
    }

    return CaptureRow(row.Elements(), captureValues);
}

void TRowBuffer::CaptureValues(TMutableUnversionedRow row)
{
    if (!row) {
        return;
    }

    for (ui32 index = 0; index < row.GetCount(); ++index) {
        CaptureValue(&row[index]);
    }
}

TMutableUnversionedRow TRowBuffer::CaptureRow(TUnversionedValueRange values, bool captureValues)
{
    int count = std::ssize(values);
    auto capturedRow = TMutableUnversionedRow::Allocate(&Pool_, count);
    auto* capturedBegin = capturedRow.Begin();

    ::memcpy(capturedBegin, values.Begin(), count * sizeof(TUnversionedValue));

    if (captureValues) {
        for (int index = 0; index < count; ++index) {
            CaptureValue(&capturedBegin[index]);
        }
    }

    UpdateMemoryUsage();

    return capturedRow;
}

std::vector<TMutableUnversionedRow> TRowBuffer::CaptureRows(TRange<TUnversionedRow> rows, bool captureValues)
{
    int rowCount = std::ssize(rows);
    std::vector<TMutableUnversionedRow> capturedRows(rowCount);
    for (int index = 0; index < rowCount; ++index) {
        capturedRows[index] = CaptureRow(rows[index], captureValues);
    }
    return capturedRows;
}

TMutableUnversionedRow TRowBuffer::CaptureAndPermuteRow(
    TUnversionedRow row,
    const TTableSchema& tableSchema,
    int schemafulColumnCount,
    const TNameTableToSchemaIdMapping& idMapping,
    bool validateDuplicateAndRequiredValueColumns,
    bool preserveIds,
    std::optional<TUnversionedValue> addend)
{
    int valueCount = schemafulColumnCount;

    if (validateDuplicateAndRequiredValueColumns) {
        ValidateDuplicateAndRequiredValueColumns(row, tableSchema, idMapping);
    }

    for (const auto& value : row) {
        ui16 originalId = value.Id;
        YT_VERIFY(originalId < idMapping.size());
        int mappedId = idMapping[originalId];
        if (mappedId < 0) {
            continue;
        }
        if (mappedId >= schemafulColumnCount) {
            ++valueCount;
        }
    }
    if (addend) {
        ++valueCount;
    }

    auto capturedRow = TMutableUnversionedRow::Allocate(&Pool_, valueCount);
    for (int pos = 0; pos < schemafulColumnCount; ++pos) {
        capturedRow[pos] = MakeUnversionedNullValue(pos);
    }

    valueCount = schemafulColumnCount;

    for (const auto& value : row) {
        ui16 originalId = value.Id;
        int mappedId = idMapping[originalId];
        if (mappedId < 0) {
            continue;
        }
        int pos = mappedId < schemafulColumnCount ? mappedId : valueCount++;
        capturedRow[pos] = value;
        if (!preserveIds) {
            capturedRow[pos].Id = mappedId;
        }
    }
    if (addend) {
        capturedRow[valueCount++] = *addend;
    }

    UpdateMemoryUsage();

    return capturedRow;
}

TMutableVersionedRow TRowBuffer::CaptureRow(TVersionedRow row, bool captureValues)
{
    if (!row) {
        return TMutableVersionedRow();
    }

    auto capturedRow = TMutableVersionedRow::Allocate(
        &Pool_,
        row.GetKeyCount(),
        row.GetValueCount(),
        row.GetWriteTimestampCount(),
        row.GetDeleteTimestampCount());
    ::memcpy(capturedRow.BeginKeys(), row.BeginKeys(), sizeof(TUnversionedValue) * row.GetKeyCount());
    ::memcpy(capturedRow.BeginValues(), row.BeginValues(), sizeof(TVersionedValue) * row.GetValueCount());
    ::memcpy(capturedRow.BeginWriteTimestamps(), row.BeginWriteTimestamps(), sizeof(TTimestamp) * row.GetWriteTimestampCount());
    ::memcpy(capturedRow.BeginDeleteTimestamps(), row.BeginDeleteTimestamps(), sizeof(TTimestamp) * row.GetDeleteTimestampCount());

    if (captureValues) {
        CaptureValues(capturedRow);
    }

    UpdateMemoryUsage();

    return capturedRow;
}

void TRowBuffer::CaptureValues(TMutableVersionedRow row)
{
    if (!row) {
        return;
    }

    for (auto& value : row.Keys()) {
        CaptureValue(&value);
    }
    for (auto& value : row.Values()) {
        CaptureValue(&value);
    }
}

TMutableVersionedRow TRowBuffer::CaptureAndPermuteRow(
    TVersionedRow row,
    const TTableSchema& tableSchema,
    const TNameTableToSchemaIdMapping& idMapping,
    bool validateDuplicateAndRequiredValueColumns,
    bool allowMissingKeyColumns)
{
    int keyColumnCount = tableSchema.GetKeyColumnCount();

    if (!allowMissingKeyColumns) {
        YT_VERIFY(keyColumnCount == row.GetKeyCount());
        YT_VERIFY(keyColumnCount <= std::ssize(idMapping));
    }

    int valueCount = 0;
    int deleteTimestampCount = row.GetDeleteTimestampCount();

    TCompactVector<TTimestamp, 64> writeTimestamps;
    for (const auto& value : row.Values()) {
        ui16 originalId = value.Id;
        YT_VERIFY(originalId < idMapping.size());
        int mappedId = idMapping[originalId];
        if (mappedId < 0) {
            continue;
        }
        YT_VERIFY(mappedId < std::ssize(tableSchema.Columns()));
        ++valueCount;
        writeTimestamps.push_back(value.Timestamp);
    }

    std::sort(writeTimestamps.begin(), writeTimestamps.end(), std::greater<TTimestamp>());
    writeTimestamps.erase(std::unique(writeTimestamps.begin(), writeTimestamps.end()), writeTimestamps.end());
    int writeTimestampCount = std::ssize(writeTimestamps);

    if (validateDuplicateAndRequiredValueColumns) {
        ValidateDuplicateAndRequiredValueColumns(
            row,
            tableSchema,
            idMapping,
            writeTimestamps.data(),
            writeTimestampCount);
    }

    auto capturedRow = TMutableVersionedRow::Allocate(
        &Pool_,
        keyColumnCount,
        valueCount,
        writeTimestampCount,
        deleteTimestampCount);

    ::memcpy(capturedRow.BeginWriteTimestamps(), writeTimestamps.data(), sizeof(TTimestamp) * writeTimestampCount);
    ::memcpy(capturedRow.BeginDeleteTimestamps(), row.BeginDeleteTimestamps(), sizeof(TTimestamp) * deleteTimestampCount);

    if (!allowMissingKeyColumns) {
        int index = 0;
        auto* dstValue = capturedRow.BeginKeys();
        for (const auto* srcValue = row.BeginKeys(); srcValue != row.EndKeys(); ++srcValue, ++index) {
            YT_VERIFY(idMapping[index] == index);
            *dstValue++ = *srcValue;
        }
    } else {
        for (int index = 0; index < keyColumnCount; ++index) {
            capturedRow.Keys()[index] = MakeUnversionedNullValue(index);
        }
        for (const auto& srcValue : row.Keys()) {
            ui16 originalId = srcValue.Id;
            int mappedId = idMapping[originalId];
            if (mappedId < 0) {
                continue;
            }
            auto* dstValue = &capturedRow.Keys()[mappedId];
            *dstValue = srcValue;
            dstValue->Id = mappedId;
        }
    }

    {
        auto* dstValue = capturedRow.BeginValues();
        for (const auto& srcValue : row.Values()) {
            ui16 originalId = srcValue.Id;
            int mappedId = idMapping[originalId];
            if (mappedId < 0) {
                continue;
            }
            *dstValue = srcValue;
            dstValue->Id = mappedId;
            ++dstValue;
        }
    }

    UpdateMemoryUsage();

    return capturedRow;
}

void TRowBuffer::Absorb(TRowBuffer&& other)
{
    Pool_.Absorb(std::move(other.Pool_));
    UpdateMemoryUsage();
}

i64 TRowBuffer::GetSize() const
{
    return Pool_.GetSize();
}

i64 TRowBuffer::GetCapacity() const
{
    return Pool_.GetCapacity();
}

void TRowBuffer::Clear()
{
    MemoryGuard_.Release();
    Pool_.Clear();
}

void TRowBuffer::Purge()
{
    MemoryGuard_.Release();
    Pool_.Purge();
}

void TRowBuffer::UpdateMemoryUsage()
{
    if (!MemoryTracker_) {
        return;
    }

    auto capacity = Pool_.GetCapacity();

    if (AllowMemoryOvercommit_) {
        MemoryGuard_.SetSize(capacity);
    } else {
        MemoryGuard_.TrySetSize(capacity)
            .ThrowOnError();
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
