"""One-time PDN reconstruction authoring tool. Atlas itself has no Python dependency.

Inputs are the native `pdn-extract` output and the reviewed source manifest.
All displayed geometry is emitted as native Atlas objects; no image is embedded.
Pillow and NumPy are build-time image readers, not application dependencies.
"""
import argparse, collections, hashlib, json, math
from pathlib import Path
import numpy as np
from PIL import Image
from routes import extract as extract_routes
from legacy import add_legacy


def compact_id(text):
    return hashlib.sha256(text.encode()).hexdigest()[:18].upper()


def simplify(points, epsilon=1.6):
    if len(points) < 4:
        return points
    keep = {0, len(points)-1}
    todo = [(0, len(points)-1)]
    while todo:
        a,b = todo.pop()
        ax,ay=points[a]; bx,by=points[b]
        dx,dy=bx-ax,by-ay; denom=dx*dx+dy*dy
        best=-1; index=-1
        for i in range(a+1,b):
            x,y=points[i]
            t=max(0,min(1,((x-ax)*dx+(y-ay)*dy)/denom)) if denom else 0
            d=(x-ax-dx*t)**2+(y-ay-dy*t)**2
            if d>best: best,index=d,i
        if best>epsilon*epsilon:
            keep.add(index);todo.extend([(a,index),(index,b)])
    return [points[i] for i in sorted(keep)]


def topology(grid, doc, prefix, scale=2, epsilon=1.6):
    """Trace every shared interface once; adjoining rings reference the same arc."""
    h,w=grid.shape
    padded=np.pad(grid,((1,1),(1,1)),constant_values=-1)
    ys,xs=np.nonzero(padded[:-1,1:-1]!=padded[1:,1:-1])
    edges=[]; adj=collections.defaultdict(list)
    for y,x in zip(ys.tolist(),xs.tolist()):
        if y<=h:
            edges.append(((x,y),(x+1,y),int(padded[y+1,x+1]),int(padded[y,x+1])))
    ys,xs=np.nonzero(padded[1:-1,:-1]!=padded[1:-1,1:])
    for y,x in zip(ys.tolist(),xs.tolist()):
        if x<=w:
            edges.append(((x,y),(x,y+1),int(padded[y+1,x]),int(padded[y+1,x+1])))
    for i,(a,b,r,l) in enumerate(edges): adj[a].append(i);adj[b].append(i)
    def junction(p):
        es=adj[p]
        return len(es)!=2 or set(edges[es[0]][2:])!=set(edges[es[1]][2:])
    seen=set(); directed=collections.defaultdict(list)
    def node(p):
        k=f'NODE-{prefix}-{p[0]}-{p[1]}'
        doc['nodes'][k]=[p[0]*scale,p[1]*scale]
        return k
    for index,e in enumerate(edges):
        if index in seen:continue
        a,b,r,l=e
        if not junction(a) and junction(b): a,b=b,a;r,l=l,r
        points=[a];start=a;current=a;i=index
        while True:
            seen.add(i)
            v1,v2,_,_=edges[i]; nxt=v2 if current==v1 else v1
            points.append(nxt);current=nxt
            if current==start or junction(current): break
            candidates=[k for k in adj[current] if k not in seen]
            if not candidates:break
            i=candidates[0]
        if points[0]==points[-1]:
            half=len(points)//2
            pts=simplify(points[:half+1],epsilon/scale)[:-1]+simplify(points[half:],epsilon/scale)
        else:pts=simplify(points,epsilon/scale)
        if len(pts)<2:continue
        ids=[node(p) for p in pts]
        arc='ARC-'+prefix+'-'+compact_id(str(points))
        doc['arcs'][arc]={'nodes':ids,'source':'pdn-reconstruction'}
        if r>=0:directed[r].append((ids[0],ids[-1],arc,False,pts))
        if l>=0:directed[l].append((ids[-1],ids[0],arc,True,list(reversed(pts))))
    result={}
    for label,items in directed.items():
        starts=collections.defaultdict(list)
        for i,item in enumerate(items):starts[item[0]].append(i)
        used=set();rings=[]
        for first in range(len(items)):
            if first in used:continue
            i=first;refs=[];points=[]
            while True:
                if i in used:raise ValueError('Unclosed boundary ring')
                used.add(i);a,b,arc,reverse,pts=items[i]
                refs.append({'id':arc,'reverse':reverse});points+=pts[:-1]
                if b==items[first][0]:break
                nexts=[k for k in starts[b] if k not in used]
                if not nexts:raise ValueError('Missing outgoing boundary')
                if len(nexts)>1:
                    incoming=(pts[-1][0]-pts[-2][0],pts[-1][1]-pts[-2][1])
                    def angle(k):
                        ps=items[k][4];v=(ps[1][0]-ps[0][0],ps[1][1]-ps[0][1])
                        return math.atan2(incoming[0]*v[1]-incoming[1]*v[0],incoming[0]*v[0]+incoming[1]*v[1])
                    i=max(nexts,key=angle)
                else:i=nexts[0]
            area=abs(sum(points[k-1][0]*points[k][1]-points[k][0]*points[k-1][1] for k in range(len(points)))/2)
            if area*scale*scale>=10:rings.append(refs)
        result[label]=rings
    return result


def components(mask):
    h,w=mask.shape
    points=set(np.flatnonzero(mask).tolist())
    results=[]
    while points:
        seed=points.pop();todo=[seed];group=[]
        while todo:
            p=todo.pop();group.append(p);x=p%w;y=p//w
            for dy in (-1,0,1):
                for dx in (-1,0,1):
                    if 0<=x+dx<w and 0<=y+dy<h:
                        n=p+dy*w+dx
                        if n in points:points.remove(n);todo.append(n)
        xs=[p%w for p in group];ys=[p//w for p in group]
        results.append({'x':min(xs),'y':min(ys),'w':max(xs)-min(xs)+1,'h':max(ys)-min(ys)+1,
                        'area':len(group),'cx':sum(xs)/len(xs),'cy':sum(ys)/len(ys)})
    return sorted(results,key=lambda c:(c['y'],c['x']))


def base_feature(fid,kind,layer,name=''):
    return dict(id=fid,kind=kind,layer_id=layer,name=name,visibility='gm',truth='unknown',
                known_to=[],evidence_ids=[],entity_id=None,opacity=1,show_label=bool(name),
                provenance={'kind':'source_map_transcription','review':'visual_source','canon':'unconfirmed'})


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--layers',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True);parser.add_argument('--symbols',type=Path,required=True)
    args=parser.parse_args()
    if args.out.exists():raise SystemExit('Output must be a new directory; existing map edits are never overwritten.')
    manifest=json.loads(Path(__file__).with_name('world.json').read_text(encoding='utf-8'))
    original=json.loads(args.symbols.read_text(encoding='utf-8-sig'))
    base=np.array(Image.open(args.layers/'layer-0.png').convert('RGBA'))
    borders=np.array(Image.open(args.layers/'layer-1.png').convert('RGBA'))
    height,width=base.shape[:2];factor=width/manifest['coordinate_width']
    doc=dict(schema_version=1,id='MAP-78325F2B-3F17-4CBE-88F2-1C28D073D5DB',name='Мир — объектная карта',
             width=width,height=height,background='#D3E0DE',style='cartographic',status='draft',story_anchor=None,
             features={},nodes={},arcs={},symbols=original.get('symbols',original),layers=[],
             reconstruction={'source_sha256':manifest['source_sha256'],'method':'shared-boundary-topology',
                 'geometry':'approximate_trace','tolerance_px':1.6,'source_date':'unassigned',
                 'hidden_source_layers':'source layers 2,3,6 and 8-31 transcribed into hidden archive layers; original PDN retained separately'})
    def layer(lid,name,locked=False):
        doc['layers'].append(dict(id=lid,name=name,kind='vector',visible=True,locked=locked,opacity=1,blend_mode=0))
    layer('LAYER-TERRITORIES','Страны и владения')
    layer('LAYER-WATERS','Берега, озёра и реки',True)
    layer('LAYER-RELIEF','Горные хребты')
    layer('LAYER-SETTLEMENTS','Города и крепости')
    layer('LAYER-ZONES','Пометки исходной карты')
    layer('LAYER-LABELS','Названия стран')
    layer('LAYER-DRAWING','Новые объекты')
    regions=[];palettes=[];unique={};seeds=[]
    for r in manifest['regions']:
        sx,sy=[round(c*factor) for c in r['seed']]
        palette=collections.Counter(map(tuple,borders[sy-3:sy+4,sx-3:sx+4].reshape(-1,4))).most_common(1)[0][0]
        if palette[3]==0:raise ValueError('Seed is outside a country: '+r['id'])
        if r['id'] not in unique:unique[r['id']]=len(regions);regions.append(r)
        seeds.append((sx,sy));palettes.append((unique[r['id']],palette))
    sample=borders[1::2,1::2].astype(np.int32);h,w=sample.shape[:2]
    grid=np.full((h,w),-1,np.int16);best=np.full((h,w),1e12);yy,xx=np.indices((h,w))
    for (label,palette),(sx,sy) in zip(palettes,seeds):
        d=np.sum((sample[:,:,:3]-np.array(palette[:3]))**2,axis=2).astype(float)
        # Identical source colours in different kingdoms are disambiguated spatially.
        d+=((xx*2-sx)**2+(yy*2-sy)**2)*0.000001
        choose=(d<best)&(sample[:,:,3]>=12)
        grid[choose]=label;best[choose]=d[choose]
    print('Tracing country topology...',flush=True)
    rings=topology(grid,doc,'POL')
    for i,r in enumerate(regions):
        fid='MAPOBJ-COUNTRY-'+r['id'].upper()
        f=base_feature(fid,'region','LAYER-TERRITORIES',r['name'])
        f.update(rings=rings[i],closed=True,fill=r['fill'],stroke='#797567',stroke_width=2.3,
                 show_label=False,role='country')
        f['provenance']['source_layer']=1
        doc['features'][fid]=f
    print('Tracing coastlines and water network...',flush=True)
    # Use the source's water colour; cities and mountains do not punch holes in land.
    water=(base[:,:,2]>200)&(base[:,:,0]<90)&(base[:,:,1]<145)
    water_grid=np.where(water[1::2,1::2],0,-1).astype(np.int16)
    water_rings=topology(water_grid,doc,'HYD',epsilon=1.2)
    water_f=base_feature('MAPOBJ-WATER-NETWORK','region','LAYER-WATERS','Водная сеть')
    water_f.update(rings=water_rings[0],closed=True,fill='#D3E0DE',stroke='#8AACA9',stroke_width=1.25,
                   opacity=0.94,show_label=False,role='water',label_offset=[0,0])
    doc['features'][water_f['id']]=water_f
    grey=base[:,:,:3].astype(np.int16)
    mountains=(np.max(grey,axis=2)-np.min(grey,axis=2)<5)&(grey[:,:,0]>=65)&(grey[:,:,0]<=150)
    peaks=[c for c in components(mountains) if 18<=c['area']<=700 and 7<=c['w']<=45 and 4<=c['h']<=30]
    print('Recognized mountain symbols:',len(peaks),flush=True)
    for c in peaks:
        fid=f"MAPOBJ-PEAK-{c['x']}-{c['y']}"
        f=base_feature(fid,'symbol','LAYER-RELIEF','Гора')
        f.update(position=[round(c['cx'],2),round(c['cy'],2)],size=max(20,c['w']*1.22),symbol_id='mountain',
                 stroke='#716957',rotation=0,show_label=False,role='relief')
        f['provenance']['source_layer']=0;f['provenance']['source_bbox']=[c[k] for k in ['x','y','w','h']]
        doc['features'][fid]=f
    black=(np.max(grey,axis=2)<55)
    marks=components(black)
    settlements=[]
    for c in marks:
        if c['area']>=30 and 6<=c['w']<=48 and 9<=c['h']<=48:
            if any(abs(c['cx']-s['cx'])<24 and abs(c['cy']-s['cy'])<20 for s in settlements):continue
            settlements.append(c)
    print('Recognized settlement symbols:',len(settlements),flush=True)
    for c in settlements:
        fid=f"MAPOBJ-SETTLEMENT-{c['x']}-{c['y']}"
        symbol='fortress' if c['w']<14 else 'city'
        f=base_feature(fid,'symbol','LAYER-SETTLEMENTS','Крепость' if symbol=='fortress' else 'Город')
        f.update(position=[round(c['cx'],2),round(c['cy'],2)],size=23 if symbol=='fortress' else 34,
                 symbol_id=symbol,stroke='#615A4B',rotation=0,show_label=False,role='settlement')
        f['provenance'].update(source_layer=0,source_bbox=[c[k] for k in ['x','y','w','h']],review='symbol_classification')
        doc['features'][fid]=f
    # Country labels are explicitly transcribed from the visible source, not invented from lore.
    for index,r in enumerate(manifest['regions']):
        fid='MAPOBJ-NAME-'+r['id'].upper()+f'-{index}'
        name=r['name'];words=name.split();lines=[];line=''
        for word in words:
            if len(line+' '+word)>19 and line:lines.append(line);line=''
            line=(line+' '+word).strip()
        if line:lines.append(line)
        f=base_feature(fid,'label','LAYER-LABELS',name)
        f.update(position=[round(x*factor,2) for x in r['label']],symbol_id='label',size=24,
                 font_size=26 if len(lines)>2 else 29,label_offset=[0,0],label_text='\n'.join(lines),
                 text_color='#514D43',label_width=350,label_height=110,role='country_label',
                 parent_id='MAPOBJ-COUNTRY-'+r['id'].upper(),min_zoom=0)
        doc['features'][fid]=f
    # OCR remains marked as a transcription requiring review; it never asserts campaign knowledge.
    labels_file=args.layers/'city-labels.json'
    label_count=0
    if labels_file.exists():
        entries=json.loads(labels_file.read_text(encoding='utf-8'))
        cleaned=[]
        for entry in sorted(entries,key=lambda e:(-len(e['text']),e['y'],e['x'])):
            text=entry['text'].strip()
            if not text or not text[0].isupper() or any(len(w)==1 for w in text.split()):continue
            if any(abs(e['x']-entry['x'])<32 and abs(e['y']-entry['y'])<22 and (text in e['text'] or e['text'] in text) for e in cleaned):continue
            cleaned.append(entry)
        for entry in sorted(cleaned,key=lambda e:(e['y'],e['x'])):
            name=entry['text'].strip();x,y=entry['x'],entry['y']
            if len(name)<3 or not any('\u0400'<=ch<='\u04ff' for ch in name):continue
            nearest=min(settlements,key=lambda c:(c['cx']-x)**2+(c['cy']-y)**2,default=None)
            fid=f'MAPOBJ-PLACENAME-{round(x)}-{round(y)}'
            f=base_feature(fid,'label','LAYER-SETTLEMENTS',name)
            f.update(position=[x,y],symbol_id='label',size=12,font_size=14,label_offset=[0,0],
                     text_color='#625B4D',label_width=max(80,entry.get('w',100)*1.3),label_height=38,
                     role='settlement_label',min_zoom=0.5)
            f['provenance'].update(source_layer=entry.get('source_layer',4),review='ocr_unverified',source_bbox=entry.get('bbox'))
            if nearest and math.hypot(nearest['cx']-x,nearest['cy']-y)<85:
                parent=f"MAPOBJ-SETTLEMENT-{nearest['x']}-{nearest['y']}"
                f['parent_id']=parent;doc['features'][parent]['name']=name
            doc['features'][fid]=f;label_count+=1
    # Visible hand-written marks survive as vector ink; they are not reinterpreted as a political event.
    annotation=np.array(Image.open(args.layers/'layer-7.png').convert('RGBA'))[1::2,1::2]
    red=(annotation[:,:,0]>180)&(annotation[:,:,1]<80)&(annotation[:,:,2]<80)&(annotation[:,:,3]>85)
    annotation_rings=topology(np.where(red,0,-1),doc,'ANN',epsilon=1)
    if annotation_rings.get(0):
        f=base_feature('MAPOBJ-SOURCE-ANNOTATIONS','region','LAYER-ZONES','Пометки автора')
        f.update(rings=annotation_rings[0],closed=True,fill='#A86158',stroke='none',stroke_width=0,show_label=False)
        doc['features'][f['id']]=f
    layer('LAYER-ARCHIVE-ROUTES','Архив · торговые пути')
    doc['layers'][-1]['visible']=False
    source_routes=np.array(Image.open(args.layers/'layer-3.png').convert('RGBA'))[1::2,1::2]
    visible=source_routes[:,:,3]>96
    colors=[]
    for c,n in collections.Counter(map(tuple,source_routes[:,:,:3][visible])).most_common(60):
        if n>100 and all(sum((int(c[k])-int(other[k]))**2 for k in range(3))>1200 for other in colors):colors.append(c)
    route_count=0
    if colors:
        distances=np.stack([np.sum((source_routes[:,:,:3].astype(np.int32)-c)**2,axis=2) for c in colors])
        classes=distances.argmin(axis=0)
        inks=['#A66E59','#AE9448','#578E9C','#79875A','#8E7399','#A28075','#786B99','#789186','#866F5B','#9A786F','#6D9690','#B5A271']
        for ci,c in enumerate(colors):
            chains=extract_routes(visible&(classes==ci))
            for chain in chains:
                if len(chain)<5:continue
                pts=simplify(chain,1)
                ids=[]
                for x,y in pts:
                    nid=f'NODE-TRADE-{ci}-{x}-{y}';doc['nodes'][nid]=[x*2,y*2];ids.append(nid)
                aid='ARC-TRADE-'+compact_id(str(chain));doc['arcs'][aid]={'nodes':ids}
                fid='MAPOBJ-TRADE-'+compact_id(str(chain))
                f=base_feature(fid,'route','LAYER-ARCHIVE-ROUTES','Торговый путь')
                f.update(rings=[[{'id':aid,'reverse':False}]],closed=False,fill='none',stroke=inks[ci%len(inks)],
                         stroke_width=4,show_label=False,role='trade_route')
                f['provenance'].update(source_layer=3,source_color=[int(v) for v in c],review='source_trace')
                doc['features'][fid]=f;route_count+=1
    layer('LAYER-ARCHIVE-NOTES','Архив · отряды и флот')
    doc['layers'][-1]['visible']=False
    notes_file=args.layers/'hidden-notes.json'
    hidden_count=0
    if notes_file.exists():
        for note in json.loads(notes_file.read_text(encoding='utf8')):
            x0,y0,x1,y1=note['bbox'];name=note['name'];is_fleet='Флот' in name or 'ВМФ' in name
            fid='MAPOBJ-SOURCE-NOTE-'+str(note['layer'])
            f=base_feature(fid,'symbol' if is_fleet else 'label','LAYER-ARCHIVE-NOTES',name)
            f.update(position=[(x0+x1)/2,(y0+y1)/2],size=84 if is_fleet else 24,
                     symbol_id='fleet' if is_fleet else 'label',label_offset=[0,67] if is_fleet else [0,0],
                     label_width=290,label_height=95,font_size=16,stroke='#685B45',text_color='#574F40',
                     role='archived_note',source_note=note['text'])
            f['provenance'].update(source_layer=note['layer'],source_bbox=note['bbox'],review='ocr_unverified',source_visible=False)
            doc['features'][fid]=f;hidden_count+=1
    legacy_count=add_legacy(doc,args.layers,simplify,components)
    # Discard unused trace chains after tiny islands/noise were filtered.
    used_arcs={ref['id'] for f in doc['features'].values() for ring in f.get('rings',[]) for ref in ring}
    doc['arcs']={k:v for k,v in doc['arcs'].items() if k in used_arcs}
    used_nodes={k for arc in doc['arcs'].values() for k in arc['nodes']}
    doc['nodes']={k:v for k,v in doc['nodes'].items() if k in used_nodes}
    for z,f in enumerate(doc['features'].values(),1):f['z_order']=z
    doc['next_z']=len(doc['features'])
    report=dict(countries=len(regions),mountains=len(peaks),settlements=len(settlements),place_labels=label_count,
                archived_notes=hidden_count,trade_segments=route_count,archived_diagram_objects=legacy_count,
                features=len(doc['features']),nodes=len(doc['nodes']),arcs=len(doc['arcs']),visible_rasters=0)
    doc['reconstruction']['counts']=report
    doc['reconstruction']['input_hashes']={str(file.name):hashlib.sha256(file.read_bytes()).hexdigest()
        for file in [Path(__file__).with_name('world.json'),args.layers/'layer-0.png',args.layers/'layer-1.png',args.layers/'layer-3.png',args.layers/'layer-7.png',labels_file,notes_file] if file.exists()}
    args.out.mkdir(parents=True)
    (args.out/'map.json').write_text(json.dumps(doc,ensure_ascii=False,sort_keys=True,indent=2)+'\n',encoding='utf-8')
    (args.out/'reconstruction.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False),flush=True)


if __name__=='__main__':main()
