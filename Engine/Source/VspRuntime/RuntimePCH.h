#pragma once

#ifdef __cplusplus
    // C Standard Library headers (C++)
    /*#include <cctype>
    #include <cerrno>
    #include <cfloat>
    #include <ciso646>
    #include <climits>
    #include <clocale>
    #include <cmath>
    #include <csetjmp>
    #include <csignal>
    #include <cstdarg>
    #include <cstdalign>
    #include <cstdbool>
    #include <cstddef>
    #include <cstdint>
    #include <cstdio>
    #include <cstdlib>
    #include <cstring>
    #include <ctgmath>
    #include <ctime>
    #include <cwchar>*/
    #include <cwctype>

    // C++ Standard Library headers
    #include <ios>
    #include <iostream>
    //#include <iomanip>
    #include <sstream>
    #include <fstream>

    #include <string>
    #include <string_view>

    //#include <array>
    //#include <deque>
    //#include <forward_list>
    //#include <list>
    //#include <map>
    //#include <set>
    #include <unordered_map>
    //#include <unordered_set>
    #include <vector>
    //#include <stack>
    //#include <queue>

    #include <memory>
    //#include <memory_resource>

    //#include <algorithm>
    //#include <functional>
    //#include <utility>
    //#include <tuple>

    //#include <atomic>
    #include <mutex>
    //#include <shared_mutex>
    //#include <thread>
    //#include <future>

    #include <stdexcept>
    #include <exception>

    //#include <type_traits>
    //#include <typeinfo>
    //#include <any>
    //#include <optional>
    //#include <variant>
    #include <chrono>
    //#include <ratio>

    //#include <complex>
    //#include <random>
    //#include <numeric>
    //#include <valarray>

    //#include <filesystem>

    //#include <bitset>
    //#include <cuchar>
    //#include <charconv>
#endif

// Third-party libraries
// ICU library definition
#define U_STATIC_IMPLEMENTATION

// fmt library definition
#define FMT_HEADER_ONLY
