#include "model.hpp"
#include <algorithm>
#include <set>

namespace atlas {
std::string storyEventKey(const Json &v) {
    const auto &a = v["story_anchor"];
    if (!a["event_id"].str().empty()) return a["event_id"].str();
    if (!a["scene_id"].str().empty())
        return "scene:" + a["scene_id"].str() + ":" + a["relation"].str("after");
    return "version:" + v["id"].str();
}
std::vector<StoryEvent> groupStoryEvents(const std::vector<Json> &ordered,
                                       const std::vector<Json> &all) {
    auto older = [](const Json &a, const Json &b) {
        if (a["recorded_at"].str() != b["recorded_at"].str())
            return a["recorded_at"].str() < b["recorded_at"].str();
        if (a["sequence"].num() != b["sequence"].num())
            return a["sequence"].num() < b["sequence"].num();
        return a["id"].str() < b["id"].str();
    };
    std::vector<StoryEvent> events;
    std::map<std::string, size_t> indices;
    for (const auto &v : ordered) {
        // Collections are views of the same campaign, not alternative realities.
        auto key = storyEventKey(v);
        auto collectionKey = v["chain_id"].str() + ":" + key;
        if (!indices.contains(collectionKey)) {
            indices[collectionKey] = events.size();
            events.push_back({key, v, {}});
        } else {
            auto &event = events[indices.at(collectionKey)];
            if (older(event.moment, v)) event.moment = v;
        }
    }
    for (auto &event : events) {
        auto chain = event.moment["chain_id"].str();
        for (const auto &v : all) {
            auto other = v["chain_id"].str();
            // Old corrected snapshots already stored in the editions archive
            // belong to their original scene, without adding extra story nodes.
            if (storyEventKey(v) == event.key &&
                (other == chain || (chain == "main" && other == "editions")))
                event.revisions.push_back(v);
        }
        std::sort(event.revisions.begin(), event.revisions.end(), older);
    }
    return events;
}
} // namespace atlas
