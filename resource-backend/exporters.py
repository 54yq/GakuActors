"""Selected-source FBX, full-resolution PNG and restored Unity AnimationClip exports."""
from __future__ import annotations
from collections import Counter
import json
from pathlib import Path
import re
import shutil
import socket
import subprocess
import time
import urllib.parse
import urllib.request
from downloads import digest, safe_label, write_json

def hidden_flags():return getattr(subprocess,'CREATE_NO_WINDOW',0)

def tool_inputs(entries,decoded,work,version):
    """Restore stripped version headers in working copies, preserving original verified cache."""
    import UnityPy
    UnityPy.config.FALLBACK_UNITY_VERSION=version
    work.mkdir(parents=True,exist_ok=True);result={}
    for entry in entries:
        if entry['section']!=2:continue
        source=decoded[entry['key']]
        target=work/(entry['md5']+'__'+safe_label(entry['name'])+'.unity3d')
        if not target.is_file():
            env=UnityPy.load(str(source))
            def restore(file):
                if hasattr(file,'version_engine') and file.version_engine in ('','0.0.0'):
                    file.version_engine=version
                if hasattr(file,'unity_version') and file.unity_version in ('','0.0.0'):
                    file.set_version(version)
                for child in (getattr(file,'files',None) or {}).values():restore(child)
            roots=list(env.files.values())
            if len(roots)!=1:raise ValueError('导出输入不是单一资源包')
            restore(roots[0]);target.write_bytes(roots[0].save(packer='lz4'))
        result[entry['key']]=target
    return result

def load_environment(entries,decoded,version):
    import UnityPy
    UnityPy.config.FALLBACK_UNITY_VERSION=version
    env=UnityPy.Environment();owners={}
    for entry in entries:
        if entry['section']!=2:continue
        before={(o.assets_file.name,o.path_id) for o in env.objects}
        env.load_file(str(decoded[entry['key']]))
        for obj in env.objects:
            key=(obj.assets_file.name,obj.path_id)
            if key not in before:owners[key]=entry['key']
    return env,owners

def export_textures(env,owners,selected,output,progress):
    folder=output/'textures';records=[]
    for obj in env.objects:
        if obj.type.name not in ('Texture2D','Sprite'):continue
        source=owners.get((obj.assets_file.name,obj.path_id))
        if source not in selected:continue
        data=obj.read();name=data.m_Name or obj.type.name
        progress('导出贴图 '+name)
        image=data.image
        if image.width*image.height>100000000:raise ValueError('贴图像素量超过首版上限')
        folder.mkdir(exist_ok=True)
        target=folder/(source.replace(':','_')+'__'+safe_label(name)+'__'+str(obj.path_id)+'.png')
        image.save(target,format='PNG')
        records.append(dict(source=source,path_id=obj.path_id,name=name,file=target.relative_to(output).as_posix(),
                            width=image.width,height=image.height,sha256=digest(target,'sha256')))
    if not records:raise ValueError('所选资源没有可导出的 Texture2D 或 Sprite；可选择包含贴图的依赖资源')
    return records

def export_models(plan,decoded,output,project,version,progress,canceled):
    tool=project/'tools/AssetStudio/GakumasModelFbx.exe'
    if not tool.is_file():raise ValueError('缺少 FBX 导出工具，请运行 Setup.ps1')
    sources=[e for e in plan['entries'] if e['key'] in plan['selected'] and e['section']==2]
    if not sources:raise ValueError('模型导出需要选择 AssetBundle 资源')
    work=output.parent/'_work/models';work.mkdir(parents=True,exist_ok=True)
    normalized=tool_inputs(plan['entries'],decoded,work/'inputs',version)
    target=output/'models';target.mkdir(exist_ok=True)
    request=dict(inputs=[str(normalized[e['key']]) for e in plan['entries'] if e['section']==2],
                 sources=[dict(path=str(normalized[e['key']]),key=e['key'].replace(':','_'),name=e['name']) for e in sources],
                 output=str(target),unity_version=version)
    request_path=work/'request.json';write_json(request_path,request)
    progress('生成 FBX 模型与材质贴图')
    with (work/'export.log').open('wb') as log:
        process=subprocess.Popen([str(tool),str(request_path)],cwd=tool.parent,stdout=log,stderr=subprocess.STDOUT,creationflags=hidden_flags())
        try:
            deadline=time.monotonic()+600
            while process.poll() is None:
                if canceled():raise InterruptedError('任务已取消')
                if time.monotonic()>deadline:raise TimeoutError('FBX 导出超时')
                time.sleep(.25)
            if process.returncode:
                tail=(work/'export.log').read_text(encoding='utf-8',errors='replace')[-2500:]
                raise ValueError('FBX 导出失败：'+tail)
        finally:
            if process.poll() is None:process.terminate();process.wait(timeout=10)
    report_path=target/'model-report.json'
    if not report_path.is_file():raise ValueError('FBX 工具没有生成报告')
    report=json.loads(report_path.read_text(encoding='utf-8'))
    files=list(target.rglob('*.fbx'))
    if not files or any(p.stat().st_size<128 for p in files):raise ValueError('未生成有效 FBX 文件')
    return report

def export_motion(plan,decoded,env,owners,output,project,version,progress,canceled):
    expected=[]
    for obj in env.objects:
        source=owners.get((obj.assets_file.name,obj.path_id))
        if obj.type.name=='AnimationClip' and source in plan['selected']:
            tree=obj.read_typetree()
            expected.append(dict(source=source,path_id=obj.path_id,name=tree.get('m_Name',''),sample_rate=tree.get('m_SampleRate'),
                                 legacy=tree.get('m_Legacy'),bindings=len(tree.get('m_ClipBindingConstant',{}).get('genericBindings',[]))))
    if not expected:raise ValueError('所选资源没有内部 AnimationClip')
    names=[x['name'] for x in expected]
    if len(set(names))!=len(names):raise ValueError('所选动作含同名片段，请拆分选择，避免错误关联')
    tool=project/'tools/AssetRipper/AssetRipper.GUI.Free.exe'
    if not tool.is_file():raise ValueError('缺少 AssetRipper，请运行 Setup.ps1')
    work=output.parent/'_work/motion';work.mkdir(parents=True,exist_ok=True)
    normalized=tool_inputs(plan['entries'],decoded,work/'inputs',version)
    folder=output/'animations';folder.mkdir(exist_ok=True)
    with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    base='http://127.0.0.1:'+str(port)
    with (work/'export.log').open('wb') as log:
        process=subprocess.Popen([str(tool),'--port',str(port),'--headless','--log-path',str(work/'assetripper.log')],
                                  cwd=work,stdout=log,stderr=subprocess.STDOUT,creationflags=hidden_flags())
        try:
            deadline=time.monotonic()+60
            while True:
                if canceled():raise InterruptedError('任务已取消')
                try:opener.open(base,timeout=2).close();break
                except OSError:
                    if process.poll() is not None or time.monotonic()>deadline:raise ValueError('AssetRipper 启动失败；详见动作导出日志')
                    time.sleep(.2)
            progress('恢复 AnimationClip 流式曲线')
            paths=list(dict.fromkeys(str(normalized[e['key']]) for e in plan['entries'] if e['section']==2))
            payload=urllib.parse.urlencode([('Path',p) for p in paths]).encode()
            opener.open(urllib.request.Request(base+'/LoadFile',data=payload),timeout=600).close()
            if canceled():raise InterruptedError('任务已取消')
            target=work/'unity-project'
            payload=urllib.parse.urlencode({'Path':str(target)}).encode()
            opener.open(urllib.request.Request(base+'/Export/UnityProject',data=payload),timeout=600).close()
            candidates={}
            for path in target.rglob('*.anim'):
                with path.open(encoding='utf-8-sig',errors='replace') as stream:text=stream.read(16384)
                if 'AnimationClip:' not in text:continue
                match=re.search(r'^  m_Name: (.*)$',text,re.M)
                name=match[1].strip().strip('"') if match else path.stem
                candidates.setdefault(name,[]).append(path)
            records=[]
            for source in expected:
                matches=candidates.get(source['name'],[])
                if len(matches)!=1:raise ValueError('动作未唯一恢复：'+source['name']+'；保留原始包与日志')
                target=folder/(source['source'].replace(':','_')+'__'+safe_label(source['name'])+'.anim')
                shutil.copy2(matches[0],target)
                meta=matches[0].with_suffix('.anim.meta')
                if meta.is_file():shutil.copy2(meta,target.with_suffix('.anim.meta'))
                records.append(dict(source,file=target.relative_to(output).as_posix(),sha256=digest(target,'sha256'),bytes=target.stat().st_size))
            return records
        finally:
            process.terminate()
            try:process.wait(timeout=10)
            except subprocess.TimeoutExpired:process.kill();process.wait()

def export_selection(plan,decoded,output,mode,project,version,progress,canceled):
    report=dict(schema=1,mode=mode,models=[],textures=[],animations=[],raw=[],warnings=[],visual_validation=False,
                working_copy_version_headers_restored=mode in ('auto','model','motion'))
    selected=[e for e in plan['entries'] if e['key'] in plan['selected']]
    if mode=='download':
        folder=output/'bundles';folder.mkdir(exist_ok=True)
        for entry in plan['entries']:
            extension='.unity3d' if entry['section']==2 else (Path(entry['name']).suffix or '.bin')
            label=safe_label(entry['name'])
            if entry['section']==2 or not label.lower().endswith(extension.lower()):label+=extension
            target=folder/(entry['key'].replace(':','_')+'__'+label)
            shutil.copy2(decoded[entry['key']],target)
            report['raw'].append(dict(source=entry['key'],file=target.relative_to(output).as_posix(),sha256=digest(target,'sha256')))
        return report
    models=[e for e in selected if e['category']=='model'] if mode=='auto' else selected if mode=='model' else []
    textures=[e for e in selected if e['category']=='texture'] if mode=='auto' else selected if mode=='texture' else []
    motions=[e for e in selected if e['category']=='motion'] if mode=='auto' else selected if mode=='motion' else []
    recognized={e['key'] for e in models+textures+motions}
    if not recognized:raise ValueError('所选资源暂不支持自动转换；可使用“仅下载原始资源”')
    env,owners=load_environment(plan['entries'],decoded,version)
    report['object_counts']=dict(Counter(o.type.name for o in env.objects))
    def subset(entries):return dict(plan,selected=[e['key'] for e in entries])
    if models:report['models']=export_models(subset(models),decoded,output,project,version,progress,canceled)
    if textures:report['textures']=export_textures(env,owners,{e['key'] for e in textures},output,progress)
    if motions:report['animations']=export_motion(subset(motions),decoded,env,owners,output,project,version,progress,canceled)
    unknown=[e['name'] for e in selected if e['key'] not in recognized]
    if unknown:report['warnings'].append('未转换的选择项：'+', '.join(unknown))
    report['warnings'].append('原游戏 Shader、动态装配、物理与完整演出未还原；模型部件分别导出。')
    return report
