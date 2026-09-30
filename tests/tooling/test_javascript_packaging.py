"""Artifact assembly refuses mismatched provenance and substituted common bytes."""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
VERSION = (ROOT / 'VERSION').read_text().strip()
COMMIT = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
TARGETS = {'linux-x86_64': 'linux-x64', 'linux-arm64': 'linux-arm64', 'macos-arm64': 'darwin-arm64', 'wasm32': None}

class JavaScriptPackaging(unittest.TestCase):
    def assets(self, directory, changed=None):
        package = dict(name='@maelys-dev/datalog', version=VERSION, license='MPL-2.0', files=['src', 'prebuilds', 'wasm'])
        for target, platform in TARGETS.items():
            files = {'package.json': json.dumps(package).encode(), 'src/index.d.ts': b'export class Engine {}\n'}
            for profile in ('small', 'large'):
                if platform:
                    files[f'prebuilds/{platform}/{profile}.node'] = b'fixture, not a native library'
                else:
                    files[f'wasm/{profile}/engine.mjs'] = b'fixture, never executed'
                    files[f'wasm/{profile}/engine.wasm'] = b'fixture, not a WASM module'
            if changed == 'common' and target == 'wasm32': files['src/index.d.ts'] += b'// substituted\n'
            if changed == 'missing' and target == 'linux-arm64': del files['prebuilds/linux-arm64/large.node']
            filename = f'maelys-datalog-{VERSION}-javascript-{target}.tar.gz'
            path = directory / filename
            with tarfile.open(path, 'w:gz') as archive:
                for name, data in files.items():
                    member = tarfile.TarInfo(name); member.size = len(data); archive.addfile(member, io.BytesIO(data))
            receipt = dict(version=VERSION, tag=f'v{VERSION}', commit=COMMIT, target=target,
                           artifacts=[dict(file=filename, sha256=hashlib.sha256(path.read_bytes()).hexdigest())])
            if changed == 'commit' and target == 'macos-arm64': receipt['commit'] = '0' * 40
            if changed == 'hash' and target == 'wasm32': receipt['artifacts'][0]['sha256'] = '0' * 64
            (directory / f'release-receipt-{target}.json').write_text(json.dumps(receipt))

    def test_inconsistent_released_inputs_are_refused_before_npm_pack(self):
        for mutation, message in [('common', 'common package bytes disagree'), ('commit', 'build commit differs'),
                                  ('hash', 'receipt mismatch'), ('missing', 'incomplete payload')]:
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as path:
                directory = Path(path); self.assets(directory, mutation)
                result = subprocess.run(['python3', 'scripts/build-javascript-package.py', path], cwd=ROOT, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.assertFalse(list(directory.glob('*.tgz')))

if __name__ == '__main__': unittest.main()
