#ifndef EMULATOR_INCLUDE_EMULATOR_KERNEL_EVENTQUEUE_H_
#define EMULATOR_INCLUDE_EMULATOR_KERNEL_EVENTQUEUE_H_

#include "common/abi.h"
#include "common/common.h"
#include "kernel/pthread.h"

#include <deque>

namespace Libs::LibKernel::EventQueue {

constexpr int16_t KERNEL_EVFILT_TIMER     = -7;
constexpr int16_t KERNEL_EVFILT_READ      = -1;
constexpr int16_t KERNEL_EVFILT_WRITE     = -2;
constexpr int16_t KERNEL_EVFILT_USER      = -11;
constexpr int16_t KERNEL_EVFILT_FILE      = -4;
constexpr int16_t KERNEL_EVFILT_GRAPHICS  = -14;
constexpr int16_t KERNEL_EVFILT_VIDEO_OUT = -13;
constexpr int16_t KERNEL_EVFILT_HRTIMER   = -15;

class KernelEqueuePrivate;
struct KernelEqueueEvent;

using KernelEqueue = KernelEqueuePrivate*;

using trigger_func_t = void (*)(KernelEqueueEvent* event, void* trigger_data);
using reset_func_t   = void (*)(KernelEqueueEvent* event);
using delete_func_t  = void (*)(KernelEqueue eq, KernelEqueueEvent* event);

struct KernelEvent {
	uintptr_t ident  = 0;
	int16_t   filter = 0;
	uint16_t  flags  = 0;
	uint32_t  fflags = 0;
	intptr_t  data   = 0;
	void*     udata  = nullptr;
};

struct KernelFilter {
	void*          data              = nullptr;
	trigger_func_t trigger_func      = nullptr;
	reset_func_t   reset_func        = nullptr;
	delete_func_t  delete_event_func = nullptr;
};

struct KernelEqueueEvent {
	bool                    triggered   = false;
	uint64_t                deadline_ns = 0;
	KernelEvent             event;
	KernelFilter            filter;
	std::deque<KernelEvent> pending_events;
};

int PS5SIM_SYSV_ABI KernelAddEvent(KernelEqueue eq, const KernelEqueueEvent& event);
int PS5SIM_SYSV_ABI KernelTriggerEvent(KernelEqueue eq, uintptr_t ident, int16_t filter,
                                     void* trigger_data);
int PS5SIM_SYSV_ABI KernelDeleteEvent(KernelEqueue eq, uintptr_t ident, int16_t filter);

int PS5SIM_SYSV_ABI KernelCreateEqueue(KernelEqueue* eq, const char* name);
int PS5SIM_SYSV_ABI KernelDeleteEqueue(KernelEqueue eq);
int PS5SIM_SYSV_ABI KernelWaitEqueue(KernelEqueue eq, KernelEvent* ev, int num, int* out,
                                   const KernelUseconds* timo);
int PS5SIM_SYSV_ABI KernelAddUserEvent(KernelEqueue eq, int id);
int PS5SIM_SYSV_ABI KernelAddUserEventEdge(KernelEqueue eq, int id);
int PS5SIM_SYSV_ABI KernelTriggerUserEvent(KernelEqueue eq, int id, void* udata);
int PS5SIM_SYSV_ABI KernelTriggerUserEventForAll(int id, void* udata);
int PS5SIM_SYSV_ABI KernelDeleteUserEvent(KernelEqueue eq, int id);
int PS5SIM_SYSV_ABI KernelAddHRTimerEvent(KernelEqueue eq, int id, const KernelTimespec* ts,
                                        void* udata);
int PS5SIM_SYSV_ABI KernelDeleteHRTimerEvent(KernelEqueue eq, int id);
int PS5SIM_SYSV_ABI KernelAddAmprEvent(KernelEqueue eq, int id, void* udata);
int PS5SIM_SYSV_ABI KernelAddAmprSystemEvent(KernelEqueue eq, int id, void* udata);
int PS5SIM_SYSV_ABI KernelDeleteAmprEvent(KernelEqueue eq, int id);
int PS5SIM_SYSV_ABI KernelDeleteAmprSystemEvent(KernelEqueue eq, int id);

intptr_t PS5SIM_SYSV_ABI  KernelGetEventData(const KernelEvent* ev);
intptr_t PS5SIM_SYSV_ABI  KernelGetEventFflags(const KernelEvent* ev);
int PS5SIM_SYSV_ABI       KernelGetEventFilter(const KernelEvent* ev);
uintptr_t PS5SIM_SYSV_ABI KernelGetEventId(const KernelEvent* ev);
void* PS5SIM_SYSV_ABI     KernelGetEventUserData(const KernelEvent* ev);
int PS5SIM_SYSV_ABI       KernelGetEventError(const KernelEvent* ev);

} // namespace Libs::LibKernel::EventQueue

#endif /* EMULATOR_INCLUDE_EMULATOR_KERNEL_EVENTQUEUE_H_ */
