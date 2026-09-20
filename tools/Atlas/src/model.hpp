#pragma once
#include "pdn.hpp"
#include <deque>

namespace atlas {
struct Point {
    double x = 0, y = 0;
};
enum class SelectionDomain { All, Borders, Land, Objects, ActiveLayer };
double distance(Point a, Point b);
double segmentDistance(Point p, Point a, Point b);
bool inside(Point p, const std::vector<Point> &ring);
Json pointJson(Point p);
Point point(const Json &j);
struct Entity {
    std::string id, name, type, path, chapter, branch;
    Json sources, fronts, aliases;
};
class Campaign {
  public:
    fs::path root;
    std::vector<Entity> entities;
    void load(const fs::path &directory);
    const Entity *find(const std::string &id) const;
    std::vector<const Entity *> search(const std::string &query, const std::string &type = "") const;
};
class Map {
    std::map<std::pair<std::string, std::string>, std::string> edgeIndex;
    size_t edgeIndexCount = 0;
    std::map<std::string, std::shared_ptr<PdnSource>> pdns;
    std::deque<std::pair<std::string, std::shared_ptr<Image>>> imageCache;

  public:
    Json doc;
    fs::path directory;
    std::string diskHash;
    std::string sessionId;
    mutable std::string timelineHash;
    Map();
    void create(int width, int height, const std::string &name);
    void load(const fs::path &path);
    void save(const fs::path &path = {});
    void autosave() const;
    bool hasRecovery() const;
    void recover();
    std::vector<Json> recoveries() const;
    void recoverSession(const std::string &id);
    void clearSessionRecovery() const;
    Json timeline() const;
    void updateTimeline(const Json &value, const std::string &expected = "");
    Json validateHistory(const Campaign *campaign = nullptr) const;
    std::vector<Json> orderedVersions(const std::string &order = "story", const std::string &chain = "",
                                     int chapter = 0, const std::string &query = "") const;
    std::string stashDraft(const std::string &label);
    void restoreDraft(const std::string &id);
    void importPdn(const fs::path &path, const fs::path &destination);
    void importRaster(const fs::path &path, const fs::path &destination);
    std::string addLayer(const std::string &name, const std::string &kind = "vector");
    std::shared_ptr<Image> raster(const Json &layer);
    std::string storeImage(const Image &image);
    void clearCache();
    std::string nodeAt(Point p, double snap);
    std::string edge(const std::string &a, const std::string &b);
    std::string addPath(const std::vector<Point> &points, bool closed, const std::string &kind,
                        const std::string &layer, const std::string &color, double width, double snap = 0);
    std::string addSymbol(Point p, const std::string &symbol, const std::string &layer,
                          const std::string &color, double size);
    std::vector<std::vector<Point>> paths(const Json &feature) const;
    Point anchor(const Json &feature) const;
    std::vector<std::string> featureNodes(const Json &feature) const;
    Json *layer(const std::string &id);
    const Json *layer(const std::string &id) const;
    std::string nearestNode(Point p, double radius) const;
    std::string hit(Point p, double tolerance, bool parent = true, double zoom = 1,
                    SelectionDomain domain = SelectionDomain::All, const std::string &activeLayer = "") const;
    bool selectable(const Json &, SelectionDomain, const std::string &activeLayer = "") const;
    std::vector<std::string> borderOwners(const std::string &) const;
    std::string politicalBorder(Point, double tolerance, const std::string &layer = "") const;
    std::vector<std::string> borderControls(const std::string &) const;
    std::vector<Point> controlledBorder(const std::string &, const std::string &node, Point at) const;
    std::vector<std::string> controlPathNodes(const std::string &, const std::string &) const;
    void moveBorderControl(const std::string &, const std::string &node, Point at, bool sea = true);
    std::string insertBorderControl(const std::string &, Point, bool sea = true);
    void deleteBorderControl(const std::string &, const std::string &node);
    std::vector<const Json *> orderedFeatures(const std::string &layer) const;
    void eraseFeature(const std::string &id);
    void moveNode(const std::string &id, Point p);
    void moveFeature(const std::string &id, Point delta);
    void moveFeatures(const std::vector<std::string> &ids, Point delta);
    std::string sharedBorder(const std::string &country, Point grab) const;
    std::vector<std::pair<std::string, Point>> borderShape(const std::string &arc, Point grab, Point delta,
                                                           double radius) const;
    void moveBorder(const std::string &arc, Point grab, Point delta, double radius);
    std::string domainLayer(bool land);
    void rebuildPoliticalTopology();
    std::string vectorLayer();
    Json validate(const Campaign *campaign = nullptr) const;
    std::string snapshot(const std::string &label, const Json &anchor, bool accepted = false);
    std::vector<Json> versions() const;
    Json version(const std::string &id) const;
    void restore(const std::string &id);
    Json diff(const Json &old) const;
    void apply(const Json &patch);
    Json forCharacter(const std::string &id) const;
};
class History {
    std::deque<std::pair<std::string, Json>> undoStack, redoStack;

  public:
    void push(const Json &before, const Json &after, const std::string &action);
    bool undo(Json &doc);
    bool redo(Json &doc);
    void clear();
    std::vector<std::string> labels() const;
};
Json documentDelta(const Json &, const Json &);
void applyDocumentDelta(Json &, const Json &, bool forward = true);
void recoverHistoryTransaction(const fs::path &);
void validateTimeline(const Json &);
// Narrative moments and the saved revisions of each moment are separate axes.
struct StoryEvent {
    std::string key;
    Json moment;
    std::vector<Json> revisions;
};
std::string storyEventKey(const Json &version);
std::vector<StoryEvent> groupStoryEvents(const std::vector<Json> &ordered,
                                       const std::vector<Json> &all);
std::vector<Point> simplify(const std::vector<Point> &pts, double tolerance, bool closed = false);
std::vector<std::vector<Point>> traceRegion(const Image &image, Point seed, int tolerance,
                                            size_t limit = 64000000);
void floodFill(Image &image, Point seed, const std::string &color, int tolerance,
               const std::vector<Point> &mask = {});
} // namespace atlas
