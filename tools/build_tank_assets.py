"""Build original prototype vehicle/factory GLBs and matching atlas icons.

Pure geometry, with named Hull/Turret/Tracks nodes for later independent turret aiming.
Run using Python with Pillow. No external assets or downloads are required.
"""
from pathlib import Path
import collections
import json
import math
import struct
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
PALETTE = [(0.24,.30,.22),(.34,.40,.29),(.075,.085,.08),(.16,.18,.17),(.48,.25,.09),(.15,.24,.28),(.28,.34,.36),(.055,.075,.08),(.12,.58,.65)]

class Mesh:
    def __init__(self): self.parts = collections.defaultdict(list)
    def face(self, points, material, part='Hull'):
        for i in range(1,len(points)-1): self.parts[(part,material)].append((points[0],points[i],points[i+1]))
    def box(self, center, size, material, part='Hull', top_scale=1):
        x,y,z=center; w,h,d=[v/2 for v in size]
        v=[(x-w,y-h,z-d),(x+w,y-h,z-d),(x+w,y-h,z+d),(x-w,y-h,z+d),
           (x-w*top_scale,y+h,z-d*top_scale),(x+w*top_scale,y+h,z-d*top_scale),
           (x+w*top_scale,y+h,z+d*top_scale),(x-w*top_scale,y+h,z+d*top_scale)]
        for f in [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)]: self.face([v[i] for i in reversed(f)],material,part)
    def cylinder(self, center, radius, length, axis, material, part='Hull', segments=12):
        rings=[]
        for sign in [-1,1]:
            ring=[]
            for i in range(segments):
                a=2*math.pi*i/segments
                q=[radius*math.cos(a),radius*math.sin(a)]; p=list(center)
                p[axis]+=sign*length/2
                others=[j for j in range(3) if j!=axis]
                p[others[0]]+=q[0];p[others[1]]+=q[1];ring.append(tuple(p))
            rings.append(ring)
        def face(points): self.face(list(reversed(points)) if axis==1 else points,material,part)
        face(list(reversed(rings[0])));face(rings[1])
        for i in range(segments):
            j=(i+1)%segments
            face([rings[0][i],rings[0][j],rings[1][j],rings[1][i]])
    def export(self, path):
        data=bytearray(); views=[]; accessors=[]; groups=collections.defaultdict(list)
        for (part,mat),triangles in self.parts.items():
            positions=[];normals=[]
            for triangle in triangles:
                a,b,c=triangle; u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)]
                n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
                l=math.sqrt(sum(t*t for t in n)) or 1;n=[t/l for t in n]
                positions.extend(triangle);normals.extend([n]*3)
            attrs={}
            for name,values in [('POSITION',positions),('NORMAL',normals)]:
                raw=struct.pack('<'+'f'*len(values)*3,*[c for v in values for c in v]);offset=len(data);data.extend(raw)
                views.append({'buffer':0,'byteOffset':offset,'byteLength':len(raw),'target':34962})
                a={'bufferView':len(views)-1,'componentType':5126,'count':len(values),'type':'VEC3'}
                if name=='POSITION':a.update(min=[min(v[i] for v in values) for i in range(3)],max=[max(v[i] for v in values) for i in range(3)])
                accessors.append(a);attrs[name]=len(accessors)-1
            groups[part].append({'attributes':attrs,'material':mat,'mode':4})
        document={'asset':{'version':'2.0','generator':'Strategy Game original prototype geometry'},'scene':0,
          'scenes':[{'nodes':list(range(len(groups)))}], 'nodes':[{'name':part,'mesh':i} for i,part in enumerate(groups)],
          'meshes':[{'name':part,'primitives':primitives} for part,primitives in groups.items()],
          'materials':[{'name':'prototype_'+str(i),'doubleSided':True,'pbrMetallicRoughness':{'baseColorFactor':[*c,1],'metallicFactor':.25,'roughnessFactor':.8}} for i,c in enumerate(PALETTE)],
          'buffers':[{'byteLength':len(data)}],'bufferViews':views,'accessors':accessors}
        raw=json.dumps(document,separators=(',',':')).encode();raw+=b' '*((-len(raw))%4);data+=b'\0'*((-len(data))%4)
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(struct.pack('<III',0x46546c67,2,28+len(raw)+len(data))+struct.pack('<II',len(raw),0x4e4f534a)+raw+struct.pack('<II',len(data),0x004e4942)+data)
    def icon(self):
        faces=[]
        for (_,material),triangles in self.parts.items():
            for tri in triangles:
                projected=[((x-z)*.707, (x+z)*.34-y*.94) for x,y,z in tri]
                faces.append((sum(x+y*.7+z for x,y,z in tri),projected,material,tri))
        xs=[p[0] for _,tri,_,_ in faces for p in tri];ys=[p[1] for _,tri,_,_ in faces for p in tri]
        scale=210/max(max(xs)-min(xs),max(ys)-min(ys));cx=(max(xs)+min(xs))/2;cy=(max(ys)+min(ys))/2
        image=Image.new('RGBA',(256,256));draw=ImageDraw.Draw(image)
        for _,tri,mat,points in sorted(faces):
            color=tuple(int(min(1,c*1.65+.05)*255) for c in PALETTE[mat])+(255,)
            draw.polygon([(128+(x-cx)*scale,128+(y-cy)*scale) for x,y in tri],fill=color)
        return image

def tank():
    m=Mesh()
    for x in [-1.4,1.4]:
        m.box((x,.48,0),(.72,.82,3.9),2,'Tracks',.88)
        for z in [-1.35,-.68,0,.68,1.35]:m.cylinder((x+math.copysign(.38,x),.48,z),.33,.08,0,3,'Tracks')
        for z in [i*.32-1.65 for i in range(11)]:m.box((x,.91,z),(.75,.07,.16),3,'Tracks')
        m.box((x,1.07,-.05),(.83,.18,3.5),0)
    m.box((0,.92,0),(2.55,.85,3.65),0,top_scale=.82)
    m.box((0,1.38,-.1),(2.3,.18,3),1)
    m.box((0,1.81,-.08),(1.9,.75,1.95),1,'Turret',.74)
    m.cylinder((0,2.24,-.24),.36,.10,1,3,'Turret')
    m.cylinder((.1,1.97,1.85),.115,2.3,2,3,'Barrel')
    m.box((.1,1.97,.92),(.43,.36,.65),0,'Barrel')
    m.box((.1,1.97,3.02),(.27,.24,.22),2,'Barrel')
    for x in [-.76,.76]:m.box((x,1.36,1.55),(.22,.12,.08),8)
    for z in [-1.25,-1,-.75]:m.box((0,1.51,z),(1.15,.04,.12),2)
    m.box((-.5,1.96,.85),(.45,.12,.04),4,'Turret')
    return m

def factory():
    m=Mesh()
    m.box((0,.15,0),(13,.3,10),3,'Foundation')
    m.box((0,2.05,0),(12,3.8,9),5,'Structure')
    for x in [-5.8,5.8]:
        for z in [-4.25,0,4.25]:
            m.box((x,2.2,z),(.28,4.2,.35),6,'Structure')
            m.box((x,1,z),(.32,.35,.39),4,'Structure')
    # Pitched steel roof.
    m.face([(-6.4,4.05,4.8),(0,5.6,4.8),(0,5.6,-4.8),(-6.4,4.05,-4.8)],6,'Roof')
    m.face([(0,5.6,4.8),(6.4,4.05,4.8),(6.4,4.05,-4.8),(0,5.6,-4.8)],6,'Roof')
    for z in [-4.53,4.53]:
        m.face([(-6,3.95,z),(6,3.95,z),(0,5.45,z)],5,'Structure')
        m.box((0,1.8,z),(5.2,3.2,.10),7,'Details')
        for y in [.45,.85,1.25,1.65,2.05,2.45,2.85,3.25]:m.box((0,y,z*1.005),(5.0,.045,.07),6,'Details')
        m.box((0,3.6,z*1.01),(5.6,.24,.13),4,'Details')
        for x in [-2.85,2.85]:m.box((x,1.8,z*1.01),(.22,3.5,.15),6,'Details')
    for x in [-6.03,6.03]:
        for z in [-2.5,-1,1,2.5]:m.box((x,3.05,z),(.10,.65,1.0),8,'Details')
    for z in [-2.3,2.3]:m.box((0,5.5,z),(2,.6,1.3),3,'Roof')
    m.cylinder((4.8,4.7,-3.5),.35,2.3,1,3,'Details')
    m.cylinder((4.8,5.9,-3.5),.45,.15,1,6,'Details')
    return m

if __name__=='__main__':
    atlas=Image.open(ROOT/'assets/textures/icons/icons_atlas.png').convert('RGBA')
    for name,mesh,key,cell in [('light_tank',tank(),'unit_light_tank',(128,384)),('tank_factory',factory(),'building_tank_factory',(320,64))]:
        mesh.export(ROOT/f'assets/models/gen/vehicles/{name}.glb')
        icon=mesh.icon();icon.save(ROOT/f'assets/icons/source/{key}.png')
        generated=ROOT/'assets/icons/source/generated';generated.mkdir(exist_ok=True);icon.save(generated/f'{key}.png')
        atlas.paste(icon.resize((64,64),Image.Resampling.LANCZOS),cell)
    atlas.save(ROOT/'assets/textures/icons/icons_atlas.png')
    print('Generated original tank/factory meshes and matching icons.')
