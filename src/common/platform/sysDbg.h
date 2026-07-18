#ifndef PS5SIM_COMMON_PLATFORM_SYSDBG_H_
#define PS5SIM_COMMON_PLATFORM_SYSDBG_H_

#include "common/common.h"

// NOLINTNEXTLINE(readability-identifier-naming)
struct sys_dbg_stack_info_t {
#if PS5SIM_PLATFORM == PS5SIM_PLATFORM_WINDOWS
	uintptr_t addr;

	uintptr_t reserved_addr;
	size_t    reserved_size;
	uintptr_t guard_addr;
	size_t    guard_size;
	uintptr_t commited_addr;
	size_t    commited_size;

	size_t total_size;
#elif PS5SIM_PLATFORM == PS5SIM_PLATFORM_LINUX
	uintptr_t code_addr;
	uintptr_t addr;
	uintptr_t commited_addr;
	size_t    commited_size;
	size_t    total_size;
	size_t    code_size;
#endif
};

void SysStackWalk(void** stack, int* depth);
void SysStackUsage(sys_dbg_stack_info_t& s);          // NOLINT(google-runtime-references)
void SysStackUsagePrint(sys_dbg_stack_info_t& stack); // NOLINT(google-runtime-references)

#endif /* PS5SIM_COMMON_PLATFORM_SYSDBG_H_ */
