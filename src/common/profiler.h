#ifndef PS5SIM_COMMON_PROFILER_H_
#define PS5SIM_COMMON_PROFILER_H_

#include "common/common.h"
#include "common/subsystems.h"

#include <cstdint>
#include <optional>
#include <tracy/Tracy.hpp> // IWYU pragma: export

namespace profiler::colors {

inline constexpr uint32_t RedA100        = 0xff8a80;
inline constexpr uint32_t Blue300        = 0x64b5f6;
inline constexpr uint32_t CyanA700       = 0x00b8d4;
inline constexpr uint32_t Green100       = 0xc8e6c9;
inline constexpr uint32_t Green200       = 0xa5d6a7;
inline constexpr uint32_t Green300       = 0x81c784;
inline constexpr uint32_t Green400       = 0x66bb6a;
inline constexpr uint32_t Amber300       = 0xffd54f;
inline constexpr uint32_t DeepOrangeA200 = 0xff6e40;

} // namespace profiler::colors

namespace Profiler {

class ScopedBlock {
public:
	explicit ScopedBlock(const tracy::SourceLocationData* source_location);
	ScopedBlock(const ScopedBlock&)            = delete;
	ScopedBlock& operator=(const ScopedBlock&) = delete;
	ScopedBlock(ScopedBlock&&)                 = delete;
	ScopedBlock& operator=(ScopedBlock&&)      = delete;
	~ScopedBlock();

	void End();

private:
	std::optional<tracy::ScopedZone> m_zone;
};

void EndBlock();
void SetThreadName(const char* name);

PS5SIM_SUBSYSTEM_DEFINE(Profiler);

} // namespace Profiler

#define PS5SIM_PROFILER_CONCAT_IMPL(a, b) a##b
#define PS5SIM_PROFILER_CONCAT(a, b)      PS5SIM_PROFILER_CONCAT_IMPL(a, b)
#define PS5SIM_PROFILER_COLOR_OR_DEFAULT(default_color, ...)                                         \
	PS5SIM_PROFILER_COLOR_OR_DEFAULT_IMPL(default_color __VA_OPT__(, ) __VA_ARGS__, default_color)
#define PS5SIM_PROFILER_COLOR_OR_DEFAULT_IMPL(default_color, color, ...) color

#define PS5SIM_PROFILER_BLOCK(name, ...)                                                             \
	PS5SIM_PROFILER_BLOCK_IMPL(__LINE__, name __VA_OPT__(, ) __VA_ARGS__)
#define PS5SIM_PROFILER_BLOCK_IMPL(line, name, ...)                                                  \
	static constexpr tracy::SourceLocationData PS5SIM_PROFILER_CONCAT(                               \
	    ps5sim_profiler_source_location_, line) {name, TracyFunction, TracyFile,                     \
	                                           static_cast<uint32_t>(line),                        \
	                                           PS5SIM_PROFILER_COLOR_OR_DEFAULT(0, __VA_ARGS__)};    \
	Profiler::ScopedBlock PS5SIM_PROFILER_CONCAT(ps5sim_profiler_block_, line)(                        \
	    &PS5SIM_PROFILER_CONCAT(ps5sim_profiler_source_location_, line))

#define PS5SIM_PROFILER_FUNCTION(...) PS5SIM_PROFILER_FUNCTION_IMPL(__LINE__ __VA_OPT__(, ) __VA_ARGS__)
#define PS5SIM_PROFILER_FUNCTION_IMPL(line, ...)                                                     \
	static constexpr tracy::SourceLocationData PS5SIM_PROFILER_CONCAT(                               \
	    ps5sim_profiler_source_location_, line) {nullptr, TracyFunction, TracyFile,                  \
	                                           static_cast<uint32_t>(line),                        \
	                                           PS5SIM_PROFILER_COLOR_OR_DEFAULT(0, __VA_ARGS__)};    \
	Profiler::ScopedBlock PS5SIM_PROFILER_CONCAT(ps5sim_profiler_block_, line)(                        \
	    &PS5SIM_PROFILER_CONCAT(ps5sim_profiler_source_location_, line))

#define PS5SIM_PROFILER_END_BLOCK Profiler::EndBlock()

#define PS5SIM_PROFILER_THREAD(name) Profiler::SetThreadName(name)

#endif /* PS5SIM_COMMON_PROFILER_H_ */
