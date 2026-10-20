#!/usr/bin/env python3
"""Build portable input/navigation regressions; no Arduino/hardware claim."""
import os
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='navigation-', dir=os.environ.get('TMPDIR')) as tmp:
    for name in ('navigation', 'input'):
        binary=Path(tmp)/name
        command=[os.environ.get('CXX','g++'), '-std=c++11', '-Wall', '-Wextra',
                 '-Werror', '-pedantic', '-g', '-I'+str(root/'lib/Navigation'),
                 str(root/('tests/test_'+name+'.cpp')), '-o', str(binary)]
        if os.environ.get('NAVIGATION_SANITIZE')=='1':
            command[1:1]=['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
    subprocess.run([os.environ.get('PYTHON','python'), str(root/'tests/test_input_integration.py')], check=True)
