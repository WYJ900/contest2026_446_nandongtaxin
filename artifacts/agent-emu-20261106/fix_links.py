import os, sys
root = os.path.expanduser('~/velaride_agent_emu')
hits = []
for top in ['apps', 'nuttx', 'frameworks', 'packages', 'vendor/openvela', 'external']:
    base = os.path.join(root, top)
    for d, dirs, files in os.walk(base):
        dirs[:] = [x for x in dirs if x not in ('.git', 'cmake_out')]
        for f in files:
            p = os.path.join(d, f)
            try:
                if os.path.islink(p) or os.path.getsize(p) > 200: continue
                data = open(p, 'rb').read()
            except OSError: continue
            if not data or b'\n' in data or b'\0' in data: continue
            try: target = data.decode()
            except UnicodeDecodeError: continue
            if ' ' in target or target.startswith('#'): continue
            if os.path.exists(os.path.normpath(os.path.join(d, target))):
                hits.append((p[len(root)+1:], target))
fix = '--fix' in sys.argv
for p, t in hits:
    print(p, '->', t)
    if fix:
        full = os.path.join(root, p); os.remove(full); os.symlink(t.rstrip('/'), full)
print('TOTAL', len(hits))
