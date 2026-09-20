"""Build the editable Atlas campaign chain using the public, hash-checked CLI.

Run in a campaign transaction. Re-running the same profile resumes unfinished work
or verifies an already completed chain; it never overwrites published snapshots.
Python is needed for this authoring tool only, not for opening the editor.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

from profile import (PROFILE_ID, POSITIONS, COUNTRY_BINDINGS, CHAPTERS, CHAPTER_NAMES,
                     SCENE_PLACES, HEROES, SHARED_POSITIONS, PARTITIONS, MERGES, STATES, SUMMARIES)

def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))

def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".new")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def metadata(text):
    match = re.search(r"(?ms)^---\s*\n(.*?)^---", text)
    result = {}
    for key, value in re.findall(r"(?m)^([a-z_]+):\s*(.*)$", match[1] if match else ""):
        try:
            result[key] = json.loads(value)
        except ValueError:
            result[key] = value
    return result

def clean(text):
    text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)
    text = re.sub(r"`([^`]+)`", r"\1", text)
    return re.sub(r"\s+", " ", text.strip()).strip("- ")

def short_summary(scene, number):
    if number in SUMMARIES:
        return SUMMARIES[number]
    sections = scene["sections"]
    # Use closed decisions or actual narrated events. Never expand the alternatives
    # or potential-consequences sections into geographical changes.
    text = sections.get("Итог сцены") or sections.get("Принятое решение") or sections.get("Событие")
    if not text:
        text = sections.get("Что изменилось в каноне", scene["title"])
    text = clean(text)
    if len(text) > 420:
        shortened = text[:420]
        last = max(shortened.rfind(". "), shortened.rfind("; "))
        text = shortened[:last + 1] if last > 150 else shortened.rsplit(" ", 1)[0] + "…"
    return text

def base_feature(identifier, name, layer, kind, z=3000):
    return dict(id=identifier, name=name, layer_id=layer, kind=kind, visibility="gm",
                known_to=[], evidence_ids=[], opacity=1, truth="cartographic_projection", z_order=z,
                provenance=dict(kind="campaign_cartography", profile=PROFILE_ID))

class Builder:
    def __init__(self, project, map_path, cli):
        self.project, self.map, self.cli = project, map_path, cli
        self.seed_file = self.map / "history/campaign.json"
        self.patch_file = self.map / ".atlas/campaign-patch.json"
        self.profile_hash = digest(Path(__file__).with_name("profile.py"))
        self.scenes = {}
        for path in (project / "01_Кампания/Ветки").rglob("*.md"):
            text = path.read_text(encoding="utf-8-sig")
            info = metadata(text)
            if info.get("type") != "scene":
                continue
            info.update(title=text.splitlines()[0].lstrip("# "), path=path.relative_to(project).as_posix(),
                        sha256=digest(path), sections=dict(re.findall(r"(?ms)^## ([^\n]+)\n(.*?)(?=^## |\Z)", text)))
            self.scenes[int(info["id"].split("-")[1])] = info
        self.entities = {e["id"]:e for e in read(project / "09_Реестры/Сущности.json")["entities"]}
        self.locations = {int(e["id"][4:]):e for e in self.entities.values() if e["type"] == "location"}
        ordered = [n for chapter in CHAPTERS.values() for n in chapter]
        assert len(ordered) == len(set(ordered)) and set(ordered) == set(self.scenes), "Profile must cover every scene exactly once"
        assert set(POSITIONS) == set(self.locations), "Every location needs a finished cartographic position"
        assert set(SCENE_PLACES) == set(self.scenes), "Every scene needs a place"
        for chapter, numbers in CHAPTERS.items():
            assert all(self.scenes[n]["chapter"] == chapter for n in numbers), "Chapter mismatch"
        for branch in {s["branch"] for s in self.scenes.values()}:
            numbers = [n for n in ordered if self.scenes[n]["branch"] == branch]
            assert numbers == sorted(numbers), "POV order must match the authored scenes"
        self.seed = read(self.seed_file) if self.seed_file.exists() else dict(
            schema_version=1, profile_id=PROFILE_ID, profile_sha256=self.profile_hash,
            initialised=False, partitions=[], scenes={}, chapters={}, completed=False,
            source_hashes={s["path"]:s["sha256"] for s in self.scenes.values()},
            original_versions={p.name:digest(p) for p in (map_path / "versions").glob("*.json")})
        assert self.seed["profile_sha256"] == self.profile_hash, "Profile changed: publish a new chain on a copy instead of rewriting history"
        assert self.seed["source_hashes"] == {s["path"]:s["sha256"] for s in self.scenes.values()}, "Story sources changed: use a new chain"
        self.refresh()

    def refresh(self):
        self.doc = read(self.map / "map.json")

    def save_progress(self):
        write(self.seed_file, self.seed)

    def command(self, command, *args, result=True):
        process = subprocess.run([str(self.cli), command, str(self.map), *map(str,args)],
                                 capture_output=True, encoding="utf-8", errors="strict")
        if process.returncode:
            raise RuntimeError(f"Atlas {command}: {process.stderr or process.stdout}")
        return json.loads(process.stdout) if result else None

    def patch(self, operations):
        if not operations:
            return
        current = self.command("inspect", "--json")["file_sha256"]
        write(self.patch_file, dict(expected_hash=current, operations=operations))
        self.command("apply", "--patch", self.patch_file, "--project", self.project, "--dry-run", result=False)
        self.command("apply", "--patch", self.patch_file, "--project", self.project, result=False)
        self.refresh()

    def upsert(self, feature):
        identifier = feature["id"]
        if identifier in self.doc["features"]:
            return dict(op="set_feature", id=identifier, values=feature)
        return dict(op="add_feature", feature=feature)

    def timeline(self, operations):
        history = self.command("history")
        write(self.patch_file, dict(expected_hash=history["history_hash"], operations=operations))
        self.command("timeline", "--patch", self.patch_file, "--project", self.project, "--dry-run", result=False)
        self.command("timeline", "--patch", self.patch_file, "--project", self.project, result=False)

    def initialise(self):
        if self.seed["initialised"]:
            return
        self.save_progress()
        self.command("draft", "--name", "Исходная карта до сюжетной цепочки")
        operations = []
        for identifier, name in [("LAYER-CAMPAIGN-PLACES","Места сюжета"),
                                 ("LAYER-CAMPAIGN-ROUTES","Путь героев"),
                                 ("LAYER-CAMPAIGN-HEROES","Герои"),
                                 ("LAYER-CAMPAIGN-EVENTS","События и контроль")]:
            if not any(l["id"] == identifier for l in self.doc["layers"]):
                operations.append(dict(op="add_layer", value=dict(id=identifier,name=name,kind="vector",visible=True,locked=False,opacity=1,blend_mode=0)))
        operations += [dict(op="set_layer", id="LAYER-SETTLEMENTS", values=dict(name="Архив · поселения исходной карты",visible=False)),
                       dict(op="set_layer", id="LAYER-ZONES", values=dict(name="Архив · пометки исходной карты",visible=False)),
                       dict(op="set_map", values=dict(name="Мир Меча и Магии · история кампании", status="working_copy",
                            campaign=dict(profile_id=PROFILE_ID, title="История кампании", chapter_count=4,
                                          chronology_relations=read(self.project / "09_Реестры/Хронология_связей.json")["relations"]), next_z=5000))]
        self.positions = {}
        for number, entity in self.locations.items():
            at = list(POSITIONS[number])
            # Prefer a complete exact label on the source image; split OCR fragments
            # deliberately fall back to the curated whole-name coordinates above.
            names = {str(n).casefold().replace("ё","е") for n in [entity["name"],*entity.get("aliases",[])]}
            matches = [f for f in self.doc["features"].values() if f.get("role") == "settlement_label"
                       and f.get("name","").casefold().replace("ё","е") in names]
            if len(matches) == 1:
                at = matches[0]["position"]
            self.positions[number] = at
            feature = base_feature("MAPOBJ-CAMPAIGN-"+entity["id"], entity["name"], "LAYER-CAMPAIGN-PLACES", "symbol",3000+number)
            kind = ("port" if "Порт" in entity["name"] or number == 7 else "fortress" if "Крепость" in entity["name"]
                    else "tower" if "Башня" in entity["name"] else "temple" if "Храм" in entity["name"] or number in [71,90]
                    else "ruins" if number in [72,91] else "capital" if number in [1,9,44,78,81,93,94,100,101] else "city")
            feature.update(position=at, symbol_id=kind, size=19, rotation=0, show_label=True, font_size=16,
                           label_offset=[8,-15], stroke="#534A3F", text_color="#39362E", role="campaign_place",
                           entity_id=entity["id"], evidence_ids=entity.get("source_ids",[]), source_path=entity["path"])
            if number == 48:
                feature["name"] = "Пустыня изгнания Михаэля"
            if number in COUNTRY_BINDINGS:
                country_id = "MAPOBJ-COUNTRY-"+COUNTRY_BINDINGS[number]
                operations.append(dict(op="set_feature",id=country_id,values=dict(entity_id=entity["id"],evidence_ids=entity.get("source_ids",[]))))
                feature.update(show_label=False, opacity=0, role="campaign_region_anchor")
            if number in [3,17,41,47,50,53,54,75,103]:
                feature.update(size=13, font_size=20, stroke="#73694D")
            operations.append(self.upsert(feature))
        pin = dict(name="Момент истории", paths=[dict(points=[[-.5,0],[0,-.5],[.5,0],[0,.5],[-.5,0]],closed=True,fill="currentColor",stroke="currentColor",stroke_width=.05)])
        actor = dict(name="Герой", paths=[dict(points=[[-.45,.45],[0,-.5],[.45,.45],[-.45,.45]],closed=True,fill="currentColor",stroke="#FFFFFF",stroke_width=.07)])
        operations += [dict(op="put_symbol",id="story-pin",value=pin),dict(op="put_symbol",id="story-actor",value=actor)]
        self.patch(operations)
        self.seed["positions"] = self.positions
        self.seed["initialised"] = True
        self.save_progress()
        self.timeline([dict(op="set_chain",id="main",values=dict(name="Вся история · 120 сцен",kind="main")),
                       dict(op="set_chain",id="chapters",values=dict(name="Итоги глав",kind="summary")),
                       dict(op="set_chain",id="archive",values=dict(name="Исходные версии",kind="archive"))])

    def claim(self, country_id):
        current = self.command("inspect", "--json")["file_sha256"]
        self.command("claim-country", "--id", country_id, "--expected-hash", current, "--dry-run", result=False)
        self.command("claim-country", "--id", country_id, "--expected-hash", current, result=False)
        self.refresh()

    def partitions(self):
        for code, location, name, bounds, colour in PARTITIONS:
            if code in self.seed["partitions"]:
                continue
            identifier = "MAPOBJ-HISTORIC-"+code
            if identifier not in self.doc["features"]:
                x0,y0,x1,y1 = bounds
                points = [[x0,y0],[x1,y0],[x1,y1],[x0,y1]]
                nodes = ["NODE-HISTORIC-"+code+"-"+str(n) for n in range(4)]
                arc = "ARC-HISTORIC-"+code
                feature = base_feature(identifier,name,"LAYER-TERRITORIES","region",2900)
                feature.update(closed=True,role="country",domain="political",fill=colour,stroke="#6D634F",stroke_width=1,
                               claim_within="MAPOBJ-COUNTRY-ETERNAL-SUN", rings=[[dict(id=arc,reverse=False)]])
                if location:
                    feature.update(entity_id=f"LOC-{location:04d}",evidence_ids=self.locations[location].get("source_ids",[]))
                operations=[dict(op="put_node",id=n,value=p) for n,p in zip(nodes,points)]
                operations += [dict(op="put_arc",id=arc,value=dict(nodes=nodes+[nodes[0]])),self.upsert(feature)]
                label=base_feature("MAPOBJ-HISTORIC-NAME-"+code,name,"LAYER-LABELS","label",2950)
                label.update(position=list(POSITIONS[location]) if location else [1510,1640],parent_id=identifier,
                             font_size=24,label_width=240,label_height=65,text_color="#534B3D",show_label=True,role="country_label")
                operations.append(self.upsert(label))
                self.patch(operations)
            if self.doc["features"][identifier].get("claim_within"):
                self.claim(identifier)
            self.seed["partitions"].append(code)
            self.save_progress()
            print("Reconstructed former country:", name, flush=True)
        self.rename_sun("Королевство Тар-Элам")

    def rename_sun(self,name):
        operations=[]
        for identifier,f in self.doc["features"].items():
            if (identifier=="MAPOBJ-COUNTRY-ETERNAL-SUN" or f.get("parent_id")=="MAPOBJ-COUNTRY-ETERNAL-SUN") and f.get("name")!=name:
                operations.append(dict(op="set_feature",id=identifier,values=dict(name=name)))
        self.patch(operations)

    def place(self,number):
        return self.doc["features"][f"MAPOBJ-CAMPAIGN-LOC-{number:04d}"]["position"]

    def route(self,identifier,name,places,colour,scene,role="campaign_route"):
        if len(places)<2 or all(p==places[0] for p in places):
            return []
        nodes=["NODE-"+identifier+"-"+str(i) for i in range(len(places))]
        arc="ARC-"+identifier
        feature=base_feature(identifier,name,"LAYER-CAMPAIGN-ROUTES","route",4200)
        feature.update(arcs=[dict(id=arc,reverse=False)],closed=False,stroke=colour,stroke_width=3,
                       role=role,opacity=.72,evidence_ids=scene.get("source_ids",[]),scene_id=scene["id"],
                       route_interpretation="editorial_connection_between_confirmed_positions")
        return ([dict(op="put_node",id=n,value=p) for n,p in zip(nodes,places)] +
                [dict(op="put_arc",id=arc,value=dict(nodes=nodes)),self.upsert(feature)])

    def actor(self,branch,places,scene,number):
        char_id,name,colour=HEROES[branch]
        identifier="MAPOBJ-CAMPAIGN-"+char_id
        old=self.doc["features"].get(identifier)
        target=self.place(places[-1])
        path=([old.get("story_position",old["position"])] if old else [])+[self.place(n) for n in places]
        path=[p for i,p in enumerate(path) if not i or p!=path[i-1]]
        operations=self.route("MAPOBJ-ROUTE-"+char_id,"Путь: "+name,path,colour,scene)
        # Erase an old route when the hero stays put; this layer shows the current leg.
        if not operations and "MAPOBJ-ROUTE-"+char_id in self.doc["features"]:
            operations.append(dict(op="delete_feature",id="MAPOBJ-ROUTE-"+char_id))
        feature=base_feature(identifier,name,"LAYER-CAMPAIGN-HEROES","symbol",4500+list(HEROES).index(branch))
        dx,dy=[(18,-22),(-18,-22),(18,22),(-18,22)][list(HEROES).index(branch)]
        feature.update(entity_id=char_id,position=[target[0]+dx,target[1]+dy],story_position=target,
                       symbol_id="story-actor",size=28,rotation=0,stroke=colour,text_color=colour,font_size=19,
                       label_offset=[10,-19],show_label=True,role="campaign_actor",evidence_ids=scene.get("source_ids",[]),
                       last_scene_id=scene["id"],location_id=f"LOC-{places[-1]:04d}")
        if number==93:
            feature["movement_kind"]="magic_displacement"
        operations.append(self.upsert(feature))
        return operations

    def scene(self,number,index):
        scene=self.scenes[number]
        for code in MERGES.get(number,[]):
            source="MAPOBJ-HISTORIC-"+code
            if source in self.doc["features"]:
                self.patch([dict(op="set_feature",id="MAPOBJ-COUNTRY-ETERNAL-SUN",values=dict(claim_sources=[source]))])
                self.claim("MAPOBJ-COUNTRY-ETERNAL-SUN")
            label="MAPOBJ-HISTORIC-NAME-"+code
            if label in self.doc["features"]:
                self.patch([dict(op="delete_feature",id=label)])
        if number==11:
            self.rename_sun("Империя Тар-Элам")
        elif number==104:
            self.rename_sun("Империя Вечного Солнца")
        places=SCENE_PLACES[number]
        target=self.place(places[-1])
        summary=short_summary(scene,number)
        operations=[]
        if scene["branch"] in HEROES:
            operations+=self.actor(scene["branch"],places,scene,number)
        for branch,location in SHARED_POSITIONS.get(number,{}).items():
            if branch!=scene["branch"]:
                operations+=self.actor(branch,[location],scene,number)
        event=base_feature("MAPOBJ-CAMPAIGN-CURRENT-EVENT",self.locations[places[-1]]["name"],"LAYER-CAMPAIGN-EVENTS","symbol",4900)
        event.update(position=target,symbol_id="story-pin",size=35,rotation=0,stroke="#B54E35",text_color="#7B3C27",
                     show_label=False,entity_id=f"LOC-{places[-1]:04d}",role="current_event",scene_id=scene["id"],
                     evidence_ids=scene.get("source_ids",[]),campaign_state=summary)
        operations.append(self.upsert(event))
        for location,state,colour in STATES.get(number,[]):
            feature=self.doc["features"][f"MAPOBJ-CAMPAIGN-LOC-{location:04d}"].copy()
            feature.update(campaign_state=state,stroke=colour,text_color=colour,status_scene_id=scene["id"],
                           evidence_ids=sorted(set(feature.get("evidence_ids",[])+scene.get("source_ids",[]))))
            operations.append(self.upsert(feature))
        parent=self.seed["scenes"].get(str([n for ns in CHAPTERS.values() for n in ns][index-2]),{}).get("version_id") if index>1 else None
        values=dict(campaign_event=dict(scene_id=scene["id"],title=scene["title"],location=scene["location"],
                         position=target,location_ids=[f"LOC-{n:04d}" for n in places],description=summary,
                         source_path=scene["path"],source_sha256=scene["sha256"],story_order=index*1024,
                         date_in_story=scene["date_in_story"]))
        if parent:
            values["parent_version"]=parent
        operations.append(dict(op="set_map",values=values))
        self.patch(operations)
        label=scene["branch"]+" · "+re.sub(r"^Сцена \d+\.\s*","",scene["title"])
        output=self.command("snapshot","--name",label,"--scene",scene["id"],"--project",self.project,
                            "--chain","main","--chapter",scene["chapter"],"--date",scene["date_in_story"],
                            "--order",index*1024,"--description",summary,"--accepted")
        self.seed["scenes"][str(number)]=dict(version_id=output["version_id"],scene_id=scene["id"],chapter=scene["chapter"],order=index*1024)
        self.save_progress()
        self.refresh()
        print(f"{index:03d}/120 · {scene['id']} · {label}",flush=True)

    def run(self):
        if self.seed["completed"]:
            self.verify()
            print("The complete campaign chain already exists; published revisions were preserved.")
            return
        self.initialise()
        if not self.seed["scenes"]:
            self.partitions()
        index=0
        for chapter,numbers in CHAPTERS.items():
            for number in numbers:
                index+=1
                if str(number) not in self.seed["scenes"]:
                    self.scene(number,index)
            if str(chapter) not in self.seed["chapters"]:
                last=self.scenes[numbers[-1]]
                result=self.command("snapshot","--name",f"Глава {chapter} · {CHAPTER_NAMES[chapter]}",
                        "--scene",last["id"],"--project",self.project,"--chain","chapters","--chapter",chapter,
                        "--date",last["date_in_story"],"--order",chapter*1024,"--description",
                        f"Состояние мира после последней сцены главы {chapter}. Все моменты главы доступны в цепочке «Вся история».","--accepted")
                self.seed["chapters"][str(chapter)]=result["version_id"]
                self.save_progress()
                self.refresh()
        history=self.command("history")
        entries={v["id"]:v for v in history["versions"]}
        final=self.seed["scenes"][str(CHAPTERS[4][-1])]["version_id"]
        self.patch([dict(op="set_map",values=dict(parent_version=final)),dict(op="set_anchor",value=entries[final]["story_anchor"])])
        operations=[]
        relations=read(self.project / "09_Реестры/Хронология_связей.json")["relations"]
        for relation in relations:
            if relation["relation"]!="after":
                continue
            a=self.seed["scenes"][str(int(relation["from"][6:]))]["version_id"]
            b=self.seed["scenes"][str(int(relation["to"][6:]))]["version_id"]
            operations.append(dict(op="set_version",id=a,values=dict(after_ids=[b])))
        self.timeline(operations)
        self.seed.update(completed=True,location_count=len(self.locations),scene_count=len(self.scenes),
                         chapter_count=len(CHAPTERS),current_version=final)
        self.save_progress()
        self.verify()
        if self.patch_file.exists():
            self.patch_file.unlink()
        print("Complete: 120 scene states + 4 chapter milestones. Original snapshots preserved.",flush=True)

    def verify(self):
        for filename,sha in self.seed["original_versions"].items():
            assert digest(self.map / "versions" / filename)==sha,"An original snapshot changed"
        report=self.command("validate","--project",self.project,"--history")
        if report.get("errors"):
            raise RuntimeError(json.dumps(report,ensure_ascii=False))
        assert len(self.seed["scenes"])==120 and len(self.seed["chapters"])==4
        assert all(v["version_id"] for v in self.seed["scenes"].values())
        write(self.map / "history/campaign-validation.json",dict(profile_id=PROFILE_ID,scene_count=120,
              chapter_count=4,location_count=len(self.locations),original_snapshots_unchanged=True,validation=report))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project",type=Path,default=Path(__file__).resolve().parents[3])
    parser.add_argument("--map",type=Path)
    parser.add_argument("--cli",type=Path)
    args=parser.parse_args()
    project=args.project.resolve()
    Builder(project,(args.map or project/"12_Карты/Карта_мира_Объекты").resolve(),
            (args.cli or project/"tools/Atlas/bin/Atlas.Cli.exe").resolve()).run()

if __name__=="__main__":
    main()
