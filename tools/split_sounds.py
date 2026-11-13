#!/usr/bin/env python3
"""Moves every sound's audio out of index.html into sounds/<id>.json.
index.html keeps only each sound's settings plus "src". Safe to re-run: sounds that are
already split are left alone, and any sound that still carries samples is split out."""
import json, os, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
path = os.path.join(root, 'index.html')
s = open(path, encoding='utf8').read()
k = s.index('window.SOUNDS = '); st = s.index('[', k)
arr, end = json.JSONDecoder().raw_decode(s[st:])
os.makedirs(os.path.join(root, 'sounds'), exist_ok=True)
for a in arr:
    if a.get('samples'):
        with open(os.path.join(root, 'sounds', a['id'] + '.json'), 'w') as f:
            json.dump({'samples': a['samples']}, f, separators=(',', ':'))
        del a['samples']
    a['src'] = 'sounds/' + a['id'] + '.json'
open(path, 'w', encoding='utf8').write(s[:st] + json.dumps(arr, separators=(',', ':')) + s[st + end:])
print(len(arr), 'sounds;', 'index.html now', round(os.path.getsize(path) / 1e6, 2), 'MB')
