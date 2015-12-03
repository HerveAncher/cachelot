#include "unit_test.h"
#include <cachelot/memalloc.h>


namespace {

using namespace cachelot;

static constexpr size_t MEMSIZE = 4 * Megabyte;
static constexpr size_t PAGESIZE = 4 * Kilobyte;
static constexpr size_t NUM_ALLOC = 100000;
static constexpr size_t NUM_REPEAT = 50;
static constexpr size_t MIN_ALLOC_SIZE = 4;
static constexpr size_t MAX_ALLOC_SIZE = PAGESIZE - 64;

// there is no memalloc in the AddressSanitizer build
#ifndef ADDRESS_SANITIZER

BOOST_AUTO_TEST_SUITE(test_memalloc)

// return `true` with probability of given `persents`
inline bool probably(unsigned persents) noexcept {
    debug_assert(persents <= 100);
    random_int<unsigned> chance(1, 100);
    return chance() > (100u - persents);
}


template <class Container>
typename Container::iterator random_choise(Container & c) noexcept {
    random_int<> random_offset(0, c.size() - 1);
    return c.begin() + random_offset();
}


BOOST_AUTO_TEST_CASE(test_free_blocks_by_size) {
    constexpr size_t page_size = 4*Kilobyte;
	memalloc::free_blocks_by_size fixture(page_size);
    // Test position_from_size
    if (memalloc::free_blocks_by_size::first_power_of_2 == 8) {
        // Small blocks (zero cell)
        auto pos = fixture.position_from_size(64);
        BOOST_CHECK_EQUAL(pos.pow_index, 0); BOOST_CHECK_EQUAL(pos.sub_index, 8);
        pos = fixture.position_from_size(63);
        BOOST_CHECK_EQUAL(pos.pow_index, 0); BOOST_CHECK_EQUAL(pos.sub_index, 7);
        pos = fixture.position_from_size(65);
        BOOST_CHECK_EQUAL(pos.pow_index, 0); BOOST_CHECK_EQUAL(pos.sub_index, 8);
        pos = fixture.position_from_size(71);
        BOOST_CHECK_EQUAL(pos.pow_index, 0); BOOST_CHECK_EQUAL(pos.sub_index, 8);
        pos = fixture.position_from_size(255);
        BOOST_CHECK_EQUAL(pos.pow_index, 0); BOOST_CHECK_EQUAL(pos.sub_index, 31);
        // Normal blocks
        pos = fixture.position_from_size(256);
        BOOST_CHECK_EQUAL(pos.pow_index, 1); BOOST_CHECK_EQUAL(pos.sub_index, 0);
        pos = fixture.position_from_size(MAX_ALLOC_SIZE);
        BOOST_CHECK_EQUAL(pos.pow_index, 4); BOOST_CHECK_EQUAL(pos.sub_index, 31);
        pos = fixture.position_from_size(1024);
        BOOST_CHECK_EQUAL(pos.pow_index, 3); BOOST_CHECK_EQUAL(pos.sub_index, 0);
        pos = fixture.position_from_size(1023);
        BOOST_CHECK_EQUAL(pos.pow_index, 2); BOOST_CHECK_EQUAL(pos.sub_index, 31);
        pos = fixture.position_from_size(2345);
        BOOST_CHECK_EQUAL(pos.pow_index, 4); BOOST_CHECK_EQUAL(pos.sub_index, 4);
    } else if (memalloc::free_blocks_by_size::first_power_of_2 == 7) {
        // TODO: Tests for 32-bit platform
    } else {
        BOOST_ERROR("Unexpected free_blocks_by_size::first_power_of_2");
    }
    // Test try_get_block / next_non_empty
    {
        uint8 _blk1_mem[sizeof(memalloc::block)]; uint8 _blk2_mem[sizeof(memalloc::block)];
        memalloc::block * blk1 = new (_blk1_mem) memalloc::block(128, 0);
        memalloc::block * blk2 = new (_blk2_mem) memalloc::block(128, 0);
        memalloc::block * result_block;
        // small blocks
        blk1->meta.size = 255; fixture.put_block(blk1);
        result_block = fixture.try_get_block(255);
        BOOST_CHECK(result_block == blk1);

        blk1->meta.size = 255; fixture.put_block(blk1);
        blk2->meta.size = 256; fixture.put_block(blk2);
        result_block = fixture.try_get_block(256);
        BOOST_CHECK(result_block == blk2);
        result_block = fixture.try_get_block(123);
        BOOST_CHECK(result_block == blk1);

        blk1->meta.size = 255; fixture.put_block(blk1);
        blk2->meta.size = 1120; fixture.put_block(blk2);
        result_block = fixture.try_get_block(1121);
        BOOST_CHECK(result_block == nullptr);
        result_block = fixture.try_get_block(255);
        BOOST_CHECK(result_block == blk1);
        result_block = fixture.try_get_block(255);
        BOOST_CHECK(result_block == blk2);
        result_block = fixture.try_get_block(255);
        BOOST_CHECK(result_block == nullptr);
    }
}


BOOST_AUTO_TEST_CASE(test_pages) {
    memalloc::pages fixture(4, (uint8 * const)0, (uint8 * const)16);
    BOOST_CHECK_EQUAL(fixture.num_pages, 4);
    // page_info_from_addr
    auto page = fixture.page_info_from_addr((void *)0);
    BOOST_CHECK(page == &fixture.all_pages[0]);
    page = fixture.page_info_from_addr((void *)4);
    BOOST_CHECK(page == &fixture.all_pages[1]);
    page = fixture.page_info_from_addr((void *)7);
    BOOST_CHECK(page == &fixture.all_pages[1]);
    page = fixture.page_info_from_addr((void *)15);
    BOOST_CHECK(page == &fixture.all_pages[3]);
    // page_boundaries_from_addr
    const uint8 * page_beg; const uint8 * page_end;
    tie(page_beg, page_end) = fixture.page_boundaries_from_addr((void *)0);
    BOOST_CHECK(page_beg == (uint8 *)0);
    BOOST_CHECK(page_end == (uint8 *)4);
    tie(page_beg, page_end) = fixture.page_boundaries_from_addr((void *)4);
    BOOST_CHECK(page_beg == (uint8 *)4);
    BOOST_CHECK(page_end == (uint8 *)8);
    tie(page_beg, page_end) = fixture.page_boundaries_from_addr((void *)14);
    BOOST_CHECK(page_beg == (uint8 *)12);
    BOOST_CHECK(page_end == (uint8 *)16);
    tie(page_beg, page_end) = fixture.page_boundaries_from_addr((void *)15);
    BOOST_CHECK(page_beg == (uint8 *)12);
    BOOST_CHECK(page_end == (uint8 *)16);
    // touch
}

// allocate and free blocks of a random size
// in case of the internal inconsistency, memalloc will trigger internal failure calling debug_assert
//
BOOST_AUTO_TEST_CASE(memalloc_stress_test) {
    // setup
    memalloc allocator(MEMSIZE, PAGESIZE);
    random_int<size_t> random_size(MIN_ALLOC_SIZE, MAX_ALLOC_SIZE);
    std::vector<void * > allocations;
    allocations.reserve(NUM_ALLOC);

    // run test NUM_REPEAT times
    for (size_t repeat_no = 0; repeat_no < NUM_REPEAT; ++repeat_no) {

        // random allocations / deallocations
        for (size_t allocation_no = 0; allocation_no < NUM_ALLOC; ++allocation_no) {
            // try to allocate new element
            // if we need to evict existing elements to free space, remove it from the allocations list
            void * ptr = allocator.alloc_or_evict(random_size(), true,
                [&allocations](void * mem) {
                    for (size_t i = 0; i < allocations.size(); ++i) {
                        if (allocations[i] == mem) {
                            allocations[i] = allocations.back();
                            allocations.pop_back();
                            return;
                        }
                    }
                    debug_assert(false && "unknown pointer");
                });
            if (ptr != nullptr) {
                allocations.push_back(ptr);
            }

            // free one of previously allocated blocks with 40% probability
            if (not allocations.empty() && probably(40)) {
                auto prev_alloc = random_choise(allocations);
                BOOST_CHECK(*prev_alloc != nullptr);
                allocator.free(*prev_alloc);
                // remove pointer from the vector
                *prev_alloc = allocations.back();
                allocations.pop_back();
            }

            // reallocate one of previously allocated blocks with 60% probability
            if (not allocations.empty() && probably(60)) {
                auto prev_alloc = random_choise(allocations);
                allocator.realloc_inplace(*prev_alloc, random_size());
            }
        }
        // free all previously allocated memory
        while (not allocations.empty()) {
            void * ptr = allocations.back();
            allocator.free(ptr);
            allocations.pop_back();
        }
        // start over again
    }
}


BOOST_AUTO_TEST_SUITE_END()

#endif // ifndef ADDRESS_SANITIZER

} // anonymouse namespace

