"""Persistent official icon cache. Offline reads; explicit prefetch may download.

The Octo cache is read-only. Verified bundles and PNGs are stored in the plugin
cache using the manifest MD5 and an exact asset name as their identity.
"""
import argparse
import base64
import concurrent.futures
import hashlib
import io
import json
import os
import re
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path
from music_catalog import AES, unpad, fields, unmask_bundle

MAX_BUNDLE = 16 * 1024 * 1024
MAX_PNG = 512 * 1024
MODEL_ICON = re.compile(r'^img_cos_[A-Za-z0-9_-]+_(?:body|head)$')
CDN = 'https://object.asset.game-gakuen-idolmaster.jp/'


def cache_directory(game):
    return game / 'GakuLoader/Cache/gakuactors/model-icons'


def latest_manifest(game):
    manifests = [p for p in (game / 'octo/pdb/400').glob('*/octocacheevai') if p.parent.name.isdecimal()]
    if not manifests:
        raise ValueError('找不到本地游戏资源清单')
    return max(manifests, key=lambda p: int(p.parent.name))


def manifest_stamp(game):
    path = latest_manifest(game)
    stat = path.stat()
    return '{}:{}:{}'.format(path.parent.name, stat.st_size, stat.st_mtime_ns)


def make_record(game, cache, name, rid, digest, size, object_name):
    key = ('A' + str(rid)).encode('ascii').hex().upper()
    path = game / 'octo/v1/400' / str(rid % 10) / key / digest
    if not path.is_file():
        path = game / 'octo/v1/400' / ('A' + digest)
    png_key = digest + '-' + hashlib.sha256(name.encode()).hexdigest()[:16]
    return {'name': name, 'id': rid, 'md5': digest, 'size': size,
            'object_name': object_name, 'game_path': path,
            'bundle': cache / 'bundles' / (digest + '.bundle'),
            'png': cache / 'png' / (png_key + '.png'),
            'meta': cache / 'png' / (png_key + '.json'), 'cache': cache}


def icon_index(game, cache=None):
    cache = Path(cache) if cache is not None else cache_directory(game)
    manifest = latest_manifest(game)
    if manifest.stat().st_size > 64 * 1024 * 1024:
        raise ValueError('游戏资源清单过大')
    raw = manifest.read_bytes()
    if len(raw) < 17 or raw[0] != 1:
        raise ValueError('不支持的资源清单格式')
    data = unpad(AES.new(hashlib.md5(b'1nuv9td1bw1udefk').digest(), AES.MODE_CBC,
                        hashlib.md5(b'LvAUtf+tnz').digest()).decrypt(raw[1:]), 16)
    if len(data) < 16 or hashlib.md5(data[16:]).digest() != data[:16]:
        raise ValueError('资源清单校验失败')
    index = {}
    for number, value in fields(data[16:]):
        if number != 2:
            continue
        item = dict(fields(value))
        name, rid = item.get(3, b'').decode('utf-8'), item.get(1)
        digest = item.get(10, b'').decode('ascii')
        if not name.startswith('img_') or not isinstance(rid, int) or not re.fullmatch(r'[a-f0-9]{32}', digest):
            continue
        record = make_record(game, cache, name, rid, digest, item.get(4), item.get(11, b'').decode('ascii'))
        index.setdefault(name, []).append(record)
    return index


def write_index(game, index):
    records = [{key: values[0][key] for key in ('name', 'id', 'md5', 'size', 'object_name')}
               for name, values in index.items() if MODEL_ICON.fullmatch(name) and len(values) == 1]
    atomic_write(cache_directory(game) / 'index.json', json.dumps(
        {'version': 1, 'manifest': manifest_stamp(game), 'records': records}, ensure_ascii=False).encode('utf-8'))


def fast_index(game):
    try:
        path = cache_directory(game) / 'index.json'
        if path.stat().st_size > 4 * 1024 * 1024:
            return None
        saved = json.loads(path.read_text(encoding='utf-8'))
        if not isinstance(saved, dict) or saved.get('version') != 1 or saved.get('manifest') != manifest_stamp(game):
            return None
        index = {}
        for row in saved['records']:
            if (not MODEL_ICON.fullmatch(row['name']) or not isinstance(row['id'], int) or
                    not re.fullmatch(r'[a-f0-9]{32}', row['md5']) or row['name'] in index):
                return None
            record = make_record(game, cache_directory(game), row['name'], row['id'], row['md5'], row['size'], row['object_name'])
            index[row['name']] = [record]
        return index
    except (OSError, ValueError, TypeError, KeyError):
        return None


def record_for(index, name):
    if not isinstance(name, str) or not name.startswith('img_') or len(name) > 256:
        raise ValueError('该模型没有官方图标记录')
    matches = index.get(name, [])
    if len(matches) != 1:
        raise ValueError('官方图标未收录或匹配不唯一')
    return matches[0]


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = None
    try:
        with tempfile.NamedTemporaryFile(prefix='icon-', suffix='.part', dir=path.parent, delete=False) as out:
            temp = Path(out.name)
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
        os.replace(temp, path)
    finally:
        if temp is not None and temp.exists():
            temp.unlink()


def cached_png(record):
    try:
        if not 8 <= record['png'].stat().st_size <= MAX_PNG or record['meta'].stat().st_size > 4096:
            return None
        meta = json.loads(record['meta'].read_text(encoding='utf-8'))
        if not isinstance(meta, dict):
            return None
        data = record['png'].read_bytes()
        if (meta.get('version') != 1 or meta.get('name') != record['name'] or
                meta.get('source_md5') != record['md5'] or
                meta.get('png_sha256') != hashlib.sha256(data).hexdigest() or
                not data.startswith(b'\x89PNG\r\n\x1a\n')):
            return None
        return data
    except (OSError, ValueError, TypeError):
        return None


def bundle_bytes(record):
    problem = '官方图标尚未缓存，可预缓存图标或在游戏中打开对应服装页'
    for path in (record['bundle'], record['game_path']):
        if not path.is_file():
            continue
        if not 8 <= path.stat().st_size <= MAX_BUNDLE:
            problem = '图标资源包大小不受支持'
            continue
        data = path.read_bytes()
        if hashlib.md5(data).hexdigest() == record['md5']:
            return data
        problem = '图标资源包校验失败'
    raise ValueError(problem)


def decode_icon(data, name):
    import UnityPy
    UnityPy.config.FALLBACK_UNITY_VERSION = '6000.0.77f1'
    env = UnityPy.load(unmask_bundle(data, name))
    image = None
    for kind in ('Sprite', 'Texture2D'):
        objects = [obj for obj in env.objects if obj.type.name == kind]
        exact = [obj for obj in objects if obj.peek_name() == name]
        candidates = exact or objects
        if len(candidates) == 1:
            image = candidates[0].read().image
            break
    if image is None:
        raise ValueError('图标图片匹配不唯一')
    if image.width < 1 or image.height < 1 or image.width * image.height > 4096 * 4096:
        raise ValueError('图标原始尺寸不受支持')
    image = image.convert('RGBA')
    image.thumbnail((400, 400))
    out = io.BytesIO()
    image.save(out, format='PNG')
    result = out.getvalue()
    if len(result) > MAX_PNG:
        raise ValueError('图标 PNG 过大')
    return result


def read_icon(index, name, cached_only=False):
    record = record_for(index, name)
    result = cached_png(record)
    if result is None:
        if cached_only:
            raise ValueError('图标尚未预缓存')
        result = decode_icon(bundle_bytes(record), name)
        meta = {'version': 1, 'name': name, 'source_md5': record['md5'],
                'png_sha256': hashlib.sha256(result).hexdigest()}
        try:
            atomic_write(record['png'], result)
            atomic_write(record['meta'], json.dumps(meta, ensure_ascii=False).encode('utf-8'))
        except OSError:
            pass
    return base64.b64encode(result).decode('ascii')


def download_bundle(record):
    size, obj = record['size'], record['object_name']
    if not isinstance(size, int) or not 8 <= size <= MAX_BUNDLE or not re.fullmatch(r'[A-Za-z0-9_-]{1,128}', obj):
        raise ValueError('官方资源清单的下载字段无效')
    target = record['bundle']
    target.parent.mkdir(parents=True, exist_ok=True)
    for attempt in range(3):
        temp = None
        try:
            digest, length = hashlib.md5(), 0
            request = urllib.request.Request(CDN + obj, headers={'User-Agent': 'GakuActors/0.4'})
            with urllib.request.urlopen(request, timeout=20) as source:
                with tempfile.NamedTemporaryFile(prefix='download-', suffix='.part', dir=target.parent, delete=False) as out:
                    temp = Path(out.name)
                    while True:
                        chunk = source.read(64 * 1024)
                        if not chunk:
                            break
                        length += len(chunk)
                        if length > size:
                            raise ValueError('下载数据超过清单声明大小')
                        digest.update(chunk)
                        out.write(chunk)
                    out.flush()
                    os.fsync(out.fileno())
            if length != size or digest.hexdigest() != record['md5']:
                raise ValueError('下载数据未通过大小或 MD5 校验')
            os.replace(temp, target)
            return length
        except urllib.error.HTTPError as error:
            if error.code not in (429, 500, 502, 503, 504) or attempt == 2:
                raise ValueError('官方资源访问失败：HTTP ' + str(error.code)) from error
        except (urllib.error.URLError, TimeoutError, OSError):
            if attempt == 2:
                raise
        finally:
            if temp is not None and temp.exists():
                temp.unlink()
        time.sleep(0.5 * (attempt + 1))


def inventory(index):
    records = [items[0] for name, items in index.items() if MODEL_ICON.fullmatch(name) and len(items) == 1]
    records.sort(key=lambda r: r['name'])
    report = {'total': len(records), 'png_ready': 0, 'game_cached': 0, 'plugin_bundles': 0,
              'missing': 0, 'missing_bytes': 0,
              'ambiguous': sum(1 for name, items in index.items() if MODEL_ICON.fullmatch(name) and len(items) != 1)}
    for record in records:
        ready = cached_png(record) is not None
        report['png_ready'] += int(ready)
        report['game_cached'] += int(record['game_path'].is_file())
        report['plugin_bundles'] += int(record['bundle'].is_file())
        if not record['game_path'].is_file() and not record['bundle'].is_file() and not ready:
            report['missing'] += 1
            report['missing_bytes'] += record['size'] if isinstance(record['size'], int) else 0
    return records, report


def prepare_record(index, record, download):
    if cached_png(record) is not None:
        return {'kind': 'png_cache', 'bytes': 0}
    count = 0
    try:
        bundle_bytes(record)
    except ValueError:
        if not download:
            raise
        count = download_bundle(record)
    read_icon(index, record['name'])
    if cached_png(record) is None:
        raise ValueError('图标已解码，但预缓存文件未能写入')
    return {'kind': 'downloaded' if count else 'local_extracted', 'bytes': count}


def prefetch(index, records, download, workers=4):
    report = {'total': len(records), 'ready': 0, 'failed': 0, 'downloaded': 0,
              'downloaded_bytes': 0, 'png_cache': 0, 'local_extracted': 0,
              'failures': [], 'cache': str(records[0]['cache']) if records else ''}
    started = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        pending = {pool.submit(prepare_record, index, r, download): r for r in records}
        for done, future in enumerate(concurrent.futures.as_completed(pending), 1):
            record = pending[future]
            try:
                result = future.result()
                report['ready'] += 1
                report[result['kind']] += 1
                report['downloaded_bytes'] += result['bytes']
            except Exception as error:
                report['failed'] += 1
                report['failures'].append({'name': record['name'], 'error': str(error)})
            if done % 25 == 0 or done == len(records):
                print(json.dumps({'completed': done, 'total': len(records), 'ready': report['ready'],
                                  'failed': report['failed'], 'downloaded': report['downloaded']}, ensure_ascii=False), flush=True)
    report['seconds'] = round(time.monotonic() - started, 2)
    report['failures'].sort(key=lambda x: x['name'])
    if records:
        atomic_write(records[0]['cache'] / 'prefetch-report.json', json.dumps(report, ensure_ascii=False, indent=2).encode('utf-8'))
    return report


def serve(game):
    try:
        index, problem = fast_index(game) or icon_index(game), None
    except Exception as error:
        index, problem = {}, str(error)
    for line in sys.stdin:
        try:
            if len(line) > 2048:
                raise ValueError('图标请求过大')
            request = json.loads(line)
            if problem:
                raise ValueError(problem)
            if request['name'] not in index:
                index = icon_index(game)
            reply = {'data': read_icon(index, request['name'], bool(request.get('cached_only'))),
                     'status': '系统模型预览图 · 本地预缓存'}
        except Exception as error:
            reply = {'data': '', 'status': str(error)}
        print(json.dumps(reply, ensure_ascii=False), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--inventory', action='store_true')
    parser.add_argument('--prefetch', action='store_true')
    parser.add_argument('--download', action='store_true')
    parser.add_argument('--only-missing', action='store_true')
    parser.add_argument('--limit', type=int)
    parser.add_argument('--workers', type=int, default=4, choices=range(1, 9))
    args = parser.parse_args()
    if not args.inventory and not args.prefetch:
        serve(args.game)
        return
    index = icon_index(args.game)
    records, report = inventory(index)
    print(json.dumps(report, ensure_ascii=False), flush=True)
    if args.prefetch:
        if args.only_missing:
            records = [r for r in records if not r['game_path'].is_file() and not r['bundle'].is_file() and cached_png(r) is None]
        if args.limit is not None:
            records = records[:max(0, args.limit)]
        result = prefetch(index, records, args.download, args.workers)
        write_index(args.game, index)
        print(json.dumps(result, ensure_ascii=False), flush=True)
        if result['failed']:
            sys.exit(2)


if __name__ == '__main__':
    main()
