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

// TODO : split this up to several tests
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
}

int main() {
    test_bitmap();
    return 0;
}