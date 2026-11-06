"""Capture the installed stable avatar and clips without altering their packages."""
import json
from pathlib import Path
import unreal
out=Path(__file__).resolve().parents[2]/'assets/character/personal-player';out.mkdir(parents=True,exist_ok=True)
(out/'source-clips').mkdir(exist_ok=True)
root='/Game/Local/CoreyPlayer/'
mesh=unreal.load_asset(root+'climber');assert mesh
slots=[]
for s in mesh.get_editor_property('materials'):
 m=s.get_editor_property('material_interface')
 slots.append({'slot':str(s.get_editor_property('imported_material_slot_name')),'material':m.get_path_name() if m else None})
(out/'native-materials.json').write_text(json.dumps(slots,indent=2))
for name in ['climber','idle','walk','jog','run','start','stop']:
 obj=mesh if name=='climber' else unreal.load_asset(root+name);assert obj,name
 task=unreal.AssetExportTask();task.object=obj;task.filename=str(out/('source.fbx' if name=='climber' else 'source-clips/'+name+'.fbx'));task.automated=True;task.prompt=False;task.replace_identical=True
 task.exporter=unreal.SkeletalMeshExporterFBX() if name=='climber' else unreal.AnimSequenceExporterFBX()
 options=unreal.FbxExportOption();options.set_editor_property('ascii',False);options.set_editor_property('export_preview_mesh',False);task.options=options
 if not unreal.Exporter.run_asset_export_task(task):raise RuntimeError('Could not capture '+name)
 unreal.log('BASELINE SOURCE EXPORTED '+name)
