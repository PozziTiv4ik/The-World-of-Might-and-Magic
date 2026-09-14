"""Two remaining source diagram layers, transcribed without asserting their meaning."""
import hashlib
import numpy as np
from PIL import Image
from routes import extract

def add_legacy(doc,layers,simplify,components):
    layer='LAYER-ARCHIVE-ROUTES'
    for l in doc['layers']:
        if l['id']==layer:l['name']='Архив · схемы и маршруты'
    added=0
    for index,title,ink,width in [(2,'Гильдия Михаэля','#B88948',6),(6,'Границы регионов империи','#827663',3)]:
        image=np.array(Image.open(layers/f'layer-{index}.png').convert('RGBA'))
        mask=image[:,:,3]>80
        if index==2:mask&=(image[:,:,0]>180)&(image[:,:,1]>45)&(image[:,:,1]<190)&(image[:,:,2]<80)
        paths=extract(mask[1::2,1::2]);rings=[]
        for path in paths:
            pts=simplify(path,.6);ids=[]
            for x,y in pts:
                nid=f'NODE-SOURCE-{index}-{x}-{y}';doc['nodes'][nid]=[x*2,y*2];ids.append(nid)
            if len(ids)<2:continue
            aid=f'ARC-SOURCE-{index}-'+hashlib.sha256(str(path).encode()).hexdigest()[:18].upper()
            doc['arcs'][aid]={'nodes':ids};rings.append([{'id':aid,'reverse':False}])
        fid=f'MAPOBJ-SOURCE-DIAGRAM-{index}'
        doc['features'][fid]=dict(id=fid,kind='stroke',name=title,layer_id=layer,rings=rings,closed=False,
            fill='none',stroke=ink,stroke_width=width,show_label=False,opacity=1,visibility='gm',truth='unknown',
            known_to=[],evidence_ids=[],entity_id=None,role='archived_diagram',provenance={'source_layer':index,'kind':'source_trace','source_visible':False})
        added+=1
        if index==2:
            marks=components((image[:,:,3]>96)&(np.min(image[:,:,:3],axis=2)>220))
            marks=[c for c in marks if c['area']>150]
            if len(marks)!=4:raise ValueError('Guild marker layout differs from the reviewed source')
            for c,text in zip(marks,['100%','29%','12%','1%']):
                mid=f"MAPOBJ-GUILD-NOTE-{c['x']}-{c['y']}"
                doc['features'][mid]=dict(id=mid,kind='label',name=text,position=[round(c['cx'],2),round(c['cy'],2)],
                    layer_id=layer,parent_id=fid,role='source_marker_label',font_size=18,label_width=78,label_height=35,
                    label_offset=[0,0],show_label=True,opacity=1,text_color='#6F6049',visibility='gm',known_to=[],
                    evidence_ids=[],truth='unknown',entity_id=None,provenance={'source_layer':2,'review':'visual_source','meaning':'unassigned'})
                added+=1
    return added
