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

	return 0;
}