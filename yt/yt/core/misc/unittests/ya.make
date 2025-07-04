GTEST(unittester-core-misc)

INCLUDE(${ARCADIA_ROOT}/yt/ya_cpp.make.inc)

PROTO_NAMESPACE(yt)

SRCS(
    adjusted_exponential_moving_average_ut.cpp
    algorithm_helpers_ut.cpp
    arithmetic_formula_ut.cpp
    async_expiring_cache_ut.cpp
    async_slru_cache_ut.cpp
    atomic_ptr_ut.cpp
    backoff_strategy_ut.cpp
    bit_packed_integer_vector_ut.cpp
    bitmap_ut.cpp
    boolean_formula_ut.cpp
    callback_ut.cpp
    checksum_ut.cpp
    codicil_ut.cpp
    collection_helpers_ut.cpp
    concurrent_cache_ut.cpp
    configurable_singleton_ut.cpp
    consistent_hashing_ut.cpp
    default_map_ut.cpp
    digest_ut.cpp
    ema_counter_ut.cpp
    enum_ut.cpp
    error_ut.cpp
    fair_scheduler_ut.cpp
    fair_share_hierarchical_queue_ut.cpp
    fenwick_tree_ut.cpp
    finally_ut.cpp
    format_ut.cpp
    fs_ut.cpp
    guid_ut.cpp
    hash_filter_ut.cpp
    hazard_ptr_ut.cpp
    heap_ut.cpp
    hedging_manager_ut.cpp
    histogram_ut.cpp
    hyperloglog_ut.cpp
    intern_registry_ut.cpp
    job_signaler_ut.cpp
    lock_free_hash_table_ut.cpp
    lru_cache_ut.cpp
    maybe_inf_ut.cpp
    moving_average_ut.cpp
    mpl_ut.cpp
    mpsc_fair_share_queue_ut.cpp
    mpsc_queue_ut.cpp
    mpsc_stack_ut.cpp
    pattern_formatter_ut.cpp
    persistent_queue_ut.cpp
    pool_allocator_ut.cpp
    proc_ut.cpp
    random_ut.cpp
    range_helpers_ut.cpp
    ref_counted_tracker_ut.cpp
    relaxed_mpsc_queue_ut.cpp
    ring_queue_ut.cpp
    skip_list_ut.cpp
    slab_allocator_ut.cpp
    sliding_window_ut.cpp
    spsc_queue_ut.cpp
    statistic_path_ut.cpp
    statistics_ut.cpp
    sync_cache_ut.cpp
    sync_expiring_cache_ut.cpp
    time_formula_ut.cpp
    three_level_stable_vector_ut.cpp
    tls_destructor_ut.cpp
    tls_expiring_cache_ut.cpp
    topological_ordering_ut.cpp
    yverify_ut.cpp
    zerocopy_output_writer_ut.cpp

    proto/ref_counted_tracker_ut.proto
)

INCLUDE(${ARCADIA_ROOT}/yt/opensource.inc)

PEERDIR(
    yt/yt/core
    yt/yt/core/test_framework
)

REQUIREMENTS(
    cpu:4
    ram:4
    ram_disk:1
)

FORK_TESTS()

SPLIT_FACTOR(5)

SIZE(MEDIUM)

IF (OS_DARWIN)
    SIZE(LARGE)
    TAG(
        ya:fat
        ya:force_sandbox
        ya:exotic_platform
        ya:large_tests_on_single_slots
    )
ENDIF()

END()
