import importlib.util
from pathlib import Path
import tempfile
import unittest
import json

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('distribution_config',ROOT/'tools/configure_distribution.py')
config=importlib.util.module_from_spec(spec);spec.loader.exec_module(config)


class DistributionConfig(unittest.TestCase):
    def test_default_and_custom_settings_generate_trust_bundle(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'out'
            pem=config.generate(ROOT/'config/distribution.json',out)
            self.assertIn('ASTER_SETUP_DEFAULT_SOURCE',(out/'aster_distribution_config.h').read_text())
            generated=''.join(json.loads(line) for line in (out/'aster_http_trust_bundle.inc').read_text().splitlines())
            self.assertEqual(generated,pem.read_text())
            custom=Path(folder)/'settings.json'
            custom.write_text(json.dumps({'source_url':'https://example.com/channel.json','ca_bundle':str(pem)}))
            config.generate(custom,out)
            self.assertIn('https://example.com/channel.json',(out/'aster_distribution_config.h').read_text())
    def test_invalid_source_or_certificate_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'settings.json'
            for source in ['https://user:pass@example.com/x','https://example.com/?secret=x','https://example.com/\n']:
                p.write_text(json.dumps({'source_url':source,'ca_bundle':str(ROOT/'config/certificates/roots.pem')}))
                with self.assertRaises(ValueError):config.generate(p,Path(folder)/'out')
            cert=Path(folder)/'bad.pem';cert.write_text('-----BEGIN PRIVATE KEY-----\n')
            p.write_text(json.dumps({'source_url':'https://example.com/x','ca_bundle':str(cert)}))
            with self.assertRaises(ValueError):config.generate(p,Path(folder)/'out')
