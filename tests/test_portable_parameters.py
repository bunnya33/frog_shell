"""Packaging parameter guards only; no application execution."""
import json,pathlib,sys,tempfile,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from portable_format import generate,source_inventory,parse_archive,sha256
class Parameters(unittest.TestCase):
    def test_frogshell_main_and_legacy_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);source=root/'source';source.mkdir();(source/'FrogShell.exe').write_bytes(b'frog fixture')
            manifest=root/'manifest.json';manifest.write_text(json.dumps(dict(version='0.1.39',files=source_inventory(source))),encoding='utf-8')
            result=generate(source,root/'generated',version='0.1.39',manifest=manifest)
            self.assertEqual(result['main_executable'],'FrogShell.exe');self.assertEqual(result['display_name'],'FrogShell')
            header=(root/'generated/payload_meta.h').read_text();self.assertIn('PORTABLE_MAIN_EXECUTABLE L"FrogShell.exe"',header)
            icon=pathlib.Path(__file__).resolve().parents[1]/'resources/branding/frogshell.ico'
            self.assertEqual((root/'generated/RabbitShell.ico').read_bytes(),icon.read_bytes())
            (source/'RabbitShell.exe').write_bytes(b'ambiguous')
            with self.assertRaises(ValueError):generate(source,root/'ambiguous',release=False,version='0.1.39')
    def test_new_manifest_and_legacy_guards(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);source=root/'source';source.mkdir();(source/'RabbitShell.exe').write_bytes(b'parameter test payload');(source/'license.txt').write_text('license',encoding='ascii')
            manifest=root/'manifest.json';manifest.write_text(json.dumps(dict(version='0.1.13',files=source_inventory(source))),encoding='utf-8')
            with self.assertRaises(ValueError):generate(source,root/'no-pin',version='0.1.13')
            with self.assertRaises(ValueError):generate(source,root/'legacy')
            with self.assertRaises(ValueError):generate(source,root/'wrong-version',version='0.1.14',manifest=manifest)
            result=generate(source,root/'generated',version='0.1.13',manifest=manifest)
            self.assertEqual(result['version'],'0.1.13');self.assertEqual(result['file_count'],2)
            payload=(root/'generated/payload.bin').read_bytes();self.assertEqual(sha256(payload),result['payload_sha256']);self.assertEqual(len(parse_archive(payload)),2)
            (source/'license.txt').write_text('tampered',encoding='ascii')
            with self.assertRaises(ValueError):generate(source,root/'changed',version='0.1.13',manifest=manifest)
if __name__=='__main__':unittest.main()
