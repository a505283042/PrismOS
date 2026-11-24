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
            # Slot B has a valid newer header but a damaged payload. The
            # firmware must fall back to verified generation 5 (slot A).
            damaged = bytearray(catalog_bytes(ROWS, 6))
            damaged[-1] ^= 0x40
            b.write_bytes(damaged)

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

    @unittest.skipUnless(shutil.which("cc"), "host C compiler not found")
    def test_native_queue_uses_mounted_sd_paths(self):
        """Compile the production (non-HOST_TEST) queue against a tiny OS
        adapter. Catch regressions where native /SD: paths are opened through
        a POSIX-only /SD alias instead of the shared Aster filesystem."""
        import os

        with tempfile.TemporaryDirectory() as dirname:
            root = Path(dirname)
            catalog_dir = root / "aster/apps/aster.hifi/data"
            catalog_dir.mkdir(parents=True)
            music_dir = root / "music"
            music_dir.mkdir()
            for song in ROWS:
                track = music_dir / song
                track.parent.mkdir(parents=True, exist_ok=True)
                track.write_bytes(b"audio")
            (catalog_dir / "hifi_catalog_a.bin").write_bytes(catalog_bytes(ROWS, 12))

            (root / "aster_os.h").write_text(r'''
#ifndef TEST_ASTER_OS_H
#define TEST_ASTER_OS_H
#include <stddef.h>
#include <stdint.h>
enum { ASTER_OS_READ = 1, ASTER_OS_SEEK_SET = 0, ASTER_OS_FILE = 1 };
typedef struct { void *handle; } aster_os_file_t;
typedef struct { uint64_t size; uint32_t type; char name[256]; } aster_os_entry_t;
int aster_os_file_open(aster_os_file_t *, const char *, unsigned);
int aster_os_file_close(aster_os_file_t *);
int aster_os_file_read(aster_os_file_t *, void *, size_t);
int aster_os_file_seek(aster_os_file_t *, int64_t, unsigned);
int aster_os_stat(const char *, aster_os_entry_t *);
#endif
''', encoding="utf-8")
            (root / "aster_power.h").write_text(r'''
int aster_power_activity_begin(void);
void aster_power_activity_end(void);
''', encoding="utf-8")
            (root / "mock_native.c").write_text(r'''
#include "aster_os.h"
#include "aster_power.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static int resolve(const char *path, char out[1024]) {
    const char *root = getenv("HIFI_TEST_SD");
    if (!root || strncmp(path, "/SD:", 4)) return -EINVAL;
    int n = snprintf(out, 1024, "%s%s", root, path + 4);
    return n > 0 && n < 1024 ? 0 : -ENAMETOOLONG;
}
int aster_os_file_open(aster_os_file_t *f, const char *path, unsigned flags) {
    char name[1024];
    if (flags != ASTER_OS_READ || resolve(path, name)) return -EINVAL;
    f->handle = fopen(name, "rb");
    return f->handle ? 0 : -ENOENT;
}
int aster_os_file_close(aster_os_file_t *f) {
    int rc = fclose((FILE *)f->handle);
    f->handle = NULL;
    return rc ? -EIO : 0;
}
int aster_os_file_read(aster_os_file_t *f, void *buf, size_t bytes) {
    return (int)fread(buf, 1, bytes, (FILE *)f->handle);
}
int aster_os_file_seek(aster_os_file_t *f, int64_t offset, unsigned whence) {
    if (whence != ASTER_OS_SEEK_SET) return -EINVAL;
    return fseek((FILE *)f->handle, (long)offset, SEEK_SET) ? -EIO : 0;
}
int aster_os_stat(const char *path, aster_os_entry_t *entry) {
    char name[1024];
    struct stat st;
    if (resolve(path, name) || stat(name, &st)) return -ENOENT;
    memset(entry, 0, sizeof(*entry));
    entry->size = (uint64_t)st.st_size;
    entry->type = S_ISREG(st.st_mode) ? ASTER_OS_FILE : 2;
    return 0;
}
int aster_power_activity_begin(void) { return 0; }
void aster_power_activity_end(void) { }
''', encoding="utf-8")
            (root / "test_native.c").write_text(r'''
#include "hifi_playlist.h"
#include <assert.h>
#include <string.h>
int main(void) {
    char next[ASTER_MEDIA_PATH_MAX] = {0};
    assert(hifi_playlist_choose("/SD:/music/a.mp3",
            HIFI_QUEUE_SEQUENCE, 1, true, next));
    assert(!strcmp(next, "/SD:/music/sub/b.flac"));
    assert(hifi_playlist_choose("/SD:/music/a.mp3",
            HIFI_QUEUE_REPEAT_ALL, -1, false, next));
    assert(!strcmp(next, "/SD:/music/sub/d.mp3"));
    assert(hifi_playlist_choose("/SD:/music/sub/b.flac",
            HIFI_QUEUE_REPEAT_ONE, 1, true, next));
    assert(!strcmp(next, "/SD:/music/sub/b.flac"));
    return 0;
}
''', encoding="utf-8")
            binary = root / "test_native"
            subprocess.run(
                ["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                 "-Wall", "-Wextra", "-Werror", "-I" + str(root),
                 "-I" + str(QUEUE_INCLUDE), "-I" + str(MEDIA_INCLUDE),
                 str(root / "test_native.c"), str(root / "mock_native.c"),
                 str(C_SOURCE), "-o", str(binary)],
                check=True, capture_output=True, text=True)
            env = os.environ.copy()
            env["HIFI_TEST_SD"] = str(root)
            subprocess.run([str(binary)], check=True, capture_output=True,
                           text=True, timeout=10, env=env)


if __name__ == "__main__":
    unittest.main()
