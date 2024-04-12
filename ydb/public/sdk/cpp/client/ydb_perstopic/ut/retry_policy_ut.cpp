#include <ydb/public/sdk/cpp/client/ydb_perstopic/ut/ut_utils/ut_utils.h>

#include <library/cpp/threading/future/future.h>
#include <library/cpp/testing/unittest/registar.h>


using namespace NThreading;
using namespace NKikimr;
using namespace NKikimr::NPersQueueTests;
using namespace NPQTopic;

namespace NYdb::NPQTopic::NTests {

Y_UNIT_TEST_SUITE(RetryPolicy) {
    Y_UNIT_TEST(TWriteSession_TestPolicy) {
        TYdbPqWriterTestHelper helper(TEST_CASE_NAME);
        helper.Write(true);
        helper.Policy->Initialize(); // Thus ignoring possible early retries on "cluster initializing"
        auto doBreakDown = [&] () {
            helper.Policy->ExpectBreakDown();
            NThreading::TPromise<void> retriesPromise = NThreading::NewPromise();
            Cerr << "WAIT for retries...\n";
            helper.Policy->WaitForRetries(30, retriesPromise);
            Cerr << "KICK tablets\n";
            helper.Setup->KickTablets();

            auto f1 = helper.Write(false);
            auto f2 = helper.Write();

            auto retriesFuture = retriesPromise.GetFuture();
            retriesFuture.Wait();
            Cerr << "WAIT for retries done\n";

            NThreading::TPromise<void> repairPromise = NThreading::NewPromise();
            auto repairFuture = repairPromise.GetFuture();
            helper.Policy->WaitForRepair(repairPromise);


            Cerr << "ALLOW tablets\n";
            helper.Setup->AllowTablets();

            Cerr << "WAIT for repair\n";
            repairFuture.Wait();
            Cerr << "REPAIR done\n";
            f1.Wait();
            f2.Wait();
            helper.Write(true);
        };
        doBreakDown();
        doBreakDown();

    }
    Y_UNIT_TEST(TWriteSession_TestBrokenPolicy) {
        TYdbPqWriterTestHelper helper(TEST_CASE_NAME);
        helper.Write();
        helper.Policy->Initialize();
        helper.Policy->ExpectFatalBreakDown();
        helper.EventLoop->AllowStop();
        auto f1 = helper.Write(false);
        helper.Setup->KickTablets();
        helper.Write(false);

        helper.EventLoop->WaitForStop();
        UNIT_ASSERT(!f1.HasValue());
        helper.Setup = nullptr;

    };

    Y_UNIT_TEST(RetryWithBatching) {
        auto setup = std::make_shared<TPersQueueYdbSdkTestSetup>(TEST_CASE_NAME);
        auto settings = setup->GetWriteSessionSettings();
        auto retryPolicy = std::make_shared<TYdbPqTestRetryPolicy>();
        settings.BatchFlushInterval(TDuration::Seconds(1000)); // Batch on size, not on time.
        settings.BatchFlushSizeBytes(100);
        settings.RetryPolicy(retryPolicy);
        auto& client = setup->GetPersQueueClient();
        auto writer = client.CreateWriteSession(settings);
        auto event = *writer->GetEvent(true);
        Cerr << NYdb::NPQTopic::DebugString(event) << "\n";
        UNIT_ASSERT(std::holds_alternative<TWriteSessionEvent::TReadyToAcceptEvent>(event));
        auto continueToken = std::move(std::get<TWriteSessionEvent::TReadyToAcceptEvent>(event).ContinuationToken);
        TString message = "1234567890";
        ui64 seqNo = 0;
        setup->KickTablets();
        writer->Write(std::move(continueToken), message, ++seqNo);
        retryPolicy->ExpectBreakDown();
        retryPolicy->WaitForRetriesSync(3);
        while (seqNo < 10) {
            auto event = *writer->GetEvent(true);
            Cerr << NYdb::NPQTopic::DebugString(event) << "\n";
            UNIT_ASSERT(std::holds_alternative<TWriteSessionEvent::TReadyToAcceptEvent>(event));
            writer->Write(
                    std::move(std::get<TWriteSessionEvent::TReadyToAcceptEvent>(event).ContinuationToken),
                    message, ++seqNo
            );
        }

        setup->AllowTablets();
        retryPolicy->WaitForRepairSync();
        WaitMessagesAcked(writer, 1, seqNo);
    }
};
}; //NYdb::NPQTopic::NTests
