"""Content-addressed isolated downloads and persisted background export jobs."""
from __future__ import annotations
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import threading
import time
import urllib.request
import uuid

CDN='https://object.asset.game-gakuen-idolmaster.jp/'

def write_json(path,data):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    temporary=path.with_suffix(path.suffix+'.part')
    temporary.write_text(json.dumps(data,ensure_ascii=False,indent=2,allow_nan=False),encoding='utf-8')
    os.replace(temporary,path)

def digest(path,algorithm='md5'):
    h=hashlib.new(algorithm)
    with Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1048576),b''): h.update(block)
    return h.hexdigest()

def safe_label(name):
    label=re.sub(r'[^a-zA-Z0-9_.\-\u3040-\u30ff\u3400-\u9fff]','_',name)[:110].strip('. ')
    if not label or label.upper().split('.')[0] in {'CON','NUL','AUX','PRN','COM1','LPT1'}:label='resource_'+label
    return label

def unmask_bundle(data,name):
    if data.startswith((b'UnityFS',b'UnityRaw',b'UnityWeb')): return data
    raw=name.encode('ascii');mask=bytearray(len(raw)*2)
    if not raw or len(data)<256: raise ValueError('包头不足以解码')
    for i,value in enumerate(raw):mask[2*i]=value;mask[-1-2*i]=(~value)&255
    seed=0x9b
    for value in mask:seed=(((seed&1)<<7)|(seed>>1))^value
    mask=bytes(value^seed for value in mask);out=bytearray(data)
    for i in range(256):out[i]^=mask[i%len(mask)]
    if not out.startswith((b'UnityFS',b'UnityRaw',b'UnityWeb')):raise ValueError('解码后不是可识别的 Unity AssetBundle')
    return bytes(out)

class DownloadCache:
    def __init__(self,root):
        self.root=Path(root).resolve();self.root.mkdir(parents=True,exist_ok=True)
    def path(self,entry):return self.root/'raw'/entry['md5']
    def valid(self,entry):
        path=self.path(entry)
        return path.is_file() and path.stat().st_size==entry['size'] and digest(path)==entry['md5']
    def fetch(self,entry,progress=lambda text:None,canceled=lambda:False,open_url=urllib.request.urlopen):
        if self.valid(entry):return self.path(entry),True
        if not re.fullmatch('[A-Za-z0-9_-]{1,256}',entry['object_name']):raise ValueError('无效 CDN 对象名')
        target=self.path(entry);target.parent.mkdir(exist_ok=True)
        temp=target.with_suffix('.part')
        for attempt in range(3):
            if canceled():raise InterruptedError('任务已取消')
            try:
                request=urllib.request.Request(CDN+entry['object_name'],headers={'User-Agent':'GakumasResourceDownloader/0.1'})
                h=hashlib.md5();length=0
                with open_url(request,timeout=35) as response,temp.open('wb') as output:
                    while True:
                        if canceled():raise InterruptedError('任务已取消')
                        block=response.read(262144)
                        if not block:break
                        length+=len(block)
                        if length>entry['size']:raise ValueError('下载超过清单声明大小')
                        h.update(block);output.write(block)
                        progress('下载 '+entry['name']+' ('+str(round(length/1048576,2))+' MiB)')
                if length!=entry['size'] or h.hexdigest()!=entry['md5']:raise ValueError('下载大小或 MD5 校验失败')
                os.replace(temp,target);return target,False
            except InterruptedError:raise
            except Exception:
                if attempt==2:raise
                time.sleep(attempt+1)
            finally:
                if temp.exists():temp.unlink()
    def decode(self,entry):
        path=self.path(entry)
        if not self.valid(entry):raise ValueError('原始缓存校验失败')
        if entry['section']!=2:return path
        decoded=self.root/'decoded'/entry['md5']/(safe_label(entry['name'])+'.unity3d')
        stamp=decoded.with_suffix('.json')
        if decoded.is_file() and stamp.is_file():
            try:
                saved=json.loads(stamp.read_text('utf-8'))
                if saved['source_name']==entry['name'] and saved['sha256']==digest(decoded,'sha256'):return decoded
            except (ValueError,KeyError,OSError):pass
        decoded.parent.mkdir(parents=True,exist_ok=True)
        blob=unmask_bundle(path.read_bytes(),entry['name'])
        temporary=decoded.with_suffix('.part');temporary.write_bytes(blob);os.replace(temporary,decoded)
        write_json(stamp,dict(source_name=entry['name'],raw_md5=entry['md5'],sha256=digest(decoded,'sha256')))
        return decoded

class Jobs:
    def __init__(self,catalog,root,project):
        self.catalog=catalog;self.root=Path(root).resolve();self.project=Path(project).resolve()
        self.folder=self.root/'jobs';self.folder.mkdir(parents=True,exist_ok=True)
        self.cache=DownloadCache(self.root/'cache');self.lock=threading.RLock()
        self.pool=ThreadPoolExecutor(max_workers=1);self.jobs={};self.cancels={}
        for path in self.folder.glob('*/job.json'):
            try:
                job=json.loads(path.read_text('utf-8'))
                if not re.fullmatch('[a-f0-9]{12}',job['id']):continue
                if job['status'] in ('queued','running'):job.update(status='interrupted',message='上次服务退出；可重新提交，已校验缓存会复用')
                self.jobs[job['id']]=job
            except (ValueError,KeyError,OSError):pass
    def update(self,job,**values):
        with self.lock:
            job.update(values);write_json(self.folder/job['id']/'job.json',job)
    def listing(self):
        with self.lock:return json.loads(json.dumps(sorted(self.jobs.values(),key=lambda j:j.get('created',0),reverse=True)))
    def submit(self,request):
        mode=request.get('mode','download')
        if mode not in ('download','auto','model','texture','motion'):raise ValueError('导出模式无效')
        version=request.get('unity_version','6000.0.77f1')
        if not isinstance(version,str) or not re.fullmatch(r'\d{4}\.\d+\.\d+[a-z]\d+',version):raise ValueError('Unity 版本格式无效')
        limit=request.get('max_mb',256)
        if type(limit) not in (int,float) or not 1<=limit<=4096:raise ValueError('下载上限须为 1 至 4096 MiB')
        with self.lock:
            if sum(j['status'] in ('queued','running') for j in self.jobs.values())>=5:raise ValueError('待处理任务已满')
            plan=self.catalog.plan(request.get('keys'))
            if plan['missing']:raise ValueError('存在缺失依赖，请先查看计划；首版不导出缺依赖模型')
            if plan['total_bytes']>limit*1048576:raise ValueError('计划超过下载体积上限')
            key=uuid.uuid4().hex[:12]
            job=dict(id=key,status='queued',mode=mode,message='等待处理',completed=0,total=plan['files'],
                     total_bytes=plan['total_bytes'],reused=0,created=time.time(),selected_names=plan['selected_names'],
                     output=str(self.folder/key/'exports'),artifacts=[],error='',unity_version=version,max_mb=limit)
            self.jobs[key]=job;self.cancels[key]=threading.Event()
            self.update(job);write_json(self.folder/key/'plan.json',plan)
            self.pool.submit(self.run,job,plan)
            return dict(job)
    def cancel(self,key):
        with self.lock:
            if key not in self.jobs:raise ValueError('任务不存在')
            if key in self.cancels:self.cancels[key].set()
            return dict(self.jobs[key])
    def retry(self,key):
        with self.lock:
            if key not in self.jobs:raise ValueError('任务不存在')
            old=self.jobs[key]
            if old['status'] in ('queued','running'):raise ValueError('任务仍在处理中')
            plan=json.loads((self.folder/key/'plan.json').read_text('utf-8'))
            if self.catalog.summary()['active']['fingerprint']!=plan['manifest']['fingerprint']:
                raise ValueError('当前清单已切换，请从资源库重新选择')
            request=dict(keys=plan['selected'],mode=old['mode'],unity_version=old['unity_version'],
                         max_mb=old.get('max_mb',max(256,plan['total_bytes']/1048576+1)))
        return self.submit(request)
    def run(self,job,plan):
        output=self.folder/job['id'];cancel=self.cancels[job['id']].is_set
        last=[0.0]
        def progress(text):
            now=time.monotonic()
            if now-last[0]>.4:self.update(job,message=text);last[0]=now
        try:
            self.update(job,status='running',message='校验与下载资源')
            unique={e['md5']:e for e in plan['entries']}
            for i,entry in enumerate(unique.values(),1):
                _,reused=self.cache.fetch(entry,progress,cancel)
                self.update(job,completed=i,reused=job['reused']+int(reused))
            decoded={}
            for entry in plan['entries']:
                if cancel():raise InterruptedError('任务已取消')
                progress('解码 '+entry['name']);decoded[entry['key']]=self.cache.decode(entry)
            exports=output/'exports';exports.mkdir(exist_ok=True)
            from exporters import export_selection
            report=export_selection(plan,decoded,exports,job['mode'],self.project,job['unity_version'],progress,cancel)
            report.update(manifest=plan['manifest'],selected=plan['selected'],unity_version=job['unity_version'])
            write_json(exports/'export-report.json',report)
            artifacts=[]
            for path in sorted(exports.rglob('*')):
                if path.is_file():artifacts.append(dict(path=path.relative_to(exports).as_posix(),bytes=path.stat().st_size))
            self.update(job,status='completed',message='导出完成' if job['mode']!='download' else '资源下载与解码完成',
                        artifacts=artifacts,report=report,finished=time.time())
        except InterruptedError as e:self.update(job,status='canceled',message=str(e),finished=time.time())
        except Exception as e:
            self.update(job,status='failed',message='处理失败，已下载缓存保留',error=str(e),finished=time.time())
        finally:self.cancels.pop(job['id'],None)
    def artifact(self,key,relative):
        if not re.fullmatch('[a-f0-9]{12}',key) or key not in self.jobs:raise ValueError('任务不存在')
        folder=(self.folder/key/'exports').resolve();path=(folder/relative).resolve()
        if not path.is_relative_to(folder) or not path.is_file() or path.is_symlink():raise ValueError('文件路径无效')
        return path
