"""Extract route centrelines and junctions; each chain is editable native geometry."""
import collections,hashlib
import numpy as np

def thin(mask):
    a=mask.astype(np.uint8).copy()
    for iteration in range(70):
        changed=False
        for phase in (0,1):
            p=np.pad(a,1)
            ns=[p[:-2,1:-1],p[:-2,2:],p[1:-1,2:],p[2:,2:],p[2:,1:-1],p[2:,:-2],p[1:-1,:-2],p[:-2,:-2]]
            neighbours=sum(ns);transitions=sum(((ns[i]==0)&(ns[(i+1)%8]==1)).astype(np.uint8) for i in range(8))
            if phase==0:guard=(ns[0]*ns[2]*ns[4]==0)&(ns[2]*ns[4]*ns[6]==0)
            else:guard=(ns[0]*ns[2]*ns[6]==0)&(ns[0]*ns[4]*ns[6]==0)
            remove=(a==1)&(neighbours>=2)&(neighbours<=6)&(transitions==1)&guard
            if np.any(remove):a[remove]=0;changed=True
        if not changed:break
    return a

def extract(mask):
    skeleton=thin(mask);ys,xs=np.nonzero(skeleton);pixels=set(zip(xs.tolist(),ys.tolist()))
    adj={}
    for x,y in pixels:
        neighbours=[]
        for dx,dy in [(-1,0),(1,0),(0,-1),(0,1),(-1,-1),(-1,1),(1,-1),(1,1)]:
            q=(x+dx,y+dy)
            if q not in pixels:continue
            if dx and dy and ((x+dx,y) in pixels or (x,y+dy) in pixels):continue
            neighbours.append(q)
        adj[(x,y)]=sorted(neighbours)
    edges=set();result=[]
    def seen(a,b):return tuple(sorted([a,b])) in edges
    for start in sorted(pixels,key=lambda p:(len(adj[p])==2,p)):
        for nxt in adj[start]:
            if seen(start,nxt):continue
            points=[start];prev=start;cur=nxt
            while True:
                edges.add(tuple(sorted([prev,cur])));points.append(cur)
                if cur==start or len(adj[cur])!=2:break
                possible=[q for q in adj[cur] if q!=prev and not seen(cur,q)]
                if not possible:break
                prev,cur=cur,possible[0]
            if len(points)>=3:result.append(points)
    return result
