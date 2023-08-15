#pragma once

namespace ks3::detail
{

using namespace std;

inline filesystem::path GetTempDirectoryPath(error_code &ec) noexcept
{
    return filesystem::temp_directory_path(ec);
}

} // namespace ks3::detail
