# SPDX-License-Identifier: MIT
"""Bounded offline interchange. This format deliberately cannot authorize a live overlay."""
import math
from typing import Any

MAX_PERSONS = 16

def safe_label(value: str) -> str:
    if not isinstance(value, str):
        raise ValueError('label must be a string')
    return ''.join(c if 32 <= ord(c) < 127 else ' ' for c in value[:64])

def _number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)

def canonical_pose(points: Any, width: int, height: int) -> list[dict[str, float]]:
    if not (type(width) is int and type(height) is int and 0 < width <= 8192 and 0 < height <= 8192):
        raise ValueError('invalid image extent')
    if len(points) not in (33, 39):
        raise ValueError('expected 33 canonical or 39 Zoo landmarks')
    result = []
    for point in points[:33]:
        if len(point) != 5 or not all(_number(float(v)) for v in point):
            raise ValueError('malformed landmark')
        x, y, z, visibility, presence = map(float, point)
        if not (0 <= visibility <= 1 and 0 <= presence <= 1):
            raise ValueError('invalid landmark scores')
        # Keep out-of-frame positions explicit; do NOT clamp and call them observed.
        result.append(dict(x=x/width, y=y/height, z_relative=z/max(width, height),
                           visibility=visibility, presence=presence))
    return result

def _rect(value: Any) -> bool:
    return (isinstance(value, list) and len(value) == 4 and all(_number(v) for v in value)
            and 0 <= value[0] < value[2] <= 1 and 0 <= value[1] < value[3] <= 1)

def validate_result(value: Any) -> dict:
    if not isinstance(value, dict) or type(value.get('schema')) is not int or value['schema'] != 1:
        raise ValueError('unsupported result schema')
    if value.get('origin') != 'offline_image':
        raise ValueError('offline results cannot assert live provenance')
    sha = value.get('image_sha256', '')
    if not isinstance(sha, str) or len(sha) != 64 or any(c not in '0123456789abcdef' for c in sha):
        raise ValueError('missing image hash')
    for name in ('width', 'height'):
        if type(value.get(name)) is not int or not 0 < value[name] <= 8192:
            raise ValueError('invalid image extent')
    persons = value.get('persons')
    if not isinstance(persons, list) or len(persons) > MAX_PERSONS:
        raise ValueError('invalid instance count')
    for p in persons:
        if not isinstance(p, dict) or not _rect(p.get('box')) or not _number(p.get('score')) or not 0 <= p['score'] <= 1:
            raise ValueError('invalid instance')
        sizes={33} if p.get('box_semantics','visible_landmark_envelope')=='visible_landmark_envelope' else {0,33} if p.get('box_semantics') in ('estimated_person_region','detector_box') else set()
        class_id=p.get('class_id',0)
        if type(class_id) is not int or not 0<=class_id<80 or (p.get('landmarks') and class_id!=0):
            raise ValueError('invalid class or pose claim')
        if p.get('facing') != 'unknown' or len(p.get('landmarks', [])) not in sizes:
            raise ValueError('unsupported pose or facing claim')
        for k in p['landmarks']:
            if not isinstance(k, dict) or not all(_number(k.get(n)) for n in ('x','y','z_relative','visibility','presence')):
                raise ValueError('invalid landmark')
            if not (0 <= k['visibility'] <= 1 and 0 <= k['presence'] <= 1):
                raise ValueError('invalid landmark scores')
    return value
