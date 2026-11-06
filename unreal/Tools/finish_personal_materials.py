"""Finish the personal player; preserve native CC4 skin materials."""
import json, os
from pathlib import Path
import unreal

ROOT = os.environ.get('ROUGAROU_PLAYER_DEST', '/Game/Local/PersonalPlayer').rstrip('/') + '/'
SOURCE = Path(os.environ.get('ROUGAROU_PLAYER_SOURCE', str(Path(__file__).resolve().parents[2] / 'assets/character/personal-player')))
if not ROOT.startswith('/Game/Local/') or '..' in ROOT:
    raise RuntimeError('Player materials destination must be under /Game/Local')
manifest = json.loads((SOURCE / 'materials.json').read_text())
EDIT = unreal.MaterialEditingLibrary
mesh = unreal.load_asset(ROOT + 'climber')
if not mesh:
    raise RuntimeError('Missing personal climber')
native = {s['slot']: s['material'] for s in json.loads((SOURCE / 'native-materials.json').read_text())}
textures = {}
needed = {v[k] for n,v in manifest['materials'].items() if n not in native for k in ('color_texture','normal_texture','roughness_texture') if k in v}
for name, spec in manifest['images'].items():
    if name not in needed: continue
    task = unreal.AssetImportTask()
    task.filename = str(SOURCE / spec['file'])
    task.destination_path = ROOT.rstrip('/')
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(ROOT + name)
    if not texture:
        raise RuntimeError('Missing atlas ' + name)
    texture.set_editor_property('srgb', spec['srgb'])
    texture.set_editor_property('max_texture_size', 4096)
    if spec['normal']:
        texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property('flip_green_channel', True)
    unreal.EditorAssetLibrary.save_loaded_asset(texture, False)
    textures[name] = texture


def constant(material, value):
    node = EDIT.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 300)
    node.set_editor_property('r', value)
    return node


def sample(material, key, normal=False):
    node = EDIT.create_material_expression(material, unreal.MaterialExpressionTextureSample, -800, 0)
    node.set_editor_property('texture', textures[key])
    if normal:
        node.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    elif not textures[key].get_editor_property('srgb'):
        node.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    return node


slots = list(mesh.get_editor_property('materials'))
for slot in slots:
    name = str(slot.get_editor_property('imported_material_slot_name'))
    if name not in manifest['materials']:
        name = str(slot.get_editor_property('material_slot_name'))
    if name not in manifest['materials']:
        raise RuntimeError('Unknown personal material slot ' + name)
    if name in native:
        material = unreal.load_asset(native[name])
        if not material: raise RuntimeError('Missing preserved skin material ' + name)
        slot.set_editor_property('material_interface', material)
        continue
    spec = manifest['materials'][name]
    material = unreal.load_asset(ROOT + name)
    if not material:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, ROOT.rstrip('/'), unreal.Material, unreal.MaterialFactoryNew())
    EDIT.delete_all_material_expressions(material)
    cloth = name.startswith('Outfit_Fabric')
    color = sample(material, spec['color_texture']) if 'color_texture' in spec else None
    if cloth:
        region = EDIT.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -800, -200)
        region.set_editor_property('coordinate_index', 1)
        select = EDIT.create_material_expression(material, unreal.MaterialExpressionComponentMask, -600, -200)
        select.set_editor_property('r', True)
        select.set_editor_property('g', False)
        select.set_editor_property('b', False)
        select.set_editor_property('a', False)
        if not EDIT.connect_material_expressions(region, '', select, EDIT.get_material_expression_input_names(select)[0]):
            raise RuntimeError('Could not connect fabric region UV')
        tint = EDIT.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -300, -200)
        for socket, value in [('A', (.80,.78,.73,1)), ('B', (.018,.095,.045,1))]:
            color_node = EDIT.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -500, -300)
            color_node.set_editor_property('constant', unreal.LinearColor(*value))
            EDIT.connect_material_expressions(color_node, '', tint, socket)
        EDIT.connect_material_expressions(select, '', tint, 'Alpha')
        EDIT.connect_material_property(tint, '', unreal.MaterialProperty.MP_BASE_COLOR)
        material.set_editor_property('two_sided', True)
        material.set_editor_property('used_with_clothing', True)
    elif color:
        EDIT.connect_material_property(color, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
    else:
        color = EDIT.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -400, 0)
        color.set_editor_property('constant', unreal.LinearColor(*spec['color']))
        EDIT.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    if 'normal_texture' in spec:
        normal = sample(material, spec['normal_texture'], True)
        EDIT.connect_material_property(normal, 'RGB', unreal.MaterialProperty.MP_NORMAL)
    roughness = .88 if cloth or name.startswith('Collar') else .68 if name.startswith('Skin') else .8
    if 'roughness_texture' in spec and not cloth:
        roughness_map = sample(material, spec['roughness_texture'])
        EDIT.connect_material_property(roughness_map, 'R', unreal.MaterialProperty.MP_ROUGHNESS)
    else:
        EDIT.connect_material_property(constant(material, roughness), '', unreal.MaterialProperty.MP_ROUGHNESS)
    EDIT.connect_material_property(constant(material, .18 if cloth else .25), '', unreal.MaterialProperty.MP_SPECULAR)
    material.set_editor_property('used_with_skeletal_mesh', True)
    if name.startswith('Skin'):
        material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_SUBSURFACE_PROFILE)
    EDIT.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    slot.set_editor_property('material_interface', material)
    unreal.log('Finished personal material ' + name)
mesh.set_editor_property('materials', slots)
unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
