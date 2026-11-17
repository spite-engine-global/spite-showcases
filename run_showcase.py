import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
SHOWCASES = {
    'warzone': ('WarzoneGame', 'Warzone/Warzone.json'),
    'city': ('CityShowcase', 'CityShowcase/CityShowcase.json'),
    'formations': ('FormationsShowcase', 'FormationsShowcase/FormationsShowcase.json'),
    'ui': ('UiStorm', 'UiStorm/UiStorm.json'),
}
parser = argparse.ArgumentParser(description='Launch a SPITE showcase')
parser.add_argument('showcase', choices=SHOWCASES)
args = parser.parse_args()
module, scene = SHOWCASES[args.showcase]
if args.showcase == 'warzone':
    audio = ROOT / 'assets_baked' / 'audio'
    if not audio.is_dir():
        parser.error('extract the release assets into assets_baked first')
    destination = ROOT / 'assets' / 'audio'
    destination.mkdir(parents=True, exist_ok=True)
    for source in audio.glob('*.wav'):
        target = destination / source.name
        if not target.exists():
            shutil.copy2(source, target)
ship = ROOT / 'ship.json'
original = ship.read_bytes()
config = json.loads(original)
config['modules'] = [module]
cli = Path(os.environ['LOCALAPPDATA']) / 'Spite' / 'bin' / 'spite.exe' if os.name == 'nt' else 'spite'
try:
    ship.write_text(json.dumps(config, indent=2) + '\n', encoding='utf-8')
    process = subprocess.Popen([str(cli), 'run'], cwd=ROOT, stdin=subprocess.PIPE)
    if scene:
        request = {'command': 'openScene', 'args': {'path': scene, 'force': True}}
        process.stdin.write((json.dumps(request) + '\n').encode())
        process.stdin.flush()
    raise SystemExit(process.wait())
finally:
    ship.write_bytes(original)
