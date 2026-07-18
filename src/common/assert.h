#ifndef PS5SIM_COMMON_ASSERT_H_
#define PS5SIM_COMMON_ASSERT_H_

#include "common/common.h"
#include "common/logging/log.h"

#include <cstdlib>
#include <string_view>

namespace Common {

#ifdef __clang__
int DbgExitHandler(char const* file, int line, std::string_view text)
    __attribute__((analyzer_noreturn));
int DbgExitHandler(char const* file, int line, fmt::text_style style, std::string_view text)
    __attribute__((analyzer_noreturn));
int DbgExitIfHandler(char const* expr, char const* file, int line)
    __attribute__((analyzer_noreturn));
int DbgNotImplementedHandler(char const* expr, char const* file, int line)
    __attribute__((analyzer_noreturn));
void DbgExit(int status) __attribute__((analyzer_noreturn));
#else
int  DbgExitHandler(char const* file, int line, std::string_view text);
int  DbgExitHandler(char const* file, int line, fmt::text_style style, std::string_view text);
int  DbgExitIfHandler(char const* expr, char const* file, int line);
int  DbgNotImplementedHandler(char const* expr, char const* file, int line);
void DbgExit(int status);
#endif

} // namespace Common

#if PS5SIM_PLATFORM == PS5SIM_PLATFORM_WINDOWS || PS5SIM_PLATFORM == PS5SIM_PLATFORM_LINUX
#define EXIT_HALT() (Common::DbgExit(321), 1)
#else
#define EXIT_HALT() (std::_Exit(321), 1)
#endif

#ifndef PS5SIM_FINAL
#define EXIT_IF(x)                                                                                 \
	((void)((x) && Common::DbgExitIfHandler(#x, __FILE__, __LINE__) != 0 && (EXIT_HALT(), 1) != 0))
#else
#define EXIT_IF(x)                                                                                 \
	do {                                                                                           \
		constexpr bool ps5sim_exit_if_disabled = false && (x);                                       \
		(void)ps5sim_exit_if_disabled;                                                               \
	} while (0)
#endif

#define EXIT(...)                                                                                  \
	do {                                                                                           \
		((void)(Common::DbgExitHandler(__FILE__, __LINE__, ::fmt::sprintf(__VA_ARGS__)) &&         \
		        (EXIT_HALT(), 1)));                                                                \
	} while (0)

#define EXIT_COLOR(style, ...)                                                                     \
	do {                                                                                           \
		((void)(Common::DbgExitHandler(__FILE__, __LINE__, (style),                                \
		                               ::fmt::sprintf(__VA_ARGS__)) &&                             \
		        (EXIT_HALT(), 1)));                                                                \
	} while (0)

#define EXIT_NOT_IMPLEMENTED(x)                                                                    \
	((void)((x) && Common::DbgNotImplementedHandler(#x, __FILE__, __LINE__) != 0 &&                \
	        (EXIT_HALT(), 1) != 0))
#define PS5SIM_NOT_IMPLEMENTED EXIT_NOT_IMPLEMENTED(true)

#endif /* PS5SIM_COMMON_ASSERT_H_ */
