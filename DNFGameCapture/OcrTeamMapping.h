#pragma once

namespace dnf::identity {

constexpr int PhysicalSideToTeam(int physicalSide, bool flipped) noexcept
{
    const int unflippedTeam = physicalSide == 0 ? 0 : 1;
    return flipped ? 1 - unflippedTeam : unflippedTeam;
}

}
