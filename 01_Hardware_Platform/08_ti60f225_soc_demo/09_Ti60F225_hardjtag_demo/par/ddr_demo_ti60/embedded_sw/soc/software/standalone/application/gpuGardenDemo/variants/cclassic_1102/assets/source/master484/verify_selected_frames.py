#!/usr/bin/env python3
"""Replay manifest-described crop/palette/resize operations independently."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image


def digest(data):
    return hashlib.sha256(data).hexdigest()


def pixels(image):
    return list(image.get_flattened_data()) if hasattr(image,"get_flattened_data") else list(image.getdata())


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--prepared",type=Path,default=Path(__file__).resolve().parent/"prepared")
    ap.add_argument("--source-dir",type=Path,default=Path(__file__).resolve().parent)
    args=ap.parse_args()
    manifest=json.loads((args.prepared/"manifest.json").read_text(encoding="utf-8"))
    source={}
    for name,record in manifest["source_files"].items():
        data=(args.source_dir/name).read_bytes()
        assert digest(data)==record["sha256"],name
        source[name]=Image.open(args.source_dir/name).convert("RGBA")
    checks=[]
    for record in manifest["sprites"]:
        name=record["name"]
        crop=source[record["source_file"]].crop(record["source_crop_xyxy_exclusive"])
        assert digest(crop.tobytes())==record["source_crop_rgba_sha256"],name
        palette={tuple(entry["source_rgb"]):tuple(entry["target_rgb"])
                 for entry in record["palette_substitution"]}
        referenced=set()
        for filename,box in record["palette_reference_crops"]:
            referenced.update(p[:3] for p in pixels(source[filename].crop(box)))
        assert set(palette.values())<=referenced,name
        converted=[]
        for pixel in pixels(crop):
            rgb=pixel[:3]
            converted.append((0,0,0,0) if rgb==(0,0,0)
                             else (*palette.get(rgb,rgb),255))
        crop.putdata(converted)
        transform=record["resize"]
        resized=crop.resize(tuple(transform["size"]),Image.Resampling.NEAREST)
        expected=Image.new("RGBA",(record["width"],record["height"]))
        expected.paste(resized,tuple(transform["paste_xy"]))
        file=args.prepared/record["png"]
        actual=Image.open(file).convert("RGBA")
        assert digest(file.read_bytes())==record["png_sha256"],name
        assert actual.size==expected.size,name
        assert actual.tobytes()==expected.tobytes(),name
        assert digest(actual.tobytes())==record["rgba_sha256"],name
        opaque=sum(p[3]!=0 for p in pixels(actual))
        assert opaque==record["opaque_pixels"] and opaque>0,name
        assert all(p[3] in (0,255) for p in pixels(actual)),name
        if name.startswith(("DART_","DIAMOND_")):
            assert any(p==(255,255,255,255) for p in pixels(actual)),name
        if name.startswith("EXPLOSION"):
            assert not any(p[:3]==(163,73,164) for p in pixels(actual)),name
        checks.append({"name":name,"original_id":record["original_id"],
                       "exact_source_replay":True,"binary_alpha":True,
                       "width":actual.width,"height":actual.height,"opaque_pixels":opaque})
    assert len(checks)==88 and len({r["original_id"] for r in checks})==88
    report={"result":"PASS","sprite_count":len(checks),
            "source_replay_matches":len(checks),
            "rgb565_slot_bytes":sum(r["width"]*r["height"]*2 for r in checks),
            "extra_atlas_bytes":0,"checks":checks}
    (args.prepared/"validation.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({k:v for k,v in report.items() if k!="checks"}))


if __name__=="__main__":
    main()
