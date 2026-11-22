"""Host C regression tests for firmware-owned global HIF2 playlist.

Run with: python -m unittest discover -s tests -p test_hifi_playlist.py -v
No ESP32 hardware or TF card is touched. The fixture rewrites only C test
paths to a temporary tree and compiles the actual firmware queue source.
"""
import json
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
C_SOURCE = ROOT / "systems/espidf/components/aster_audio/hifi_playlist.c"
QUEUE_INCLUDE = ROOT / "systems/espidf/components/aster_audio"
MEDIA_INCLUDE = ROOT / "common/audio"
ROWS = ["a.mp3", "sub/b.flac", "c.wav", "sub/d.mp3"]


def catalog_bytes(paths, generation):
    rows = b"".join(struct.pack("<Q152s", 8, p.encode("utf-8")) for p in paths)
    prefix = struct.pack("<8I", 0x32464948, 2, generation, len(paths),
                         zlib.crc32(rows), 0, 0, 0)
    return prefix + struct.pack("<I", zlib.crc32(prefix)) + rows


@unittest.skipUnless(shutil.which("cc"), "host C compiler not found")
class Playlist(unittest.TestCase):
    def test_recursive_order_repeat_shuffle_and_corrupt_slot(self):
        with tempfile.TemporaryDirectory() as dirname:
            root = Path(dirname)
            music = root / "music"
            music.mkdir()
            for song in ROWS:
                path = music / song
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"testaudio")

            a, b = root / "hifi_catalog_a.bin", root / "hifi_catalog_b.bin"
            a.write_bytes(catalog_bytes(ROWS, 5))
            b.write_bytes(b"partial header")

            native = C_SOURCE.read_text(encoding="utf-8")
            native = native.replace(
                '"/SD/aster/apps/aster.hifi/data/hifi_catalog_a.bin"',
                json.dumps(str(a)))
            native = native.replace(
                '"/SD/aster/apps/aster.hifi/data/hifi_catalog_b.bin"',
                json.dumps(str(b)))
            native = native.replace('"/SD/music/%s"',
                                    json.dumps(str(music) + "/%s"))
            assert str(a) in native and str(b) in native
            implementation = root / "hifi_playlist.c"
            implementation.write_text(native, encoding="utf-8")

            code = r'''
#include "hifi_playlist.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static char out[ASTER_MEDIA_PATH_MAX];
static const char *names[] = {
    "/SD:/music/a.mp3", "/SD:/music/sub/b.flac",
    "/SD:/music/c.wav", "/SD:/music/sub/d.mp3"
};
static unsigned index_of(const char *path)
{
    for (unsigned i=0;i<4;i++)if(!strcmp(names[i],path))return i;
    assert(!"unknown song");
    return 0;
}
int main(void)
{
    assert(hifi_playlist_choose(names[0],HIFI_QUEUE_SEQUENCE,1,true,out));
    assert(!strcmp(out,names[1])); /* folder path, not root-only scan */
    assert(!hifi_playlist_choose(names[3],HIFI_QUEUE_SEQUENCE,1,true,out));
    assert(hifi_playlist_choose(names[3],HIFI_QUEUE_REPEAT_ALL,1,true,out));
    assert(!strcmp(out,names[0]));
    assert(hifi_playlist_choose(names[2],HIFI_QUEUE_REPEAT_ONE,1,true,out));
    assert(!strcmp(out,names[2]));
    assert(hifi_playlist_choose(names[2],HIFI_QUEUE_REPEAT_ONE,1,false,out));
    assert(!strcmp(out,names[3]));
    assert(hifi_playlist_choose(names[0],HIFI_QUEUE_REPEAT_ALL,-1,false,out));
    assert(!strcmp(out,names[3]));

    hifi_playlist_seed(12345);
    hifi_playlist_reset_shuffle();
    const char *current=names[0];
    unsigned visited=1u<<0;
    const char *history[4]={current,0,0,0};
    for(unsigned n=1;n<4;n++) {
        assert(hifi_playlist_choose(current,HIFI_QUEUE_SHUFFLE,1,true,out));
        unsigned index=index_of(out);
        assert(!(visited & (1u<<index)));  /* no repeat in a cycle */
        visited|=1u<<index;
        history[n]=names[index];
        current=history[n];
    }
    assert(visited==15);
    assert(hifi_playlist_choose(current,HIFI_QUEUE_SHUFFLE,-1,false,out));
    assert(!strcmp(out,history[2]));
    assert(hifi_playlist_choose(history[2],HIFI_QUEUE_SHUFFLE,1,false,out));
    assert(!strcmp(out,history[3]));
    puts("HIF2 global queue OK");
    return 0;
}
'''
            harness = root / "test.c"
            harness.write_text(code, encoding="utf-8")
            executable = root / "test"
            cmd = ["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                   "-DHIFI_PLAYLIST_HOST_TEST",
                   "-Wall", "-Wextra", "-Werror", "-I" + str(QUEUE_INCLUDE),
                   "-I" + str(MEDIA_INCLUDE), str(harness),
                   str(implementation), "-o", str(executable)]
            subprocess.run(cmd, check=True, capture_output=True, text=True)
            subprocess.run([str(executable)], check=True, capture_output=True,
                           text=True, timeout=8)


if __name__ == "__main__":
    unittest.main()
