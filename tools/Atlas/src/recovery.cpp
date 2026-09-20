#include "model.hpp"
#include <algorithm>
namespace atlas {
namespace {
fs::path globalRecovery() {
    wchar_t path[32768];
    auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);
    return (n>0 && n<32768 ? fs::path(path):fs::temp_directory_path())/L"Atlas"/L"Recovery";
}
fs::path recoveryRoot(const Map &m) { return m.directory.empty()?globalRecovery():m.directory/L".atlas"/L"recovery"; }
}
void Map::autosave() const {
    auto data=fields({{"session_id",sessionId},{"map_id",doc["id"]},{"base_hash",diskHash},
                     {"recorded_at",nowUtc()},{"document",doc},{"directory",pathText(directory)}});
    atomicText(recoveryRoot(*this)/pathOf(sessionId+".json"),data.dump()+"\n");
}
void Map::clearSessionRecovery() const {
    for(const auto &root:{recoveryRoot(*this),globalRecovery()}) {
        std::error_code ec;fs::remove(root/pathOf(sessionId+".json"),ec);
    }
}
std::vector<Json> Map::recoveries() const {
    std::vector<Json> result;
    auto add=[&](const fs::path &file,bool legacy) {
        try {
            auto value=Json::parse(readText(file));
            if(!value["document"].isObject())return;
            auto id=legacy?"legacy":file.stem().string();
            result.push_back(fields({{"id",id},{"recorded_at",value["recorded_at"]},
                {"name",value["document"]["name"]},{"base_matches",value["base_hash"].str()==diskHash},
                {"own_session",id==sessionId},{"different",!(value["document"]==doc)},{"path",pathText(file)}}));
        }catch(...) {}
    };
    auto root=recoveryRoot(*this);
    if(fs::exists(root))for(const auto &entry:fs::directory_iterator(root))
        if(entry.is_regular_file() && entry.path().extension()==L".json")add(entry.path(),false);
    if(!directory.empty() && fs::exists(directory/L".atlas"/L"autosave.json"))
        add(directory/L".atlas"/L"autosave.json",true);
    std::sort(result.begin(),result.end(),[](const auto &a,const auto &b){
        if(a["own_session"].boolean()!=b["own_session"].boolean())return a["own_session"].boolean();
        if(a["recorded_at"].str()!=b["recorded_at"].str())return a["recorded_at"].str()>b["recorded_at"].str();
        return a["id"].str()<b["id"].str();
    });
    return result;
}
bool Map::hasRecovery() const {
    auto list=recoveries();return std::any_of(list.begin(),list.end(),[](const auto &r){return r["different"].boolean();});
}
void Map::recoverSession(const std::string &id) {
    auto list=recoveries();
    auto it=std::find_if(list.begin(),list.end(),[&](const auto &v){return v["id"].str()==id;});
    if(it==list.end())throw std::runtime_error("Автосохранение не найдено");
    auto j=Json::parse(readText(pathOf((*it)["path"].str())));
    if(j["base_hash"].str()!=diskHash)
        throw std::runtime_error("Основная карта изменилась после этого автосохранения. Открой восстановление как отдельный черновик.");
    Map candidate;candidate.directory=directory;candidate.doc=j["document"];
    auto errors=candidate.validate()["errors"];
    if(errors.size())throw std::runtime_error(errors.dump());
    doc=std::move(candidate.doc);clearCache();
}
void Map::recover() {
    auto list=recoveries();
    if(list.empty())throw std::runtime_error("Нет автосохранений для восстановления");
    for(const auto &r:list)if(r["own_session"].boolean()){recoverSession(r["id"].str());return;}
    for(const auto &r:list)if(r["base_matches"].boolean()){recoverSession(r["id"].str());return;}
    throw std::runtime_error("Доступны восстановления другой редакции; выбери их в списке черновиков");
}
} // namespace atlas
