#ifndef PS5SIM_LOADER_X64_INSTRUCTION_EMULATOR_H_
#define PS5SIM_LOADER_X64_INSTRUCTION_EMULATOR_H_

namespace Loader::X64InstructionEmulator {

[[nodiscard]] bool TryEmulate(void* native_context);

} // namespace Loader::X64InstructionEmulator

#endif /* PS5SIM_LOADER_X64_INSTRUCTION_EMULATOR_H_ */
