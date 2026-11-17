"""Independent local Gakumas resource browser and exporter."""
from __future__ import annotations
import argparse
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import json
import mimetypes
from pathlib import Path
import sys
from urllib.parse import urlsplit,parse_qs,unquote,quote
# The Windows embeddable interpreter runs in isolated mode and does not add
# the script directory to sys.path. Resolve sibling modules from this copy.
sys.path.insert(0,str(Path(__file__).resolve().parent))
from catalog import Catalog,CatalogError
from downloads import Jobs

PROJECT=Path(__file__).resolve().parent
VERSION='0.1.0'

def make_handler(catalog,jobs):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,format,*args):pass
        def local_request(self):
            host=self.headers.get('Host','')
            expected={'127.0.0.1:'+str(self.server.server_port),'localhost:'+str(self.server.server_port)}
            if host not in expected:raise ValueError('只接受本机请求')
            origin=self.headers.get('Origin')
            if origin and origin!='http://'+host:raise ValueError('请求来源与本机页面不一致')
        def json(self,data,status=200):
            raw=json.dumps(data,ensure_ascii=False,allow_nan=False).encode('utf-8')
            self.send_response(status);self.send_header('Content-Type','application/json; charset=utf-8')
            self.send_header('Content-Length',str(len(raw)));self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(raw)
        def file(self,path,download=False):
            self.send_response(200);self.send_header('Content-Type',mimetypes.guess_type(path.name)[0] or 'application/octet-stream')
            self.send_header('Content-Length',str(path.stat().st_size));self.send_header('X-Content-Type-Options','nosniff')
            self.send_header('Content-Disposition',('attachment' if download else 'inline')+"; filename*=UTF-8''"+quote(path.name,safe=''))
            self.end_headers()
            with path.open('rb') as stream:
                while True:
                    block=stream.read(262144)
                    if not block:break
                    self.wfile.write(block)
        def do_GET(self):
            try:
                self.local_request();parsed=urlsplit(self.path);q={k:v[0] for k,v in parse_qs(parsed.query).items()}
                if parsed.path=='/api/meta':return self.json(dict(version=VERSION,**catalog.summary(),tools={
                    'fbx':(PROJECT/'tools/AssetStudio/GakumasModelFbx.exe').is_file(),
                    'anim':(PROJECT/'tools/AssetRipper/AssetRipper.GUI.Free.exe').is_file()},data_root=str(catalog.root)))
                if parsed.path=='/api/resources':
                    data=catalog.search(q.get('q',''),q.get('category',''),q.get('page','1'))
                    for e in data['items']:
                        path=jobs.cache.path(e);e['cached']=path.is_file() and path.stat().st_size==e['size']
                    return self.json(data)
                if parsed.path=='/api/resource':return self.json(catalog.get(q.get('key','')))
                if parsed.path=='/api/jobs':return self.json(jobs.listing())
                if parsed.path=='/api/catalog':return self.json(catalog.export_json())
                if parsed.path=='/api/artifact':
                    path=jobs.artifact(q.get('id',''),q.get('path',''))
                    return self.file(path,download=path.suffix.lower() not in ('.png','.jpg','.jpeg','.json'))
                if parsed.path in ('/','/index.html'):return self.file(PROJECT/'web/index.html')
                if parsed.path in ('/app.js','/style.css'):return self.file(PROJECT/'web'/parsed.path[1:])
                return self.json(dict(error='页面不存在'),404)
            except (ValueError,KeyError,OSError) as e:return self.json(dict(error=str(e)),400)
            except (BrokenPipeError,ConnectionResetError):pass
        def do_POST(self):
            try:
                self.local_request()
                if self.headers.get('X-Gakumas-Client')!='1':raise ValueError('请从本机工具页面提交')
                length=int(self.headers.get('Content-Length','0'))
                if self.path=='/api/import':
                    if not 0<length<=128*1048576:raise ValueError('清单文件大小无效')
                    content=self.rfile.read(length)
                    if len(content)!=length:raise ValueError('清单上传被截断')
                    name=unquote(self.headers.get('X-File-Name','octocacheevai'))
                    return self.json(catalog.import_bytes(content,name))
                if not 0<length<=65536:raise ValueError('请求大小无效')
                if self.headers.get('Content-Type','').split(';')[0]!='application/json':raise ValueError('需要 JSON 请求')
                request=json.loads(self.rfile.read(length))
                if not isinstance(request,dict):raise ValueError('请求应为对象')
                if self.path=='/api/plan':
                    plan=catalog.plan(request.get('keys'))
                    return self.json({k:v for k,v in plan.items() if k!='entries'}|{'items':plan['entries'][:120]})
                if self.path=='/api/jobs':return self.json(jobs.submit(request),202)
                if self.path=='/api/cancel':return self.json(jobs.cancel(request.get('id','')))
                if self.path=='/api/retry':return self.json(jobs.retry(request.get('id','')),202)
                return self.json(dict(error='接口不存在'),404)
            except (ValueError,KeyError,OSError) as e:return self.json(dict(error=str(e)),400)
            except Exception as e:return self.json(dict(error=type(e).__name__+': '+str(e)),500)
    return Handler

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data',type=Path,default=PROJECT/'data')
    parser.add_argument('--port',type=int,default=8776)
    parser.add_argument('--import-manifest',type=Path)
    args=parser.parse_args()
    catalog=Catalog(args.data)
    if args.import_manifest:
        print(json.dumps(catalog.import_bytes(args.import_manifest.read_bytes(),args.import_manifest.name),ensure_ascii=False))
        return
    jobs=Jobs(catalog,args.data,PROJECT)
    server=ThreadingHTTPServer(('127.0.0.1',args.port),make_handler(catalog,jobs));server.daemon_threads=True
    print('Gakumas Resource Downloader '+VERSION+' | http://127.0.0.1:'+str(args.port),flush=True)
    try:server.serve_forever()
    except KeyboardInterrupt:pass
    finally:
        server.server_close()
        for event in list(jobs.cancels.values()):event.set()
        jobs.pool.shutdown(wait=True)

if __name__=='__main__':
    if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf-8')
    main()
