#pragma once

#include "yql_yt_settings.h"

#include <yql/essentials/public/udf/udf_data_type.h>

#include <util/system/types.h>
#include <util/datetime/base.h>
#include <util/generic/size_literals.h>
#include <util/generic/set.h>

namespace NYql {

constexpr size_t YQL_JOB_CODEC_BLOCK_COUNT = 16;
constexpr size_t YQL_JOB_CODEC_BLOCK_SIZE = 1_MB;

constexpr size_t YQL_JOB_CODEC_MEM = YQL_JOB_CODEC_BLOCK_COUNT * YQL_JOB_CODEC_BLOCK_SIZE + (30_MB);

constexpr size_t YQL_ARROW_MEMORY_POOL_RESERVE = 640_MB;

constexpr ui64 DEFAULT_TOP_SORT_LIMIT = 1000ULL;

constexpr ui16 DEFAULT_WIDE_FLOW_LIMIT = 101U;

constexpr bool DEFAULT_USE_FLOW = true;

constexpr bool DEFAULT_USE_NATIVE_YT_TYPES = false;

constexpr bool DEFAULT_USE_INTERMEDIATE_STREAMS = false;

constexpr bool DEFAULT_USE_SKIFF = true;

constexpr bool DEFAULT_USE_SYS_COLUMNS = true;

constexpr bool DEFAULT_USE_MULTISET_ATTRS = true;

constexpr bool DEFAULT_MAP_JOIN_USE_FLOW = true;

constexpr bool DefaultHybridDqExecution = false;
constexpr auto DefaultHybridDqDataSizeLimitForOrdered = 1_GB;
constexpr auto DefaultHybridDqDataSizeLimitForUnordered = 16_GB;

constexpr bool DEFAULT_ROW_SPEC_COMPACT_FORM = false;

constexpr bool DEFAULT_USE_NEW_PREDICATE_EXTRACTION = false;
constexpr bool DEFAULT_PRUNE_KEY_FILTER_LAMBDA = false;
constexpr bool DEFAULT_DQ_PRUNE_KEY_FILTER_LAMBDA = false;
constexpr bool DEFAULT_MERGE_ADJACENT_POINT_RANGES = true;
constexpr bool DEFAULT_KEY_FILTER_FOR_STARTS_WITH = true;
constexpr ui64 DEFAULT_MAX_KEY_RANGE_COUNT = 1000ULL;

constexpr bool DEFAULT_USE_NATIVE_DESC_SORT = false;

constexpr ui64 DEFAULT_MAX_CHUNKS_FOR_DQ_READ = 500;

constexpr bool DEFAULT_USE_KEY_BOUND_API = true;

constexpr ui32 DEFAULT_MAX_OPERATION_FILES = 1000;

constexpr bool DEFAULT_JOIN_COMMON_USE_MULTI_OUT = false;

constexpr bool DEFAULT_USE_RPC_READER_IN_DQ = false;
constexpr size_t DEFAULT_RPC_READER_INFLIGHT = 1;
constexpr TDuration DEFAULT_RPC_READER_TIMEOUT = TDuration::Seconds(120);
const TSet<TString> DEFAULT_BLOCK_READER_SUPPORTED_TYPES = {"pg", "tuple"};
const TSet<NUdf::EDataSlot> DEFAULT_BLOCK_READER_SUPPORTED_DATA_TYPES =
    {
        NUdf::EDataSlot::Int8, NUdf::EDataSlot::Uint8,
        NUdf::EDataSlot::Int16, NUdf::EDataSlot::Uint16,
        NUdf::EDataSlot::Int32, NUdf::EDataSlot::Uint32,
        NUdf::EDataSlot::Int64, NUdf::EDataSlot::Uint64,
        NUdf::EDataSlot::Bool, NUdf::EDataSlot::Double,
        NUdf::EDataSlot::String, NUdf::EDataSlot::Json,
        NUdf::EDataSlot::Yson, NUdf::EDataSlot::Utf8,
        NUdf::EDataSlot::Date, NUdf::EDataSlot::Datetime,
        NUdf::EDataSlot::Timestamp, NUdf::EDataSlot::Interval,
        NUdf::EDataSlot::Float,
    };
const TSet<TString> DEFAULT_BLOCK_INPUT_SUPPORTED_TYPES = {"tuple"};
const TSet<NUdf::EDataSlot> DEFAULT_BLOCK_INPUT_SUPPORTED_DATA_TYPES =
    {
        NUdf::EDataSlot::Int8, NUdf::EDataSlot::Uint8,
        NUdf::EDataSlot::Int16, NUdf::EDataSlot::Uint16,
        NUdf::EDataSlot::Int32, NUdf::EDataSlot::Uint32,
        NUdf::EDataSlot::Int64, NUdf::EDataSlot::Uint64,
        NUdf::EDataSlot::Bool, NUdf::EDataSlot::Double,
        NUdf::EDataSlot::String, NUdf::EDataSlot::Utf8,
        NUdf::EDataSlot::Yson, NUdf::EDataSlot::Float,
        NUdf::EDataSlot::Date, NUdf::EDataSlot::Datetime,
        NUdf::EDataSlot::Timestamp, NUdf::EDataSlot::Interval,
    };
const TSet<TString> DEFAULT_BLOCK_OUTPUT_SUPPORTED_TYPES = {};
const TSet<NUdf::EDataSlot> DEFAULT_BLOCK_OUTPUT_SUPPORTED_DATA_TYPES =
    {
        NUdf::EDataSlot::Int8, NUdf::EDataSlot::Uint8,
        NUdf::EDataSlot::Int16, NUdf::EDataSlot::Uint16,
        NUdf::EDataSlot::Int32, NUdf::EDataSlot::Uint32,
        NUdf::EDataSlot::Int64, NUdf::EDataSlot::Uint64,
        NUdf::EDataSlot::Bool, NUdf::EDataSlot::Double,
        NUdf::EDataSlot::String, NUdf::EDataSlot::Utf8
    };

constexpr auto DEFAULT_SWITCH_MEMORY_LIMIT = 128_MB;

constexpr ui32 DEFAULT_MAX_INPUT_TABLES = 3000;
constexpr ui32 DEFAULT_MAX_OUTPUT_TABLES = 100;
constexpr ui64 DEFAULT_APPLY_STORED_CONSTRAINTS = 0ULL;

constexpr ui64 DEFAULT_TABLE_CONTENT_LOCAL_EXEC = 0;

constexpr ui32 DEFAULT_BATCH_LIST_FOLDER_CONCURRENCY = 5;

constexpr bool DEFAULT_PARTITION_BY_CONSTANT_KEYS_VIA_MAP = true;

constexpr ui64 DEFAULT_LLVM_NODE_COUNT_LIMIT = 200000;

constexpr ui16 DEFAULT_MIN_COLUMN_GROUP_SIZE = 2;
constexpr ui16 DEFAULT_MAX_COLUMN_GROUPS = 64;

constexpr bool DEFAULT_DISABLE_FUSE_OPERATIONS = false;
constexpr bool DEFAULT_ENABLE_FUSE_MAP_TO_MAPREDUCE = false;

constexpr bool DEFAULT_ENABLE_DQ_WRITE_CONSTRAINTS = false;

constexpr bool DEFAULT_USE_QL_FILTER = false;
constexpr bool DEFAULT_PRUNE_QL_FILTER_LAMBDA = true;

constexpr bool DEFAULT_DROP_UNUSED_KEYS_FROM_KEY_FILTER = false;

constexpr bool DEFAULT_REPORT_EQUIJOIN_STATS = false;

constexpr ERuntimeClusterSelectionMode DEFAULT_RUNTIME_CLUSTER_SELECTION = NYql::ERuntimeClusterSelectionMode::Disable;

constexpr bool DEFAULT_ALLOW_REMOTE_CLUSTER_INPUT = false;

constexpr bool DEFAULT_USE_COLUMN_GROUPS_FROM_INPUT_TABLE = false;

constexpr bool DEFAULT_USE_NATIVE_DYNAMIC_TABLE_READ = false;

constexpr bool DEFAULT_FORBID_SENSITIVE_DATA_IN_OPERATION_SPEC = false;

constexpr ui64 DEFAULT_LOCAL_TABLE_CONTENT_LIMIT = 10_GB;

} // NYql
