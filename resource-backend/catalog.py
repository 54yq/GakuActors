"""Octo manifest import and dependency planning; no game installation required."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import re
import sqlite3
from contextlib import contextmanager
import threading
from collections import Counter, deque

SCHEMA = 1
CATEGORIES = {'model':'模型部件', 'texture':'图片与贴图', 'motion':'动作',
              'stage':'舞台与场景', 'audio':'音频', 'other':'其他资源'}

class CatalogError(ValueError):
    pass

def varint(data, offset):
    value = 0
    for shift in range(0, 70, 7):
        if offset >= len(data): raise CatalogError('Protobuf 字段被截断')
        byte = data[offset]; offset += 1
        value |= (byte & 127) << shift
        if byte < 128: return value, offset
    raise CatalogError('Protobuf 整数过长')

def fields(data):
    pos = 0
    while pos < len(data):
        tag, pos = varint(data, pos)
        number, wire = tag >> 3, tag & 7
        if number == 0: raise CatalogError('Protobuf 字段编号为零')
        if wire == 0: value, pos = varint(data, pos)
        elif wire == 2:
            size, pos = varint(data, pos)
            if size > len(data)-pos: raise CatalogError('Protobuf 字段长度越界')
            value = data[pos:pos+size]; pos += size
        elif wire in (1,5):
            size = 8 if wire == 1 else 4
            if pos+size > len(data): raise CatalogError('Protobuf 定长字段被截断')
            value = data[pos:pos+size]; pos += size
        else: raise CatalogError('不支持的 Protobuf wire type')
        yield number, value

def category(name):
    n = name.lower()
    if n.startswith(('mdl_', 'crw_mdl_', 'gmk_')): return 'model'
    if n.startswith(('mot_', 'crw_mot_')): return 'motion'
    if n.startswith(('img_', 'tex_', 'spr_')) or n.endswith(('.png','.jpg','.tga')): return 'texture'
    if n.startswith(('env_', 'tln_', 'prp_')): return 'stage'
    if n.startswith(('sud_', 'bgm_', 'se_', 'vo_')) or n.endswith(('.acb','.awb','.hca')): return 'audio'
    return 'other'

def validate_entry(item):
    if item.get('section') not in (2,4) or type(item.get('id')) is not int or item['id'] < 0:
        raise CatalogError('资源类型或 ID 无效')
    if not isinstance(item.get('name'),str) or not item['name'] or len(item['name'])>1024:
        raise CatalogError('资源名无效')
    if not re.fullmatch('[a-f0-9]{32}',item.get('md5','')): raise CatalogError('资源 MD5 无效')
    if not re.fullmatch('[A-Za-z0-9_-]{1,256}',item.get('object_name','')): raise CatalogError('资源下载地址标识无效')
    if type(item.get('size')) is not int or not 0 < item['size'] <= 2**31:
        raise CatalogError('资源大小无效')
    if not isinstance(item.get('dependencies'),list) or any(type(d) is not int or d<0 for d in item['dependencies']):
        raise CatalogError('资源依赖无效')
    item['key'] = ('a' if item['section']==2 else 'r')+':'+str(item['id'])
    item['category'] = category(item['name'])
    return item

def decode_manifest(raw):
    if not raw or len(raw)>128*1048576: raise CatalogError('清单为空或超过 128 MiB')
    if raw.lstrip().startswith(b'{'):
        document=json.loads(raw.decode('utf-8-sig'))
        if document.get('format')!='gakumas-resource-catalog' or document.get('schema')!=SCHEMA:
            raise CatalogError('JSON 不是本工具导出的清单；请导入 octocacheevai')
        entries=[validate_entry(dict(e)) for e in document['entries']]
        revision=document.get('revision')
    else:
        if raw[0]!=1 or (len(raw)-1)%16: raise CatalogError('不是可识别的 Octo 加密清单')
        from Crypto.Cipher import AES
        from Crypto.Util.Padding import unpad
        try:
            plain=unpad(AES.new(hashlib.md5(b'1nuv9td1bw1udefk').digest(),AES.MODE_CBC,
                    hashlib.md5(b'LvAUtf+tnz').digest()).decrypt(raw[1:]),16)
        except ValueError as e: raise CatalogError('清单解密失败') from e
        if len(plain)<16 or hashlib.md5(plain[16:]).digest()!=plain[:16]:
            raise CatalogError('清单完整性校验失败')
        revision=None; entries=[]
        for number,value in fields(plain[16:]):
            if number==1: revision=value
            if number not in (2,4): continue
            values={}; deps=[]
            for field,v in fields(value):
                if field==8:
                    if isinstance(v,int): deps.append(v)
                    else:
                        offset=0
                        while offset<len(v):
                            dep,offset=varint(v,offset); deps.append(dep)
                else: values[field]=v
            # Deleted records are not downloadable current resources.
            if values.get(9)==4: continue
            entry=dict(section=number,id=values.get(1),name=values.get(3,b'').decode('utf-8'),
                       size=values.get(4),md5=values.get(10,b'').decode('ascii'),
                       object_name=values.get(11,b'').decode('ascii'),dependencies=sorted(set(deps)))
            entries.append(validate_entry(entry))
    if not entries or len(entries)>1000000: raise CatalogError('清单资源数量无效')
    keys=[e['key'] for e in entries]
    if len(set(keys))!=len(keys): raise CatalogError('清单含重复资源 ID')
    return dict(format='gakumas-resource-catalog',schema=SCHEMA,revision=revision,entries=entries)

class Catalog:
    def __init__(self,root):
        self.root=Path(root).resolve(); self.root.mkdir(parents=True,exist_ok=True)
        self.db=self.root/'catalog.sqlite3'; self.lock=threading.RLock()
        with self.connect() as db:
            db.execute('CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY,value TEXT NOT NULL)')
            db.execute('CREATE TABLE IF NOT EXISTS entries (key TEXT PRIMARY KEY,section INTEGER,id INTEGER,name TEXT,size INTEGER,md5 TEXT,object_name TEXT,category TEXT,dependencies TEXT)')
            db.execute('CREATE INDEX IF NOT EXISTS entry_category ON entries(category)')
            db.execute('CREATE INDEX IF NOT EXISTS entry_name ON entries(name)')
    @contextmanager
    def connect(self):
        db=sqlite3.connect(self.db,timeout=30); db.row_factory=sqlite3.Row
        try:
            with db:
                yield db
        finally:
            db.close()
    def import_bytes(self,raw,filename='octocacheevai'):
        document=decode_manifest(raw); fingerprint=hashlib.sha256(raw).hexdigest()
        folder=self.root/'manifests'; folder.mkdir(exist_ok=True)
        source=folder/(fingerprint+'.manifest')
        if not source.exists(): source.write_bytes(raw)
        metadata=dict(fingerprint=fingerprint,revision=document['revision'],entry_count=len(document['entries']),
                      filename=Path(filename.replace('\\','/')).name[:160],source_file=source.name)
        with self.lock,self.connect() as db:
            db.execute('DELETE FROM entries')
            db.executemany('INSERT INTO entries VALUES (?,?,?,?,?,?,?,?,?)',[
                (e['key'],e['section'],e['id'],e['name'],e['size'],e['md5'],e['object_name'],e['category'],json.dumps(e['dependencies'])) for e in document['entries']])
            db.execute('INSERT OR REPLACE INTO meta VALUES (?,?)',('active',json.dumps(metadata)))
        return self.summary()
    def summary(self):
        with self.lock,self.connect() as db:
            row=db.execute('SELECT value FROM meta WHERE key=?',('active',)).fetchone()
            metadata=json.loads(row[0]) if row else None
            counts={r[0]:r[1] for r in db.execute('SELECT category,count(*) FROM entries GROUP BY category')}
        return dict(active=metadata,categories=[dict(id=k,label=v,count=counts.get(k,0)) for k,v in CATEGORIES.items()])
    @staticmethod
    def unpack(row):
        if row is None: raise CatalogError('资源不存在')
        result=dict(row); result['dependencies']=json.loads(result['dependencies']); return result
    def get(self,key):
        with self.lock,self.connect() as db: return self.unpack(db.execute('SELECT * FROM entries WHERE key=?',(key,)).fetchone())
    def search(self,query='',kind='',page=1):
        if kind and kind not in CATEGORIES: raise CatalogError('资源分类无效')
        page=max(1,int(page)); clauses=[]; args=[]
        for word in query[:300].split(): clauses.append('instr(lower(name),?)>0'); args.append(word.lower())
        if kind: clauses.append('category=?'); args.append(kind)
        where=' WHERE '+' AND '.join(clauses) if clauses else ''
        with self.lock,self.connect() as db:
            total=db.execute('SELECT count(*) FROM entries'+where,args).fetchone()[0]
            rows=db.execute('SELECT * FROM entries'+where+' ORDER BY name LIMIT 40 OFFSET ?',args+[(page-1)*40]).fetchall()
        return dict(total=total,page=page,page_size=40,items=[self.unpack(r) for r in rows])
    def plan(self,keys):
        if not isinstance(keys,list) or not 1<=len(keys)<=100 or any(not isinstance(k,str) for k in keys):
            raise CatalogError('请选择 1 至 100 个资源')
        with self.lock,self.connect() as db:
            selected=[self.unpack(db.execute('SELECT * FROM entries WHERE key=?',(k,)).fetchone()) for k in dict.fromkeys(keys)]
            result={}; missing=[]; queue=deque(selected)
            while queue:
                item=queue.popleft()
                if item['key'] in result: continue
                result[item['key']]=item
                if len(result)>20000: raise CatalogError('依赖数量超过首版上限，请缩小选择')
                for dep in item['dependencies']:
                    # Octo dependencies are assetBundleList IDs, including resources which reference a bundle.
                    key='a:'+str(dep)
                    row=db.execute('SELECT * FROM entries WHERE key=?',(key,)).fetchone()
                    if row is None: missing.append(dict(source=item['key'],dependency=key))
                    elif key not in result: queue.append(self.unpack(row))
            active=db.execute('SELECT value FROM meta WHERE key=?',('active',)).fetchone()
        unique={e['md5']:e for e in result.values()}
        return dict(manifest=json.loads(active[0]) if active else None,selected=[e['key'] for e in selected],entries=list(result.values()),
                    files=len(unique),total_bytes=sum(e['size'] for e in unique.values()),missing=missing,
                    selected_names=[e['name'] for e in selected])
    def export_json(self):
        with self.lock,self.connect() as db:
            active=db.execute('SELECT value FROM meta WHERE key=?',('active',)).fetchone()
            rows=db.execute('SELECT * FROM entries ORDER BY section,id').fetchall()
        return dict(format='gakumas-resource-catalog',schema=SCHEMA,revision=json.loads(active[0])['revision'] if active else None,
                    entries=[self.unpack(r) for r in rows])
