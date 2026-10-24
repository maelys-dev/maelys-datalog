"""Negative controls for the artifact retention guard."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('retention', ROOT / 'tools/check_artifact_retention.py')
retention = importlib.util.module_from_spec(spec)
spec.loader.exec_module(retention)

STEPS = """jobs:
  build:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
{upload}
      - name: Afterwards
        run: echo done
        # retention-days: 3 belongs to no upload here
"""


class ArtifactRetention(unittest.TestCase):
    def errors(self, upload, name='ci.yml'):
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / name
            path.write_text(STEPS.format(upload=upload))
            return [error for p in retention.workflows(Path(scratch)) for error in retention.check(p)]

    def test_accepts_named_and_bare_steps(self):
        self.assertEqual(self.errors("""      - name: Keep
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: kept
          path: out/
          retention-days: 3"""), [])
        self.assertEqual(self.errors("""      - uses: actions/upload-artifact@v4
        with:
          name: kept
          path: out/
          retention-days: 14  # the longest allowed"""), [])

    def test_refuses_missing_retention(self):
        errors = self.errors("""      - name: Keep
        uses: actions/upload-artifact@v4
        with:
          name: kept
          path: out/""")
        self.assertEqual(len(errors), 1)
        self.assertIn('without retention-days', errors[0])

    def test_refuses_long_zero_and_computed_retention(self):
        for value, message in (('30', 'outside 1..14'), ('0', 'outside 1..14'),
                               ('${{ inputs.days }}', 'literal integer')):
            errors = self.errors(f"""      - uses: actions/upload-artifact@v4
        with:
          name: kept
          path: out/
          retention-days: {value}""")
            self.assertEqual(len(errors), 1, value)
            self.assertIn(message, errors[0])

    def test_retention_of_another_step_does_not_count(self):
        errors = self.errors("""      - uses: actions/upload-artifact@v4
        with:
          name: kept
          path: out/
      - uses: actions/download-artifact@v4
        with:
          name: kept
          retention-days: 3""")
        self.assertEqual(len(errors), 1)

    def test_generated_release_workflow_is_not_read(self):
        self.assertEqual(self.errors("""      - uses: actions/upload-artifact@v4
        with:
          name: kept""", name='release.yml'), [])

    def test_repository_workflows_pass(self):
        files = retention.workflows(ROOT / '.github/workflows')
        self.assertTrue(files)
        self.assertEqual([error for p in files for error in retention.check(p)], [])


if __name__ == '__main__':
    unittest.main()
