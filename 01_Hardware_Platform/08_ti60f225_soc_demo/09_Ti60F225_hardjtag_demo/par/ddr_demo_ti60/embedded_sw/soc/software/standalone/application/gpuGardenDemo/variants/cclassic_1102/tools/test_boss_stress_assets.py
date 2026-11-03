"""Host-only metadata/texture ABI checks; not a board performance claim."""
from pathlib import Path
import hashlib,json,re,struct,subprocess,zlib
V=Path(__file__).resolve().parents[1]
ROOT=next(p for p in V.parents if (p/'01_Hardware_Platform').is_dir())
OUT=ROOT/'06_Controller_Dependencies/cboss_assets_1102';OUT.mkdir(exist_ok=True)
test=r'''#include <assert.h>
#include <stdio.h>
#include "garden_assets.h"
int main(void){
 assert(GARDEN_ASSET_COUNT==872);
 assert(GARDEN_ASSET_TRIPLE_BOSS0==859 && GARDEN_ASSET_TRIPLE_BOSS2==861);
 assert(GARDEN_ASSET_LARGE_BOMB_BASE==862 && GARDEN_ASSET_LARGE_BLAST_BASE==864);
 assert(GARDEN_ATLAS_BYTES==755700 && sizeof(garden_sprite_t)==8);
 FILE *out=fopen("descriptors.bin","wb");assert(out);
 for(unsigned i=0;i<GARDEN_ASSET_COUNT;i++){const garden_sprite_t *s=garden_asset(i);assert(s);assert(fwrite(s,sizeof(*s),1,out)==1);}
 assert(fclose(out)==0 && garden_asset(GARDEN_ASSET_COUNT)==0);
 for(unsigned type=1;type<=2;type++)for(unsigned colour=1;colour<=5;colour++)for(unsigned angle=0;angle<1024;angle++){
  unsigned id=garden_bullet_precision_id(type,colour,angle);assert(id<840 && garden_asset_is_enemy_bullet(id));
 }
 puts("PASS native runtime descriptor ABI and 10240 existing bullet mappings");
}'''
(OUT/'test.c').write_text(test,encoding='ascii')
cc='D:/mingw64/bin/gcc.exe';exe=OUT/'test_assets.exe'
subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(V/'src'),str(OUT/'test.c'),str(V/'src/garden_assets.c'),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],cwd=OUT,capture_output=True,text=True);run.check_returncode();print(run.stdout,end='')
records=list(struct.iter_unpack('<IBBbb',(OUT/'descriptors.bin').read_bytes()))
manifest=json.loads((V/'assets/manifest.json').read_text(encoding='utf-8'));blob=(V/'assets/atlas.bin').read_bytes()
for i,(off,w,h,ax,ay) in enumerate(records):
    m=manifest['sprites'][i];assert (off,w,h,ax,ay)==tuple(m[k] for k in ('offset','width','height','anchor_x','anchor_y'))
    assert off+w*h*2<=len(blob)
assert zlib.crc32(blob)==int(manifest['crc32'],16)
header=(V/'src/garden_assets.h').read_text(encoding='utf-8')
assert int(re.search(r'GARDEN_ATLAS_CRC32 0x([0-9A-F]+)',header)[1],16)==zlib.crc32(blob)
scatter=(V/'assets/pure_scatter16.bin').read_bytes();tile_checks=[]
reference_atlas=(V.parent/'cjui_1031/assets/atlas.bin').read_bytes()
assert blob[:len(reference_atlas)]==reference_atlas
assert scatter==(V.parent/'cjui_1031/assets/pure_scatter16.bin').read_bytes()
scatter_meta=json.loads((V/'assets/pure_scatter16.json').read_text(encoding='utf-8'))
assert scatter_meta['source_atlas_sha256']==hashlib.sha256(blob).hexdigest()
assert scatter_meta['blob_sha256']==hashlib.sha256(scatter).hexdigest()
for i in range(27):
    asset,packed,source_crc,tile_crc=struct.unpack_from('<4I',scatter,64+i*16)
    off,w,h,_,_=records[asset];assert (w,h)==((packed>>16)&255,packed>>24)
    assert zlib.crc32(blob[off:off+w*h*2])==source_crc
    assert zlib.crc32(scatter[12872+i*512:12872+(i+1)*512])==tile_crc
    tile_checks.append(asset)
assert len({hashlib.sha256(blob[records[i][0]:records[i][0]+records[i][1]*records[i][2]*2]).hexdigest() for i in (859,860,861)})==3
result={'status':'PASS','hardware_access':False,'scope':'actual compiled C metadata, atlas bounds/CRC, original bullet mapping and scatter source CRC compatibility',
    'descriptors':len(records),'scatter_tiles_verified':len(tile_checks),'original_prefix_bytes_unchanged':len(reference_atlas),
    'scatter_blob_unchanged':True,'scatter_source_full_atlas_binding':True,'atlas_sha256':hashlib.sha256(blob).hexdigest(),'atlas_bytes':len(blob),
    'safe_end_exclusive':hex(0x005c4000+len(blob)),'batch_list_start':hex(0x0067d000),'scratch_start':hex(0x0067e000)}
(V/'assets/cboss_runtime_asset_test.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result,indent=2))
