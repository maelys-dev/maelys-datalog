"""Compile real negative-control libraries for the native artifact guard."""
import importlib.util
from pathlib import Path
import platform
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('binary', ROOT / 'tools/check_javascript_binary.py')
binary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(binary)


class JavaScriptBinary(unittest.TestCase):
    def test_unstripped_and_leaked_symbols_are_rejected(self):
        mac = platform.system() == 'Darwin'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source = path / 'fixture.c'
            # malloc supplies a real glibc version requirement on ELF.
            source.write_text('#include <stdlib.h>\n'
                              'void *napi_register_module_v1(void) { return malloc(16); }\n'
                              'int node_api_module_get_api_version_v1(void) { return 8; }\n')
            library = str(path / 'fixture.node')
            link = ['-bundle', '-mmacosx-version-min=13.5'] if mac else ['-shared', '-fPIC']
            subprocess.run(['cc', '-g', *link, str(source), '-o', library], check=True)
            with self.assertRaisesRegex(AssertionError, 'unstripped'):
                binary.check(library)
            subprocess.run(['strip', *(['-S', '-x'] if mac else ['--strip-unneeded']), library], check=True)
            binary.check(library)
            key = 'macosMinimum' if mac else 'glibcMaximum'
            saved = binary.POLICY[key]
            try:
                binary.POLICY[key] = '0.0'
                with self.assertRaisesRegex(AssertionError, 'target drift|floor exceeds'):
                    binary.check(library)
            finally:
                binary.POLICY[key] = saved
            source.write_text(source.read_text() + 'int leaked_engine_symbol(void) { return 1; }\n')
            subprocess.run(['cc', *link, str(source), '-o', library], check=True)
            subprocess.run(['strip', *(['-S', '-x'] if mac else ['--strip-unneeded']), library], check=True)
            with self.assertRaisesRegex(AssertionError, 'unexpected exports'):
                binary.check(library)


if __name__ == '__main__':
    unittest.main()
