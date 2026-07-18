#ifndef EMULATOR_INCLUDE_EMULATOR_KERNEL_EVENTFLAG_H_
#define EMULATOR_INCLUDE_EMULATOR_KERNEL_EVENTFLAG_H_

#include "common/abi.h"
#include "common/common.h"
#include "kernel/pthread.h"

namespace Libs::LibKernel::EventFlag {

class KernelEventFlagPrivate;

using KernelEventFlag = KernelEventFlagPrivate*;

int PS5SIM_SYSV_ABI KernelCreateEventFlag(KernelEventFlag* ef, const char* name, uint32_t attr,
                                        uint64_t init_pattern, const void* param);
int PS5SIM_SYSV_ABI KernelDeleteEventFlag(KernelEventFlag ef);
int PS5SIM_SYSV_ABI KernelWaitEventFlag(KernelEventFlag ef, uint64_t bit_pattern, uint32_t wait_mode,
                                      uint64_t* result_pat, KernelUseconds* timeout);
int PS5SIM_SYSV_ABI KernelPollEventFlag(KernelEventFlag ef, uint64_t bit_pattern, uint32_t wait_mode,
                                      uint64_t* result_pat);
int PS5SIM_SYSV_ABI KernelSetEventFlag(KernelEventFlag ef, uint64_t bit_pattern);
int PS5SIM_SYSV_ABI KernelClearEventFlag(KernelEventFlag ef, uint64_t bit_pattern);
int PS5SIM_SYSV_ABI KernelCancelEventFlag(KernelEventFlag ef, uint64_t set_pattern,
                                        int* num_wait_threads);

} // namespace Libs::LibKernel::EventFlag

#endif /* EMULATOR_INCLUDE_EMULATOR_KERNEL_EVENTFLAG_H_ */
