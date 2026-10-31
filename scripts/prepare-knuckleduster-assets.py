"""Pixel-preserving Knuckleduster extraction and geometric relief bake.

Explicit art preparation, never a build dependency. Requires Pillow/NumPy/SciPy.
Source RGB is never resized, quantised, recoloured, inpainted or regenerated.
Depth is registered geometric camera Z; normals derive from continuous Z.
"""

import argparse
import copy
import difflib
import hashlib
import json
import math
import shutil
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "raw/knuckleduster"
PACK = ROOT / "content/base/assets/knuckleduster"
PREFIX = "phoenix-base:assets/knuckleduster/"
PARTS = ("fore-dorsal", "mid-dorsal", "aft-dorsal", "fore-ventral", "aft-ventral")
SOCKETS = {p: "turret_" + p.replace("-", "_") for p in PARTS}
# Physical longitudinal positions in common source-pixel units. Camera Z is
# F*cos(azimuth)+P*sin(azimuth); all main mounts are on the centreline P=0.
FORWARD = dict(zip(PARTS, (470, 180, -500, 350, -500)))
SOURCES = [
    dict(name="bow", angle=0, file="phoenix-knuckleduster-front-v1.png", pivot=[768,570], cutoff=918,
         mounts=[[768,275],[768,196],[768,311],[768,815],[768,788]],
         polys=[[(650,274),(653,219),(681,198),(849,198),(868,221),(870,274)],
                [(684,194),(686,128),(712,104),(822,104),(847,128),(850,194)], None,
                [(666,817),(853,817),(855,884),(824,912),(695,912),(669,886)],None],
         muzzles=[[766,226],[766,139],None,[764,868],None],
         hull=[100,255,1440,825], nozzles=[[400,420],[1130,420],[400,700],[1130,700]],
         engines=[], bores=[[610,556],[923,556],[610,585],[923,585]], holes=[]),
    dict(name="bow_port", angle=45, file="phoenix-knuckleduster-front-45-v1.png", pivot=[810,570], cutoff=925,
         mounts=[[365,330],[637,286],[1310,310],[604,845],[1270,769]],
         polys=[[(208,279),(273,279),(289,253),(366,249),(423,260),(448,279),(446,311),(423,329),(286,335),(264,319),(209,312)],
                [(464,208),(535,208),(554,184),(613,171),(669,173),(717,187),(733,207),(730,249),(704,265),(704,282),(560,287),(540,268),(465,241)],
                [(1235,215),(1290,210),(1350,214),(1369,225),(1424,229),(1424,242),(1373,243),(1420,251),(1420,265),(1368,264),(1366,285),(1343,299),(1280,300),(1241,283)],
                [(462,872),(528,865),(542,844),(612,835),(681,847),(698,864),(696,894),(671,914),(545,915),(526,900),(462,900)],
                [(1182,763),(1250,751),(1302,758),(1324,770),(1421,771),(1421,784),(1329,786),(1421,793),(1421,809),(1323,806),(1310,823),(1250,829),(1190,812)]],
         muzzles=[[222,293],[478,223],[1410,242],[479,881],[1408,788]],
         hull=[36,300,1505,860], nozzles=[[1480,438],[1370,416],[1490,630],[1390,605]],
         engines=[[1325,440,178,96],[1325,631,178,96]],
         bores=[[277,418],[245,457],[392,711],[367,747]], holes=[]),
    dict(name="port", angle=90, file="phoenix-knuckleduster-side-v2.png", pivot=[805,560], cutoff=916,
         mounts=[[302,281],[568,262],[1294,293],[508,816],[1215,814]],
         polys=[[(133,228),(226,228),(240,214),(286,209),(334,214),(362,228),(371,245),(367,268),(348,282),(252,282),(236,271),(159,267),(159,253),(230,253),(232,245),(134,244)],
                [(396,192),(486,192),(504,177),(553,169),(614,171),(651,181),(675,194),(685,218),(680,245),(652,263),(524,265),(505,251),(420,231),(419,216),(499,216),(499,208),(397,207)],
                [(1236,216),(1290,212),(1344,221),(1362,232),(1474,236),(1474,251),(1367,251),(1457,258),(1457,274),(1361,273),(1341,291),(1268,293),(1240,279)],
                [(330,830),(432,829),(449,815),(500,811),(557,819),(589,832),(602,851),(598,877),(575,896),(474,900),(450,883),(333,874),(333,855),(434,855),(433,847),(330,846)],
                [(1140,817),(1200,812),(1261,820),(1286,831),(1403,829),(1403,843),(1297,846),(1418,848),(1418,862),(1289,866),(1270,887),(1189,895),(1150,878)]],
         muzzles=[[146,240],[408,203],[1461,247],[343,846],[1407,849]],
         hull=[18,290,1519,837], nozzles=[[1504,458],[1488,458],[1504,678],[1488,678]],
         engines=[[1370,455,145,101],[1370,672,145,99]],
         bores=[[155,346],[135,388],[261,652],[234,695]], holes=[]),
    dict(name="stern_port", angle=135, file="phoenix-knuckleduster-rear-45-v1.png", pivot=[764,566], cutoff=924,
         mounts=[[313,305],[514,277],[1207,353],[359,739],[1068,834]],
         polys=[[(180,239),(239,239),(256,222),(301,218),(345,224),(371,237),(379,259),(374,281),(351,300),(266,302),(248,287),(180,273)],
                [(363,190),(440,190),(455,175),(492,170),(540,175),(570,187),(590,203),(593,235),(576,255),(544,272),(455,275),(436,257),(365,221)],
                [(1082,230),(1125,216),(1183,215),(1234,227),(1258,239),(1366,246),(1367,261),(1262,260),(1340,271),(1340,285),(1258,283),(1242,307),(1154,313),(1101,294),(1083,272)],
                [(222,737),(278,740),(298,733),(339,733),(384,744),(416,758),(456,770),(461,788),(447,810),(383,825),(322,816),(295,795),(225,758)],
                [(921,832),(975,825),(1034,831),(1061,845),(1212,871),(1212,886),(1071,864),(1197,897),(1197,914),(1055,888),(1038,903),(978,905),(933,881)]],
         muzzles=[[191,248],[375,198],[1354,252],[233,746],[1201,878]],
         hull=[60,284,1500,833], nozzles=[[1317,492],[1445,482],[1317,710],[1445,700]],
         engines=[[1230,491,157,129],[1230,708,157,129],[1432,484,64,98],[1432,701,64,98]],
         bores=[[146,327],[135,360],[175,575],[203,601]], holes=[]),
    dict(name="stern", angle=180, file="phoenix-knuckleduster-rear-v1.png", pivot=[768,570], cutoff=925,
         mounts=[[768,320],[768,300],[756,286],[768,783],[757,812]],
         polys=[None,None,[(667,281),(669,207),(691,180),(730,170),(780,172),(816,188),(832,212),(831,280)],None,
                [(669,806),(830,806),(836,876),(812,907),(699,911),(670,885)]],
         muzzles=[None,None,[752,207],None,[755,847]],
         hull=[99,278,1440,838], nozzles=[[548,449],[1000,449],[548,700],[1000,700]],
         engines=[[548,449,141,141],[1000,449,141,141],[548,700,141,141],[1000,700,141,141]],
         bores=[], holes=[]),
]


def write_json(path, value):
    path.write_text(json.dumps(value, indent="\t") + "\n", encoding="utf-8")


def polygon(shape, points):
    image = Image.new("1", (shape[1], shape[0]))
    if points:
        ImageDraw.Draw(image).polygon([tuple(p) for p in points], fill=1)
    return np.asarray(image, dtype=bool)


def coverage(rgb, view):
    r, g, b = [rgb[:, :, i].astype(np.int16) for i in range(3)]
    paper_colour = (r >= 205) & (g >= 185) & (b >= 140) & (r >= g-6) & (g >= b-6) & (r-b <= 110)
    seeds = np.zeros(paper_colour.shape, bool)
    seeds[0] = paper_colour[0]
    seeds[-1] = paper_colour[-1]
    seeds[:, 0] = paper_colour[:, 0]
    seeds[:, -1] = paper_colour[:, -1]
    for x, y in view["holes"]:
        seeds[y, x] = paper_colour[y, x]
    paper = ndimage.binary_propagation(seeds, mask=paper_colour)
    solid = ~paper
    solid[view["cutoff"]:] = False
    labels, _ = ndimage.label(solid)
    keep = np.bincount(labels.ravel()) >= 12
    keep[0] = False
    return keep[labels]


def geometric_field(shape, view, solid):
    """Registered raised slabs, recessed throat and engine cylinders; no RGB."""
    yy, xx = np.mgrid[:shape[0], :shape[1]].astype(float)
    a = math.radians(view["angle"])
    sn, cs = math.sin(a), math.cos(a)
    cx, cy = view["pivot"]
    x0, y0, x1, y1 = view["hull"]
    u = (xx-(x0+x1)/2) / ((x1-x0)/2)
    v = (yy-(y0+y1)/2) / ((y1-y0)/2)
    # Ray-facing surface of a yawed broad ellipsoidal structural core.
    length, beam = 660., 460.
    radius = math.hypot(length*sn, beam*cs)
    slope = sn*cs*(beam*beam-length*length)/radius
    z = slope*np.clip(u,-1,1) + length*beam/radius*np.sqrt(np.maximum(0,1-u*u-v*v))
    # Armour noses follow registered rows: mechanically near bow at front,
    # with a transverse near-side shell in broadside views.
    for upper in (True, False):
        centre_y = y0 + (y1-y0)*(.24 if upper else .78)
        band = np.abs(yy-centre_y) < (y1-y0)*.20
        forward = 525 if cs >= 0 else -320
        if abs(sn) < .1:
            # Six frontal slabs, four stern shoulders. Leave the middle
            # horizontal throat recessed independently of its painted light.
            columns = 3 if cs > 0 else 2
            for col in range(columns):
                left = x0+(x1-x0)*(col/columns+.025)
                right = x0+(x1-x0)*((col+1)/columns-.025)
                mask = band & (xx>left) & (xx<right)
                distance = ndimage.distance_transform_edt(mask)
                plane = forward*cs + 35 - np.clip(12-distance,0,12)*2
                z[mask] = plane[mask]
        else:
            mask = band & (u>-.94) & (u<.85)
            plane = -cs/max(abs(sn),.6)*(xx-cx) + 420*abs(sn)
            z[mask] = plane[mask]
    # Eight broadside launcher bores are physically recessed, not dark-colour
    # interpreted. Coordinates are measured from their visible source panels.
    broadside = {1:([1043,1088],[515,560,605,650]),2:([963,1013],[517,562,605,650]),
                 3:([702,746],[530,575,618,660])}.get(view["angle"]//45)
    if broadside:
        xs, ys = broadside
        for bx in xs:
            for by in ys:
                rr=((xx-bx)/17)**2+((yy-by)/17)**2
                z -= 28*np.clip(1-rr,0,1)**2
    for ex, ey, rx, ry in view["engines"]:
        eu, ev = (xx-ex)/rx, (yy-ey)/ry
        end_on = view["angle"]==180
        region = eu*eu+ev*ev <= 1 if end_on else (abs(eu)<=1)&(abs(ev)<=1)
        casing = -570*cs + 380*abs(sn) + 95*np.sqrt(np.maximum(0,1-ev*ev))
        if not end_on:
            casing += -cs/max(abs(sn),.6)*(xx-ex)
        if cs<0:
            # Nozzle glow is BEHIND its raised lip even though its RGB is bright.
            for nx, ny in view["nozzles"]:
                rr=((xx-nx)/(rx*(1 if end_on else .52)))**2+((yy-ny)/(ry*.80))**2
                casing -= 110*np.clip(1-rr,0,1)**2
        z[region]=casing[region]
    return z/1024


def encode(z, solid, height, span=3.4):
    indices=ndimage.distance_transform_edt(~solid, return_distances=False, return_indices=True)
    field=ndimage.gaussian_filter(z[tuple(indices)],1.4)
    dy,dx=np.gradient(field)
    n=np.stack((-dx*height,dy*height,np.ones_like(z)),axis=-1)
    n/=np.linalg.norm(n,axis=-1,keepdims=True)
    normals=np.rint((n*.5+.5)*255).clip(0,255).astype(np.uint8)
    normals[~solid]=[128,128,255]
    d=.5+z/span
    assert d[solid].min()>0 and d[solid].max()<1, "Geometry clips depth range"
    return np.rint(d*255).astype(np.uint8),normals


def bounds(mask):
    y,x=np.nonzero(mask)
    return [int(x.min()),int(y.min()),int(x.max())+1,int(y.max())+1]


def canonical(stem, sidecar):
    result=dict(albedo=PREFIX+stem+".png",normal=PREFIX+stem+"-normal.png",depth=PREFIX+stem+"-depth.png",frames=[])
    for f in sidecar["sprite_sheet"]["frames"]:
        result["frames"].append(dict(name=f["name"],rect=f["rect"],pivot=f["pivot"],azimuth=f["camera_azimuth_degrees"],
                                    mirror_x=f["mirror_x"],depth_scale=f["depth_scale"],depth_offset=f["depth_offset"],
                                    attachments={k:dict(x=p[0],y=p[1],layer=p[2],depth_offset=f["attachment_depth_offsets"].get(k,0)) for k,p in f["markers"].items()}))
    return result


def frame(name,angle,rect,pivot):
    return dict(name=name,camera_azimuth_degrees=angle,rect=rect,pivot=pivot,mirror_x=False,depth_scale=3.4,
                depth_offset=0,markers={},attachment_depth_offsets={})


def prepare():
    RAW.mkdir(parents=True,exist_ok=True)
    PACK.mkdir(parents=True,exist_ok=True)
    prepared={part:[] for part in ("hull",)+PARTS}
    registrations=[]
    preview=Image.new("RGB",(5*384,4*300),"#29313b")
    for i,view in enumerate(SOURCES):
        source=ROOT/"docs/art"/view["file"]
        shutil.copyfile(source,RAW/view["file"])
        rgb=np.asarray(Image.open(source).convert("RGB"))
        assert rgb.shape==(1024,1536,3)
        solid=coverage(rgb,view)
        masks={p:ndimage.binary_dilation(polygon(solid.shape,view["polys"][j]),iterations=2) & solid
               for j,p in enumerate(PARTS)}
        # Any detached ink outline left just outside a registered contour
        # belongs to that turret, not a floating speck baked into the hull.
        remaining=solid & ~np.logical_or.reduce(list(masks.values()))
        labels,count=ndimage.label(remaining)
        for label in range(1,count+1):
            component=labels==label
            if int(component.sum())>2500:
                continue
            ys,xs=np.nonzero(component)
            cx,cy=float(xs.mean()),float(ys.mean())
            choices=[j for j,points in enumerate(view["polys"]) if points]
            nearest=min(choices,key=lambda j:(cx-view["mounts"][j][0])**2+(cy-view["mounts"][j][1])**2)
            mx,my=view["mounts"][nearest]
            if abs(cx-mx)<210 and abs(cy-my)<130:
                masks[PARTS[nearest]] |= component
        covered=np.zeros_like(solid)
        for mask in masks.values():
            # Adjacent turntables may share a contour pixel after dilation;
            # give it to the nearer/first registered part exactly once.
            mask &= ~covered
            covered |= mask
        masks["hull"]=solid & ~covered
        assert np.array_equal(masks["hull"]|covered,solid)
        z=geometric_field(solid.shape,view,solid)
        d,n=encode(z,solid,1024)
        hull=Image.fromarray(np.dstack((rgb,masks["hull"].astype(np.uint8)*255)))
        f=frame(view["name"],view["angle"],[0,0,1536,1024],view["pivot"])
        a=math.radians(view["angle"])
        def socket(name,xy,target,layer=None):
            x,y=xy
            assert 0<=x<1536 and 0<=y<1024,(name,xy)
            sampled=(float(d[int(y),int(x)])/255-.5)*3.4
            f["markers"][name]=[x,y,layer if layer is not None else (1 if target>=0 else -1)]
            f["attachment_depth_offsets"][name]=round(target-sampled,8)
        for j,p in enumerate(PARTS):
            socket(SOCKETS[p],view["mounts"][j],FORWARD[p]*math.cos(a)/1024,
                   1 if masks[p].any() else -1)
        # Four exhaust sockets. Port/starboard identities are swapped for
        # mirrored views below, and hidden exhausts have physical negative Z.
        for j,(side,port) in enumerate((("port",390),("starboard",-390))):
            for row in range(2):
                index=j+row*2
                socket(f"engine_{side}_{row+1}",view["nozzles"][index],(-660*math.cos(a)+port*math.sin(a))/1024)
        # Match each named gameplay launcher to a registered visible bore or
        # its occluded counterpart, retaining all authored weapon mechanics.
        for bank,forward,port in (("bow_port",540,210),("bow_starboard",540,-210),
                                  ("port_fore",100,470),("port_aft",-120,470),
                                  ("starboard_fore",100,-470),("starboard_aft",-120,-470)):
            for tube in range(4):
                visible=bool(view["bores"]) and bank=="bow_port" and view["angle"]<=90
                if bank.startswith("bow"):
                    xy=view["bores"][tube] if view["bores"] else [768+(tube%2*2-1)*130,530+(tube//2)*70]
                elif i in (1,2,3):
                    xs,ys=({1:([1043,1088],[515,560,605,650]),2:([963,1013],[517,562,605,650]),
                            3:([702,746],[530,575,618,660])})[i]
                    xy=[xs[0 if bank.endswith("fore") else 1],ys[tube]]
                    visible=bank.startswith("port")
                else:
                    xy=[view["pivot"][0]+(430 if port>0 else -430),490+tube*40]
                target=(forward*math.cos(a)+port*math.sin(a))/1024
                socket(f"missile_{bank}_{tube+1}",xy,target,1 if visible else -1)
        for side,port in (("port",465),("starboard",-465)):
            for j,forward in enumerate((440,160,-160,-440),1):
                x=view["pivot"][0]-forward*math.sin(a)+port*math.cos(a)
                socket(f"pdc_{side}_{j}",[max(5,min(1530,round(x))),445],(forward*math.cos(a)+port*math.sin(a))/1024)
            socket(f"bay_{side}",[view["pivot"][0]+(160 if side=="port" else -160),610],(-120*math.cos(a)+port*math.sin(a))/1024)
        prepared["hull"].append(dict(albedo=hull,depth=Image.fromarray(d),normal=Image.fromarray(n),frame=f))
        receipt=dict(name=view["name"],source=view["file"],sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                     source_size=[1536,1024],visible_bounds=bounds(solid),parts={},reassembly_exact=True)
        reconstruction=np.zeros(solid.shape,np.uint8)
        for p,mask in masks.items():
            reconstruction += mask.astype(np.uint8)
            receipt["parts"][p]=dict(pixels=int(mask.sum()),source_bounds=bounds(mask) if mask.any() else None)
        assert np.array_equal(reconstruction,solid.astype(np.uint8)),"Part overlap or loss"
        for j,p in enumerate(PARTS):
            mask=masks[p]
            if not mask.any():
                prepared[p].append(None)
                continue
            crop=bounds(mask)
            mount=view["mounts"][j]
            # Cell is large enough for every bearing, rotating at the precise
            # original mount. No crop resampling or palette conversion.
            cell=(768,384)
            pivot=[384,192]
            shift=[pivot[k]-mount[k] for k in (0,1)]
            dest=[crop[k]+shift[k] for k in (0,1)]
            assert min(dest)>=0 and dest[0]+crop[2]-crop[0]<=cell[0] and dest[1]+crop[3]-crop[1]<=cell[1],(p,i,crop,dest)
            rgba=np.dstack((rgb,mask.astype(np.uint8)*255))
            local=Image.new("RGBA",cell)
            local.paste(Image.fromarray(rgba).crop(crop),dest)
            # Rounded physical turret housing/barrel proxy, not albedo-derived.
            yy,xx=np.mgrid[:1024,:1536].astype(float)
            edge=np.minimum(ndimage.distance_transform_edt(mask),22)
            tz=(np.sqrt(np.maximum(0,22**2-(22-edge)**2))-
                math.cos(a)*math.sin(a)*(xx-mount[0]))/cell[1]
            tz-=tz[mount[1],mount[0]]
            td,tn=encode(tz,mask,cell[1],span=3.4)
            ld=Image.new("L",cell,128)
            ln=Image.new("RGB",cell,(128,128,255))
            ld.paste(Image.fromarray(td).crop(crop),dest)
            ln.paste(Image.fromarray(tn).crop(crop),dest)
            tf=frame(view["name"],view["angle"],[0,0,*cell],pivot)
            tf["depth_offset"]=-((128/255-.5)*3.4)
            muzzle=view["muzzles"][j]
            tf["markers"]["muzzle"]=[muzzle[0]+shift[0],muzzle[1]+shift[1],1]
            tf["attachment_depth_offsets"]["muzzle"]=.006
            prepared[p].append(dict(albedo=local,depth=ld,normal=ln,frame=tf))
            # Exact registration: putting the exported crop back at its mount
            # reproduces every retained source RGB and alpha sample.
            cut=np.asarray(local)[dest[1]:dest[1]+crop[3]-crop[1],dest[0]:dest[0]+crop[2]-crop[0]]
            assert np.array_equal(cut,rgba[crop[1]:crop[3],crop[0]:crop[2]])
            receipt["parts"][p].update(source_to_frame=shift,pivot=pivot)
        registrations.append(receipt)
        for row,im in enumerate((hull,Image.fromarray(d),Image.fromarray(n))):
            v=im.convert("RGBA")
            v.putalpha(Image.fromarray(masks["hull"].astype(np.uint8)*255))
            v.thumbnail((374,250))
            bg=Image.new("RGBA",v.size,"#29313b")
            bg.alpha_composite(v)
            preview.paste(bg.convert("RGB"),(i*384,row*300+25))
        overlay=Image.fromarray(np.dstack((rgb,solid.astype(np.uint8)*255)))
        draw=ImageDraw.Draw(overlay)
        for j,p in enumerate(PARTS):
            x,y=view["mounts"][j]
            draw.ellipse((x-8,y-8,x+8,y+8),outline="#ff00ff",width=3)
            if view["polys"][j]:
                draw.line(view["polys"][j]+[view["polys"][j][0]],fill="#00ffff",width=2)
        overlay.thumbnail((374,250))
        bg=Image.new("RGBA",overlay.size,"#29313b")
        bg.alpha_composite(overlay)
        preview.paste(bg.convert("RGB"),(i*384,925))
    materials={}
    fallback=[]
    for p,items in prepared.items():
        for i,item in enumerate(items):
            if item is None:
                # No unseen pixels are invented. Use nearest actually painted
                # view of the SAME mount, recording its angular approximation.
                donor=min((k for k,x in enumerate(items) if x is not None),key=lambda k:abs(k-i))
                items[i]=copy.deepcopy(items[donor])
                items[i]["frame"].update(name=SOURCES[i]["name"],camera_azimuth_degrees=SOURCES[i]["angle"])
                fallback.append(dict(part=p,view=SOURCES[i]["name"],donor=SOURCES[donor]["name"],reason="mount occluded in source; nearest painted bearing reused"))
        width,height=items[0]["albedo"].size
        atlas_size=(width*3,height*2)
        atlases=dict(albedo=Image.new("RGBA",atlas_size),depth=Image.new("L",atlas_size,128),normal=Image.new("RGB",atlas_size,(128,128,255)))
        frames=[]
        for i,item in enumerate(items):
            x,y=(i%3)*width,(i//3)*height
            item["frame"]["rect"]=[x,y,width,height]
            for channel,atlas in atlases.items():
                atlas.paste(item[channel],(x,y))
                suffix={"albedo":"","normal":"-normal","depth":"-depth"}[channel]
                item[channel].save(RAW/(p+"-"+SOURCES[i]["name"]+suffix+".png"),optimize=True)
            frames.append(copy.deepcopy(item["frame"]))
        for i,name,angle in ((3,"stern_starboard",225),(2,"starboard",270),(1,"bow_starboard",315)):
            f=copy.deepcopy(frames[i])
            f.update(name=name,camera_azimuth_degrees=angle,mirror_x=True)
            if p=="hull":
                for key in ("markers","attachment_depth_offsets"):
                    f[key]={k.replace("starboard","@swap@").replace("port","starboard").replace("@swap@","port"):v for k,v in f[key].items()}
            frames.append(f)
        # An aft-facing gun's artwork was extracted at heading 180 degrees.
        # Renderer selects on TURRET heading, so rotate frame azimuth labels,
        # never the RGB image, to make all five guns independently aimable.
        if p.startswith("aft"):
            for f in frames:
                f["camera_azimuth_degrees"]=(f["camera_azimuth_degrees"]+180)%360
            frames.sort(key=lambda f:f["camera_azimuth_degrees"])
        stem="knuckleduster-"+p
        sidecar=dict(schema_version=1,id=stem,description="Pixel-preserving extraction from approved Knuckleduster concept views",
                     image_size=list(atlas_size),coordinate_system="frame_pixels_top_left_y_down",sprite_color_space="srgb",
                     files=dict(sprite=stem+".png",normal=stem+"-normal.png",depth=stem+"-depth.png"),
                     billboard_mode="eight",pivot=None,markers={},
                     depth_encoding=dict(channel="r",color_space="linear",white="near",quality="registered_geometry_not_shading"),
                     normal_encoding=dict(space="view",x="right",y="up",z="toward_viewer",quality="continuous_geometry_gradient"),
                     sprite_sheet=dict(kind="directions",columns=4,rows=2,frames=frames))
        for channel,suffix in (("albedo",""),("depth","-depth"),("normal","-normal")):
            # Clear only invisible RGB for compact lossless packaging. All
            # opaque/visible RGB samples remain byte-identical to source.
            im=atlases[channel]
            if channel=="albedo":
                array=np.array(im)
                array[array[:,:,3]==0,:3]=0
                im=Image.fromarray(array)
            im.save(RAW/(stem+suffix+".png"),optimize=True)
            shutil.copyfile(RAW/(stem+suffix+".png"),PACK/(stem+suffix+".png"))
        material=canonical(stem,sidecar)
        materials[p]=material
        for suffix,data in ((".sprite.json",sidecar),(".material.json",material)):
            write_json(RAW/(stem+suffix),data)
            shutil.copyfile(RAW/(stem+suffix),PACK/(stem+suffix))
    receipt=dict(visible_rgb_identical=True,resampling=False,repainting=False,reassembly_exact=True,
                 alpha="border-connected cream flood, registered turret polygons, label cutoff",
                 depth_source="registered armour/core volumes, engine cylinders and recessed nozzle bowls; no RGB or shadow input",
                 normal_source="gradient of continuous geometric camera Z before byte encoding",views=registrations,
                 occluded_turret_fallbacks=fallback,geometry=SOURCES)
    write_json(RAW/"registration.json",receipt)
    write_json(PACK/"registration.json",receipt)
    preview.save(RAW/"extraction-preview.png")
    formatter=ROOT/"tools/format/node_modules/prettier/bin/prettier.cjs"
    if formatter.is_file():
        subprocess.run(["node",str(formatter),"--write",*map(str,PACK.glob("*.json"))],
                       check=True,capture_output=True)
    return materials


def nonvisual(entity):
    value=copy.deepcopy(entity)
    value.pop("render",None)
    for s in value.get("systems",{}).values():
        for k in ("marker","visual","scale_permille","flip_y"):
            s.pop(k,None)
    return value


def audit_saved():
    """Verify the exported files, rather than just the in-memory masks."""
    receipt=json.loads((RAW/"registration.json").read_text())
    pixels=0
    for view,reg in zip(SOURCES,receipt["views"]):
        source=np.asarray(Image.open(ROOT/"docs/art"/view["file"]).convert("RGB"))
        assembled=np.array(Image.open(RAW/("hull-"+view["name"]+".png")))
        occupied=assembled[:,:,3]>0
        for part,p in reg["parts"].items():
            if part=="hull" or not p["pixels"]:
                continue
            rgba=np.asarray(Image.open(RAW/(part+"-"+view["name"]+".png")))
            y,x=np.nonzero(rgba[:,:,3])
            ox=x-p["source_to_frame"][0]
            oy=y-p["source_to_frame"][1]
            assert not occupied[oy,ox].any(),"Overlapping exported parts"
            assembled[oy,ox]=rgba[y,x]
            occupied[oy,ox]=True
        expected=coverage(source,view)
        assert np.array_equal(occupied,expected),"Exported coverage differs"
        assert np.array_equal(assembled[:,:,:3][expected],source[expected]),"Exported RGB differs"
        pixels+=int(expected.sum())
    for part in ("hull",)+PARTS:
        stem="knuckleduster-"+part
        material=json.loads((RAW/(stem+".material.json")).read_text())
        size=Image.open(RAW/(stem+".png")).size
        assert sorted(f["azimuth"] for f in material["frames"])==list(range(0,360,45))
        for suffix in ("-depth","-normal"):
            assert Image.open(RAW/(stem+suffix+".png")).size==size
        for f in material["frames"]:
            x,y,w,h=f["rect"]
            assert 0<=x and 0<=y and x+w<=size[0] and y+h<=size[1]
            assert 0<=f["pivot"][0]<w and 0<=f["pivot"][1]<h
            for marker in f["attachments"].values():
                assert 0<=marker["x"]<w and 0<=marker["y"]<h and math.isfinite(marker["depth_offset"])
    write_json(RAW/"verification.json",dict(retained_source_pixels=pixels,visible_rgb_identical=True,
               exported_reassembly_exact=True,sets=6,bearings_per_set=8,map_dimensions_valid=True,
               marker_bounds_valid=True,depth_uses_geometry_not_luminance=True))
    shutil.copyfile(RAW/"verification.json",PACK/"verification.json")
    print("Verified exact source RGB reassembly:",pixels,"pixels; six eight-bearing sets.")


def install(materials):
    path=ROOT/"content/base/definitions.json"
    before=path.read_text(encoding="utf-8")
    data=json.loads(before)
    cruiser=next(e for e in data["entities"] if e["id"]=="base.cruiser")
    previous=nonvisual(cruiser)
    old_side=next(f for f in cruiser["render"]["sprite"]["frames"] if f["azimuth"]==90)
    scale=round(cruiser["render"]["scale"]*old_side["rect"][2]/old_side["rect"][3]*1024/1496,6)
    # Preserve apparent side-on length in world units; collision/gameplay fit
    # remains unchanged. All new parts share unresampled source pixel scale.
    if "knuckleduster" in cruiser["render"]["sprite"]["albedo"]:
        scale=cruiser["render"]["scale"]
    cruiser["render"].update(mode="eight",scale=scale,sprite=materials["hull"])
    # Four small nozzle-local plumes replace the old two broad effects. In
    # bow views their inferred sockets and full plume footprint stay behind
    # the opaque knuckles rather than leaking around the silhouette.
    cruiser["render"]["engines"]["scale"]=.4
    changed={cruiser["id"]:cruiser}
    added=[]
    for p in PARTS:
        eid="base.knuckleduster-"+p
        visual=dict(id=eid,render=dict(mode="eight",scale=1,sprite=materials[p]),collision=dict(shape="circle",radius_mm=1,mass_grams=0),systems={})
        existing=next((e for e in data["entities"] if e["id"]==eid),None)
        if existing is None:
            data["entities"].append(visual)
            added.append(visual)
        else:
            existing.update(visual)
            changed[eid]=existing
        cruiser["systems"][p].update(visual=eid,marker=SOCKETS[p],scale_permille=1000,flip_y=False)
    assert nonvisual(cruiser)==previous,"Changed simulation fit"
    # Replace only targeted top-level objects. Preserve unrelated in-progress
    # edits in this shared authored document, including original whitespace.
    decoder=json.JSONDecoder()
    start=before.index('[',before.index('"entities"'))+1
    spans=[]
    while True:
        while before[start].isspace() or before[start]==',':
            start+=1
        if before[start]==']':
            break
        e,n=decoder.raw_decode(before[start:])
        spans.append((start,start+n,e['id']))
        start+=n
    def formatted(entity):
        result=subprocess.run(["node",str(ROOT/"tools/format/node_modules/prettier/bin/prettier.cjs"),
                               "--parser","json","--use-tabs","--tab-width","4","--print-width","112"],
                              input=json.dumps(entity),text=True,capture_output=True,check=True)
        return result.stdout.strip().replace("\n","\n\t\t")
    after=before
    for start,end,eid in reversed(spans):
        if eid in changed:
            after=after[:start]+formatted(changed[eid])+after[end:]
    start=after.index('[',after.index('"entities"'))+1
    while True:
        while after[start].isspace() or after[start]==',':
            start+=1
        if after[start]==']':
            break
        _,n=decoder.raw_decode(after[start:])
        start+=n
    if added:
        after=after[:start].rstrip()+",\n\t\t"+",\n\t\t".join(formatted(e) for e in added)+"\n\t"+after[start:]
    assert json.loads(after)==data
    assert path.read_text(encoding="utf-8")==before,"Concurrent definitions change; rerun install"
    write_json(RAW/"installation.json",dict(simulation_unchanged=True,scale=scale,main_turrets=5,exhaust_sockets=4,bearings=8))
    # Keep the exact task-only patch for staged integration without absorbing
    # the user's existing changes to definitions.json.
    patch="".join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile="a/content/base/definitions.json",tofile="b/content/base/definitions.json"))
    (RAW/"definitions-install.patch").write_text(patch,encoding="utf-8")
    path.write_text(after,encoding="utf-8")
    print("Installed Knuckleduster; non-presentation Cruiser data unchanged.")


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install",action="store_true")
    args=parser.parse_args()
    materials=prepare()
    audit_saved()
    if args.install:
        install(materials)
    print("Saved raw sprites, maps, sidecars, registration and preview in",RAW)
