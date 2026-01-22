LIBRARY()

SRCS(
    common.cpp
    persqueue.cpp
    read_events.cpp
    read_session.cpp
    topic_partition_accessor.cpp
    write_events.cpp
    write_session.cpp
)

PEERDIR(
    ydb/public/sdk/cpp/src/client/persqueue_over_topic/include
    ydb/public/sdk/cpp/src/client/federated_topic
    ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/topic
    ydb/public/sdk/cpp/src/client/topic/impl
    ydb/public/sdk/cpp/src/client/topic/common
    ydb/public/sdk/cpp/src/client/driver
    ydb/public/sdk/cpp/src/client/scheme
    ydb/public/sdk/cpp/src/client/proto

    ydb/public/api/grpc
    ydb/public/api/grpc/draft
    ydb/public/api/protos
)

END()
