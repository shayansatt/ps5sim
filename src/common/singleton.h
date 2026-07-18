#ifndef PS5SIM_COMMON_SINGLETON_H_
#define PS5SIM_COMMON_SINGLETON_H_

#include <cstdlib>
#include <new>

namespace Common {

template <class T>
class Singleton {
public:
	static T* Instance() {
		if (!g_m_instance) {
			// NOLINTNEXTLINE(cppcoreguidelines-no-malloc,hicpp-no-malloc)
			g_m_instance = static_cast<T*>(std::malloc(sizeof(T)));
			new (g_m_instance) T;
		}

		return g_m_instance;
	}

	PS5SIM_CLASS_NO_COPY(Singleton);

protected:
	Singleton();
	~Singleton();

private:
	static inline T* g_m_instance = nullptr;
};

// template<class T> T* Singleton<T>::instance = 0;

} // namespace Common

#endif /* PS5SIM_COMMON_SINGLETON_H_ */
