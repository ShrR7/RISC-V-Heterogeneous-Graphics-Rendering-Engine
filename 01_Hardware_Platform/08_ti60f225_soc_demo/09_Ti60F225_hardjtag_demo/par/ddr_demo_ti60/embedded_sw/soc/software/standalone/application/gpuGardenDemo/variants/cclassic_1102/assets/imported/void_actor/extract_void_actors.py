"""Compose preserved Foozle layers and fit actors for the isolated C art variant.

No recolouring, synthetic art or promotional crops. Run with Pillow available.
Source frame coordinates and exact anchor fractions are saved in manifest.json.
"""
from fractions import Fraction
import hashlib
import json
from pathlib import Path

from PIL import Image

OUT = Path(__file__).resolve().parent
# Originals are bundled alongside this variant; no research/build directory needed.
RAW = OUT.parents[1] / 'source/foozle'
PROVENANCE = json.loads((RAW / 'download_manifest.json').read_text(encoding='utf-8'))
SOURCE_INFO = {item['path']: item for item in PROVENANCE['files']}


def half_up(value):
    value = Fraction(value)
    return (value.numerator * 2 + value.denominator) // (2 * value.denominator)


def frame(path, size, index=0):
    data = (RAW / path).read_bytes()
    record = SOURCE_INFO[path]
    digest = hashlib.sha256(data).hexdigest()
    if digest != record['sha256']:
        raise ValueError('Original source changed: ' + path)
    image = Image.open(RAW / path).convert('RGBA')
    if image.height != size or image.width % size:
        raise ValueError('Expected horizontal square sheet: ' + path)
    if not 0 <= index < image.width // size:
        raise ValueError('Bad frame index: ' + path)
    box = (index * size, 0, (index + 1) * size, size)
    tile = image.crop(box)
    return tile, {
        'path': path, 'source_url': record['source_url'],
        'repository': record['repository'], 'commit': record['commit'],
        'source_sha256': digest, 'source_git_blob_sha1': record['git_blob_sha1'],
        'sheet_dimensions': list(image.size), 'frame_size': [size, size],
        'frame_index': index, 'sheet_crop_box': list(box),
        'frame_alpha_bbox': list(tile.getchannel('A').getbbox()),
        'license': 'CC0-1.0',
    }


def compose(layers):
    canvas = Image.new('RGBA', layers[0].size, (0, 0, 0, 0))
    for layer in layers:
        if layer.size != canvas.size:
            raise ValueError('Layer dimensions differ')
        canvas.alpha_composite(layer)
    return canvas


def extract_group(names, canvases, sources, max_size, centre, rotate_180=False):
    original_size = canvases[0].size
    anchor = tuple(centre)
    if rotate_180:
        canvases = [canvas.transpose(Image.Transpose.ROTATE_180) for canvas in canvases]
        # Anchors are continuous canvas coordinates, not pixel indices.
        anchor = (original_size[0] - anchor[0], original_size[1] - anchor[1])
    boxes = [canvas.getchannel('A').getbbox() for canvas in canvases]
    union = (min(box[0] for box in boxes), min(box[1] for box in boxes),
             max(box[2] for box in boxes), max(box[3] for box in boxes))
    width, height = union[2] - union[0], union[3] - union[1]
    ratio = min(Fraction(max_size[0], width), Fraction(max_size[1], height))
    out_size = (min(max_size[0], half_up(width * ratio)),
                min(max_size[1], half_up(height * ratio)))
    exact_anchor = (Fraction((anchor[0] - union[0]) * out_size[0], width),
                    Fraction((anchor[1] - union[1]) * out_size[1], height))
    integer_anchor = tuple(half_up(value) for value in exact_anchor)
    records = []
    for name, canvas, layer_sources, bbox in zip(names, canvases, sources, boxes):
        output = canvas.crop(union).resize(out_size, Image.Resampling.NEAREST)
        target = OUT / (name + '.png')
        output.save(target)
        records.append({
            'name': name, 'file': target.name, 'path': target.name,
            'width': out_size[0], 'height': out_size[1], 'dimensions': list(out_size),
            'anchor': list(integer_anchor), 'anchor_x': integer_anchor[0],
            'anchor_y': integer_anchor[1],
            'anchor_exact_fraction': [str(value) for value in exact_anchor],
            'anchor_rounding': 'nearest integer; positive half rounds up',
            'anchor_error_pixels': [str(Fraction(integer_anchor[i]) - exact_anchor[i]) for i in range(2)],
            'source_canvas_dimensions': list(original_size),
            'source_logical_centre': list(centre),
            'rotated_logical_centre': list(anchor),
            'rotate_180': rotate_180,
            'composed_bbox_after_rotation': list(bbox),
            'group_union_bbox_after_rotation': list(union),
            'max_fit_dimensions': list(max_size),
            'requested_uniform_fit_scale': str(ratio),
            'actual_raster_scale': [str(Fraction(out_size[0], width)), str(Fraction(out_size[1], height))],
            'resampling': 'NEAREST', 'colour_transform': 'none',
            'png_sha256': hashlib.sha256(target.read_bytes()).hexdigest(),
            'source_layers_in_compositing_order': layer_sources,
        })
    return records


def main():
    dev = 'devshare/assets/images/foozle/'
    luke = 'luke/img/enemy/lv1_to_5/'
    records = []
    hull, hull_info = frame(dev + 'ship.png', 48)
    engine, engine_info = frame(dev + 'ship_engine.png', 48)
    players, player_sources = [], []
    for i in range(3):
        flame, flame_info = frame(dev + 'ship_engine_thruster.png', 48, i)
        # Engine hardware beneath hull, flame over the shared-coordinate stack.
        players.append(compose([engine, hull, flame]))
        player_sources.append([engine_info, hull_info, flame_info])
    records.extend(extract_group(['PLAYER_0', 'PLAYER_1', 'PLAYER_2'],
                                 players, player_sources, (34, 46), (24, 24)))

    hull, hull_info = frame(dev + 'scout.png', 64)
    flame, flame_info = frame(dev + 'scout_engine.png', 64)
    records.extend(extract_group(['DARTER1'], [compose([flame, hull])],
                                 [[flame_info, hull_info]], (49, 51), (32, 32), True))

    hull, hull_info = frame(luke + 'base/Bomber.png', 64)
    flame, flame_info = frame(luke + 'Engine/Bomber.png', 64)
    records.extend(extract_group(['STOPPER1'], [compose([flame, hull])],
                                 [[flame_info, hull_info]], (65, 102), (32, 32), True))

    hull, hull_info = frame(luke + 'base/Dreadnought.png', 128)
    flame, flame_info = frame(luke + 'Engine/Dreadnought.png', 128)
    records.extend(extract_group(['BOSS1'], [compose([flame, hull])],
                                 [[flame_info, hull_info]], (160, 117), (64, 64), True))

    manifest = {
        'schema': 1, 'variant': 'void_art_20261013',
        'license': 'CC0-1.0', 'author': 'Foozle / David (chroma_dave)',
        'official_sources': PROVENANCE['official_sources'],
        'raw_source_manifest_sha256': hashlib.sha256((RAW / 'download_manifest.json').read_bytes()).hexdigest(),
        'scope': 'Only derived PNGs in the isolated variant. Collision/game logic unchanged.',
        'process': 'Same source canvas layer compositing; common alpha-bbox crop; nearest-neighbour aspect fit; optional whole-canvas 180-degree rotation.',
        'notes': [
            'All layers use preserved mirrored PNG source bytes with verified Git blob hash.',
            'All animation frames in a group share exact dimensions, crop and anchor.',
            'Continuous anchor coordinates are recorded as exact fractions; renderer anchor is their nearest integer.',
            'Pixel dimensions require integer rounding; actual x/y raster scales are recorded independently.',
            'Enemy and Boss nose point down; player nose points up.',
            'No shield layer or additional runtime compositing command is required.',
        ],
        'sprites': records,
    }
    (OUT / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({sprite['name']: {'size': sprite['dimensions'], 'anchor': sprite['anchor']} for sprite in records}))


if __name__ == '__main__':
    main()
