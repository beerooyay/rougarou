"""Import a built character and its matching clips; defaults to the Corey fallback."""
import argparse, json, os, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--source-dir',type=Path,default=ROOT/'assets/character/corey')
parser.add_argument('--destination',default='/Game/Local/CoreyCloth')
parser.add_argument('--finish-script',type=Path,default=ROOT/'unreal/Tools/finish_corey_materials.py')
parser.add_argument('--external-materials',action='store_true',help='Import slot identities; finish script imports shared texture atlases')
parser.add_argument('--preserve-normals',action='store_true',help='Keep the authored surface normals rather than rebuilding them from FBX smoothing groups')
args=parser.parse_args()
OUT=args.source_dir.resolve()
EDITOR=Path('/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor')
DEST=args.destination.rstrip('/')
if not DEST.startswith('/Game/Local/') or '..' in DEST:raise RuntimeError('character destination must be under /Game/Local')
for kind in ['mesh','anims']:
    settings={'bImportMaterials':kind=='mesh' and not args.external_materials,'bImportTextures':kind=='mesh' and not args.external_materials,'bImportAnimations':kind=='anims','bImportMesh':kind=='mesh'}
    if kind=='mesh':settings.update(MeshTypeToImport='FBXIT_SkeletalMesh',bImportAsSkeletal=True,bCreatePhysicsAsset=False,SkeletalMeshImportData={'bUseT0AsRefPose':False,'bUpdateSkeletonReferencePose':True,'VertexColorImportOption':'Replace'})
    else:settings.update(MeshTypeToImport='FBXIT_Animation',Skeleton=DEST+'/climber_Skeleton',AnimSequenceImportData={'AnimationLength':'FBXALIT_ExportedTime'})
    if kind=='mesh' and args.preserve_normals:settings['SkeletalMeshImportData']['NormalImportMethod']='FBXNIM_ImportNormals'
    names=['climber'] if kind=='mesh' else ['idle','walk','jog','run','start','stop']
    spec=OUT/(kind+'-import.json')
    spec.write_text(json.dumps({'ImportGroups':[{'GroupName':kind,'FactoryName':'FbxFactory','DestinationPath':DEST,'Filenames':[str(OUT/(n+'.fbx')) for n in names],'bReplaceExisting':True,'ImportSettings':settings}]},indent=2))
    log=OUT/(kind+'-import.log')
    with (OUT/(kind+'-import.out')).open('w') as stdout:
        # Interchange's compatibility converter can discard FbxFactory normal
        # settings. Use the matching legacy factory for explicit normal import.
        importer_flags=['-dpcvars=Interchange.FeatureFlags.Import.FBX=0'] if args.preserve_normals else []
        subprocess.run([str(EDITOR),str(ROOT/'unreal/Rougarou.uproject'),'-run=ImportAssets','-dest='+DEST,'-importsettings='+str(spec),'-nosourcecontrol','-unattended','-nullrhi','-nosound','-nop4','-noanalytics','-abslog='+str(log)]+importer_flags,stdout=stdout,stderr=subprocess.STDOUT,check=True)
    if 'finished execution (result 0)' not in log.read_text():raise RuntimeError('import did not complete: '+str(log))
    for n in names:
        if not (ROOT/'unreal/Content'/DEST.removeprefix('/Game/')/(n+'.uasset')).exists():raise RuntimeError('missing imported asset: '+n)
    print('Imported',kind,flush=True)

with (OUT/'material-finish.out').open('w') as stdout:
    finish_env={**os.environ,'ROUGAROU_PLAYER_SOURCE':str(OUT),'ROUGAROU_PLAYER_DEST':DEST}
    subprocess.run([str(EDITOR),str(ROOT/'unreal/Rougarou.uproject'),'-run=pythonscript','-script='+str(args.finish_script.resolve()),'-unattended','-nullrhi','-nosound','-nop4','-noanalytics','-abslog='+str(OUT/'material-finish.log')],stdout=stdout,stderr=subprocess.STDOUT,check=True,env=finish_env)
print('Finished surface materials',flush=True)
