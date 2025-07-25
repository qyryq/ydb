LIBRARY()

    IF (OS_WINDOWS)
        CFLAGS(
            -DKIKIMR_DISABLE_S3_OPS
        )
    ELSE()
        SRCS(
            s3.cpp
        )
    ENDIF()

    SRCS(
        agent.cpp
        agent.h
        agent_impl.h
        blob_mapping_cache.cpp
        blob_mapping_cache.h
        blocks.cpp
        blocks.h
        channel_kind.cpp
        comm.cpp
        defs.h
        garbage.cpp
        metrics.cpp
        proxy.cpp
        query.cpp
        read.cpp
        request.cpp
        resolved_value.cpp
        resolved_value.h
        status.cpp

        # DS Proxy queries
        storage_put.cpp
        storage_get.cpp
        storage_get_block.cpp
        storage_block.cpp
        storage_check_integrity.cpp
        storage_discover.cpp
        storage_range.cpp
        storage_collect_garbage.cpp
        storage_status.cpp
        storage_patch.cpp
    )

    PEERDIR(
        ydb/core/blobstorage/vdisk/common
        ydb/core/blob_depot
        ydb/core/protos
        ydb/core/wrappers
    )

END()
