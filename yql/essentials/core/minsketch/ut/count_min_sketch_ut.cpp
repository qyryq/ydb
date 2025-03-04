#include "count_min_sketch.h"

#include <library/cpp/testing/unittest/registar.h>

namespace NKikimr {

Y_UNIT_TEST_SUITE(CountMinSketch) {

    Y_UNIT_TEST(CountAndProbe) {
        std::unique_ptr<TCountMinSketch> countMin(TCountMinSketch::Create(256, 8));

        TString str1("foo");
        countMin->Count(str1.data(), str1.size());
        auto probe1 = countMin->Probe(str1.data(), str1.size());

        TString str2("bar");
        for (size_t i = 0; i < 4; ++i) {
            countMin->Count(str2.data(), str2.size());
        }
        auto probe2 = countMin->Probe(str2.data(), str2.size());

        ui32 integer1 = 1234567890U;
        countMin->Count((const char *)&integer1, sizeof(ui32));
        auto probe3 = countMin->Probe((const char *)&integer1, sizeof(ui32));

        ui64 integer2 = 1234567890ULL;
        countMin->Count((const char *)&integer2, sizeof(ui64));
        auto probe4 = countMin->Probe((const char *)&integer2, sizeof(ui64));

        ui64 integer3 = 1234512345ULL;
        auto probe5 = countMin->Probe((const char *)&integer3, sizeof(ui64));

        UNIT_ASSERT_VALUES_EQUAL(probe1, 1);
        UNIT_ASSERT_VALUES_EQUAL(probe2, 4);
        UNIT_ASSERT_VALUES_EQUAL(probe3, 1);
        UNIT_ASSERT_VALUES_EQUAL(probe4, 1);
        UNIT_ASSERT_VALUES_EQUAL(probe5, 0);

        UNIT_ASSERT_VALUES_EQUAL(countMin->GetElementCount(), 7);
    }

    Y_UNIT_TEST(Add) {
        std::unique_ptr<TCountMinSketch> countMinA(TCountMinSketch::Create(256, 8));
        std::unique_ptr<TCountMinSketch> countMinB(TCountMinSketch::Create(256, 8));

        TString str("foo");
        countMinA->Count(str.data(), str.size());

        ui32 integer = 0;
        countMinB->Count((const char *)&integer, sizeof(ui32));

        *countMinA += *countMinB;

        auto probe1 = countMinA->Probe(str.data(), str.size());
        auto probe2 = countMinA->Probe((const char *)&integer, sizeof(ui32));

        UNIT_ASSERT_VALUES_EQUAL(probe1, 1);
        UNIT_ASSERT_VALUES_EQUAL(probe2, 1);
    }

}

} // NKikimr
