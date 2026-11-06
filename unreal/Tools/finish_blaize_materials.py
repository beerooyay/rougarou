"""Import the original glTF PBR maps without tinting or garment substitutions."""
import json
import os
from pathlib import Path
import unreal

ROOT = os.environ['ROUGAROU_PLAYER_DEST'].rstrip('/')
SOURCE = Path(os.environ['ROUGAROU_PLAYER_SOURCE'])
assert ROOT in ('/Game/Local/BlaizePlayer', '/Game/Local/Blaize2Player', '/Game/Local/Blaize2ShoulderPlayer')
manifest = json.loads((SOURCE/'materials.json').read_text())
edit = unreal.MaterialEditingLibrary
textures = {}
for name, spec in manifest['images'].items():
    task = unreal.AssetImportTask()
    task.filename = str(SOURCE/spec['file'])
    task.destination_path = ROOT
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(ROOT+'/'+name)
    if not texture: raise RuntimeError('Missing original texture '+name)
    texture.set_editor_property('srgb', spec['srgb'])
    if spec['normal']:
        texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property('flip_green_channel', True)
    unreal.EditorAssetLibrary.save_loaded_asset(texture, False)
    textures[name] = texture

mesh = unreal.load_asset(ROOT+'/climber')
slots = list(mesh.get_editor_property('materials'))
for slot in slots:
    name = str(slot.get_editor_property('imported_material_slot_name'))
    spec = manifest['materials'][name]
    material = unreal.load_asset(ROOT+'/'+name)
    if not material:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    edit.delete_all_material_expressions(material)
    for role, channel, prop in [('color_texture','RGB',unreal.MaterialProperty.MP_BASE_COLOR),
                                ('normal_texture','RGB',unreal.MaterialProperty.MP_NORMAL),
                                ('roughness_texture','G',unreal.MaterialProperty.MP_ROUGHNESS),
                                ('metallic_texture','B',unreal.MaterialProperty.MP_METALLIC)]:
        node = edit.create_material_expression(material, unreal.MaterialExpressionTextureSample, -500, 0)
        node.set_editor_property('texture', textures[spec[role]])
        sampler = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if role=='color_texture' else unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if role=='normal_texture' else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
        node.set_editor_property('sampler_type', sampler)
        if not edit.connect_material_property(node, channel, prop): raise RuntimeError('PBR connection failed: '+role)
    material.set_editor_property('two_sided', spec['two_sided'])
    material.set_editor_property('used_with_skeletal_mesh', True)
    edit.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    slot.set_editor_property('material_interface', material)
mesh.set_editor_property('materials', slots)
unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)

# Reimports retain old LOD build settings; explicitly preserve authored normals.
subsystem = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
settings = subsystem.get_lod_build_settings(mesh, 0)
settings.recompute_normals = False
settings.compute_weighted_normals = False
subsystem.set_lod_build_settings(mesh, 0, settings)
unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
