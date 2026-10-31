"""Packs the project as a CloudPebble import: dist/PrintPulse-cloudpebble.zip.

Run after `pebble build`, which generates src/pkjs/config_page.js. CloudPebble
may build with its own wscript, so the zip carries that file ready-made.
"""
import os
import sys
import zipfile

root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else '.')
if not os.path.exists(os.path.join(root, 'src', 'pkjs', 'config_page.js')):
    sys.exit('Run `pebble build` first: src/pkjs/config_page.js is missing.')

os.makedirs(os.path.join(root, 'dist'), exist_ok=True)
out = os.path.join(root, 'dist', 'PrintPulse-cloudpebble.zip')
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for item in ('package.json', 'wscript', 'resources', 'src'):
        path = os.path.join(root, item)
        if os.path.isfile(path):
            z.write(path, 'PrintPulse/' + item)
            continue
        for folder, _, files in os.walk(path):
            for name in sorted(files):
                full = os.path.join(folder, name)
                z.write(full, 'PrintPulse/' + os.path.relpath(full, root).replace(os.sep, '/'))
print('Wrote %s (%d files)' % (out, len(z.namelist())))
