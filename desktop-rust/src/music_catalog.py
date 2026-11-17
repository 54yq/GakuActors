"""Read a local Octo manifest and locate cached game music.

Manifest format and key derivation: kishidanatsumi/gkmasToolkit,
ManifestDecryptor.py and octodb.proto (see README.md). No network requests.
Requires PyCryptodome; export additionally requires ffmpeg on PATH.
"""
import argparse
import hashlib
import json
import struct
import subprocess
import tempfile
import shutil
import sys
import os
import re
import urllib.request
from pathlib import Path

if '__file__' in globals():
    sys.path.insert(0, str(Path(__file__).resolve().parent / 'GakuActors.Desktop.deps'))
from Crypto.Cipher import AES
from Crypto.Util.Padding import unpad


def fields(buf):
    pos = 0
    def varint(pos):
        value = shift = 0
        while True:
            if pos >= len(buf) or shift > 63:
                raise ValueError('invalid protobuf varint')
            byte = buf[pos]
            pos += 1
            value |= (byte & 127) << shift
            if byte < 128:
                return value, pos
            shift += 7
    while pos < len(buf):
        tag, pos = varint(pos)
        number, wire = tag >> 3, tag & 7
        if not number:
            raise ValueError('invalid protobuf field')
        if wire == 0:
            value, pos = varint(pos)
        elif wire == 2:
            length, pos = varint(pos)
            if length > len(buf) - pos:
                raise ValueError('truncated protobuf field')
            value = buf[pos:pos + length]
            pos += length
        elif wire in (1, 5):
            size = 8 if wire == 1 else 4
            value = buf[pos:pos + size]
            if len(value) != size:
                raise ValueError('truncated protobuf field')
            pos += size
        else:
            raise ValueError('unsupported protobuf wire type')
        yield number, value


def read_catalog(manifest, cache):
    data = manifest.read_bytes()
    if len(data) < 17 or data[0] != 1:
        raise ValueError('unrecognized manifest header')
    key = hashlib.md5(b'1nuv9td1bw1udefk').digest()
    iv = hashlib.md5(b'LvAUtf+tnz').digest()
    data = unpad(AES.new(key, AES.MODE_CBC, iv).decrypt(data[1:]), 16)
    if data[:16] != hashlib.md5(data[16:]).digest():
        raise ValueError('manifest checksum mismatch')
    revision = None
    result = []
    for number, value in fields(data[16:]):
        if number == 1:
            revision = value
        if number not in (2, 4):
            continue
        item = dict(fields(value))
        name = item.get(3, b'').decode('utf-8')
        is_live = number == 2 and name.startswith('sud_music_live_')
        is_general = number == 4 and name.startswith('sud_music') and name.endswith(('.awb', '.acb'))
        if not is_live and not is_general:
            continue
        resource_id = item[1]
        digest = item[10].decode('ascii')
        if len(digest) != 32 or any(c not in '0123456789abcdef' for c in digest):
            raise ValueError('invalid resource hash')
        folder = (('A' if is_live else 'R') + str(resource_id)).encode('ascii').hex().upper()
        path = cache / str(resource_id % 10) / folder / digest
        own_path = cache.parents[2] / 'GakuLoader' / 'Cache' / 'gakuactors' / 'music' / digest
        chosen = path if path.is_file() else own_path
        obj = item.get(11, b'').decode('ascii')
        result.append({'id': resource_id, 'name': name, 'size': item.get(4),
                       'md5': digest, 'object_name': obj, 'path': str(chosen),
                       'game_path': str(path),
                       'cached': chosen.is_file(), 'kind': 'live' if is_live else 'general'})
    return revision, result


def add_titles(entries, game):
    path = game / 'gakumas-local' / 'local-files' / 'masterTrans' / 'Music.json'
    try:
        values = json.loads(path.read_text(encoding='utf-8'))['data']
        titles = {row['id']: row.get('title', '') for row in values if isinstance(row, dict) and 'id' in row}
    except (OSError, ValueError, KeyError, TypeError):
        titles = {}
    try:
        characters = json.loads((game / 'gakumas-local' / 'local-files' / 'masterTrans' / 'Character.json').read_text(encoding='utf-8'))['data']
        performers = {row['id']: row.get('lastName', '') + row.get('firstName', '') for row in characters if isinstance(row, dict) and 'id' in row}
    except (OSError, ValueError, KeyError, TypeError):
        performers = {}
    for item in entries:
        performer = ''
        match = re.match(r'^sud_music_(?:live|general)_([a-z0-9]+)-(\d{3})-([a-z0-9]+)(?:_|-|\.)', item['name'])
        title = ''
        if match:
            category, number, performer = match.groups()
            if category == 'all':
                title = titles.get(f'music-all-{performer}-{number}', '')
                if not title:
                    title = titles.get(f'music-all-{number}-inst', '').removesuffix(' [Instrumental]')
            elif category == performer:
                title = titles.get(f'music-char-{category}-{number}', '')
        item['title'] = title
        item['performer'] = performers.get(performer, '')


def hca_streams(path):
    """Return bounded stream slices from an AFS2 archive, without modifying it."""
    data = path.read_bytes()
    if data[:4] != b'AFS2' or len(data) < 16:
        raise ValueError('not an AFS2 archive')
    offset_size, id_size = data[5], data[6]
    count, alignment = struct.unpack_from('<IH', data, 8)
    if offset_size not in (2, 4, 8) or id_size not in (2, 4) or not count or count > 4096 or not alignment:
        raise ValueError('unsupported AFS2 header')
    start = 16 + count * id_size
    end = start + (count + 1) * offset_size
    if end > len(data):
        raise ValueError('truncated AFS2 table')
    offsets = [int.from_bytes(data[start+i*offset_size:start+(i+1)*offset_size], 'little') for i in range(count+1)]
    streams = []
    for index in range(count):
        begin = (offsets[index] + alignment - 1) // alignment * alignment
        finish = offsets[index+1]
        if begin < end or finish > len(data) or begin >= finish:
            raise ValueError('invalid AFS2 offsets')
        stream = data[begin:finish]
        if stream[:4] != b'HCA\0':
            raise ValueError('stream is not an unmasked HCA file')
        streams.append(stream)
    return streams


def unmask_bundle(data, name):
    """Undo the name-derived mask on the first 256 bytes (gkmasToolkit)."""
    if data.startswith(b'UnityFS'):
        return data
    raw = name.encode('ascii')
    if not raw or len(data) < 256:
        raise ValueError('invalid Unity asset bundle')
    mask = bytearray(len(raw) * 2)
    for i, byte in enumerate(raw):
        mask[i * 2] = byte
        mask[-1 - i * 2] = (~byte) & 255
    seed = 0x9b
    for byte in mask:
        seed = (((seed & 1) << 7) | (seed >> 1)) ^ byte
    mask = bytes(x ^ seed for x in mask)
    result = bytearray(data)
    for i in range(256):
        result[i] ^= mask[i % len(mask)]
    if not result.startswith(b'UnityFS'):
        raise ValueError('asset bundle header did not decode')
    return bytes(result)


def unity_audio(data, name):
    try:
        import UnityPy
    except ImportError as exc:
        raise ValueError('UnityPy is required to export Live audio') from exc
    UnityPy.config.FALLBACK_UNITY_VERSION = '6000.0.77f1'
    env = UnityPy.load(unmask_bundle(data, name))
    samples = []
    for obj in env.objects:
        if obj.type.name == 'AudioClip':
            samples.extend(obj.read().samples.values())
    if len(samples) != 1:
        raise ValueError('expected exactly one audio sample in the Live bundle')
    if samples[0][:4] != b'RIFF' or samples[0][8:12] != b'WAVE':
        raise ValueError('Live audio decoder did not produce a WAV file')
    return samples[0]


def download(item, game):
    """Download only the selected catalog object into a separate plugin cache."""
    obj = item.get('object_name', '')
    size = item.get('size')
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,128}', obj) or not isinstance(size, int) or not 0 < size <= 100 * 1024 * 1024:
        raise ValueError('invalid remote resource metadata')
    directory = game / 'GakuLoader' / 'Cache' / 'gakuactors' / 'music'
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / item['md5']
    if target.is_file():
        if target.stat().st_size == size and hashlib.md5(target.read_bytes()).hexdigest() == item['md5']:
            return target
        raise ValueError('existing plugin cache file failed verification')
    temp = None
    try:
        with tempfile.NamedTemporaryFile(prefix='gkm-', suffix='.part', dir=directory, delete=False) as out:
            temp = Path(out.name)
            request = urllib.request.Request('https://object.asset.game-gakuen-idolmaster.jp/' + obj, headers={'User-Agent': 'GakuActors/0.4'})
            digest = hashlib.md5()
            length = 0
            with urllib.request.urlopen(request, timeout=30) as source:
                while True:
                    chunk = source.read(1024 * 1024)
                    if not chunk:
                        break
                    length += len(chunk)
                    if length > size:
                        raise ValueError('download is larger than the manifest entry')
                    digest.update(chunk)
                    out.write(chunk)
        if length != size or digest.hexdigest() != item['md5']:
            raise ValueError('download failed manifest size or hash verification')
        if target.exists():
            if target.stat().st_size != size or hashlib.md5(target.read_bytes()).hexdigest() != item['md5']:
                raise ValueError('plugin cache changed during download')
        else:
            os.replace(temp, target)
        return target
    finally:
        if temp is not None and temp.exists():
            temp.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True, help='game root directory')
    parser.add_argument('--manifest', type=Path, help='specific octocacheevai; default: newest numeric version')
    parser.add_argument('--json', type=Path, help='write an inventory to this path')
    parser.add_argument('--filter', default='', help='case-insensitive substring in resource name')
    parser.add_argument('--export', help='exact AWB name to decode to PCM WAV')
    parser.add_argument('--download', help='download one exact audio resource to the library cache')
    parser.add_argument('--output', type=Path, help='WAV output path; must not already exist')
    args = parser.parse_args()
    if bool(args.export) != bool(args.output):
        parser.error('--export and --output must be used together')
    manifest = args.manifest
    if manifest is None:
        candidates = list((args.game / 'octo' / 'pdb' / '400').glob('*/octocacheevai'))
        candidates = [p for p in candidates if p.parent.name.isdecimal()]
        if not candidates:
            raise FileNotFoundError('no local manifest found')
        manifest = max(candidates, key=lambda p: int(p.parent.name))
    revision, entries = read_catalog(manifest, args.game / 'octo' / 'v1' / '400')
    add_titles(entries, args.game)
    by_name = {item['name']: item for item in entries}
    if args.download:
        item = by_name.get(args.download)
        if not item or not (item['name'].endswith('.awb') or item['kind'] == 'live'):
            raise ValueError('the selected audio is not in the manifest')
        if not item['cached']:
            download(item, args.game)
        return
    awbs = [item for item in entries if item['name'].endswith('.awb') or item['kind'] == 'live']
    awbs.sort(key=lambda item: item['name'])
    for item in awbs:
        acb = by_name.get(item['name'][:-4] + '.acb') if item['kind'] == 'general' else None
        item['acb_path'] = acb['path'] if acb else None
        item['acb_cached'] = bool(acb and acb['cached'])
    if args.json:
        payload = json.dumps({'manifest': str(manifest), 'revision': revision, 'songs': awbs}, ensure_ascii=False)
        if str(args.json) == '-':
            print(payload)
            return
        args.json.write_text(payload, encoding='utf-8')
    available = [item for item in awbs if item['cached'] and item['acb_cached']]
    print('manifest:', manifest, 'revision:', revision)
    print('music resources:', len(entries), 'cached pairs:', len(available))
    for item in available:
        if args.filter.lower() in item['name'].lower():
            print(item['name'], item['path'])
    if args.export:
        item = by_name.get(args.export)
        if not item or not (item['name'].endswith('.awb') or item['kind'] == 'live'):
            raise ValueError('the selected audio is not in the manifest')
        if args.output.exists():
            raise FileExistsError(args.output)
        source_path = Path(item['path']) if item['cached'] else download(item, args.game)
        with source_path.open('rb') as source:
            digest = hashlib.md5()
            for chunk in iter(lambda: source.read(1024 * 1024), b''):
                digest.update(chunk)
        if digest.hexdigest() != item['md5']:
            raise ValueError('cached AWB does not match the manifest hash')
        if item['kind'] == 'live':
            wav = unity_audio(source_path.read_bytes(), item['name'])
            with args.output.open('xb') as out:
                out.write(wav)
            print('exported:', args.output)
            return
        streams = hca_streams(source_path)
        if len(streams) != 1:
            raise ValueError('multi-stream AWB needs explicit stream selection')
        decoder = args.game / 'GakuActors' / 'GakuActors.Vgmstream' / 'vgmstream-cli.exe'
        if not decoder.is_file():
            found = shutil.which('vgmstream-cli')
            if not found:
                raise ValueError('vgmstream HCA decoder not installed')
            decoder = Path(found)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'audio.hca'
            dst = Path(tmp) / 'audio.wav'
            src.write_bytes(streams[0])
            subprocess.run([str(decoder), '-o', str(dst), str(src)], check=True)
            with args.output.open('xb') as out, dst.open('rb') as inp:
                shutil.copyfileobj(inp, out)
        print('exported:', args.output)


if __name__ == '__main__':
    main()
