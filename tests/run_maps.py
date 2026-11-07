#!/usr/bin/env python3
"""Boot a disposable image with Maps preinstalled, a demo tile pack in /Maps
and be300's GPS card replaying a track, and check the app follows it.

Build the package here (./build.sh out/pkg/maps.gzp) and the OS image with
the input script in the sibling glaze-os checkout (GLAZE_DIR):
  mkdir -p ../glaze-os/out/input && cp tests/ui/t_maps.txt ../glaze-os/out/input/
  (cd ../glaze-os && ./build.sh INPUT_SCRIPT=out/input/t_maps.txt)
Then: python3 tests/run_maps.py [--seconds 40] [--no-gps]
The final screen is out/maps-*/screenshot_*.bmp.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile


def prepare(root, glaze):
    work = Path(tempfile.mkdtemp(prefix='maps-', dir=root/'out'))
    shutil.copytree(glaze/'userdata', work/'userdata')
    archive = (root/'out/pkg/maps.gzp').read_bytes()
    assert archive[:4] == b'GZPK'
    bundle = work/'userdata/Applications/maps.app'
    for i in range(struct.unpack_from('<I', archive, 4)[0]):
        name, off, size = struct.unpack_from('<48sII', archive, 8+56*i)
        target = bundle/name.split(b'\0')[0].decode()
        assert target.resolve().is_relative_to(bundle.resolve())
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(archive[off:off+size])
    (work/'userdata/Maps').mkdir()
    subprocess.run([sys.executable, '-I', str(glaze/'tests/maptiles_test.py'), '--keep', str(work/'userdata/Maps')],
                   check=True, capture_output=True)
    (work/'userdata/Maps/demo.mbtiles').unlink()
    subprocess.run(['make', 'OUT=out/native', 'out/native/buildtools/mkfsimg'], cwd=glaze, check=True, capture_output=True)
    blocks = re.search(r'#define GZ_FLASH_BLOCKS\s+(\d+)', (glaze/'lib/include/glaze/abi.h').read_text()).group(1)
    subprocess.run([str(glaze/'out/native/buildtools/mkfsimg'), str(work/'userdata'), blocks, str(work/'fs.img')], check=True)
    nand = bytearray((glaze/'out/glaze.bin').read_bytes())
    fs = (work/'fs.img').read_bytes()
    assert len(fs) == len(nand)-0x500000
    nand[0x500000:] = fs
    (work/'glaze.bin').write_bytes(nand)
    return work


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=40)
    parser.add_argument('--no-gps', action='store_true')
    parser.add_argument('--gui', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    glaze = Path(os.environ.get('GLAZE_DIR', root.parent/'glaze-os')).resolve()
    work = prepare(root, glaze)
    print(work, flush=True)
    emulator = os.environ.get('BE300')
    if not emulator:
        common = subprocess.check_output(['git', 'rev-parse', '--git-common-dir'], cwd=root, text=True).strip()
        emulator = str((root/common).resolve().parent.parent/'be300/build-host/be300')
    env = dict(os.environ)
    if not args.gui:
        env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    command = [emulator, '--nand', 'glaze.bin', '--rtc-host-time']
    if not args.no_gps:
        command += ['--cf-gps', str(glaze/'tests/gps/track.nmea')]
    with (work/'stdout.log').open('w') as stdout, (work/'stderr.log').open('w') as stderr:
        p = subprocess.Popen(command, cwd=work, env=env, stdout=stdout, stderr=stderr)
        try:
            p.wait(timeout=args.seconds)
        except subprocess.TimeoutExpired:
            p.send_signal(signal.SIGTERM)
            p.wait(timeout=10)
    log = (work/'stdout.log').read_text(errors='replace')
    failures = [line for line in log.splitlines() if any(s in line for s in ('fault:', 'PANIC', 'dynamic linker:'))
                or re.search(r'proc: maps pid \d+ exited', line)]
    for line in log.splitlines():
        if any(s in line for s in ('gps:', 'sb: launch', 'fault:', 'PANIC', 'dynamic linker:')):
            print(line)
    if failures or p.returncode not in (0, -signal.SIGTERM):
        raise SystemExit(f'Emulator failed: exit={p.returncode}, errors={failures}')
    if not args.no_gps and 'gps: NMEA at 4800 baud' not in log:
        raise SystemExit('The GPS card was not found')
    if 'sb: launch' in log and 'maps' not in ''.join(l for l in log.splitlines() if 'sb: launch' in l):
        raise SystemExit('Maps was not launched')
    print(f'Artifacts: {work}')


if __name__ == '__main__':
    main()
