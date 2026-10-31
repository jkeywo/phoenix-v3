"""Extract approved Switchblade art; bake geometry-based Z and matching normals.

Explicit asset preparation, not a build/test dependency. Pillow/NumPy/SciPy.
Albedo RGB is copied without resampling or repainting. Depth uses registered
planes, cabin volumes and engine cylinders, never RGB, luminance or shadows.
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
RAW = ROOT / "raw/switchblade"
PACK = ROOT / "content/base/assets/switchblade"
PREFIX = "phoenix-base:assets/switchblade/"
SCALE = 18.0
SPAN = 2.0

# Registration is in unscaled source pixels. F is bow-positive, P port-positive;
# camera-facing Z = F*cos(azimuth) + P*sin(azimuth), in visible-height units.
# End-on illustrations are crops of the approved paired elevation sheet.
VIEWS = [
    dict(name="bow", angle=0, source="switchblade-front-rear-v1.png", crop=[250, 0, 620, 955],
         pivot=[448, 700], top=[448, 285], bottom=[448, 840],
         top_poly=[(376,235),(496,235),(505,277),(483,298),(402,298),(377,278)],
         bottom_poly=[(392,826),(503,826),(504,879),(484,904),(406,904),(389,877)],
         top_muzzle=[448,263], bottom_muzzle=[448,850],
         engines=[[448,425,111,104],[448,695,86,82]], nozzles=[[448,425],[448,695]],
         cabin=[(393,637),(492,637),(508,813),(387,813)], missiles=[(426,484),(470,484),(470,610),(426,610)],
         holes=[], cutoff=956),
    dict(name="bow_port", angle=45, source="switchblade-front-side-45-v2.png", crop=None,
         pivot=[720,825], top=[600,345], bottom=[530,1045],
         top_poly=[(474,282),(521,282),(541,287),(565,284),(635,288),(671,305),(675,340),(651,361),(564,361),(535,349),(529,335),(494,328),(475,322)],
         bottom_poly=[(414,1005),(455,1004),(467,1009),(479,1000),(555,1000),(598,1016),(605,1055),(576,1084),(509,1082),(486,1063),(472,1051),(433,1047),(414,1039)],
         top_muzzle=[488,307], bottom_muzzle=[431,1031],
         engines=[[918,506,133,119],[918,890,130,98]], nozzles=[[1029,510],[1027,894]],
         cabin=[(163,908),(334,731),(503,702),(617,814),(617,1001),(207,1001)],
         missiles=[(310,482),(579,495),(579,716),(310,710)],
         holes=[], cutoff=1164),
    dict(name="port", angle=90, source="switchblade-side-elevation-v3.png", crop=None,
         pivot=[733,825], top=[610,345], bottom=[480,974],
         top_poly=[(467,303),(525,303),(532,294),(561,290),(577,284),(641,284),(669,298),(670,338),(648,354),(563,353),(548,340),(532,337),(523,330),(468,330)],
         bottom_poly=[(362,980),(412,980),(424,977),(435,965),(451,959),(519,960),(548,974),(550,1012),(525,1030),(456,1033),(439,1020),(426,1009),(412,1007),(362,1007)],
         top_muzzle=[477,318], bottom_muzzle=[370,993],
         engines=[[1081,452,135,96],[1081,826,132,94]], nozzles=[[1204,453],[1204,825]],
         cabin=[(99,904),(376,754),(513,748),(589,809),(589,959),(143,959)],
         missiles=[(275,553),(590,553),(590,717),(275,717)],
         holes=[[967,605],[1010,628],[971,665]], cutoff=1159),
    dict(name="stern_port", angle=135, source="switchblade-rear-side-45-v2.png", crop=None,
         pivot=[664,820], top=[482,374], bottom=[401,934],
         top_poly=[(389,352),(422,350),(433,336),(463,330),(500,333),(529,346),(530,374),(506,395),(453,395),(437,383),(390,380)],
         bottom_poly=[(291,902),(325,901),(337,899),(351,889),(389,889),(423,900),(445,912),(445,940),(419,957),(365,955),(344,943),(333,934),(294,928)],
         top_muzzle=[396,367], bottom_muzzle=[299,913],
         engines=[[875,510,166,170],[874,947,167,139]], nozzles=[[948,512],[948,950]],
         cabin=[(282,792),(347,732),(416,733),(462,782),(460,916),(335,896)],
         missiles=[(342,574),(468,574),(468,715),(342,715)],
         holes=[[772,749]], cutoff=1164),
    dict(name="stern", angle=180, source="switchblade-front-rear-v1.png", crop=[900,0,1290,955],
         pivot=[1092,700], top=[1092,285], bottom=[1092,840],
         top_poly=[(1025,239),(1141,239),(1150,276),(1120,301),(1061,301),(1030,281)],
         bottom_poly=[(1059,811),(1123,811),(1130,867),(1113,893),(1070,893),(1057,864)],
         top_muzzle=[1092,262], bottom_muzzle=[1092,849],
         engines=[[1092,432,127,113],[1092,702,94,92]], nozzles=[[1092,432],[1092,702]],
         cabin=None, missiles=None, holes=[], cutoff=956),
]


def write_json(path, value):
    path.write_text(json.dumps(value, indent="\t") + "\n", encoding="utf-8")


def polygon(shape, points):
    im = Image.new("1", (shape[1], shape[0]))
    ImageDraw.Draw(im).polygon([tuple(p) for p in points], fill=1)
    return np.asarray(im, dtype=bool)


def coverage(rgb, view):
    # Remove border-connected paper, not ivory armour. Geometric aperture seeds
    # remove only registered open spaces enclosed by the physical frame.
    r, g, b = (rgb[:, :, i].astype(np.int16) for i in range(3))
    pale = (r >= 205) & (g >= 185) & (b >= 140) & (r >= g-6) & (g >= b-6) & (r-b <= 110)
    seeds = np.zeros(pale.shape, bool)
    seeds[0], seeds[-1] = pale[0], pale[-1]
    seeds[:, 0], seeds[:, -1] = pale[:, 0], pale[:, -1]
    for x, y in view["holes"]:
        if pale[y, x]:
            seeds[y, x] = True
    paper = ndimage.binary_propagation(seeds, mask=pale)
    solid = ~paper
    solid[view["cutoff"]:] = False
    if view["crop"]:
        x0, y0, x1, y1 = view["crop"]
        keep = np.zeros(solid.shape, bool)
        keep[y0:y1, x0:x1] = True
        solid &= keep
    labels, _ = ndimage.label(solid)
    good = np.bincount(labels.ravel()) >= 14
    good[0] = False
    return good[labels]


def bounds(mask):
    y, x = np.nonzero(mask)
    return [int(x.min()), int(y.min()), int(x.max())+1, int(y.max())+1]


def geometric_z(solid, view, visible_height):
    """Registered visible-surface approximation, no colour input whatsoever."""
    yy, xx = np.mgrid[:solid.shape[0], :solid.shape[1]].astype(float)
    a = math.radians(view["angle"])
    sine, cosine = math.sin(a), math.cos(a)
    slope = -cosine / max(abs(sine), .5) if abs(sine) > .1 else 0
    z = slope * (xx-view["pivot"][0])/visible_height
    edge = np.minimum(ndimage.distance_transform_edt(solid), 6)
    z += np.sqrt(np.maximum(0, 36-(6-edge)**2))/visible_height + .035*abs(sine)
    if abs(sine) < .1:
        # The blade's leading/trailing edge, not its broad painted side.
        z[:] = .08 if cosine > 0 else .02
    for key, forward, width in (("cabin", .28, .085), ("missiles", .22, .055)):
        if view[key]:
            mask = polygon(solid.shape, view[key])
            cx = sum(p[0] for p in view[key])/len(view[key])
            plane = forward*cosine + width*abs(sine) + slope*(xx-cx)/visible_height
            bevel = ndimage.distance_transform_edt(mask)
            plane -= np.clip(7-bevel, 0, 7)/visible_height
            z[mask] = plane[mask]
    # Separate stern cylinders. Bright exhaust cores are RECESSED relative to
    # the nozzle lip; their emissive-looking RGB is never sampled here.
    for index, (cx, cy, rx, ry) in enumerate(view["engines"]):
        u, v = (xx-cx)/rx, (yy-cy)/ry
        mask = (u*u+v*v <= 1) if abs(sine) < .1 else ((abs(u)<=1) & (abs(v)<=1))
        centre = -.28*cosine
        casing = centre + .105*np.sqrt(np.maximum(0, 1-v*v))
        if abs(sine) > .1:
            casing += slope*(xx-cx)/visible_height
        if cosine < -.1:
            nx, ny = view["nozzles"][index]
            nrx = rx * (1 if abs(sine)<.1 else .57)
            rr = ((xx-nx)/nrx)**2 + ((yy-ny)/(ry*.85))**2
            # continuous recessed bowl inside the raised rim
            casing -= .075*np.clip(1-rr, 0, 1)**2
        z[mask] = casing[mask]
    return z


def encode(z, solid, height, span=SPAN):
    # Nearest-valid extension prevents alpha gaps from becoming false pits.
    nearest = ndimage.distance_transform_edt(~solid, return_distances=False, return_indices=True)
    continuous = ndimage.gaussian_filter(z[tuple(nearest)], 1.6)
    dy, dx = np.gradient(continuous)
    n = np.stack((-dx*height, dy*height, np.ones_like(z)), axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    # Five effective bits bound error and keep the complete pack transferable.
    normals = np.rint((n*.5+.5)*31)*255/31
    normals = np.rint(normals).clip(0,255).astype(np.uint8)
    normals[~solid] = [128,128,255]
    encoded = .5 + z/span
    assert encoded[solid].min() > 0 and encoded[solid].max() < 1
    depth = np.rint(encoded*255).clip(0,255).astype(np.uint8)
    return depth, normals


def turret_z(mask, pivot, view, frame_height):
    yy, xx = np.mgrid[:mask.shape[0], :mask.shape[1]].astype(float)
    a = math.radians(view["angle"])
    slope = -math.cos(a)/max(abs(math.sin(a)), .5) if abs(math.sin(a))>.1 else 0
    t = np.minimum(ndimage.distance_transform_edt(mask), 14)
    z = slope*(xx-pivot[0])/frame_height + np.sqrt(np.maximum(0,196-(14-t)**2))/frame_height
    return z-z[pivot[1],pivot[0]]


def canonical(stem, frames):
    result = dict(albedo=PREFIX+stem+".png", normal=PREFIX+stem+"-normal.png", depth=PREFIX+stem+"-depth.png", frames=[])
    for f in frames:
        result["frames"].append(dict(name=f["name"], rect=f["rect"], pivot=f["pivot"], azimuth=f["camera_azimuth_degrees"],
                                     mirror_x=f["mirror_x"], depth_scale=f["depth_scale"], depth_offset=f["depth_offset"],
                                     attachments={k:dict(x=p[0],y=p[1],layer=p[2],depth_offset=f["attachment_depth_offsets"].get(k,0)) for k,p in f["markers"].items()}))
    if stem.endswith("hull"):
        result.update(radar_icon="ship",radar_sprite="phoenix-base:assets/radar/ship.png")
    return result


def prepare():
    RAW.mkdir(parents=True,exist_ok=True)
    PACK.mkdir(parents=True,exist_ok=True)
    prepared = {part:[] for part in ("hull","top-turret","bottom-turret")}
    receipts=[]
    for view in VIEWS:
        # Raw is the durable local source set. On first preparation import the
        # approved concept files; subsequent bakes need only raw/switchblade.
        source = RAW / view["source"]
        if not source.exists():
            source = ROOT / "docs/art" / view["source"]
            shutil.copyfile(source,RAW/view["source"])
        rgb=np.asarray(Image.open(source).convert("RGB"))
        solid=coverage(rgb,view)
        box=bounds(solid)
        visible_h=box[3]-box[1]
        margin=max(8,round(visible_h*.025))
        hull_h=visible_h+2*margin
        masks={}
        for kind in ("top","bottom"):
            masks[kind+"-turret"]=polygon(solid.shape,view[kind+"_poly"]) & solid
        assert not np.any(masks["top-turret"] & masks["bottom-turret"])
        masks["hull"]=solid & ~masks["top-turret"] & ~masks["bottom-turret"]
        z=geometric_z(solid,view,visible_h)
        # Keep depth underneath extracted mounts, because the CPU samples it.
        depth,normal=encode(z,solid,visible_h)
        mount_offsets={}
        frame_receipt=dict(name=view["name"],source=view["source"],source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                           source_size=[rgb.shape[1],rgb.shape[0]],visible_bounds=box,rgb_identity=True,parts={})
        for part in ("hull","top-turret","bottom-turret"):
            mask=masks[part]
            rgba=np.dstack((rgb,mask.astype(np.uint8)*255))
            if part=="hull":
                crop=box
                width=box[2]-box[0]+2*margin
                height=hull_h
                shift=[margin-box[0],margin-box[1]]
                pivot=[view["pivot"][k]+shift[k] for k in (0,1)]
                local_z=z
                d,n=depth,normal
            else:
                crop=bounds(mask)
                # Fixed 384px turret cell and common mount pivot allow each
                # independently selected turret bearing to traverse in place.
                width,height=384,256
                pivot=[240,128]
                kind=part.split("-")[0]
                shift=[pivot[k]-view[kind][k] for k in (0,1)]
                # Turret Z uses source pixels / HULL rectangle height, while
                # renderer calibrates its own cell. Convert to turret units.
                local_z=turret_z(mask,view[kind],view,height)
                d,n=encode(local_z,mask,height,span=1.8)
            local=Image.new("RGBA",(width,height))
            ld=Image.new("L",local.size,128)
            ln=Image.new("RGB",local.size,(128,128,255))
            dest=[crop[k]+shift[k] for k in (0,1)]
            assert min(dest)>=0 and dest[0]+crop[2]-crop[0]<=width and dest[1]+crop[3]-crop[1]<=height,(view["name"],part,crop,dest)
            for target,array in ((local,rgba),(ld,d),(ln,n)):
                target.paste(Image.fromarray(array).crop(crop),dest)
            frame=dict(name=view["name"],rect=[0,0,width,height],pivot=pivot,camera_azimuth_degrees=view["angle"],mirror_x=False,
                       depth_scale=SPAN*visible_h/hull_h if part=="hull" else 1.8,depth_offset=0,markers={},attachment_depth_offsets={})
            if part=="hull":
                for kind,forward in (("top",.11),("bottom",.23)):
                    name="turret_forward" if kind=="top" else "turret_aft"
                    mx,my=view[kind]
                    target_z=(forward*math.cos(math.radians(view["angle"])))*visible_h/hull_h
                    sampled=(float(d[my,mx])/255-.5)*frame["depth_scale"]
                    # Layer is only a tiny draw-order bias. Actual occlusion
                    # comes from the calibrated physical depth at the mount.
                    layer=-1 if view["angle"]>90 else 1
                    frame["markers"][name]=[mx+shift[0],my+shift[1],layer]
                    frame["attachment_depth_offsets"][name]=round(target_z-sampled,8)
                    mount_offsets[name]=dict(source=[mx,my],camera_z=target_z)
                for i,(mx,my) in enumerate(view["nozzles"]):
                    name="engine_upper" if i==0 else "engine_lower"
                    target_z=-.39*math.cos(math.radians(view["angle"]))*visible_h/hull_h
                    sampled=(float(d[my,mx])/255-.5)*frame["depth_scale"]
                    frame["markers"][name]=[mx+shift[0],my+shift[1],1 if view["angle"]>=90 else -1]
                    frame["attachment_depth_offsets"][name]=round(target_z-sampled,8)
                # Small legacy auxiliaries occupy separate sockets, avoiding
                # two independently aimed gun sprites at each main mount.
                for name,point in (("aux_beam",[view["top"][0]+15,view["top"][1]+72]),
                                   ("aux_pdc",[view["bottom"][0]+45,view["bottom"][1]-100]),
                                   ("muzzle",[view["pivot"][0],view["pivot"][1]-130])):
                    frame["markers"][name]=[point[0]+shift[0],point[1]+shift[1],1]
                    frame["attachment_depth_offsets"][name]=.01
            else:
                kind=part.split("-")[0]
                mx,my=view[kind+"_muzzle"]
                frame["markers"]["muzzle"]=[mx+shift[0],my+shift[1],1]
                frame["attachment_depth_offsets"]["muzzle"]=.004
                # Neutralise 128/255 quantisation at the shared pivot.
                frame["depth_offset"]=round(-((float(ld.getpixel(tuple(pivot)))/255-.5)*1.8),8)
            stem="switchblade-"+part+"-"+view["name"]
            local.save(RAW/(stem+".png"),optimize=True)
            ld.save(RAW/(stem+"-depth.png"),optimize=True)
            ln.save(RAW/(stem+"-normal.png"),optimize=True)
            # Exact integer source/atlas registration, including all mounts.
            region=np.asarray(local)[dest[1]:dest[1]+crop[3]-crop[1],dest[0]:dest[0]+crop[2]-crop[0]]
            original=rgba[crop[1]:crop[3],crop[0]:crop[2]]
            assert np.array_equal(region,original)
            frame_receipt["parts"][part]=dict(source_bounds=crop,source_to_frame=shift,retained_pixels=int(mask.sum()),pivot=pivot)
            prepared[part].append(dict(frame=frame,albedo=local,depth=ld,normal=ln))
        frame_receipt["mounts"]=mount_offsets
        receipts.append(frame_receipt)
    materials={}
    for part,items in prepared.items():
        w=sum(p["albedo"].width+8 for p in items)
        h=max(p["albedo"].height for p in items)
        assert w<=8192 and w*h<=16777216
        maps=dict(albedo=Image.new("RGBA",(w,h)),depth=Image.new("L",(w,h),128),normal=Image.new("RGB",(w,h),(128,128,255)))
        x=0
        for item in items:
            item["frame"]["rect"][0]=x
            for channel,atlas in maps.items():
                atlas.paste(item[channel],(x,0))
            x+=item["albedo"].width+8
        frames=[p["frame"] for p in items]
        for index,name,angle in ((3,"stern_starboard",225),(2,"starboard",270),(1,"bow_starboard",315)):
            frame=copy.deepcopy(frames[index])
            frame.update(name=name,camera_azimuth_degrees=angle,mirror_x=True)
            frames.append(frame)
        stem="switchblade-"+part
        for channel,suffix in (("albedo",""),("normal","-normal"),("depth","-depth")):
            maps[channel].save(RAW/(stem+suffix+".png"),optimize=True)
        sidecar=dict(schema_version=1,id=stem,image_size=[w,h],coordinate_system="frame_pixels_top_left_y_down",
                     files=dict(sprite=stem+".png",normal=stem+"-normal.png",depth=stem+"-depth.png"),
                     billboard_mode="eight",render_scale=SCALE if part=="hull" else 1,emissive=False,
                     depth_encoding=dict(channel="r",color_space="linear",white="near",quality="registered_geometry_not_shading"),
                     normal_encoding=dict(space="view",x="right",y="up",z="toward_viewer",quality="gradient_of_continuous_geometric_Z"),
                     sprite_sheet=dict(kind="directions",frames=frames))
        write_json(RAW/(stem+".sprite.json"),sidecar)
        materials[part]=canonical(stem,frames)
        write_json(RAW/(stem+".material.json"),materials[part])
        for suffix in (".png","-depth.png","-normal.png",".sprite.json",".material.json"):
            shutil.copyfile(RAW/(stem+suffix),PACK/(stem+suffix))
    receipt=dict(rgb_preserved=True,resampling=False,depth_source="registered geometric volumes and alpha; no RGB, luminance, shadow inputs",
                 normal_source="continuous geometric Z gradient before 8-bit depth quantisation",geometry=VIEWS,frames=receipts)
    write_json(RAW/"registration.json",receipt)
    write_json(PACK/"registration.json",receipt)
    preview=Image.new("RGB",(5*360,3*390),"#252a35")
    for i,item in enumerate(prepared["hull"]):
        for row,channel in enumerate(("albedo","depth","normal")):
            im=item[channel].convert("RGBA")
            im.putalpha(item["albedo"].getchannel("A"))
            im.thumbnail((350,350))
            bg=Image.new("RGBA",im.size,"#252a35")
            bg.alpha_composite(im)
            preview.paste(bg.convert("RGB"),(i*360+(360-im.width)//2,row*390+30))
            ImageDraw.Draw(preview).text((i*360+8,row*390+8),item["frame"]["name"]+" / "+channel,fill="white")
    preview.save(RAW/"materials-preview.png")
    return materials


def without_presentation(entity):
    entity=copy.deepcopy(entity)
    entity.pop("render",None)
    for system in entity.get("systems",{}).values():
        for key in ("visual","marker","scale_permille","flip_y"):
            system.pop(key,None)
    return entity


def install(materials):
    path=ROOT/"content/base/definitions.json"
    before=path.read_text(encoding="utf-8")
    data=json.loads(before)
    raider=next(e for e in data["entities"] if e["id"]=="base.pirate-raider")
    sim=without_presentation(raider)
    raider["render"].update(mode="eight",scale=SCALE,sprite=materials["hull"])
    # Reuse the established dynamic exhaust effect, with two registered nozzles
    # and one unchanged propulsion System.
    effect=copy.deepcopy(next(e for e in data["entities"] if e["id"]=="base.cruiser")["render"]["engines"])
    effect.update(scale=.18,range=22,power=.9)
    raider["render"]["engines"]=effect
    changed={raider["id"]:raider}
    legacy=next(e for e in data["entities"] if e["id"]=="base.pirate-turret")
    legacy["render"].update(mode="eight",scale=1,sprite=materials["top-turret"])
    changed[legacy["id"]]=legacy
    added=[]
    for kind in ("top","bottom"):
        eid="base.pirate-"+kind+"-turret"
        visual=dict(id=eid,render=dict(mode="eight",scale=1,sprite=materials[kind+"-turret"]),
                    collision=dict(shape="circle",radius_mm=1,mass_grams=0),systems={})
        existing=next((e for e in data["entities"] if e["id"]==eid),None)
        if existing:
            existing.update(visual)
            changed[eid]=existing
        else:
            data["entities"].append(visual)
            added.append(visual)
        raider["systems"][kind].update(visual=eid,scale_permille=1000,flip_y=False)
    for sid,marker in (("beam-turret","aux_beam"),("pdc-turret","aux_pdc")):
        raider["systems"][sid].update(marker=marker,scale_permille=70)
    assert without_presentation(raider)==sim,"Changed simulation data"
    # Replace only targeted entity objects, preserving concurrent unrelated
    # work and the rest of this large authored document byte for byte.
    baseline=RAW/"definitions-before-install.txt"
    if not baseline.exists():
        baseline.write_text(before,encoding="utf-8")
    after=before
    def formatted(entity):
        result=subprocess.run(["node",str(ROOT/"tools/format/node_modules/prettier/bin/prettier.cjs"),
                               "--parser","json","--use-tabs","--tab-width","4","--print-width","112"],
                              input=json.dumps(entity),text=True,capture_output=True,check=True)
        return result.stdout.strip().replace("\n","\n\t\t")
    decoder=json.JSONDecoder()
    # IDs are not necessarily the first member of an entity object. Decode the
    # actual top-level array elements rather than searching backwards for '{'.
    start=after.index('[',after.index('"entities"'))+1
    spans=[]
    while True:
        while after[start].isspace() or after[start]==',':
            start+=1
        if after[start]==']':
            break
        entity,length=decoder.raw_decode(after[start:])
        spans.append((start,start+length,entity['id']))
        start+=length
    for start,end,eid in reversed(spans):
        if eid in changed:
            after=after[:start]+formatted(changed[eid])+after[end:]
    # New visuals are placed next to the raider, never changing array order of
    # existing entities or any equipment inheritance.
    # Insert before the array's closing bracket. Offsets before the first edit
    # are no longer valid after replacements, so locate the array structurally.
    start=after.index('[',after.index('"entities"'))+1
    while True:
        while after[start].isspace() or after[start]==',':
            start+=1
        if after[start]==']':
            break
        _,length=decoder.raw_decode(after[start:])
        start+=length
    if added:
        preceding=after[:start].rstrip()
        after=preceding+",\n\t\t"+",\n\t\t".join(formatted(e) for e in added)+"\n\t"+after[start:]
    # Validate before writing to the shared source document.
    assert json.loads(after)==data
    if path.read_text(encoding="utf-8")!=before:
        raise RuntimeError("Definitions changed during bake; rerun install")
    path.write_text(after,encoding="utf-8")
    patch="".join(difflib.unified_diff(baseline.read_text(encoding="utf-8").splitlines(True),after.splitlines(True),fromfile="a/content/base/definitions.json",tofile="b/content/base/definitions.json"))
    (RAW/"definitions-install.patch").write_text(patch,encoding="utf-8")
    write_json(RAW/"installation.json",dict(simulation_unchanged=True,source_views=5,bearings=8,primary_turrets=2,exhausts=2,render_scale=SCALE))
    # Preserve retired art locally, but do not package unused multi-megabyte
    # atlases alongside their replacements (the content transfer cap is fixed).
    backup=RAW/"retired-pirate-art"
    backup.mkdir(exist_ok=True)
    for stem in ("pirate-hull","pirate-turret"):
        for suffix in (".png","-normal.png","-depth.png",".material.json",".sprite.json"):
            retired=ROOT/"content/base/assets/combat"/(stem+suffix)
            if retired.exists():
                shutil.copyfile(retired,backup/retired.name)
                retired.unlink()
    print("Installed Switchblade; all non-presentation raider data unchanged.")


if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--install",action="store_true")
    args=parser.parse_args()
    baked=prepare()
    if args.install:
        install(baked)
    print("Saved original art, per-view sprites/maps, atlases and registration in",RAW)
