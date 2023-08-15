#pragma once

namespace ks3::detail
{

inline WsaLoader::WsaLoader() noexcept : WinSockInitialized{true} {}

inline WsaLoader::~WsaLoader() noexcept = default;

} // namespace ks3::detail
