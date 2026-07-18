#ifndef PS5SIM_COMMON_SYSTEM_INFO_H_
#define PS5SIM_COMMON_SYSTEM_INFO_H_

#include <string>

namespace Common {

struct SystemInfo {
	std::string ProcessorName;
};

[[nodiscard]] SystemInfo GetSystemInfo();

} // namespace Common

#endif /* PS5SIM_COMMON_SYSTEM_INFO_H_ */
