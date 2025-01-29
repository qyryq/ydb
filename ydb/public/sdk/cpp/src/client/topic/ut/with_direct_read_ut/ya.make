UNITTEST_FOR(ydb/public/sdk/cpp/src/client/topic)

INCLUDE(${ARCADIA_ROOT}/ydb/public/sdk/cpp/sdk_common.inc)

IF (SANITIZER_TYPE == "thread" OR WITH_VALGRIND)
    TIMEOUT(1200)
    SIZE(LARGE)
    TAG(ya:fat)
ELSE()
    TIMEOUT(600)
    SIZE(MEDIUM)
ENDIF()

FORK_SUBTESTS()

PEERDIR(
    ydb/core/testlib/default
    library/cpp/testing/gmock_in_unittest
    ydb/public/lib/json_value
    ydb/public/lib/yson_value
    ydb/public/sdk/cpp/src/client/driver
    ydb/public/sdk/cpp/src/client/topic/ut/ut_utils
    ydb/public/sdk/cpp/src/client/persqueue_public/ut/ut_utils
    ydb/core/persqueue/ut/common
    ydb/core/tx/schemeshard/ut_helpers
)

YQL_LAST_ABI_VERSION()

ENV(TOPIC_DIRECT_READ="1")

SRCDIR(
    ydb/public/sdk/cpp/src/client/topic/ut
    ydb/public/sdk/cpp/src/client/topic
)

SRCS(
    basic_usage_ut.cpp
    describe_topic_ut.cpp
    local_partition_ut.cpp
    topic_to_table_ut.cpp
)

END()
