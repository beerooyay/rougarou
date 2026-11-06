"""Generate the native hold finish with fine, world-space grain."""
import unreal

path = '/Game/Gym'
name = 'M_HoldGrit'
edit = unreal.MaterialEditingLibrary
material = unreal.load_asset(f'{path}/{name}')
if material is None:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, path, unreal.Material, unreal.MaterialFactoryNew()
    )
edit.delete_all_material_expressions(material)

color = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -560, -160)
color.set_editor_property('parameter_name', 'Color')
color.set_editor_property('default_value', unreal.LinearColor(.42, .42, .42, 1))

grain = edit.create_material_expression(material, unreal.MaterialExpressionNoise, -560, 60)
fast_gradient = next(name for name in dir(unreal.NoiseFunction) if 'GRADIENT' in name and '3' in name)
grain.set_editor_property('noise_function', getattr(unreal.NoiseFunction, fast_gradient))
for key, value in {
    'scale': 7.0,
    'quality': 1,
    'levels': 1,
    'output_min': .79,
    'output_max': 1.12,
}.items():
    grain.set_editor_property(key, value)

tint = edit.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, -110)
edit.connect_material_expressions(color, '', tint, 'A')
edit.connect_material_expressions(grain, '', tint, 'B')
edit.connect_material_property(tint, '', unreal.MaterialProperty.MP_BASE_COLOR)

for prop, value in [('MP_ROUGHNESS', .89), ('MP_SPECULAR', .27)]:
    constant = edit.create_material_expression(material, unreal.MaterialExpressionConstant, -240, 100 if prop == 'MP_ROUGHNESS' else 220)
    constant.set_editor_property('r', value)
    edit.connect_material_property(constant, '', getattr(unreal.MaterialProperty, prop))

edit.recompile_material(material)
if not unreal.EditorAssetLibrary.save_loaded_asset(material, False):
    raise RuntimeError('Could not save the hold material')
