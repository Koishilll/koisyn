#pragma once

#include <filesystem>

namespace ks3::detail
{

using namespace std;
using namespace std::filesystem;

// workaround: Android don't support a correct temp_directory_path
// (it returns /tmp but it's a invalid path).
// But we want to read or write sdcard, we need to request the external
// storage permissions which may make users confusion and is not we wanted.
inline path GetTempDirectoryPath(error_code &ec) noexcept
{
    create_directory("/sdcard/tmp", ec);
    return {"/sdcard/tmp"};
}

} // namespace ks3::detail
