"""Fit RGB-to-thermal registration from measured corresponding points."""
import argparse
import json
import math
from pathlib import Path


def solve(a, b):
    rows = [list(row) + [v] for row, v in zip(a, b)]
    for col in range(3):
        pivot = max(range(col, 3), key=lambda r: abs(rows[r][col]))
        if abs(rows[pivot][col]) < 1e-10:
            raise ValueError('标定点共线或覆盖范围不足')
        rows[col], rows[pivot] = rows[pivot], rows[col]
        scale = rows[col][col]
        rows[col] = [v / scale for v in rows[col]]
        for r in range(3):
            if r != col:
                scale = rows[r][col]
                rows[r] = [v - scale * q for v, q in zip(rows[r], rows[col])]
    return [row[3] for row in rows]


def fit(data):
    pairs, region = data['pairs'], data['rgb_region']
    if (len(pairs) < 4 or len(region) != 4
            or not all(type(v) in (int, float) and math.isfinite(v) and 0 <= v <= 1
                       for row in [region, *pairs] for v in row)
            or not (region[0] < region[2] and region[1] < region[3])
            or any(len(row) != 4 for row in pairs)):
        raise ValueError('至少4组实测归一化对应点 [rgb_x,rgb_y,ir_x,ir_y] 和有效RGB区域')
    design = [[x, y, 1] for x, y, _, _ in pairs]
    normal = [[sum(row[i] * row[j] for row in design) for j in range(3)] for i in range(3)]
    affine = []
    for out in (2, 3):
        affine += solve(normal, [sum(row[i] * p[out] for row, p in zip(design, pairs)) for i in range(3)])
    if abs(affine[0]*affine[4]-affine[1]*affine[3]) < 1e-6:
        raise ValueError('空间映射不可逆')
    error = max(math.hypot((affine[0]*x+affine[1]*y+affine[2]-u)*32,
                           (affine[3]*x+affine[4]*y+affine[5]-v)*24) for x,y,u,v in pairs)
    if error > 1:
        raise ValueError('拟合误差超过1个热像元，需重新测量对应点')
    return affine, region, error


def header(data):
    affine, region, error = fit(data)
    number = lambda values: ','.join(format(v, '.9f')+'f' for v in values)
    return ('#pragma once\n// Measured RGB/IR registration; valid only for the calibrated near-field setup.\n'
            'static const bool FUSION_CALIBRATED=true;\n'
            'static const float FUSION_RGB_TO_IR[6]={' + number(affine) + '};\n'
            'static const float FUSION_RGB_REGION[4]={' + number(region) + '};\n'), error


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('measured_points', type=Path)
    parser.add_argument('--output', type=Path, help='生成标定头文件；未指定时只检查，不改工程')
    args = parser.parse_args()
    text, error = header(json.loads(args.measured_points.read_text(encoding='utf-8-sig')))
    if args.output:
        args.output.write_text(text, encoding='utf-8')
    print(f'标定点拟合最大残差 {error:.3f} 热像元；仍需独立检查真人和照片。')
