#pragma once

#if defined _WIN32 && _WIN32 != 0

#include "windows/get_native_socket_winuser_magic.h"
#include "windows/wsa_loader_windows.h"
#include "windows/get_truncated_length_windows.h"
#include "other/get_temp_directory_path_other.h"

#elif __ANDROID__ // ^^^ Windows / Android vvv

//#include "linux/get_native_socket_epoll_magic.h"
#include "linux/get_native_socket_epoll.h"
#include "other/wsa_loader_other.h"
#include "other/get_truncated_length_other.h"
#include "android/get_temp_directory_path_android.h"

#elif __linux__ || __FreeBSD__ // ^^^ Android / Unix-like vvv

//#include "linux/get_native_socket_epoll_magic.h"
#include "linux/get_native_socket_epoll.h"
#include "other/wsa_loader_other.h"
#include "other/get_truncated_length_other.h"
#include "other/get_temp_directory_path_other.h"

#elif __APPLE__ // ^^^ Unix-like / MacOS vvv

#include "macos/get_native_socket_kqueue_magic.h"
#include "other/wsa_loader_other.h"
#include "other/get_truncated_length_other.h"
#include "other/get_temp_directory_path_other.h"

#else // ^^^ MacOS / Unsupported vvv

#error "Unsupported Platform"

#endif // ^^^ Windows / Android / Unix-like / MacOS / Unsupported ^^^
