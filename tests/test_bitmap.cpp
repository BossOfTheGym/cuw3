#include "cuw3/bitmap.hpp"

#include <format>
#include <iostream>

using namespace cuw3;

using CuwBitmap = Bitmap<uint64, 96>;

template<class T, T bits>
std::ostream& operator<<(std::ostream& os, const Bitmap<T, bits>& bm) {
    for (auto& bin : bm.bins) {
        os << std::format("{:016X}", bin) << " ";
    }
    return os;
}

void test_bitmap() {
    CuwBitmap bitmap{};

    {
        // all
        for (uint64 i = 0; i < bitmap.bit_capacity; i++) {
            bitmap.set(i);
        }
        std::cout << "all set" << std::endl;
        std::cout << bitmap << std::endl << std::endl;

        for (uint64 i = 0; i < bitmap.bit_capacity; i++) {
            bitmap.unset(i);
        }
        std::cout << "all unset" << std::endl;
        std::cout << bitmap << std::endl << std::endl;
    }

    {
        // even
        for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
            bitmap.set(i);
        }
        std::cout << "even set" << std::endl;
        std::cout << bitmap << std::endl << std::endl;

        for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
            bitmap.unset(i);
        }
        std::cout << "even unset" << std::endl;
        std::cout << bitmap << std::endl << std::endl;
    }

    {
        // odd
        for (uint64 i = 1; i < bitmap.bit_capacity; i += 2) {
            bitmap.set(i);
        }
        std::cout << "odd set" << std::endl;
        std::cout << bitmap << std::endl << std::endl;

        for (uint64 i = 1; i < bitmap.bit_capacity; i += 2) {
            bitmap.unset(i);
        }
        std::cout << "odd unset" << std::endl;
        std::cout << bitmap << std::endl << std::endl;
    }

    {
        // even & odd
        // even
        for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
            bitmap.set(i);
        }
        for (uint64 i = 1; i < bitmap.bit_capacity; i += 2) {
            bitmap.set(i);
        }
        std::cout << "even & odd set" << std::endl;
        std::cout << bitmap << std::endl << std::endl;

        for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
            bitmap.unset(i);
        }
        for (uint64 i = 1; i < bitmap.bit_capacity; i += 2) {
            bitmap.unset(i);
        }
        std::cout << "even & odd unset" << std::endl;
        std::cout << bitmap << std::endl << std::endl;
    }

    {
        // set_first_unset & get_first_set
        for (uint64 i = 0; i < bitmap.bit_capacity; i++){
            auto bit = bitmap.set_first_unset();
            CUW3_CHECK(bit != bitmap.null_bit, "set_first_unset does not work");
        }
        std::cout << "set_first_unset all set" << std::endl;
        std::cout << bitmap << std::endl << std::endl;

        for (uint64 i = 0; i < bitmap.bit_capacity; i++) {
            auto bit = bitmap.get_first_set();
            CUW3_CHECK(bit != bitmap.null_bit, "get_first_set does not work");
            bitmap.unset(bit);
        }
        std::cout << "get_first_set all unset" << std::endl;
        std::cout << bitmap << std::endl << std::endl;
    }

    {
        // partial set & get_first_set
        for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
            bitmap.set(i);
        }
        std::cout << "partial set & get_first_set set" << std::endl;
        std::cout << bitmap << std::endl << std::endl;

        while (true) {
            auto bit = bitmap.get_first_set();
            if (bit == bitmap.null_bit) {
                break;
            }
            bitmap.unset(bit);
        }
        std::cout << "partial set & get_first_set unset" << std::endl;
        std::cout << bitmap << std::endl << std::endl;
    }

    {
        // middle set & unset
        for (uint64 i = 48; i < 80; i++) {
            auto bit = bitmap.set_first_unset(48);
            CUW3_CHECK(bit != bitmap.null_bit, "set_first_unset does not work");
        }
        std::cout << "middle set & get_first_set set" << std::endl;
        std::cout << bitmap << std::endl << std::endl;

        for (uint64 i = 48; i < 80; i++) {
            auto bit = bitmap.get_first_set(34);
            CUW3_CHECK(bit != bitmap.null_bit, "get_first_set does not work");
            bitmap.unset(bit);
        }
        std::cout << "middle set & get_first_set unset" << std::endl;
        std::cout << bitmap << std::endl << std::endl;
    }

    {
        // any_set / all_reset - basic
        bitmap.reset();
        CUW3_CHECK(!bitmap.any_set(), "any_set should be false on empty bitmap");
        CUW3_CHECK(bitmap.all_reset(), "all_reset should be true on empty bitmap");

        bitmap.set(50);
        CUW3_CHECK(bitmap.any_set(), "any_set should be true after setting bit");
        CUW3_CHECK(!bitmap.all_reset(), "all_reset should be false after setting bit");
        CUW3_CHECK(!bitmap.any_set(51), "any_set(51) should be false when only bit 50 is set");
        CUW3_CHECK(bitmap.any_set(50), "any_set(50) should be true");

        // edge bits
        bitmap.reset();
        bitmap.set(0);
        CUW3_CHECK(bitmap.any_set(0), "any_set(0) with bit 0 set");
        CUW3_CHECK(!bitmap.any_set(1), "any_set(1) with only bit 0 set");
        bitmap.reset();
        bitmap.set(95);
        CUW3_CHECK(bitmap.any_set(95), "any_set(95) with bit 95 set");

        // bin boundaries
        bitmap.reset();
        bitmap.set(63);
        CUW3_CHECK(bitmap.any_set(63), "any_set(63) bin boundary");
        CUW3_CHECK(!bitmap.any_set(64), "any_set(64) should be false when only 63 set");
        bitmap.set(64);
        CUW3_CHECK(bitmap.any_set(64), "any_set(64) with bit 64 set");
        CUW3_CHECK(bitmap.any_set(0), "any_set(0) with bits 63,64 set");

        // all bits set
        bitmap.reset();
        for (uint64 i = 0; i < bitmap.bit_capacity; i++) {
            bitmap.set(i);
        }
        CUW3_CHECK(bitmap.any_set(), "any_set with all bits");
        CUW3_CHECK(!bitmap.all_reset(), "all_reset should be false with all bits");
        for (uint64 i = 0; i < bitmap.bit_capacity; i++) {
            CUW3_CHECK(bitmap.any_set(i), "any_set from each position with all bits");
        }

        // even bits
        bitmap.reset();
        for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
            bitmap.set(i);
        }
        CUW3_CHECK(bitmap.any_set(), "any_set with even bits");
        CUW3_CHECK(bitmap.any_set(0), "any_set(0) with even bits");
        CUW3_CHECK(bitmap.any_set(2), "any_set(2) with even bits");
        CUW3_CHECK(bitmap.any_set(1), "any_set(1) should find bit 2");
        CUW3_CHECK(bitmap.any_set(63), "any_set(63) should find bit 64");

        // odd bits
        bitmap.reset();
        for (uint64 i = 1; i < bitmap.bit_capacity; i += 2) {
            bitmap.set(i);
        }
        CUW3_CHECK(bitmap.any_set(), "any_set with odd bits");
        CUW3_CHECK(bitmap.any_set(1), "any_set(1) with odd bits");
        CUW3_CHECK(bitmap.any_set(0), "any_set(0) should find bit 1");
        CUW3_CHECK(bitmap.any_set(64), "any_set(64) should find bit 65");

        bitmap.reset();
        std::cout << "any_set / all_reset passed" << std::endl << std::endl;
    }

    {
        // count - basic
        bitmap.reset();
        CUW3_CHECK(bitmap.count() == 0, "count should be 0 on empty bitmap");

        bitmap.set(50);
        CUW3_CHECK(bitmap.count() == 1, "count should be 1");
        CUW3_CHECK(bitmap.count(50) == 1, "count(50) should be 1");
        CUW3_CHECK(bitmap.count(51) == 0, "count(51) should be 0");

        // edge bits
        bitmap.reset();
        bitmap.set(0);
        bitmap.set(95);
        CUW3_CHECK(bitmap.count() == 2, "count with bits 0,95");
        CUW3_CHECK(bitmap.count(1) == 1, "count(1) should be 1");
        CUW3_CHECK(bitmap.count(95) == 1, "count(95) should be 1");

        // bin boundaries
        bitmap.reset();
        bitmap.set(63);
        bitmap.set(64);
        CUW3_CHECK(bitmap.count() == 2, "count with bits 63,64");
        CUW3_CHECK(bitmap.count(63) == 2, "count(63) should be 2");
        CUW3_CHECK(bitmap.count(64) == 1, "count(64) should be 1");

        // all bits
        bitmap.reset();
        for (uint64 i = 0; i < bitmap.bit_capacity; i++) {
            bitmap.set(i);
        }
        CUW3_CHECK(bitmap.count() == 96, "count should be 96 for all bits");
        CUW3_CHECK(bitmap.count(64) == 32, "count(64) should be 32");
        CUW3_CHECK(bitmap.count(32) == 64, "count(32) should be 64");

        // even bits
        bitmap.reset();
        for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
            bitmap.set(i);
        }
        CUW3_CHECK(bitmap.count() == 48, "count should be 48 for even bits");
        CUW3_CHECK(bitmap.count(64) == 16, "count(64) should be 16 for even bits");
        CUW3_CHECK(bitmap.count(1) == 47, "count(1) should be 47 for even bits");

        // odd bits
        bitmap.reset();
        for (uint64 i = 1; i < bitmap.bit_capacity; i += 2) {
            bitmap.set(i);
        }
        CUW3_CHECK(bitmap.count() == 48, "count should be 48 for odd bits");
        CUW3_CHECK(bitmap.count(64) == 16, "count(64) should be 16 for odd bits");
        CUW3_CHECK(bitmap.count(2) == 47, "count(2) should be 47 for odd bits");

        bitmap.reset();
        std::cout << "count passed" << std::endl << std::endl;
    }

    {
        // sample_set_bit - basic
        bitmap.reset();
        CUW3_CHECK(bitmap.sample_set_bit(0) == bitmap.null_bit, "sample_set_bit should return null_bit on empty bitmap");

        bitmap.set(10);
        bitmap.set(30);
        bitmap.set(70);

        CUW3_CHECK(bitmap.sample_set_bit(0) == 10, "sample_set_bit(0) should return first set bit");
        CUW3_CHECK(bitmap.sample_set_bit(1) == 30, "sample_set_bit(1) should return second set bit");
        CUW3_CHECK(bitmap.sample_set_bit(2) == 70, "sample_set_bit(2) should return third set bit");
        CUW3_CHECK(bitmap.sample_set_bit(3) == 10, "sample_set_bit(3) should wrap around to first");
        CUW3_CHECK(bitmap.sample_set_bit(100) == 30, "sample_set_bit(100) should wrap around");

        CUW3_CHECK(bitmap.sample_set_bit(0, 20) == 30, "sample_set_bit(0, 20) should skip bit 10");
        CUW3_CHECK(bitmap.sample_set_bit(1, 20) == 70, "sample_set_bit(1, 20) should return 70");
        CUW3_CHECK(bitmap.sample_set_bit(0, 71) == bitmap.null_bit, "sample_set_bit(0, 71) should return null_bit");

        // edge bits
        bitmap.reset();
        bitmap.set(0);
        bitmap.set(95);
        CUW3_CHECK(bitmap.sample_set_bit(0) == 0, "sample_set_bit(0) with bits 0,95");
        CUW3_CHECK(bitmap.sample_set_bit(1) == 95, "sample_set_bit(1) with bits 0,95");
        CUW3_CHECK(bitmap.sample_set_bit(0, 1) == 95, "sample_set_bit(0, 1) should return 95");

        // bin boundaries
        bitmap.reset();
        bitmap.set(63);
        bitmap.set(64);
        CUW3_CHECK(bitmap.sample_set_bit(0) == 63, "sample_set_bit(0) bin boundary");
        CUW3_CHECK(bitmap.sample_set_bit(1) == 64, "sample_set_bit(1) bin boundary");
        CUW3_CHECK(bitmap.sample_set_bit(0, 64) == 64, "sample_set_bit(0, 64) should return 64");

        // single bit
        bitmap.reset();
        bitmap.set(42);
        for (uint64 seed = 0; seed < 10; seed++) {
            CUW3_CHECK(bitmap.sample_set_bit(seed) == 42, "sample_set_bit with single bit should always return 42");
        }

        // all bits - verify we can sample each position
        bitmap.reset();
        for (uint64 i = 0; i < bitmap.bit_capacity; i++) {
            bitmap.set(i);
        }
        for (uint64 seed = 0; seed < bitmap.bit_capacity; seed++) {
            CUW3_CHECK(bitmap.sample_set_bit(seed) == seed, "sample_set_bit should return seed for all-set bitmap");
        }

        // even bits
        bitmap.reset();
        for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
            bitmap.set(i);
        }
        CUW3_CHECK(bitmap.sample_set_bit(0) == 0, "sample even bit 0");
        CUW3_CHECK(bitmap.sample_set_bit(1) == 2, "sample even bit 1 -> 2");
        CUW3_CHECK(bitmap.sample_set_bit(32) == 64, "sample even bit 32 -> 64");
        CUW3_CHECK(bitmap.sample_set_bit(47) == 94, "sample even bit 47 -> 94");
        for (uint64 seed = 0; seed < 48; seed++) {
            gsize bit = bitmap.sample_set_bit(seed);
            CUW3_CHECK(bit % 2 == 0, "sampled bit should be even");
            CUW3_CHECK(bitmap.get(bit), "sampled bit should be set");
        }

        // odd bits
        bitmap.reset();
        for (uint64 i = 1; i < bitmap.bit_capacity; i += 2) {
            bitmap.set(i);
        }
        CUW3_CHECK(bitmap.sample_set_bit(0) == 1, "sample odd bit 0 -> 1");
        CUW3_CHECK(bitmap.sample_set_bit(1) == 3, "sample odd bit 1 -> 3");
        CUW3_CHECK(bitmap.sample_set_bit(32) == 65, "sample odd bit 32 -> 65");
        CUW3_CHECK(bitmap.sample_set_bit(47) == 95, "sample odd bit 47 -> 95");
        for (uint64 seed = 0; seed < 48; seed++) {
            gsize bit = bitmap.sample_set_bit(seed);
            CUW3_CHECK(bit % 2 == 1, "sampled bit should be odd");
            CUW3_CHECK(bitmap.get(bit), "sampled bit should be set");
        }

        bitmap.reset();
        std::cout << "sample_set_bit passed" << std::endl << std::endl;
    }

    {
        // get_last_set_bit - basic
        bitmap.reset();
        CUW3_CHECK(bitmap.get_last_set_bit() == bitmap.null_bit, "get_last_set_bit should return null_bit on empty bitmap");

        bitmap.set(10);
        CUW3_CHECK(bitmap.get_last_set_bit() == 10, "get_last_set_bit should return 10");

        bitmap.set(70);
        CUW3_CHECK(bitmap.get_last_set_bit() == 70, "get_last_set_bit should return 70");

        bitmap.set(95);
        CUW3_CHECK(bitmap.get_last_set_bit() == 95, "get_last_set_bit should return 95 (last bit)");

        CUW3_CHECK(bitmap.get_last_set_bit(71) == 95, "get_last_set_bit(71) should return 95");
        CUW3_CHECK(bitmap.get_last_set_bit(11) == 95, "get_last_set_bit(11) should return 95");
        CUW3_CHECK(bitmap.get_last_set_bit(70) == 95, "get_last_set_bit(70) should return 95");

        // edge bits
        bitmap.reset();
        bitmap.set(0);
        CUW3_CHECK(bitmap.get_last_set_bit() == 0, "get_last_set_bit with only bit 0");
        CUW3_CHECK(bitmap.get_last_set_bit(0) == 0, "get_last_set_bit(0) with only bit 0");
        CUW3_CHECK(bitmap.get_last_set_bit(1) == bitmap.null_bit, "get_last_set_bit(1) should be null when only 0 set");

        bitmap.reset();
        bitmap.set(95);
        CUW3_CHECK(bitmap.get_last_set_bit() == 95, "get_last_set_bit with only bit 95");
        CUW3_CHECK(bitmap.get_last_set_bit(0) == 95, "get_last_set_bit(0) with only bit 95");
        CUW3_CHECK(bitmap.get_last_set_bit(95) == 95, "get_last_set_bit(95) with only bit 95");

        // bin boundaries
        bitmap.reset();
        bitmap.set(63);
        CUW3_CHECK(bitmap.get_last_set_bit() == 63, "get_last_set_bit with bit 63");
        CUW3_CHECK(bitmap.get_last_set_bit(64) == bitmap.null_bit, "get_last_set_bit(64) should be null when only 63 set");

        bitmap.set(64);
        CUW3_CHECK(bitmap.get_last_set_bit() == 64, "get_last_set_bit with bits 63,64");
        CUW3_CHECK(bitmap.get_last_set_bit(64) == 64, "get_last_set_bit(64) should return 64");
        CUW3_CHECK(bitmap.get_last_set_bit(63) == 64, "get_last_set_bit(63) should return 64");

        // start excludes result
        bitmap.reset();
        bitmap.set(30);
        bitmap.set(50);
        bitmap.set(80);
        CUW3_CHECK(bitmap.get_last_set_bit() == 80, "get_last_set_bit should return 80");
        CUW3_CHECK(bitmap.get_last_set_bit(51) == 80, "get_last_set_bit(51) should return 80");
        CUW3_CHECK(bitmap.get_last_set_bit(31) == 80, "get_last_set_bit(31) should return 80");
        CUW3_CHECK(bitmap.get_last_set_bit(81) == bitmap.null_bit, "get_last_set_bit(81) should be null");

        // all bits
        bitmap.reset();
        for (uint64 i = 0; i < bitmap.bit_capacity; i++) {
            bitmap.set(i);
        }
        CUW3_CHECK(bitmap.get_last_set_bit() == 95, "get_last_set_bit with all bits");
        for (uint64 start = 0; start < bitmap.bit_capacity; start++) {
            CUW3_CHECK(bitmap.get_last_set_bit(start) == 95, "get_last_set_bit from any start with all bits");
        }

        // even bits
        bitmap.reset();
        for (uint64 i = 0; i < bitmap.bit_capacity; i += 2) {
            bitmap.set(i);
        }
        CUW3_CHECK(bitmap.get_last_set_bit() == 94, "get_last_set_bit with even bits should be 94");
        CUW3_CHECK(bitmap.get_last_set_bit(0) == 94, "get_last_set_bit(0) even");
        CUW3_CHECK(bitmap.get_last_set_bit(64) == 94, "get_last_set_bit(64) even");
        CUW3_CHECK(bitmap.get_last_set_bit(95) == bitmap.null_bit, "get_last_set_bit(95) even should be null");
        CUW3_CHECK(bitmap.get_last_set_bit(93) == 94, "get_last_set_bit(93) even should be 94");

        // odd bits
        bitmap.reset();
        for (uint64 i = 1; i < bitmap.bit_capacity; i += 2) {
            bitmap.set(i);
        }
        CUW3_CHECK(bitmap.get_last_set_bit() == 95, "get_last_set_bit with odd bits should be 95");
        CUW3_CHECK(bitmap.get_last_set_bit(0) == 95, "get_last_set_bit(0) odd");
        CUW3_CHECK(bitmap.get_last_set_bit(64) == 95, "get_last_set_bit(64) odd");
        CUW3_CHECK(bitmap.get_last_set_bit(94) == 95, "get_last_set_bit(94) odd should be 95");

        bitmap.reset();
        std::cout << "get_last_set_bit passed" << std::endl << std::endl;
    }
}

int main() {
    test_bitmap();
    return 0;
}