#pragma once

#include <algorithm>
#include <utility>
#include <vector>

namespace dnf::identity {

template <typename Candidate>
void KeepBestOcrScorePerPlayer(std::vector<Candidate>& candidates, Candidate candidate)
{
    const auto found = std::find_if(candidates.begin(), candidates.end(),
        [&candidate](const Candidate& current) { return current.playerIndex == candidate.playerIndex; });
    if (found == candidates.end()) {
        candidates.push_back(std::move(candidate));
        return;
    }

    if (candidate.score > found->score ||
        (candidate.score == found->score && candidate.realIdLength > found->realIdLength)) {
        *found = std::move(candidate);
    }
}

template <typename Candidate>
std::pair<int, int> TopTwoOcrPlayers(const std::vector<Candidate>& candidates,
    int teamFilter = -1, int excludedTeam = -1)
{
    int best = -1;
    int second = -1;
    const auto better = [&candidates](int left, int right) {
        return candidates[left].score > candidates[right].score ||
            (candidates[left].score == candidates[right].score &&
                candidates[left].realIdLength > candidates[right].realIdLength);
    };

    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
        const auto& candidate = candidates[i];
        if (teamFilter >= 0 && candidate.team != teamFilter) continue;
        if (excludedTeam >= 0 && candidate.team == excludedTeam) continue;

        if (best < 0 || better(i, best)) {
            second = best;
            best = i;
        }
        else if (second < 0 || better(i, second)) {
            second = i;
        }
    }
    return { best, second };
}

}
