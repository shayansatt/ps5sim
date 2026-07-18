#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_RENDERINTERNAL_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_RENDERINTERNAL_H_

#include "common/assert.h"
#include "graphics/shader/recompiler/ShaderIR.h"

#include <cstring>

namespace Libs::Graphics {

template <typename T>
T DecodeNativeDescriptor(const ShaderRecompiler::IR::DescriptorValue& value) {
	T result {};
	EXIT_IF(value.dword_count < sizeof(result) / sizeof(uint32_t));
	std::memcpy(&result, value.dwords.data(), sizeof(result));
	return result;
}

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_RENDERINTERNAL_H_
