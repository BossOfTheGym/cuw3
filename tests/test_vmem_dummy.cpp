#include <cstring>
#include <iostream>

#include "cuw3/vmem.hpp"
#include "cuw3/utils.hpp"

int main() {
	std::cout << "page_size: " << cuw3::vmem_page_size() << std::endl;
	std::cout << "huge_page_size: " << cuw3::vmem_huge_page_size() << std::endl;
	std::cout << "allocation granularity: " << cuw3::vmem_alloc_granularity() << std::endl;

	std::cout << std::endl;

	void* alloc = cuw3::vmem_alloc(1 << 20, cuw3::VMemReserveCommit);
	std::cout << "alloc: " << alloc << std::endl;
	cuw3::vmem_free(alloc, 1 << 20);

	std::cout << std::endl;

	void* alloc_aligned = cuw3::vmem_alloc_aligned(1 << 23, cuw3::VMemReserveCommit, 1 << 22);
	std::cout << "alloc_aligned: " << alloc_aligned << " aligned: " << cuw3::is_aligned(alloc_aligned, 1 << 22) << std::endl;
	cuw3::vmem_free(alloc_aligned, 1 << 23);

	std::cout << std::endl;

	void* alloc_reserved = cuw3::vmem_alloc(1 << 20, cuw3::VMemReserve);
	std::cout << "alloc_reserved: " << alloc_reserved << std::endl;
	if (cuw3::vmem_commit(alloc_reserved, 1 << 20)) {
		std::cout << "committed!" << std::endl;
	}
	if (cuw3::vmem_decommit(alloc_reserved, 1 << 20)) {
		std::cout << "decommitted!" << std::endl;
	}
	cuw3::vmem_free(alloc_reserved, 1 << 20);

	std::cout << std::endl;

	cuw3::usize huge_size_log2 = 40;
	cuw3::usize huge_size = (cuw3::usize)1 << huge_size_log2;
	cuw3::usize huge_alignment_log2 = 30;
	cuw3::usize huge_alignment = (cuw3::usize)1 << huge_alignment_log2;
	cuw3::usize scatter_size_log2 = 30;

	void* huge_alloc = cuw3::vmem_alloc_aligned(huge_size, cuw3::VMemReserve, huge_alignment);
	std::cout << "huge_alloc: " << huge_alloc << " aligned: " << cuw3::is_aligned(huge_alloc, huge_alignment) << std::endl;
	if (huge_alloc) {
		cuw3::usize page_size = cuw3::vmem_page_size();
		cuw3::usize page_size_log2 = cuw3::intlog2(page_size);

		cuw3::usize stride_log2 = huge_size_log2 - page_size_log2 - (scatter_size_log2 - page_size_log2) + page_size_log2;
		cuw3::usize stride = (cuw3::usize)1 << stride_log2;
		cuw3::usize strides = (cuw3::usize)1 << (huge_size_log2 - stride_log2);

		void* addr = huge_alloc;
		for (cuw3::usize i = 0; i < strides; i++) {
			if (cuw3::vmem_commit(addr, page_size)) {
				std::memset(addr, 0xFF, page_size);
			} else {
				std::cerr << "failed to commit page at offset " << cuw3::subptr(addr, huge_alloc) << std::endl;
				break;
			}
			addr = cuw3::advance_ptr(addr, stride);
		}
		if (cuw3::vmem_decommit(huge_alloc, huge_size)) {
			std::cout << "decommitted!" << std::endl;
		}
	}
	cuw3::vmem_free(huge_alloc, huge_size);

	return 0;
}