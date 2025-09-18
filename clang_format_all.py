import os
import sys

def safe_remove(l, i):
    if i in l:
        l.remove(i)

for root, dirs, files in os.walk('.'):
    for file in files:
        if file.endswith('.cpp') or file.endswith('.hpp') or file.endswith('.h'):
            if sys.platform == 'win32':
                os.system(f'clang-format -i {os.path.join(root, file)}');
            else:
                os.system(f'clang-format-18 -i {os.path.join(root, file)}');
    safe_remove(dirs, 'build')
    safe_remove(dirs, '.git')
    safe_remove(dirs, '.vs')
    safe_remove(dirs, '.github')
    safe_remove(dirs, 'resources')

