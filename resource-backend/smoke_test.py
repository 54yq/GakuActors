"""Bounded end-to-end checks against this project's local API; no game path is used."""
import argparse,json,sys,urllib.request,urllib.parse
from pathlib import Path

ROOT=Path(__file__).resolve().parent
OPENER=urllib.request.build_opener(urllib.request.ProxyHandler({}))
def api(path,body=None):
    request=urllib.request.Request('http://127.0.0.1:8776'+path,data=json.dumps(body).encode() if body is not None else None,
        headers={'Content-Type':'application/json','X-Gakumas-Client':'1'})
    return json.load(OPENER.open(request,timeout=30))

def submit():
    destination=ROOT/'data/smoke-jobs.json'
    if destination.exists():raise ValueError('Smoke jobs already exist; use status to inspect them instead of resubmitting')
    def exact(name):
        data=api('/api/resources?'+urllib.parse.urlencode({'q':name}))
        records=[e for e in data['items'] if e['name']==name]
        if len(records)!=1:raise ValueError('Missing or ambiguous source '+name)
        return records[0]['key']
    models=[exact(n) for n in ['mdl_chr_hski-base-0000_face','mdl_chr_hski-base-0000_hair','mdl_chr_hski-cstm-0000_body','img_cos_hski-cstm-0000_body']]
    motion=[exact('mot_all_chr_amao_idle-001_pose')]
    requests=[dict(label='model-and-texture',keys=models,mode='auto',max_mb=128),dict(label='native-motion',keys=motion,mode='motion',max_mb=32),dict(label='raw-cache-reuse',keys=motion,mode='download',max_mb=32)]
    jobs=[]
    for request in requests:
        plan=api('/api/plan',{'keys':request['keys']})
        if plan['missing']:raise ValueError('Missing dependencies')
        print('plan',request['label'],plan['files'],plan['total_bytes'])
        job=api('/api/jobs',request);jobs.append(dict(label=request['label'],id=job['id']))
        destination.write_text(json.dumps(jobs,indent=2),encoding='utf-8')
        print('submitted',job['id'])

def status():
    jobs=api('/api/jobs')
    (ROOT/'data/smoke-status.json').write_text(json.dumps(jobs,ensure_ascii=False,indent=2),encoding='utf-8')
    for job in jobs:
        print(json.dumps({k:job.get(k) for k in ('id','status','message','completed','total','reused','error','output')},ensure_ascii=False))

if __name__=='__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    parser=argparse.ArgumentParser();parser.add_argument('action',choices=['submit','status']);args=parser.parse_args()
    {'submit':submit,'status':status}[args.action]()
