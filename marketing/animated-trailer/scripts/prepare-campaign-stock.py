"""Download the user-authorized licensed wedding imagery; retain source credits."""
from pathlib import Path
import hashlib, json, urllib.request
from PIL import Image, ImageOps

root = Path(__file__).resolve().parents[1]
originals = root / 'assets/licensed/weddings'
output = root / 'public/media/campaign'
originals.mkdir(parents=True, exist_ok=True)
output.mkdir(parents=True, exist_ok=True)
items = [
    ('wedding-bride-01', 30138406, 'Alexander Mass', 'alexander-mass-748453803', 'https://www.pexels.com/photo/elegant-bridal-portrait-by-window-in-soft-light-30138406/'),
    ('wedding-bride-02', 30138407, 'Alexander Mass', 'alexander-mass-748453803', 'https://www.pexels.com/photo/elegant-bridal-portrait-by-window-in-soft-light-30138406/'),
    ('wedding-bride-03', 31252207, 'Alexander Mass', 'alexander-mass-748453803', 'https://www.pexels.com/photo/elegant-bridal-portrait-by-window-in-soft-light-30138406/'),
    ('wedding-couple', 9517379, 'Trung Nguyen', 'trungnguyenphotog', 'https://www.pexels.com/photo/bride-and-groom-standing-on-the-hill-back-to-back-9517379/'),
    ('wedding-dawn', 20703918, 'Lina Ospina', 'lina-ospina-1098622807', 'https://www.pexels.com/photo/bride-and-groom-in-mountains-at-dawn-20703918/'),
]
manifest = []
for name, photo_id, author, handle, page in items:
    url = f'https://images.pexels.com/photos/{photo_id}/pexels-photo-{photo_id}.jpeg?cs=srgb&dl=pexels-{handle}-{photo_id}.jpg&fm=jpg'
    source = originals / f'{name}.jpg'
    if not source.exists():
        request = urllib.request.Request(url, headers={'User-Agent': 'Looksy authorized media preparation'})
        with urllib.request.urlopen(request, timeout=60) as response:
            source.write_bytes(response.read())
    image = ImageOps.exif_transpose(Image.open(source)).convert('RGB')
    dimensions = list(image.size)
    image.thumbnail((1800, 1800), Image.Resampling.LANCZOS)
    image.save(output / f'{name}.jpg', quality=96, subsampling=0)
    manifest.append({
        'name': name, 'photographer': author, 'photo_id': photo_id,
        'discovery_page': page, 'download': url,
        'license': 'https://www.pexels.com/license/',
        'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'source_dimensions': dimensions, 'preview_dimensions': list(image.size),
        'scope': 'Licensed photographic JPEG for an authored product animation; not an original camera RAW, not a native Looksy export, not an endorsement.',
    })
    print(name, dimensions, flush=True)
(root / 'campaign-stock-sources.json').write_text(json.dumps(manifest, indent=2) + '\n')
