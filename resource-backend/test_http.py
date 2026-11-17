import json
from pathlib import Path
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from app import make_handler
from catalog import Catalog
from downloads import Jobs
from http.server import ThreadingHTTPServer
from test_core import manifest

class LocalApiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary=Path(__file__).resolve().parent/'data/test-temp';temporary.mkdir(parents=True,exist_ok=True)
        cls.temp=tempfile.TemporaryDirectory(dir=temporary)
        cls.catalog=Catalog(cls.temp.name)
        cls.jobs=Jobs(cls.catalog,cls.temp.name,Path(__file__).resolve().parent)
        cls.server=ThreadingHTTPServer(('127.0.0.1',0),make_handler(cls.catalog,cls.jobs))
        cls.port=cls.server.server_port;cls.base='http://127.0.0.1:'+str(cls.port)
        cls.thread=threading.Thread(target=cls.server.serve_forever,daemon=True);cls.thread.start()
        cls.opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown();cls.server.server_close();cls.thread.join();cls.jobs.pool.shutdown();cls.temp.cleanup()
    def request(self,path,body=None,headers=None):
        request=urllib.request.Request(self.base+path,data=body,headers=headers or {})
        try:
            with self.opener.open(request,timeout=5) as response:return response.status,response.read()
        except urllib.error.HTTPError as e:return e.code,e.read()
    def test_import_and_search_without_game_path(self):
        code,body=self.request('/api/import',manifest(),{'X-Gakumas-Client':'1','X-File-Name':'octocacheevai'})
        self.assertEqual(code,200);self.assertEqual(json.loads(body)['active']['entry_count'],3)
        code,body=self.request('/api/resources?q=mdl_chr');self.assertEqual(json.loads(body)['total'],1)
    def test_unknown_host_and_foreign_origin_are_rejected(self):
        for headers in ({'Host':'attacker.invalid'},{'Origin':'https://attacker.invalid'}):
            self.assertEqual(self.request('/api/meta',headers=headers)[0],400)
    def test_mutation_requires_explicit_client_header(self):
        code,body=self.request('/api/import',manifest())
        self.assertEqual(code,400);self.assertIn('error',json.loads(body))
    def test_unknown_route_cannot_read_source_or_parent_files(self):
        for path in ('/../app.py','/app.py','/data/catalog.sqlite3','/%2e%2e/app.py'):
            self.assertEqual(self.request(path)[0],404)
    def test_invalid_job_and_invalid_modes_rejected(self):
        self.catalog.import_bytes(manifest())
        for request in ({'keys':['not-real']},{'keys':['a:1'],'mode':'vmd'},{'keys':['a:1'],'unity_version':'../../outside'}):
            code,_=self.request('/api/jobs',json.dumps(request).encode(),{'X-Gakumas-Client':'1','Content-Type':'application/json'})
            self.assertEqual(code,400)

if __name__=='__main__':unittest.main()
