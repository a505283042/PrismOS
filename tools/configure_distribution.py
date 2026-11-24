#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate distribution settings and embed a public PEM trust bundle at build time."""
import argparse
import json
from pathlib import Path
import ssl
from urllib.parse import urlsplit


def write_if_changed(path, text):
    if not path.exists() or path.read_text(encoding='ascii') != text:
        path.write_text(text, encoding='ascii')


def generate(config, output):
    data = json.loads(config.read_text(encoding='utf-8-sig'))
    source = data['source_url']
    url = urlsplit(source)
    if (url.scheme not in ('https', 'http') or not url.hostname or url.username or url.password
        or url.query or url.fragment or not source.isascii() or len(source) >= 192
        or any(ord(c) <= 32 for c in source)):
        raise ValueError('Expected HTTP(S) source URL without credentials/query/fragment, below 192 bytes')
    certificate = (config.parent / data['ca_bundle']).resolve(strict=True)
    pem = certificate.read_text(encoding='ascii')
    if 'PRIVATE KEY' in pem or len(pem) > 65536:
        raise ValueError('Expected a public certificate bundle below 64 KiB')
    ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT).load_verify_locations(cadata=pem)
    output.mkdir(parents=True, exist_ok=True)
    write_if_changed(output/'aster_distribution_config.h',
        '#pragma once\n#define ASTER_SETUP_DEFAULT_SOURCE '+json.dumps(source)+'\n')
    write_if_changed(output/'aster_http_trust_bundle.inc',
        ''.join(json.dumps(line+'\n')+'\n' for line in pem.splitlines()))
    return certificate


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print(generate(args.config.resolve(), args.output))
