"""Neutral, aspect-preserving NS3C v1 cover conversion. Requires Pillow."""
import argparse
import io
import pathlib
import struct
from PIL import Image, ImageOps, ImageCms

WIDTH, HEIGHT = 104, 136
HEADER = struct.pack('<4sHHHH', b'NS3C', WIDTH, HEIGHT, 1, 0)

def convert(source):
    with Image.open(source) as original:
        image = ImageOps.exif_transpose(original)
        icc = original.info.get('icc_profile')
        alpha = image.convert('RGBA').getchannel('A')
        if icc:
            profile = ImageCms.ImageCmsProfile(io.BytesIO(icc))
            # Keep the native CMYK/LAB color mode for its matching input profile.
            base = image if image.mode in ('RGB', 'CMYK', 'LAB', 'L') else image.convert('RGB')
            image = ImageCms.profileToProfile(base, profile, ImageCms.createProfile('sRGB'), outputMode='RGB')
        else:
            image = image.convert('RGB')
        image = image.convert('RGBA')
        image.putalpha(alpha)
        image = ImageOps.contain(image, (WIDTH, HEIGHT), Image.Resampling.LANCZOS)
        canvas = Image.new('RGBA', (WIDTH, HEIGHT), (0, 0, 90, 255))
        canvas.alpha_composite(image, ((WIDTH-image.width)//2, (HEIGHT-image.height)//2))
        raw = canvas.convert('RGB').tobytes()
        pixels = zip(raw[0::3], raw[1::3], raw[2::3])
        values = [((r*31+127)//255 << 11) | ((g*63+127)//255 << 5) | ((b*31+127)//255) for r,g,b in pixels]
        return HEADER + struct.pack('<%dH' % len(values), *values)

def preview(data):
    if len(data) != 28300 or data[:12] != HEADER:
        raise ValueError('Invalid NS3C v1 cover')
    rgb = []
    for (c,) in struct.iter_unpack('<H', data[12:]):
        r,g,b = c>>11, (c>>5)&63, c&31
        rgb.append(((r<<3)|(r>>2), (g<<2)|(g>>4), (b<<3)|(b>>2)))
    image = Image.new('RGB', (WIDTH, HEIGHT)); image.putdata(rgb)
    return image

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=pathlib.Path, help='Image or folder of images; keep the exact ROM stem')
    parser.add_argument('destination', type=pathlib.Path, help='Output folder; copy .cov files to /JOGOS/GBC/cover')
    parser.add_argument('--preview', action='store_true', help='Also save a PNG decoded from the actual RGB565 output')
    args = parser.parse_args()
    files = [args.source] if args.source.is_file() else sorted(p for p in args.source.iterdir() if p.suffix.lower() in ('.png','.jpg','.jpeg','.webp','.bmp','.tif','.tiff') and not p.name.endswith('.cov-preview.png'))
    if not files: parser.error('No source images found')
    stems = [p.stem.casefold() for p in files]
    if len(set(stems)) != len(stems): parser.error('Duplicate stems: rename the sources before converting')
    args.destination.mkdir(parents=True, exist_ok=True)
    for source in files:
        data = convert(source)
        target = args.destination / (source.stem+'.cov')
        temporary = target.with_suffix('.cov.tmp')
        temporary.write_bytes(data); temporary.replace(target)
        if args.preview: preview(data).save(target.with_suffix('.cov-preview.png'))
        print(str(target)+' (28300 bytes, RGB565 LE, no contrast enhancement)')

if __name__ == '__main__': main()
