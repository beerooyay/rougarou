"""Stage Pages with one immutable module URL namespace per release."""
import argparse
import re
import shutil
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('destination', type=Path)
parser.add_argument('revision')
args = parser.parse_args()
if not re.fullmatch(r'[a-zA-Z0-9_-]+', args.revision):
    parser.error('revision must be a URL-safe build identifier')
source = Path(__file__).resolve().parents[1] / 'web'
shutil.copytree(source, args.destination)
pattern = re.compile(r'''(['"])(\.{1,2}/[^'"\s?]+\.(?:js|json|css))(?:\?[^'"\s]*)?\1''')
for path in args.destination.rglob('*'):
    if path.suffix not in ('.js', '.html', '.mjs'):
        continue
    text = pattern.sub(lambda m: f'{m[1]}{m[2]}?v={args.revision}{m[1]}', path.read_text())
    if path.name == 'index.html':
        text = text.replace('<head>', f'<head>\n<meta name="rougarou-build" content="{args.revision}">')
    path.write_text(text)
print(f'Staged {args.revision} at {args.destination}')
