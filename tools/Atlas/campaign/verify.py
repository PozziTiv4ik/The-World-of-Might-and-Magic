"""Independent checks of the saved campaign, in addition to Atlas validate --history."""
from pathlib import Path
from functools import lru_cache
import argparse
import copy
import hashlib
import json

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project',type=Path,default=Path(__file__).resolve().parents[3])
    args=parser.parse_args()
    project=args.project.resolve()
    root=project/'12_Карты/Карта_мира_Объекты'
    def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
    def child(ref):
        path=(root/ref).resolve()
        if not path.is_relative_to(root):raise ValueError('History path escapes project')
        return path
    @lru_cache(maxsize=3)
    def document(ref):
        value=read(child(ref))
        if value.get('storage')!='atlas-delta-1':return value
        doc=copy.deepcopy(document(value['base']))
        for change in value['changes']:
            path=change['path']
            if not path:
                doc=copy.deepcopy(change['after']);continue
            at=doc
            for key in path[:-1]:at=at[key]
            if change['had_after']:at[path[-1]]=copy.deepcopy(change['after'])
            else:at.pop(path[-1],None)
        return doc
    seed=read(root/'history/campaign.json')
    catalog=read(root/'history/manifest.json')
    assert seed['completed'] and len(seed['scenes'])==120 and len(seed['chapters'])==4
    current=read(root/'map.json')
    assert current['parent_version']==seed['current_version']
    main_entries={k:e for k,e in catalog['entries'].items() if e['chain_id']=='main'}
    assert len(main_entries)==120
    assert {e['story_anchor']['scene_id'] for e in main_entries.values()}=={f'SCENE-{i:04d}' for i in range(1,121)}
    assert len([e for e in catalog['entries'].values() if e['chain_id']=='chapters'])==4
    entities={e['id'] for e in read(project/'09_Реестры/Сущности.json')['entities'] if e['type']=='location'}
    assert entities=={f['entity_id'] for f in current['features'].values() if f.get('id','').startswith('MAPOBJ-CAMPAIGN-LOC-')}
    checks=5
    for path,sha in seed['source_hashes'].items():
        assert hashlib.sha256((project/path).read_bytes()).hexdigest()==sha,'Literary scene changed'
        checks+=1
    for name,sha in seed['original_versions'].items():
        assert hashlib.sha256((root/'versions'/name).read_bytes()).hexdigest()==sha,'Original version changed'
        checks+=1
    moments={}
    for number,entry in sorted(seed['scenes'].items(),key=lambda item:item[1]['order']):
        header=read(root/'versions'/f"{entry['version_id']}.json")
        doc=document(header['document_ref'])
        assert doc['campaign_event']['scene_id']==entry['scene_id']
        assert header['story_anchor']['story_date'].strip()
        assert header['story_anchor']['chapter']==entry['chapter']
        assert doc['campaign_event']['source_path'] in seed['source_hashes']
        assert len([f for f in doc['features'].values() if f.get('role')=='current_event'])==1
        moments[int(number)]={
            'countries':{k for k,f in doc['features'].items() if f.get('role')=='country'},
            'heroes':{f.get('entity_id'):f.get('location_id') for f in doc['features'].values() if f.get('role')=='campaign_actor'},
            'states':{k:f.get('campaign_state','') for k,f in doc['features'].items() if k.startswith('MAPOBJ-CAMPAIGN-LOC-')}
        }
        checks+=5
    assert len(moments[1]['countries'])==51
    assert not {'MAPOBJ-HISTORIC-SHAHIBDIA','MAPOBJ-HISTORIC-SHAMAT'} & moments[11]['countries']
    assert 'MAPOBJ-HISTORIC-OBSIDIAN' not in moments[50]['countries']
    assert 'MAPOBJ-HISTORIC-LAW-CLANS' not in moments[104]['countries']
    assert len(moments[63]['countries'])==47
    assert 'CHAR-0136' in moments[47]['heroes'],'False death report removed Michael'
    assert moments[28]['heroes']['CHAR-0059']=='LOC-0094','Dream moved Alexandros'
    assert moments[81]['heroes']['CHAR-0136']=='LOC-0007','Proposed voyage was treated as arrival'
    assert moments[93]['heroes']['CHAR-0136']=='LOC-0048'
    assert moments[63]['heroes']['CHAR-0059']=='LOC-0009','Pending temple choice moved Alexandros'
    assert 'разрешение' in moments[107]['states']['MAPOBJ-CAMPAIGN-LOC-0002'].lower()
    assert 'открыт' in moments[107]['states']['MAPOBJ-CAMPAIGN-LOC-0096'].lower()
    assert 'построены' in moments[107]['states']['MAPOBJ-CAMPAIGN-LOC-0086'].lower()
    checks+=13
    output=dict(passed=checks,failed=0,scenes=120,chapters=4,locations=106,
                first_countries=51,current_countries=47,original_snapshots_unchanged=True,literary_sources_unchanged=True)
    print(json.dumps(output,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
