using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using AssetStudio;
using Newtonsoft.Json;

// Independent integration of AssetStudioMod's public model exporter API (MIT).
public class GakumasModelFbx {
 public class Source { public string path; public string key; public string name; }
 public class Request { public string[] inputs; public Source[] sources; public string output; public string unity_version; }
 static string Full(string value){return Path.GetFullPath(value);}
 static string Label(string value){return String.Join("_",value.Split(Path.GetInvalidFileNameChars())).Trim('.',' ');}
 static int Renderers(GameObject root,HashSet<long> seen){
  if(!seen.Add(root.m_PathID))return 0;
  int count=(root.m_SkinnedMeshRenderer!=null||root.m_MeshRenderer!=null)?1:0;
  if(root.m_Transform!=null)foreach(var child in root.m_Transform.m_Children){Transform transform;GameObject go;
   if(!child.TryGet(out transform)||!transform.m_GameObject.TryGet(out go))throw new Exception("Unresolved child Transform");
   count+=Renderers(go,seen);
  }
  return count;
 }
 static List<string> RepairSingleSourceMesh(SerializedFile file) {
  var meshes=file.Objects.OfType<Mesh>().ToArray();var renderers=file.Objects.OfType<MeshRenderer>().ToArray();
  var repairs=new List<string>();
  if(meshes.Length!=1||renderers.Length!=1)return repairs;
  GameObject go;if(!renderers[0].m_GameObject.TryGet(out go))return repairs;
  var filter=go.m_MeshFilter;
  if(filter==null){
   var candidates=file.Objects.OfType<MeshFilter>().Where(f=>{GameObject owner;return f.m_GameObject.TryGet(out owner)&&object.ReferenceEquals(owner,go);}).ToArray();
   if(candidates.Length!=1)return repairs;filter=candidates[0];go.m_MeshFilter=filter;
  }
  Mesh resolved;
  if(filter.m_Mesh==null||!filter.m_Mesh.TryGet(out resolved)){
   filter.m_Mesh=new PPtr<Mesh>{AssetsFile=file,m_FileID=0,m_PathID=meshes[0].m_PathID};
   repairs.Add("Single source-file Mesh assigned to single unresolved MeshRenderer: "+go.m_Name+" -> "+meshes[0].m_Name);
  }
  return repairs;
 }
 public static int Main(string[] args){try{
  var request=JsonConvert.DeserializeObject<Request>(File.ReadAllText(args[0]));
  var manager=new AssetsManager();manager.OptionLoaders.Clear();
  manager.Options.CustomUnityVersion=new UnityVersion(request.unity_version);
  manager.LoadFilesAndFolders(request.inputs);
  var reports=new List<object>();int total=0;
  foreach(var source in request.sources){
   var files=manager.AssetsFileList.Where(f=>String.Equals(Full(f.originalPath),Full(source.path),StringComparison.OrdinalIgnoreCase)).ToArray();
   if(files.Length==0)throw new Exception("Selected source was not loaded: "+source.name);
   int exported=0;
   foreach(var file in files){
    var repairs=RepairSingleSourceMesh(file);
    foreach(var root in file.Objects.OfType<GameObject>()){
    if(root.m_Transform==null)continue;
    Transform father;if(root.m_Transform.m_Father.TryGet(out father))continue;
    int renderers=Renderers(root,new HashSet<long>());if(renderers==0)continue;
    var model=new ModelConverter(root,ImageFormat.Png,new List<AnimationClip>());
    if(model.MeshList.Count==0)throw new Exception("Source has renderers but no convertible meshes: "+source.name);
    var folder=Path.Combine(request.output,source.key);Directory.CreateDirectory(folder);
    var target=Path.Combine(folder,Label(root.m_Name)+"_"+root.m_PathID+".fbx");
    ModelExporter.ExportFbx(target,model,new Fbx.Settings{ScaleFactor=1,ExportAnimations=false});
    if(!File.Exists(target)||new FileInfo(target).Length<128)throw new Exception("FBX was not written");
    reports.Add(new{source=source.key,resource=source.name,root=root.m_Name,path_id=root.m_PathID,
     meshes=model.MeshList.Count,materials=model.MaterialList.Count,textures=model.TextureList.Count,
     skin_bone_bindings=model.MeshList.Sum(mesh=>mesh.BoneList==null?0:mesh.BoneList.Count),
     animations=model.AnimationList.Count,file=target,selection="selected_source_roots",shader_restored=false,mesh_reference_repairs=repairs});
    exported++;total++;
   }
   }
   if(exported==0)throw new Exception("Selected source has no root with model geometry: "+source.name);
  }
  File.WriteAllText(Path.Combine(request.output,"model-report.json"),JsonConvert.SerializeObject(reports,Formatting.Indented));
  manager.Clear();Console.WriteLine("GAKUMAS_FBX_OK models="+total);return 0;
 }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
