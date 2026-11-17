"""Refresh the PC catalog for GakuActors; leave the standalone service untouched.

Protocol based on GkmasObjectManager (GPL-3.0):
https://github.com/AllenHeartcore/GkmasObjectManager/blob/main/GkmasObjectManager/manifest/__init__.py
"""
import hashlib
import json
from pathlib import Path
import sys
import urllib.request
from Crypto.Cipher import AES
from Crypto.Util.Padding import pad, unpad

API = 'https://api.asset.game-gakuen-idolmaster.jp/v2/pub/a/400/v/705100/list/0'
LIMIT = 128 * 1048576
HEADERS = {'Accept': 'application/x-protobuf,x-octo-app/400', 'X-OCTO-KEY': '0jv0wsohnnsigttbfigushbtl3a8m7l5', 'User-Agent': 'GakuActors/0.5'}
LOCAL = 'http://127.0.0.1:8777'

def wrap_online(data):
    if len(data) < 33 or len(data) > LIMIT: raise ValueError('在线清单长度无效')
    iv, encrypted = data[:16], data[16:]
    if len(encrypted) % 16 == 1 and encrypted[0] == 1: encrypted = encrypted[1:]
    if not encrypted or len(encrypted) % 16: raise ValueError('在线清单密文长度无效')
    key = hashlib.sha256(b'x5HFaJCJywDyuButLM0f').digest()
    plain = unpad(AES.new(key, AES.MODE_CBC, iv).decrypt(encrypted), 16)
    payload = hashlib.md5(plain).digest() + plain
    return b'\x01' + AES.new(hashlib.md5(b'1nuv9td1bw1udefk').digest(), AES.MODE_CBC,
                              hashlib.md5(b'LvAUtf+tnz').digest()).encrypt(pad(payload, 16))

def local(path, body=None, headers=None):
    req = urllib.request.Request(LOCAL + path, data=body, headers=headers or {})
    with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(req, timeout=15) as response:
        return json.load(response)

def refresh(backend):
    sys.path.insert(0, str(backend))
    from catalog import decode_manifest
    initial = local('/api/meta')['active']
    if any(j['status'] in ('queued', 'running') for j in local('/api/jobs')):
        return '有任务正在进行，跳过本次更新；继续使用当前清单。'
    req = urllib.request.Request(API, headers=HEADERS)
    with urllib.request.urlopen(req, timeout=20) as response:
        online = response.read(LIMIT + 1)
    wrapped = wrap_online(online)
    document = decode_manifest(wrapped)
    revision = document['revision']
    if not isinstance(revision, int) or revision <= 0: raise ValueError('在线清单缺少有效修订号')
    if initial and isinstance(initial.get('revision'), int):
        if revision < initial['revision']: raise ValueError('在线修订号低于当前清单，已保留现有版本')
        if revision == initial['revision']: return '清单已是当前修订版本：%s。' % revision
    current = local('/api/meta')['active']
    if current != initial or any(j['status'] in ('queued', 'running') for j in local('/api/jobs')):
        return '清单或任务状态发生变化，已跳过更新。'
    result = local('/api/import', wrapped, {'X-Gakumas-Client':'1', 'X-File-Name':'online-pc-705100.octocacheevai'})
    return '在线清单已更新：revision %s，%s 条资源。' % (result['active']['revision'], result['active']['entry_count'])

def main():
    data = Path(sys.argv[2]); data.mkdir(parents=True, exist_ok=True)
    try: status = refresh(Path(sys.argv[1]))
    except Exception as error: status = '自动检查失败，继续使用现有清单：%s' % error
    (data / 'catalog-update-status.txt').write_text(status + '\n', encoding='utf-8')
    print(status)

if __name__ == '__main__': main()
