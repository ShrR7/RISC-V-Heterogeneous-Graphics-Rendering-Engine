#!/usr/bin/env python3
"""Bake a bounded Garden 1.0.9 RGB565 atlas; Python 3 + Pillow, no Allegro.

SPDX-License-Identifier: GPL-3.0-or-later
BMP extractions and frozen component poses follow upstream display_init.c and
display.c. Geometry is rasterized by Pillow, so polygon/circle edge pixels may
differ from Allegro. Translucent bullets are baked against stage-1 BACK1; their
outside is key-transparent. HUD font is a new 5x7 bitmap alphabet, not the
upstream garden.dat font. No game textures are drawn or invented by this tool.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import shutil
import struct
import zlib
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
KEY = 0xF81F
# The original 160 KiB budget left most of the verified DDR asset window unused.
# Reserve its final 8 KiB for the Alpha panel and diagnostics, as main.c does.
MAX_BYTES = 0x3C000 - 8192
CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:/-._%"
assert len(CHARS) == 43

# palette.c base_colours[BASECOL_L1_SPRING], in Allegro 6-bit channels.
SPRING = [(45,45,25),(35,35,18),(45,25,35),(35,18,25),
          (11,16,12),(15,19,16),(16,23,18),(19,26,19),(43,33,43)]
BASE = [(0,0,0),(10,10,10),(50,50,50),(35,35,35),(22,22,22)] + SPRING
INNER = [(0,0,10),(10,10,20),(10,5,0),(6,6,0),(12,0,0),
         (7,0,7),(0,0,12),(0,3,3),(0,12,0)]
OUTER = [(10,20,30),(15,25,35),(30,13,5),(25,25,5),(30,20,10),
         (20,13,20),(10,20,30),(2,12,12),(11,20,11)]


def rgb6(v):
    return tuple(max(0, min(63, int(x))) * 255 // 63 for x in v)


def palette_color(index):
    if index < 14:
        return rgb6(BASE[index])
    if index == 255:
        return (255,255,255)
    if 14 <= index <= 247:
        # create_outer/inner_bullet_colour: indices 14+26*c and 27+26*c.
        colour, sub = divmod(index - 14, 26)
        if colour <= 8:
            inside = sub >= 13
            base = BASE[(sub % 13) + 1]
            bias = INNER[colour] if inside else OUTER[colour]
            return rgb6([int(base[k] / 1.5 + bias[k] * 1.5) if inside
                         else base[k] // 4 + bias[k] * 4 for k in range(3)])
    raise ValueError(f"Unsupported source palette index {index}")


def trans_colours(colour):
    # Freeze draw_trans_rle_sprite against original stage-1 COL_BACK1.
    bg = SPRING[4]
    return (rgb6([int(bg[k] / 1.5 + INNER[colour][k] * 1.5) for k in range(3)]),
            rgb6([bg[k] // 4 + OUTER[colour][k] * 4 for k in range(3)]))


def load_indexed(path):
    image = Image.open(path)
    if image.mode != "P":
        raise ValueError(f"Expected indexed Garden BMP: {path}")
    pixels = image.tobytes()
    rgba = []
    for pixel in pixels:
        pixel = {116:1, 61:8}.get(pixel, pixel)  # fix_outline().
        rgba.append((0,0,0,0) if pixel == 0 else (*palette_color(pixel),255))
    out = Image.new("RGBA", image.size)
    out.putdata(rgba)
    return out


def piece(sheet, rect, flip=""):
    x,y,w,h = rect
    image = sheet.crop((x,y,x+w,y+h))
    if "h" in flip:
        image = image.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    if "v" in flip:
        image = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    return image


def trim(image, anchor, scale=None):
    bounds = image.getchannel("A").getbbox()
    if bounds is None:
        raise ValueError("Empty sprite")
    ax,ay = anchor[0]-bounds[0], anchor[1]-bounds[1]
    image = image.crop(bounds)
    if scale is not None:
        old = image.size
        size = (max(1,round(old[0]*scale)), max(1,round(old[1]*scale)))
        image = image.resize(size, Image.Resampling.NEAREST)
        ax,ay = round(ax*size[0]/old[0]), round(ay*size[1]/old[1])
    return image,(ax,ay)


def canvas(size=320):
    image=Image.new("RGBA",(size,size))
    return image,size//2,size//2


def stamp(dest, image, x,y):
    dest.alpha_composite(image,(int(x),int(y)))


def circle(draw,x,y,r,fill,border):
    draw.ellipse((x-r,y-r,x+r,y+r),fill=fill,outline=border)


def player(small):
    im,x,y=canvas()
    stamp(im,piece(small,(219,30,21,18)),x-10,y-7)
    stamp(im,piece(small,(242,32,10,8)),x-11,y-8)
    stamp(im,piece(small,(242,32,10,8),"h"),x+2,y-8)
    stamp(im,piece(small,(242,41,9,20)),x-10,y+2)
    stamp(im,piece(small,(242,41,9,20),"h"),x+2,y+2)
    return trim(im,(x,y))


def darter(small):
    im,x,y=canvas()
    stamp(im,piece(small,(219,1,27,28)),x-13,y-18)
    d=ImageDraw.Draw(im)
    circle(d,x,y-1,10,palette_color(4),palette_color(1))
    circle(d,x,y,7,palette_color(5),palette_color(1))
    circle(d,x+5,y,2,palette_color(5),palette_color(1))
    stamp(im,piece(small,(141,1,49,51)),x-24,y-19)
    return trim(im,(x,y))


def stopper(large):
    im,x,y=canvas()
    stamp(im,piece(large,(130,100,91,95)),x-45,y-49)
    stamp(im,piece(large,(1,1,93,146)),x-46,y-101)
    return trim(im,(x,y),0.70)


def boss(large):
    im,x,y=canvas()
    # display.c ENEMY_BOSS1, all extension variables=0, turret angles=0.
    stamps=[((976,12,155,95),"",-77,-47),
            ((888,122,58,65),"",-103,-32),((888,122,58,65),"h",46,-32),
            ((957,119,102,63),"",-101,-76),((957,119,102,63),"h",0,-76),
            ((957,119,102,63),"v",-101,13),((957,119,102,63),"hv",0,13),
            ((1069,118,69,31),"",-34,-47),((1069,118,69,31),"v",-34,17),
            ((1146,127,31,67),"",-48,-33),((1146,127,31,67),"h",18,-33)]
    for rect,flip,dx,dy in stamps:
        stamp(im,piece(large,rect,flip),x+dx,y+dy)
    d=ImageDraw.Draw(im)
    circle(d,x,y,19,palette_color(5),palette_color(1))
    for dx in (-71,71):
        circle(d,x+dx,y,16,palette_color(5),palette_color(1))
        for a in (-11,11):
            circle(d,x+dx+a,y,3,palette_color(5),palette_color(1))
    bb=im.getchannel("A").getbbox()
    return trim(im,(x,y),160/(bb[2]-bb[0]))


def shot():
    # display.c PBULLET_MGUN, level=0, frozen speckle at y=3,7,11.
    inner,outer=trans_colours(2)
    im=Image.new("RGBA",(5,19)); d=ImageDraw.Draw(im)
    d.line((2,1,2,17),fill=inner)
    d.line((1,1,1,17),fill=outer);d.line((3,1,3,17),fill=outer)
    d.point((2,0),fill=outer);d.point((2,18),fill=outer)
    for y in (4,8,12):
        d.point((1,y),fill=inner);d.point((3,y),fill=inner)
        d.point((0,y),fill=outer);d.point((4,y),fill=outer)
    return im,(2,1)


def shock(frame):
    # Active prepare_mshockwaves(), orange group i=0, samples j=0,6,12,19.
    rad1=30+(frame//2+1);rad2=1+2*(frame+1)
    inner,outer=trans_colours(2)
    im=Image.new("RGBA",(rad1*2+1,rad1*2+1));d=ImageDraw.Draw(im)
    circle(d,rad1,rad1,rad1,None if frame>=18 else inner,outer)
    if frame<18:
        circle(d,rad1,rad1,rad2,(0,0,0,0),outer)
    return trim(im,(rad1,rad1),0.5)


def circle_bullet(colour):
    # prepare_enemy_bullets(): RLE_circle[4][TCOL_Bn].
    inner,outer=trans_colours(colour)
    im=Image.new("RGBA",(10,10));d=ImageDraw.Draw(im)
    circle(d,5,5,4,inner,outer)
    return trim(im,(5,5))


def part(angle,length,cos=True):
    return int((math.cos if cos else math.sin)(2*math.pi*angle/1024)*length)


def polygon_bullet(shape,colour,rotation):
    # prepare_enemy_bullets(): angle=0..1023, polygon then coat_sprite().
    inner,outer=trans_colours(colour)
    angle=rotation*128
    def pt(a,n):return (25+part(a,n),25+part(a,n,False))
    if shape=="dart":
        pts=[pt(angle,23),pt(angle+128,11),(25,25),pt(angle-128,11)]
    else:
        pts=[pt(angle,15),pt(angle+256,9),pt(angle+512,15),pt(angle-256,9)]
    fill=Image.new("L",(50,50));ImageDraw.Draw(fill).polygon(pts,fill=255)
    # Four-neighbour coat_sprite, not a diagonal dilation / stylized outline.
    src=fill.load();im=Image.new("RGBA",fill.size);out=im.load()
    for x in range(50):
        for y in range(50):
            if src[x,y]:out[x,y]=(*inner,255)
            elif 0<x<49 and 0<y<49 and any(src[nx,ny] for nx,ny in
                  ((x-1,y),(x+1,y),(x,y-1),(x,y+1))):
                out[x,y]=(*outer,255)
    return trim(im,(25,25))


# New HUD typography: authored 5x7 masks, explicitly separate from garden.dat.
FONT_ROWS={
" ":[0,0,0,0,0,0,0],"A":[14,17,17,31,17,17,17],"B":[30,17,17,30,17,17,30],
"C":[14,17,16,16,16,17,14],"D":[30,17,17,17,17,17,30],"E":[31,16,16,30,16,16,31],
"F":[31,16,16,30,16,16,16],"G":[14,17,16,23,17,17,15],"H":[17,17,17,31,17,17,17],
"I":[14,4,4,4,4,4,14],"J":[7,2,2,2,18,18,12],"K":[17,18,20,24,20,18,17],
"L":[16,16,16,16,16,16,31],"M":[17,27,21,21,17,17,17],"N":[17,25,21,19,17,17,17],
"O":[14,17,17,17,17,17,14],"P":[30,17,17,30,16,16,16],"Q":[14,17,17,17,21,18,13],
"R":[30,17,17,30,20,18,17],"S":[15,16,16,14,1,1,30],"T":[31,4,4,4,4,4,4],
"U":[17,17,17,17,17,17,14],"V":[17,17,17,17,17,10,4],"W":[17,17,17,21,21,21,10],
"X":[17,17,10,4,10,17,17],"Y":[17,17,10,4,4,4,4],"Z":[31,1,2,4,8,16,31],
"0":[14,17,19,21,25,17,14],"1":[4,12,4,4,4,4,14],"2":[14,17,1,2,4,8,31],
"3":[30,1,1,14,1,1,30],"4":[2,6,10,18,31,2,2],"5":[31,16,16,30,1,1,30],
"6":[14,16,16,30,17,17,14],"7":[31,1,2,4,8,8,8],"8":[14,17,17,14,17,17,14],
"9":[14,17,17,15,1,1,14],":":[0,4,4,0,4,4,0],"/":[1,2,2,4,8,8,16],
"-":[0,0,0,31,0,0,0],".":[0,0,0,0,0,6,6],"_":[0,0,0,0,0,0,31],
"%":[17,2,4,4,8,16,17]}


def glyph(ch,cyan,scale=1):
    im=Image.new("RGBA",(8,8));p=im.load()
    rgb=(80,232,255) if cyan else (232,244,255)
    for y,row in enumerate(FONT_ROWS[ch]):
        for x in range(5):
            if row & (1<<(4-x)):p[x+1,y]=(*rgb,255)
    if scale!=1:im=im.resize((8*scale,8*scale),Image.Resampling.NEAREST)
    return im,(0,0)


def neon_image(image,role,colour=0):
    """Recolour source-derived shapes; dimensions and the key mask stay exact."""
    ramps={
        "player":((8,28,54),(32,124,184),(128,244,255)),
        "darter":((40,12,52),(164,48,156),(255,172,232)),
        "stopper":((40,22,12),(204,96,32),(255,224,132)),
        "boss":((32,16,64),(124,64,192),(216,180,255)),
        "shot":((8,36,48),(32,196,224),(204,255,255)),
        "effect":((40,12,40),(224,64,148),(255,240,192)),
    }
    bullet_ramps=[((48,8,32),(232,52,112),(255,180,216)),
                  ((8,32,56),(48,184,248),(168,244,255)),
                  ((8,40,40),(36,224,172),(168,255,208)),
                  ((48,32,8),(248,176,40),(255,244,160)),
                  ((32,12,56),(180,92,248),(244,188,255))]
    low,mid,high=bullet_ramps[colour%5] if role=="bullet" else ramps[role]
    pixels=[]; rgba=image.load()
    for index,(r,g,b,a) in enumerate(struct.iter_unpack("4B",image.tobytes())):
        if not a:
            pixels.append((0,0,0,0));continue
        lum=(r*3+g*6+b)//10
        if role=="bullet":
            # Original bullet rims are brighter than their centres; preserve
            # that contrast, so transparent projectiles remain easy to track.
            t=min(255,lum*2);start,end=(mid,high) if t>=128 else (low,mid)
            t=(t-128)*2 if t>=128 else t*2
        else:
            # Keep metallic interiors dark. A source-mask edge gets an emissive
            # rim, baked into existing pixels rather than a runtime glow pass.
            x,y=index%image.width,index//image.width
            edge=any(nx<0 or ny<0 or nx>=image.width or ny>=image.height or
                     rgba[nx,ny][3]==0 for nx,ny in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)))
            start,end=(mid,high) if edge or role in ("shot","effect") else (low,mid)
            if edge:t=220
            elif role in ("shot","effect"):t=min(255,lum)
            else:
                shade=192-y*64//max(1,image.height-1)
                t=min(255,lum*lum*shade//(255*160))
        rgb=tuple(start[k]+(end[k]-start[k])*t//255 for k in range(3))
        pixels.append((*rgb,a))
    out=Image.new("RGBA",image.size);out.putdata(pixels)
    assert out.getchannel("A").tobytes()==image.getchannel("A").tobytes()
    return out


def rgb565_bytes(image):
    out=bytearray();collisions=0
    rgba = image.tobytes()
    for i in range(0, len(rgba), 4):
        r,g,b,a = rgba[i:i+4]
        value=KEY if a==0 else ((r>>3)<<11)|((g>>2)<<5)|(b>>3)
        if a and value==KEY:
            value^=1;collisions+=1  # opaque magenta remains opaque.
        out+=struct.pack("<H",value)
    return bytes(out),collisions


def decode_rgb565(data, size):
    """Preview the actual uploaded 565 data, with the actual colour-key mask."""
    pixels=[]
    for (value,) in struct.iter_unpack("<H",data):
        if value==KEY:
            pixels.append((0,0,0,0))
        else:
            r=(value>>11)&31;g=(value>>5)&63;b=value&31
            pixels.append(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2),255))
    image=Image.new("RGBA",size);image.putdata(pixels)
    return image


def validate_atlas(records, atlas):
    end=0;transparent=0
    assert len(records)==264
    for index,record in enumerate(records):
        assert record["id"]==index and record["offset"]%4==0
        assert record["offset"]>=end
        assert record["stride"]==record["width"]*2
        assert record["bytes"]==record["stride"]*record["height"]
        end=record["offset"]+record["bytes"]
        assert end<=len(atlas)
        data=atlas[record["offset"]:end]
        transparent+=sum(v==KEY for (v,) in struct.iter_unpack("<H",data))
    assert len(atlas)<=MAX_BYTES and 0x5C4000+len(atlas)<0x5FE000
    assert transparent>0
    return transparent


def c_metadata(records,atlas):
    hpath=ROOT/"src/garden_assets.h"
    header=hpath.read_text(encoding="utf-8")
    import re
    header=re.sub(r"#define GARDEN_ATLAS_BYTES .*",f"#define GARDEN_ATLAS_BYTES {len(atlas)}u",header)
    header=re.sub(r"#define GARDEN_ATLAS_CRC32 .*",f"#define GARDEN_ATLAS_CRC32 0x{zlib.crc32(atlas):08X}u",header)
    hpath.write_text(header,encoding="utf-8",newline="\n")
    lines=["/* SPDX-License-Identifier: GPL-3.0-or-later",
           " * Generated by tools/build_atlas.py; atlas is not linked into firmware. */",
           '#include "garden_assets.h"',"",f'const char garden_atlas_sha256[65] = "{hashlib.sha256(atlas).hexdigest()}";',
           "static const garden_sprite_t sprites[GARDEN_ASSET_COUNT] = {"]
    for r in records:
        lines.append("    {%du, %du, %du, %du, %d, %d}, /* %d: %s */" %
            (r["offset"],r["width"],r["height"],r["stride"],r["anchor_x"],r["anchor_y"],r["id"],r["name"]))
    lines += ["};", "", "const garden_sprite_t *garden_asset(unsigned id)",
              "{ return id < GARDEN_ASSET_COUNT ? &sprites[id] : 0; }", "",
              "unsigned garden_bullet_id(unsigned type, unsigned colour, unsigned angle)","{",
              "    unsigned rotation = ((angle + 64u) & 1023u) >> 7;",
              "    if (colour < 1u || colour > 5u) colour = 3u;",
              "    return (type == 2u ? GARDEN_ASSET_DIAMOND_BASE : GARDEN_ASSET_DART_BASE)",
              "           + (colour - 1u) * 8u + rotation;", "}", "",
              "const garden_sprite_t *garden_bullet_asset(unsigned type, unsigned colour, unsigned angle)",
              "{ return garden_asset(garden_bullet_id(type, colour, angle)); }", "",
              "int garden_glyph(char c)", "{", f'    static const char chars[] = "{CHARS}";',
              "    unsigned i;", "    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');",
              "    for (i = 0; i < GARDEN_FONT_GLYPHS; ++i)",
              "        if (chars[i] == c) return (int)(GARDEN_ASSET_FONT_WHITE_BASE + i);",
              "    return -1;", "}", "", "const garden_sprite_t *garden_glyph_asset(char c, unsigned cyan)", "{",
              "    int id = garden_glyph(c);", "    if (id < 0) return 0;",
              "    return garden_asset((unsigned)id + (cyan ? GARDEN_FONT_GLYPHS : 0u));", "}", ""]
    (ROOT/"src/garden_assets.c").write_text("\n".join(lines),encoding="utf-8",newline="\n")


def preview(images,path):
    # Show large actors, a representative orientation for each colour, and fonts.
    selected=[i for i in range(len(images)) if i<12 or (12<=i<92 and (i-12)%8==2)]
    out=Image.new("RGB",(960,900),(20,30,27));d=ImageDraw.Draw(out)
    d.text((16,12),"GARDEN 1.0.9 - SOURCE-DERIVED RGB565 ATLAS",fill=(230,235,220))
    x=16;y=42;rowheight=0
    for i in selected:
        name,im,_=images[i]
        factor=1 if im.width>100 or im.height>80 else 2
        view=im.resize((im.width*factor,im.height*factor),Image.Resampling.NEAREST)
        cellw=max(100,view.width+12);cellh=max(92,view.height+30)
        if x+cellw>944:
            x=16;y+=rowheight+20;rowheight=0
        d.text((x,y),name,fill=(175,211,190))
        out.paste(view,(x,y+20),view)
        x+=cellw;rowheight=max(rowheight,cellh)
    font_top=y+rowheight+30
    for n,cyan in enumerate((False,True)):
        yy=font_top+n*28
        d.text((16,yy),"NEW HUD FONT " + ("CYAN" if cyan else "WHITE"),fill=(175,211,190))
        for j,ch in enumerate(CHARS):
            im,_=glyph(ch,cyan);im=im.resize((16,16),Image.Resampling.NEAREST)
            out.paste(im,(150+j*16,yy),im)
    end=font_top+78
    d.text((16,end),"Actor crops and fixed poses follow original BMP / source. Effects use offline geometry baking.",fill=(160,175,160))
    d.text((16,end+20),"New HUD font is adaptation content. Original GPL-3.0-or-later; see manifest.json / NOTICE.txt.",fill=(160,175,160))
    out.crop((0,0,960,end+48)).save(path)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,default=ROOT/"vendor/garden-1.0.9")
    args=parser.parse_args();src=args.source.resolve();assets=ROOT/"assets"
    original=assets/"original";original.mkdir(parents=True,exist_ok=True)
    (assets/"sprites").mkdir(exist_ok=True)
    source_hashes={}
    for file in ("data/gfx/small.bmp","data/gfx/large.bmp","data/gfx/trans.bmp",
                 "data/gfx/garden.dat","COPYING","AUTHORS"):
        data=(src/file).read_bytes();source_hashes[file]=hashlib.sha256(data).hexdigest()
        shutil.copyfile(src/file,original/Path(file).name)
    for file in ("src/display_init.c","src/display.c","src/palette.c","src/palette.h"):
        source_hashes[file]=hashlib.sha256((src/file).read_bytes()).hexdigest()
    small=load_indexed(original/"small.bmp");large=load_indexed(original/"large.bmp")
    images=[];provenance=[]
    def add(name,result,origin):
        image,anchor=result;images.append((name,image,anchor));provenance.append(origin)
    add("PLAYER",player(small),"small.bmp cuts: display_init.c:382-386; composite display.c:523-533; wings1=wings2=0")
    add("DARTER1",darter(small),"small.bmp display_init.c:372-379; display.c:1861-1884 closed pose, angle1=0")
    add("STOPPER1",stopper(large),"large.bmp display_init.c:547,550; display.c:2015-2020 closed pose; nearest-neighbour scale=0.70")
    add("BOSS1",boss(large),"large.bmp display_init.c:584-597; display.c:1211-1283; closed pose extensions=0 angles=0; nearest-neighbour width=160")
    add("SHOT",shot(),"display.c:3141-3165 PBULLET_MGUN level=0; fixed speckle y=3,7,11")
    for i,frame in enumerate((0,6,12,19)):
        add(f"EXPLOSION{i}",shock(frame),f"display_init.c:1486-1549 prepare_mshockwaves group0 frame{frame}; nearest-neighbour scale=0.5")
    for name,col in (("RED",4),("BLUE",6),("GREEN",8)):
        add(f"CIRCLE_{name}",circle_bullet(col),f"display_init.c:762-773 RLE_circle[4][TCOL{col}]")
    for shape in ("dart","diamond"):
        for colour in range(5):
            for rotation in range(8):
                add(f"{shape.upper()}_B{colour+1}_R{rotation}",polygon_bullet(shape,colour+4,rotation),
                    f"display_init.c:675-754 {shape}_bullet; original geometry; angle={rotation*128}; TCOL_B{colour+1}")
    # Offline palette conversion changes no collision, anchor or clipping data.
    for i,(name,image,anchor) in enumerate(images):
        role=("player" if i==0 else "darter" if i==1 else "stopper" if i==2 else
              "boss" if i==3 else "shot" if i==4 else "effect" if i<9 else "bullet")
        colour=i-9 if i<12 else ((i-12)%40)//8
        images[i]=(name,neon_image(image,role,colour),anchor)
        provenance[i]+="; C UI experiment: neon RGB palette remap, original alpha mask retained"
    for scale in (1,2):
        for cyan in (False,True):
            for ch in CHARS:
                size="LARGE_" if scale==2 else ""
                add(f"FONT_{size}{'CYAN' if cyan else 'WHITE'}_{ord(ch):02X}",glyph(ch,cyan,scale),
                    f"New adaptation HUD typography; authored 5x7 mask in {8*scale}x{8*scale} cell; not upstream garden.dat font")
    assert len(images)==264
    atlas=bytearray();records=[]
    for index,((name,im,anchor),origin) in enumerate(zip(images,provenance)):
        atlas+=b"\x00"*((-len(atlas))%4)
        data,collisions=rgb565_bytes(im)
        records.append(dict(id=index,name=name,offset=len(atlas),width=im.width,height=im.height,
                            stride=im.width*2,anchor_x=anchor[0],anchor_y=anchor[1],bytes=len(data),
                            source=origin,opaque_key_collisions_adjusted=collisions))
        atlas+=data
        im565=decode_rgb565(data,im.size)
        # Compare the uploaded transparency to the source transparency.
        assert im565.getchannel("A").tobytes()==im.getchannel("A").tobytes()
        images[index]=(name,im565,anchor)
        if index<12:im565.save(assets/"sprites"/(name.lower()+".png"))
    if len(atlas)>MAX_BYTES:raise SystemExit(f"Atlas exceeds reserved DDR window: {len(atlas)} > {MAX_BYTES}")
    blob=bytes(atlas);transparent=validate_atlas(records,blob)
    sha=hashlib.sha256(blob).hexdigest();crc=zlib.crc32(blob)
    (assets/"atlas.bin").write_bytes(blob)
    (assets/"atlas.sha256").write_text(f"{sha}  atlas.bin\n",encoding="ascii",newline="\n")
    manifest=dict(format="little-endian RGB565, no header, four-byte aligned sprite starts",
                  address="0x005C4000",safe_window_end="0x00600000",scratch_start="0x005FE000",
                  key_colour="0xF81F",bytes=len(blob),budget_bytes=MAX_BYTES,
                  sha256=sha,crc32=f"0x{crc:08X}",stride_unit="bytes",license="GPL-3.0-or-later",
                  original_author="Linley Henzell",source_project="Garden of coloured lights 1.0.9",
                  source_url="https://sourceforge.net/projects/garden/",
                  source_archive="garden-of-coloured-lights_1.0.9.orig.tar.gz",
                  source_files_sha256=source_hashes,transparency="index0 -> key; opaque key collision -> key xor1",
                  adaptations=["Spring palette frozen; translucent pixels baked against stage-1 BACK1",
                               "Pillow geometric rasterization can differ by edge pixels from Allegro",
                               "STOPPER1 70%, BOSS1 width160, shock50%, nearest-neighbour offline resize",
                               "8 bullet directions; geometry and five original colour families retained",
                               "Frozen actor poses, turret rotations and MGUN random speckle",
                               "C UI experiment: neon palette remap with original dimensions and transparency",
                               "8x8 and 16x16 HUD fonts are new adaptation content, not extracted from garden.dat"],sprites=records)
    (assets/"manifest.json").write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+"\n",encoding="utf-8",newline="\n")
    c_metadata(records,blob);preview(images,assets/"atlas_preview.png")
    notice="""Garden 1.0.9 source-derived assets

Original game programmed by Linley Henzell; see original/AUTHORS.
Original BMP and source-derived geometry are distributed under GPL-3.0-or-later.
The complete license text is retained in original/COPYING. The complete
upstream source archive is in ../vendor/garden-of-coloured-lights_1.0.9.orig.tar.gz.

assets/original contains unmodified source resources. atlas.bin is converted
RGB565 output, not an original upstream asset. Every sprite in manifest.json
records its crop or rendering recipe and the hashes of the source files.
The actor poses are fixed offline. Runtime alpha is replaced with colours baked
against the spring background. Polygon/circle rasterization uses Pillow and may
differ at boundary pixels from Allegro. Scales are recorded in the manifest.

The neon palette remap retains original geometry, anchors and transparency.
The 8x8/16x16 HUD font is newly authored adaptation content (GPL-3.0-or-later), not the
original garden.dat font. garden.dat is retained unmodified for provenance.

Regenerate: python tools/build_atlas.py
Dependency: Python 3 with Pillow (pip install pillow).
Load separately: restore assets/atlas.bin binary 0x005C4000
The C files contain only metadata, not the atlas pixel bytes.
"""
    (assets/"NOTICE.txt").write_text(notice,encoding="utf-8",newline="\n")
    print(json.dumps({"sprites":len(records),"bytes":len(blob),"limit":MAX_BYTES,"key_pixels":transparent,
                      "crc32":f"0x{crc:08X}","sha256":sha}))


if __name__=="__main__":main()
