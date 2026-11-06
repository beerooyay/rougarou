"""Import the local rebuilt character after running rebuild_character.py in Blender."""
import json, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'assets/character/locomotion'
EDITOR=Path('/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor')
DEST='/Game/Local/Player'
for kind in ['mesh','anims']:
    settings={'bImportMaterials':kind=='mesh','bImportTextures':kind=='mesh','bImportAnimations':kind=='anims','bImportMesh':kind=='mesh'}
    if kind=='mesh':settings.update(MeshTypeToImport='FBXIT_SkeletalMesh',bImportAsSkeletal=True,bCreatePhysicsAsset=False,SkeletalMeshImportData={'bUseT0AsRefPose':False})
    else:settings.update(MeshTypeToImport='FBXIT_Animation',Skeleton=DEST+'/climber_Skeleton',AnimSequenceImportData={'AnimationLength':'FBXALIT_ExportedTime'})
    names=['climber'] if kind=='mesh' else ['idle','walk','jog','run','start','stop']
    spec=OUT/(kind+'-import.json')
    spec.write_text(json.dumps({'ImportGroups':[{'GroupName':kind,'FactoryName':'FbxFactory','DestinationPath':DEST,'Filenames':[str(OUT/(n+'.fbx')) for n in names],'bReplaceExisting':True,'ImportSettings':settings}]},indent=2))
    log=OUT/(kind+'-import.log')
    with (OUT/(kind+'-import.out')).open('w') as stdout:
        subprocess.run([str(EDITOR),str(ROOT/'unreal/Rougarou.uproject'),'-run=ImportAssets','-dest='+DEST,'-importsettings='+str(spec),'-nosourcecontrol','-unattended','-nullrhi','-nosound','-nop4','-noanalytics','-abslog='+str(log)],stdout=stdout,stderr=subprocess.STDOUT,check=True)
    if 'finished execution (result 0)' not in log.read_text():raise RuntimeError('import did not complete: '+str(log))
    for n in names:
        if not (ROOT/'unreal/Content/Local/Player'/(n+'.uasset')).exists():raise RuntimeError('missing imported asset: '+n)
    print('Imported',kind,flush=True)
