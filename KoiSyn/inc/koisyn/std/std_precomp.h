#pragma once

#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <barrier>
#include <bit>
#include <bitset>
#include <charconv>
#include <chrono>
#include <codecvt>
#include <compare>
#include <complex>
#include <concepts>
#include <condition_variable>
//#include <coroutine> // Android NDK r25b no contains
#include <deque>
#include <exception>
#include <execution>
//#include <expected> // C++23
#include <filesystem>
//#include <format> // Clang 14 experimental
#include <forward_list>
#include <fstream>
#include <functional>
#include <future>
#include <initializer_list>
#include <iomanip>
#include <ios>
#include <iosfwd>
#include <iostream>
#include <istream>
#include <iterator>
#include <latch>
#include <limits>
#include <list>
#include <locale>
#include <map>
#include <memory>
//#include <memory_resource> // Clang 16
#include <mutex>
#include <new>
//#include <numbers> // Android NDK r25b no contains
#include <numeric>
#include <optional>
#include <ostream>
//#include <print> // C++23
#include <queue>
#include <random>
//#include <ranges> // Android NDK r25b no contains
#include <ratio>
#include <regex>
#include <scoped_allocator>
#include <semaphore>
#include <set>
#include <shared_mutex>
//#include <source_location> // Clang 15 with bug / Clang 16
#include <span>
//#include <spanstream> // C++23
#include <sstream>
#include <stack>
//#include <stacktrace> // C++23
#include <stdexcept>
//#include <stdfloat> // C++23
//#include <stop_token> // Clang 17 partial
#include <streambuf>
#include <string>
#include <string_view>
#include <strstream>
//#include <syncstream> // Clang not supported
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <valarray>
#include <variant>
#include <vector>
#include <version>

#include <cassert>
#include <cctype>
#include <cerrno>
#include <cfenv>
#include <cfloat>
#include <cinttypes>
#include <climits>
#include <clocale>
#include <cmath>
#include <csetjmp>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
//#include <cuchar> // Android NDK r25b no contains
#include <cwchar>
#include <cwctype>

// backport
//#include "std_ranges.h"
//#include "std_print.h"
//#include "std_int128.h"
