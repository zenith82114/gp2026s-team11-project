import csv
import math
import os
from PIL import Image
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

SPPS = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512]
METHODS = {
    'Random': 'random_{}.png',
    'Sobol': 'sobol_{}.png',
    'Scrambled Sobol': 'scrambled_{}.png',
}
REF_PATH = 'ref_2048.png'


def load_img(path):
    img = Image.open(path).convert('RGB')
    return np.asarray(img).astype(np.float32) / 255.0


def mse(img, ref):
    return float(np.mean((img - ref) ** 2))


def psnr_from_mse(val):
    if val <= 0.0:
        return float('inf')
    return 10.0 * math.log10(1.0 / val)


def cleanup_intermediate_images():
    for method_name, pattern in METHODS.items():
        for spp in SPPS:
            path = pattern.format(spp)
            if os.path.exists(path):
                os.remove(path)


def main():
    if not os.path.exists(REF_PATH):
        raise FileNotFoundError(f'Missing reference image: {REF_PATH}. Press REF first.')

    ref = load_img(REF_PATH)
    rows = []

    for method_name, pattern in METHODS.items():
        for spp in SPPS:
            path = pattern.format(spp)
            if not os.path.exists(path):
                raise FileNotFoundError(f'Missing image: {path}')
            img = load_img(path)
            err = mse(img, ref)
            rows.append({
                'method': method_name,
                'spp': spp,
                'mse': err,
                'psnr': psnr_from_mse(err),
            })

    with open('psnr_results.csv', 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=['method', 'spp', 'mse', 'psnr'])
        writer.writeheader()
        writer.writerows(rows)

    plt.figure()
    for method_name in METHODS.keys():
        xs = [r['spp'] for r in rows if r['method'] == method_name]
        ys = [r['mse'] for r in rows if r['method'] == method_name]
        plt.plot(xs, ys, marker='o', label=method_name)
    plt.xscale('log', base=2)
    plt.yscale('log')
    plt.xlabel('Samples per pixel')
    plt.ylabel('MSE to reference')
    plt.title('MSE vs spp')
    plt.grid(True, which='both')
    plt.legend()
    plt.tight_layout()
    plt.savefig('mse_vs_spp.png', dpi=200)
    plt.close()

    plt.figure()
    for method_name in METHODS.keys():
        xs = [r['spp'] for r in rows if r['method'] == method_name]
        ys = [r['psnr'] for r in rows if r['method'] == method_name]
        plt.plot(xs, ys, marker='o', label=method_name)
    plt.xscale('log', base=2)
    plt.xlabel('Samples per pixel')
    plt.ylabel('PSNR (dB)')
    plt.title('PSNR vs spp')
    plt.grid(True, which='both')
    plt.legend()
    plt.tight_layout()
    plt.savefig('psnr_vs_spp.png', dpi=200)
    plt.close()

    cleanup_intermediate_images()
    print('Generated: mse_vs_spp.png, psnr_vs_spp.png, psnr_results.csv')
    print('Deleted intermediate sampling images. Kept ref_2048.png.')


if __name__ == '__main__':
    main()
