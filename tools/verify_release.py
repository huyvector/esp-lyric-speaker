#!/usr/bin/env python3
"""Check published firmware checksums, manifests and merged-image layout."""
from pathlib import Path
import hashlib, json

root = Path(__file__).resolve().parents[1]
for directory in (root / 'firmware', root / 'docs/firmware'):
    for line in (directory / 'SHA256SUMS.txt').read_text().splitlines():
        if not line.strip(): continue
        digest, name = line.split(maxsplit=1)
        assert hashlib.sha256((directory / name).read_bytes()).hexdigest() == digest, name
for role, family, chip, boot in [('audio', 'ESP32', 0, 0x1000), ('display', 'ESP32-C3', 5, 0)]:
    manifest = json.loads((root / f'docs/manifest-{role}.json').read_text())
    assert manifest['version'] == '0.4.11'
    assert len(manifest['builds']) == 1
    build = manifest['builds'][0]
    assert build['chipFamily'] == family
    assert len(build['parts']) == 1 and build['parts'][0]['offset'] == 0
    image = (root / 'docs' / build['parts'][0]['path']).read_bytes()
    assert image == (root / f'firmware/{role}/factory.bin').read_bytes()
    assert image[boot] == 0xe9 and int.from_bytes(image[boot+12:boot+14], 'little') == chip
    assert image[boot+2:boot+4] == b'\x02\x20'  # DIO / 4 MB / 40 MHz
    for offset, name in [(0x8000, 'partitions.bin'), (0xe000, 'ota-data.bin'), (0x10000, 'app.bin')]:
        part = (root / f'firmware/{role}/{name}').read_bytes()
        assert image[offset:offset+len(part)] == part, (role, name)
print('Release integrity, chip guards, factory offsets and flash settings: PASS')
