import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock
from Crypto.Cipher import AES
from Crypto.Util.Padding import pad
from catalog import Catalog,CatalogError,decode_manifest
from downloads import DownloadCache,Jobs,unmask_bundle

_previous_temp=None
def setUpModule():
    global _previous_temp
    _previous_temp=tempfile.tempdir
    folder=Path(__file__).resolve().parent/'data/test-temp';folder.mkdir(parents=True,exist_ok=True)
    tempfile.tempdir=str(folder)
def tearDownModule():tempfile.tempdir=_previous_temp

def integer(value):
    result=bytearray()
    while value>127:result.append((value&127)|128);value>>=7
    result.append(value);return bytes(result)
def scalar(key,value):return integer(key<<3)+integer(value)
def blob(key,value):return integer((key<<3)|2)+integer(len(value))+value
def item(identity,name,deps=(),state=3):
    return scalar(1,identity)+blob(3,name.encode())+scalar(4,5)+blob(10,hashlib.md5(b'hello').hexdigest().encode())+blob(11,b'safe_object')+blob(8,b''.join(integer(d) for d in deps))+scalar(9,state)
def manifest():
    # Resource ID 1 overlaps asset ID 1; dependencies still refer to asset bundles.
    plain=scalar(1,42)+blob(2,item(1,'mdl_chr_test_body',[2]))+blob(2,item(2,'tex_test',[1]))+blob(4,item(1,'notes.txt',[2]))+blob(2,item(99,'removed',state=4))
    data=hashlib.md5(plain).digest()+plain
    return b'\x01'+AES.new(hashlib.md5(b'1nuv9td1bw1udefk').digest(),AES.MODE_CBC,hashlib.md5(b'LvAUtf+tnz').digest()).encrypt(pad(data,16))

class ManifestTests(unittest.TestCase):
    def test_encrypted_manifest_packed_dependencies_and_deleted_records(self):
        data=decode_manifest(manifest())
        self.assertEqual(data['revision'],42)
        self.assertEqual({x['key'] for x in data['entries']},{'a:1','a:2','r:1'})
        self.assertEqual(data['entries'][0]['dependencies'],[2])
    def test_import_failure_preserves_active_catalog(self):
        with tempfile.TemporaryDirectory() as td:
            c=Catalog(td);before=c.import_bytes(manifest())
            damaged=bytearray(manifest());damaged[30]^=8
            with self.assertRaises(CatalogError):c.import_bytes(bytes(damaged))
            self.assertEqual(c.summary(),before)
            self.assertEqual(c.search()['total'],3)
    def test_dependency_cycle_terminates_and_resource_ids_do_not_collide(self):
        with tempfile.TemporaryDirectory() as td:
            c=Catalog(td);c.import_bytes(manifest());p=c.plan(['r:1'])
            self.assertEqual({e['key'] for e in p['entries']},{'a:1','a:2','r:1'})
            self.assertFalse(p['missing']);self.assertEqual(p['files'],1)
    def test_missing_dependency_is_visible(self):
        data=decode_manifest(manifest());data['entries'][0]['dependencies']=[700]
        with tempfile.TemporaryDirectory() as td:
            c=Catalog(td);c.import_bytes(json.dumps(data).encode())
            self.assertEqual(c.plan(['a:1'])['missing'],[dict(source='a:1',dependency='a:700')])
    def test_json_roundtrip_and_literal_search(self):
        with tempfile.TemporaryDirectory() as td:
            c=Catalog(td);c.import_bytes(manifest())
            exported=json.dumps(c.export_json()).encode()
            other=Catalog(Path(td)/'other');other.import_bytes(exported)
            self.assertEqual(other.search('mdl_chr test')['total'],1)
            self.assertEqual(other.search("' OR 1=1 --")['total'],0)
            self.assertEqual(other.plan(['r:1'])['selected_names'],['notes.txt'])
    def test_duplicate_id_and_remote_path_injection_rejected(self):
        for mutation in ('duplicate','remote'):
            data=decode_manifest(manifest())
            if mutation=='duplicate':data['entries'].append(dict(data['entries'][0]))
            else:data['entries'][0]['object_name']='../elsewhere?token=secret'
            with self.assertRaises(CatalogError):decode_manifest(json.dumps(data).encode())

class CacheTests(unittest.TestCase):
    def entry(self):return dict(md5=hashlib.md5(b'hello').hexdigest(),size=5,object_name='safe_object',name='sample',section=4)
    def test_corrupt_cached_file_is_replaced_only_after_verified_download(self):
        with tempfile.TemporaryDirectory() as td:
            cache=DownloadCache(td);entry=self.entry();path=cache.path(entry);path.parent.mkdir();path.write_bytes(b'wrong')
            opener=Mock(side_effect=lambda *a,**k:io.BytesIO(b'hello'))
            got,reused=cache.fetch(entry,open_url=opener)
            self.assertFalse(reused);self.assertEqual(got.read_bytes(),b'hello')
            got,reused=cache.fetch(entry,open_url=opener)
            self.assertTrue(reused);self.assertEqual(opener.call_count,1)
    def test_oversized_transfer_never_becomes_a_valid_cache(self):
        with tempfile.TemporaryDirectory() as td:
            cache=DownloadCache(td);entry=self.entry();opener=Mock(side_effect=lambda *a,**k:io.BytesIO(b'hello extra'))
            from unittest.mock import patch
            with patch('downloads.time.sleep'),self.assertRaisesRegex(ValueError,'超过'):
                cache.fetch(entry,open_url=opener)
            self.assertFalse(cache.path(entry).exists());self.assertFalse(cache.path(entry).with_suffix('.part').exists())
    def test_cancellation_does_not_start_network(self):
        with tempfile.TemporaryDirectory() as td:
            cache=DownloadCache(td);opener=Mock()
            with self.assertRaises(InterruptedError):cache.fetch(self.entry(),canceled=lambda:True,open_url=opener)
            opener.assert_not_called()
    def test_mask_roundtrip_and_invalid_header(self):
        plain=b'UnityFS\0'+bytes(400);name='mot_test';raw=name.encode();mask=bytearray(len(raw)*2)
        for i,value in enumerate(raw):mask[2*i]=value;mask[-1-2*i]=(~value)&255
        seed=0x9b
        for value in mask:seed=(((seed&1)<<7)|(seed>>1))^value
        mask=bytes(value^seed for value in mask);masked=bytearray(plain)
        for i in range(256):masked[i]^=mask[i%len(mask)]
        self.assertEqual(unmask_bundle(bytes(masked),name),plain)
        with self.assertRaises(ValueError):unmask_bundle(bytes(400),name)
    def test_job_blocks_missing_dependencies_and_artifact_traversal(self):
        with tempfile.TemporaryDirectory() as td:
            c=Catalog(td);data=decode_manifest(manifest());data['entries'][0]['dependencies']=[800]
            c.import_bytes(json.dumps(data).encode());jobs=Jobs(c,td,Path(__file__).resolve().parent)
            try:
                with self.assertRaisesRegex(ValueError,'缺失依赖'):jobs.submit(dict(keys=['a:1']))
                identity='a'*12;jobs.jobs[identity]={'id':identity};folder=Path(td)/'jobs'/identity/'exports';folder.mkdir(parents=True)
                (Path(td)/'private.txt').write_text('private')
                with self.assertRaises(ValueError):jobs.artifact(identity,'../../../private.txt')
            finally:jobs.pool.shutdown()
    def test_raw_export_includes_dependency_bundles(self):
        from exporters import export_selection
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td);output=folder/'exports';output.mkdir()
            first=folder/'first';first.write_bytes(b'first');dependency=folder/'dependency';dependency.write_bytes(b'dep')
            entries=[dict(key='a:1',name='model',section=2),dict(key='a:2',name='material',section=2)]
            report=export_selection(dict(entries=entries,selected=['a:1']),{'a:1':first,'a:2':dependency},output,'download',folder,'6000.0.77f1',lambda x:None,lambda:False)
            self.assertEqual({r['source'] for r in report['raw']},{'a:1','a:2'})
            self.assertEqual((output/'bundles/a_2__material.unity3d').read_bytes(),b'dep')

if __name__=='__main__':unittest.main()
