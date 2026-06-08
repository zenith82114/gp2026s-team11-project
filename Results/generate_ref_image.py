import os
import sys
from PIL import Image
import numpy as np

# Canonical copy of the reference-builder the C++ harness writes/runs at runtime
# (src/main.cpp: ensureReferenceScriptExists). Keep the two in sync.
# argv[1] is the per-scene/per-mode prefix, e.g. "scene2_mode2_".
PREFIX = sys.argv[1] if len(sys.argv) > 1 else ''
REFERENCE_FILES = [
    PREFIX + 'random_ref_2048.png',
    PREFIX + 'sobol_ref_2048.png',
    PREFIX + 'scrambled_ref_2048.png',
]
REF_PATH = PREFIX + 'ref_2048.png'


def load_img(path):
    img = Image.open(path).convert('RGB')
    return np.asarray(img).astype(np.float32) / 255.0


def save_img(path, arr):
    arr = np.clip(arr, 0.0, 1.0)
    Image.fromarray((arr * 255.0 + 0.5).astype(np.uint8), mode='RGB').save(path)


def main():
    imgs = []
    for path in REFERENCE_FILES:
        if not os.path.exists(path):
            raise FileNotFoundError(f'Missing reference image: {path}')
        imgs.append(load_img(path))

    ref = np.mean(np.stack(imgs, axis=0), axis=0)
    save_img(REF_PATH, ref)

    for path in REFERENCE_FILES:
        if os.path.exists(path):
            os.remove(path)

    print(f'Generated {REF_PATH} from the three seed images.')
    print('Deleted intermediate reference images.')


if __name__ == '__main__':
    main()
