#include "cuw3/bitmap.hpp"

#include <format>
#include <iostream>

#include "gtest/gtest.h"

using namespace cuw3;

using CuwBitmap = Bitmap<uint64, 96>;
using CuwBitmapArray = std::array<uint64, 2>;

template<class T, T bits>
std::ostream& operator<<(std::ostream& os, const Bitmap<T, bits>& bm) {
    for (auto& bin : bm.bins) {
        os << std::format("{:016X}", bin) << " ";
    }
    return os;
}

bool check_cuw_bitmap(const CuwBitmap& bitmap, const CuwBitmapArray& check) {
    return std::ranges::equal(bitmap.bins, check);
}

TEST(Bitmap, AllSetUnset) {
    const CuwBitmapArray check_all_set = {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFF};
    const CuwBitmapArray check_all_unset = {};

    CuwBitmap bitmap{};
    for (uint64 i = 0; i < bitmap.bit_capacity; i++) {
        bitmap.set(i);
    }
    ASSERT_TRUE(check_cuw_bitmap(bitmap, check_all_set));

    for (uint64 i = 0; i < bitmap.bit_capacity; i++) {
        bitmap.unset(i);
    }
    ASSERT_TRUE(check_cuw_bitmap(bitmap, check_all_unset));
}

TEST(Bitmap, EvenSetUnset) {
    const CuwBitmapArray check_all_even = {0x5555555555555555, 0x55555555};
    const CuwBitmapArray check_all_empty = {};

    CuwBitmap bitmap{};
    for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
        bitmap.set(i);
    }
    ASSERT_TRUE(check_cuw_bitmap(bitmap, check_all_even));

    for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
        bitmap.unset(i);
    }
    ASSERT_TRUE(check_cuw_bitmap(bitmap, check_all_empty));
}

TEST(Bitmap, OddSetUnset) {
    const CuwBitmapArray check_all_odd = {0xAAAAAAAAAAAAAAAA, 0xAAAAAAAA};
    const CuwBitmapArray check_all_empty = {};

    CuwBitmap bitmap{};
    for (uint64 i = 1; i < bitmap.bit_capacity; i += 2) {
        bitmap.set(i);
    }
    ASSERT_TRUE(check_cuw_bitmap(bitmap, check_all_odd));

    for (uint64 i = 1; i < bitmap.bit_capacity; i += 2) {
        bitmap.unset(i);
    }
    ASSERT_TRUE(check_cuw_bitmap(bitmap, check_all_empty));
}

TEST(Bitmap, SetFirstUnset) {
    const CuwBitmapArray check_all_empty = {};

    CuwBitmap bitmap{};
    for (uint64 start = 0; start < bitmap.bit_capacity; start++) {
        for (uint64 i = 0; i < bitmap.bit_capacity - start; i++){
            auto bit = bitmap.set_first_unset(start);
            ASSERT_TRUE(bit == start + i);
        }
        for (uint64 i = 0; i < bitmap.bit_capacity - start; i++) {
            auto bit = bitmap.get_first_set(start);
            ASSERT_TRUE(bit == start + i);
            bitmap.unset(bit);
        }
        ASSERT_TRUE(check_cuw_bitmap(bitmap, check_all_empty));
    }
}

TEST(Bitmap, PartialSetGetFirstSet) {
    const CuwBitmapArray check_all_empty = {};

    CuwBitmap bitmap{};

    for (uint start = 0; start < bitmap.bit_capacity; start++) {
        for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
            bitmap.set(i);
        }
    
        uint expected_bit = align(start, 2);
        for (uint64 i = expected_bit; i < bitmap.bit_capacity; i += 2 ) {
            auto bit = bitmap.get_first_set(start);
            ASSERT_TRUE(bit == i);
            bitmap.unset(bit);
        }
        bitmap.reset();
    
        ASSERT_TRUE(check_cuw_bitmap(bitmap, check_all_empty));
    }
}

TEST(Bitmap, AnySetAllReset) {
    const CuwBitmapArray check_all_empty = {};

    CuwBitmap bitmap{};

    // any_set / all_reset - basic
    bitmap.reset();
    ASSERT_TRUE(!bitmap.any_set());
    ASSERT_TRUE(bitmap.all_reset());

    bitmap.set(50);
    ASSERT_TRUE(bitmap.any_set());
    ASSERT_TRUE(!bitmap.all_reset());
    ASSERT_TRUE(!bitmap.any_set(51));
    ASSERT_TRUE(bitmap.any_set(50));

    // edge bits
    bitmap.reset();
    bitmap.set(0);
    ASSERT_TRUE(bitmap.any_set(0));
    ASSERT_TRUE(!bitmap.any_set(1));
    bitmap.reset();
    bitmap.set(95);
    ASSERT_TRUE(bitmap.any_set(95));

    // bin boundaries
    bitmap.reset();
    bitmap.set(63);
    ASSERT_TRUE(bitmap.any_set(63));
    ASSERT_TRUE(!bitmap.any_set(64));
    bitmap.set(64);
    ASSERT_TRUE(bitmap.any_set(64));
    ASSERT_TRUE(bitmap.any_set(0));

    // all bits set
    bitmap.reset();
    for (uint64 i = 0; i < bitmap.bit_capacity; i++) {
        bitmap.set(i);
    }
    ASSERT_TRUE(bitmap.any_set());
    ASSERT_TRUE(!bitmap.all_reset());
    for (uint64 i = 0; i < bitmap.bit_capacity; i++) {
        ASSERT_TRUE(bitmap.any_set(i));
    }

    // even bits
    bitmap.reset();
    for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
        bitmap.set(i);
    }
    ASSERT_TRUE(bitmap.any_set());
    ASSERT_TRUE(bitmap.any_set(0));
    ASSERT_TRUE(bitmap.any_set(2));
    ASSERT_TRUE(bitmap.any_set(1));
    ASSERT_TRUE(bitmap.any_set(63));

    // odd bits
    bitmap.reset();
    for (uint64 i = 1; i < bitmap.bit_capacity; i += 2) {
        bitmap.set(i);
    }
    ASSERT_TRUE(bitmap.any_set());
    ASSERT_TRUE(bitmap.any_set(1));
    ASSERT_TRUE(bitmap.any_set(0));
    ASSERT_TRUE(bitmap.any_set(64));

    bitmap.reset();

    ASSERT_TRUE(check_cuw_bitmap(bitmap, check_all_empty));
}

TEST(Bitmap, Count) {
    const CuwBitmapArray check_all_empty = {};

    CuwBitmap bitmap{};

    // count - basic
    bitmap.reset();
    ASSERT_TRUE(bitmap.count() == 0);

    bitmap.set(50);
    ASSERT_TRUE(bitmap.count() == 1);
    ASSERT_TRUE(bitmap.count(50) == 1);
    ASSERT_TRUE(bitmap.count(51) == 0);

    // edge bits
    bitmap.reset();
    bitmap.set(0);
    bitmap.set(95);
    ASSERT_TRUE(bitmap.count() == 2);
    ASSERT_TRUE(bitmap.count(1) == 1);
    ASSERT_TRUE(bitmap.count(95) == 1);

    // bin boundaries
    bitmap.reset();
    bitmap.set(63);
    bitmap.set(64);
    ASSERT_TRUE(bitmap.count() == 2);
    ASSERT_TRUE(bitmap.count(63) == 2);
    ASSERT_TRUE(bitmap.count(64) == 1);

    // all bits
    bitmap.reset();
    for (uint64 i = 0; i < bitmap.bit_capacity; i++) {
        bitmap.set(i);
    }
    ASSERT_TRUE(bitmap.count() == 96);
    ASSERT_TRUE(bitmap.count(64) == 32);
    ASSERT_TRUE(bitmap.count(32) == 64);

    // even bits
    bitmap.reset();
    for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
        bitmap.set(i);
    }
    ASSERT_TRUE(bitmap.count() == 48);
    ASSERT_TRUE(bitmap.count(64) == 16);
    ASSERT_TRUE(bitmap.count(1) == 47);

    // odd bits
    bitmap.reset();
    for (uint64 i = 1; i < bitmap.bit_capacity; i += 2) {
        bitmap.set(i);
    }
    ASSERT_TRUE(bitmap.count() == 48);
    ASSERT_TRUE(bitmap.count(64) == 16);
    ASSERT_TRUE(bitmap.count(2) == 47);

    bitmap.reset();

    ASSERT_TRUE(check_cuw_bitmap(bitmap, check_all_empty));
}

TEST(Bitmap, SampleSetBit) {
    const CuwBitmapArray check_all_empty = {};

    CuwBitmap bitmap{};

    // sample_set_bit - basic
    bitmap.reset();
    ASSERT_TRUE(bitmap.sample_set_bit(0) == bitmap.null_bit);

    bitmap.set(10);
    bitmap.set(30);
    bitmap.set(70);

    ASSERT_TRUE(bitmap.sample_set_bit(0) == 10);
    ASSERT_TRUE(bitmap.sample_set_bit(1) == 30);
    ASSERT_TRUE(bitmap.sample_set_bit(2) == 70);
    ASSERT_TRUE(bitmap.sample_set_bit(3) == 10);
    ASSERT_TRUE(bitmap.sample_set_bit(100) == 30);

    ASSERT_TRUE(bitmap.sample_set_bit(0, 20) == 30);
    ASSERT_TRUE(bitmap.sample_set_bit(1, 20) == 70);
    ASSERT_TRUE(bitmap.sample_set_bit(0, 71) == bitmap.null_bit);

    // edge bits
    bitmap.reset();
    bitmap.set(0);
    bitmap.set(95);
    ASSERT_TRUE(bitmap.sample_set_bit(0) == 0);
    ASSERT_TRUE(bitmap.sample_set_bit(1) == 95);
    ASSERT_TRUE(bitmap.sample_set_bit(0, 1) == 95);

    // bin boundaries
    bitmap.reset();
    bitmap.set(63);
    bitmap.set(64);
    ASSERT_TRUE(bitmap.sample_set_bit(0) == 63);
    ASSERT_TRUE(bitmap.sample_set_bit(1) == 64);
    ASSERT_TRUE(bitmap.sample_set_bit(0, 64) == 64);

    // single bit
    bitmap.reset();
    bitmap.set(42);
    for (uint64 seed = 0; seed < 10; seed++) {
        ASSERT_TRUE(bitmap.sample_set_bit(seed) == 42);
    }

    // all bits - verify we can sample each position
    bitmap.reset();
    for (uint64 i = 0; i < bitmap.bit_capacity; i++) {
        bitmap.set(i);
    }
    for (uint64 seed = 0; seed < bitmap.bit_capacity; seed++) {
        ASSERT_TRUE(bitmap.sample_set_bit(seed) == seed);
    }

    // even bits
    bitmap.reset();
    for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
        bitmap.set(i);
    }
    ASSERT_TRUE(bitmap.sample_set_bit(0) == 0);
    ASSERT_TRUE(bitmap.sample_set_bit(1) == 2);
    ASSERT_TRUE(bitmap.sample_set_bit(32) == 64);
    ASSERT_TRUE(bitmap.sample_set_bit(47) == 94);
    for (uint64 seed = 0; seed < 48; seed++) {
        gsize bit = bitmap.sample_set_bit(seed);
        ASSERT_TRUE(bit % 2 == 0);
        ASSERT_TRUE(bitmap.get(bit));
    }

    // odd bits
    bitmap.reset();
    for (uint64 i = 1; i < bitmap.bit_capacity; i += 2) {
        bitmap.set(i);
    }
    ASSERT_TRUE(bitmap.sample_set_bit(0) == 1);
    ASSERT_TRUE(bitmap.sample_set_bit(1) == 3);
    ASSERT_TRUE(bitmap.sample_set_bit(32) == 65);
    ASSERT_TRUE(bitmap.sample_set_bit(47) == 95);
    for (uint64 seed = 0; seed < 48; seed++) {
        gsize bit = bitmap.sample_set_bit(seed);
        ASSERT_TRUE(bit % 2 == 1);
        ASSERT_TRUE(bitmap.get(bit));
    }

    bitmap.reset();

    ASSERT_TRUE(check_cuw_bitmap(bitmap, check_all_empty));
}

TEST(Bitmap, GetLastSetBit) {
    const CuwBitmapArray check_all_empty = {};
    
    CuwBitmap bitmap{};

    bitmap.reset();
    ASSERT_TRUE(bitmap.get_last_set_bit() == bitmap.null_bit);

    bitmap.set(10);
    ASSERT_TRUE(bitmap.get_last_set_bit() == 10);

    bitmap.set(70);
    ASSERT_TRUE(bitmap.get_last_set_bit() == 70);

    bitmap.set(95);
    ASSERT_TRUE(bitmap.get_last_set_bit() == 95);

    ASSERT_TRUE(bitmap.get_last_set_bit(71) == 95);
    ASSERT_TRUE(bitmap.get_last_set_bit(11) == 95);
    ASSERT_TRUE(bitmap.get_last_set_bit(70) == 95);

    // edge bits
    bitmap.reset();
    bitmap.set(0);
    ASSERT_TRUE(bitmap.get_last_set_bit() == 0);
    ASSERT_TRUE(bitmap.get_last_set_bit(0) == 0);
    ASSERT_TRUE(bitmap.get_last_set_bit(1) == bitmap.null_bit);

    bitmap.reset();
    bitmap.set(95);
    ASSERT_TRUE(bitmap.get_last_set_bit() == 95);
    ASSERT_TRUE(bitmap.get_last_set_bit(0) == 95);
    ASSERT_TRUE(bitmap.get_last_set_bit(95) == 95);

    // bin boundaries
    bitmap.reset();
    bitmap.set(63);
    ASSERT_TRUE(bitmap.get_last_set_bit() == 63);
    ASSERT_TRUE(bitmap.get_last_set_bit(64) == bitmap.null_bit);

    bitmap.set(64);
    ASSERT_TRUE(bitmap.get_last_set_bit() == 64);
    ASSERT_TRUE(bitmap.get_last_set_bit(64) == 64);
    ASSERT_TRUE(bitmap.get_last_set_bit(63) == 64);

    // start excludes result
    bitmap.reset();
    bitmap.set(30);
    bitmap.set(50);
    bitmap.set(80);
    ASSERT_TRUE(bitmap.get_last_set_bit() == 80);
    ASSERT_TRUE(bitmap.get_last_set_bit(51) == 80);
    ASSERT_TRUE(bitmap.get_last_set_bit(31) == 80);
    ASSERT_TRUE(bitmap.get_last_set_bit(81) == bitmap.null_bit);

    // all bits
    bitmap.reset();
    for (uint64 i = 0; i < bitmap.bit_capacity; i++) {
        bitmap.set(i);
    }
    ASSERT_TRUE(bitmap.get_last_set_bit() == 95);
    for (uint64 start = 0; start < bitmap.bit_capacity; start++) {
        ASSERT_TRUE(bitmap.get_last_set_bit(start) == 95);
    }

    // even bits
    bitmap.reset();
    for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
        bitmap.set(i);
    }
    ASSERT_TRUE(bitmap.get_last_set_bit() == 94);
    ASSERT_TRUE(bitmap.get_last_set_bit(0) == 94);
    ASSERT_TRUE(bitmap.get_last_set_bit(64) == 94);
    ASSERT_TRUE(bitmap.get_last_set_bit(95) == bitmap.null_bit);
    ASSERT_TRUE(bitmap.get_last_set_bit(93) == 94);

    // odd bits
    bitmap.reset();
    for (uint64 i = 1; i < bitmap.bit_capacity; i += 2) {
        bitmap.set(i);
    }
    ASSERT_TRUE(bitmap.get_last_set_bit() == 95);
    ASSERT_TRUE(bitmap.get_last_set_bit(0) == 95);
    ASSERT_TRUE(bitmap.get_last_set_bit(64) == 95);
    ASSERT_TRUE(bitmap.get_last_set_bit(94) == 95);

    bitmap.reset();

    ASSERT_TRUE(check_cuw_bitmap(bitmap, check_all_empty));
}