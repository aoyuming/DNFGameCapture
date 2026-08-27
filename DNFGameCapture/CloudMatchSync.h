#pragma once

#include "json.hpp"

#include <cstdint>
#include <functional>
#include <string>

using DnfCloudMatchNameNormalizer =
    std::function<bool(const std::string&, std::string&)>;

bool DnfConvertCloudMatchSnapshot(const nlohmann::json& cloudSnapshot,
    std::uint64_t expectedClientRevision, bool swapped,
    const DnfCloudMatchNameNormalizer& normalizeName,
    nlohmann::json& teamSnapshot, std::string& errorCode);

nlohmann::json DnfBuildCloudMatchPreview(
    const nlohmann::json& localTeamSnapshot,
    const nlohmann::json& remoteTeamSnapshot, bool swapped);
