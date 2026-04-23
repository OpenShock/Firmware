#if __cplusplus >= 202302L
// C++23 :3
#elif __cplusplus >= 202002L
#error "C++20 compiler detected, OpenShock requires a C++23 compliant compiler"
#elif __cplusplus >= 201703L
#error "C++17 compiler detected, OpenShock requires a C++23 compliant compiler"
#elif __cplusplus >= 201402L
#error "C++14 compiler detected, OpenShock requires a C++23 compliant compiler"
#elif __cplusplus >= 201103L
#error "C++11 compiler detected, OpenShock requires a C++23 compliant compiler"
#elif __cplusplus >= 199711L
#error "C++98 compiler detected, OpenShock requires a C++23 compliant compiler"
#elif __cplusplus == 1
#error "Pre-C++98 compiler detected, OpenShock requires a C++23 compliant compiler"
#else
#error "Unknown C++ standard detected, OpenShock requires a C++23 compliant compiler"
#endif