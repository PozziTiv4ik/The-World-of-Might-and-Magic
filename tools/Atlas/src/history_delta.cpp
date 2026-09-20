#include "model.hpp"
#include <algorithm>
#include <set>
namespace atlas {
namespace {
void differences(const Json &a, const Json &b, Json path, Json &out) {
    if (a == b) return;
    if (a.isObject() && b.isObject()) {
        std::set<std::string> keys;
        for (const auto &[k,v] : a.obj()) keys.insert(k);
        for (const auto &[k,v] : b.obj()) keys.insert(k);
        for (const auto &k : keys) {
            Json next=path; next.push(k);
            if (a.contains(k) && b.contains(k)) differences(a[k],b[k],next,out);
            else out.push(fields({{"path",next},{"had_before",a.contains(k)},{"had_after",b.contains(k)},
                                  {"before",a[k]},{"after",b[k]}}));
        }
    } else out.push(fields({{"path",path},{"had_before",true},{"had_after",true},{"before",a},{"after",b}}));
}
void applyDelta(Json &doc, const Json &delta, bool forward) {
    for (const auto &change : delta.arr()) {
        const auto &path=change["path"].arr();
        const auto &value=change[forward?"after":"before"];
        if (path.empty()) { doc=value; continue; }
        Json *at=&doc;
        for (size_t i=0;i+1<path.size();++i) {
            if (!at->isObject() || !at->contains(path[i].str())) throw std::runtime_error("Undo path no longer exists");
            at=&(*at)[path[i].str()];
        }
        if (change[forward?"had_after":"had_before"].boolean()) (*at)[path.back().str()]=value;
        else at->obj().erase(path.back().str());
    }
}
}
Json documentDelta(const Json &before,const Json &after) {
    Json delta=Json::array(); differences(before,after,Json::array(),delta); return delta;
}
void applyDocumentDelta(Json &doc,const Json &delta,bool forward) { applyDelta(doc,delta,forward); }
void History::push(const Json &before, const Json &after, const std::string &action) {
    Json delta=Json::array();
    differences(before,after,Json::array(),delta);
    if (!delta.size()) return;
    undoStack.push_back({action,std::move(delta)});
    redoStack.clear();
    while (undoStack.size()>250) undoStack.pop_front();
}
bool History::undo(Json &doc) {
    if (undoStack.empty()) return false;
    applyDelta(doc,undoStack.back().second,false);
    redoStack.push_back(std::move(undoStack.back())); undoStack.pop_back(); return true;
}
bool History::redo(Json &doc) {
    if (redoStack.empty()) return false;
    applyDelta(doc,redoStack.back().second,true);
    undoStack.push_back(std::move(redoStack.back())); redoStack.pop_back(); return true;
}
void History::clear() { undoStack.clear(); redoStack.clear(); }
std::vector<std::string> History::labels() const {
    std::vector<std::string> result;
    for(auto it=undoStack.rbegin();it!=undoStack.rend();++it)result.push_back(it->first);
    return result;
}
} // namespace atlas
