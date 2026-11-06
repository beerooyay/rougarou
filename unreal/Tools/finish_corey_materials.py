"""Unreal Python commandlet: set explicit fabric, stubble and skin finishes."""
import unreal
root='/Game/Local/CoreyCloth/'
finishes={'Ivory_Cotton':(.96,.18),'Forest_Green_Twill':(.9,.18),'Brown_Buzz_Fibers':(.95,.12),'Brown_Buzz_Scalp':(.85,.18),'Ivory_Collar_Rib':(.96,.18),'Climbing_Shoe_Rubber':(.87,.2),'Ochre_Shoe_Panels':(.8,.22),'Std_Skin_Head':(.65,.25),'Std_Skin_Body':(.65,.25),'Std_Skin_Arm':(.65,.25),'Std_Skin_Leg':(.65,.25)}
mesh=unreal.load_asset(root+'climber')
if not mesh:raise RuntimeError('missing skeletal mesh climber')
slots=list(mesh.get_editor_property('materials'))
unreal.log('Climber slots: '+', '.join(f"{s.get_editor_property('material_slot_name')}={s.get_editor_property('material_interface').get_name() if s.get_editor_property('material_interface') else None}" for s in slots))
# Flat authored colours for garment pieces the importer leaves without a material (reimports keep the old slot list).
plain={'Brown_Buzz_Scalp':(.085,.05,.03)}
for name,(rough,specular) in finishes.items():
 material=unreal.load_asset(root+name)
 if not material and name in plain:
  material=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,root.rstrip('/'),unreal.Material,unreal.MaterialFactoryNew())
  color=unreal.MaterialEditingLibrary.create_material_expression(material,unreal.MaterialExpressionConstant3Vector,-300,0)
  color.set_editor_property('constant',unreal.LinearColor(*plain[name],1))
  unreal.MaterialEditingLibrary.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
  changed=False
  for s in slots:
   if str(s.get_editor_property('material_slot_name'))==name or s.get_editor_property('material_interface') is None:
    s.set_editor_property('material_interface',material);changed=True
  if changed:mesh.set_editor_property('materials',slots);unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)
 if not material:raise RuntimeError('missing material '+name)
 for parameter,value,prop in [('SurfaceRoughness',rough,unreal.MaterialProperty.MP_ROUGHNESS),('SurfaceSpecular',specular,unreal.MaterialProperty.MP_SPECULAR)]:
  node=next((e for e in unreal.MaterialEditingLibrary.get_material_expressions(material) if isinstance(e,unreal.MaterialExpressionScalarParameter) and str(e.get_editor_property('parameter_name'))==parameter),None)
  if node is None:node=unreal.MaterialEditingLibrary.create_material_expression(material,unreal.MaterialExpressionScalarParameter,-300,200 if parameter=='SurfaceRoughness' else 350)
  node.set_editor_property('parameter_name',parameter);node.set_editor_property('default_value',value);unreal.MaterialEditingLibrary.connect_material_property(node,'',prop)
 material.set_editor_property('used_with_skeletal_mesh',True)
 if name.startswith('Std_Skin'):material.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_SUBSURFACE_PROFILE)
 if name in ('Ivory_Cotton','Forest_Green_Twill'):
  material.set_editor_property('used_with_clothing',True)
 unreal.MaterialEditingLibrary.recompile_material(material)
 unreal.EditorAssetLibrary.save_loaded_asset(material,False)
 unreal.log('Finished Corey material '+name)
