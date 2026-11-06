"""Unreal Python commandlet: author the gym surface materials (world-aligned, no UVs needed).

M_Panel  - plywood or charcoal climbing panel with vertical grain and a 20cm t-nut grid.
M_Rubber - matte crash-pad vinyl with a fine grain.
Run:  UnrealEditor Rougarou.uproject -run=pythonscript -script=unreal/Tools/build_gym_materials.py -unattended -nullrhi
"""
import unreal

PATH = '/Game/Gym'
EDIT = unreal.MaterialEditingLibrary


def material(name):
    asset = unreal.load_asset(f'{PATH}/{name}')
    if asset is None:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, PATH, unreal.Material, unreal.MaterialFactoryNew())
    EDIT.delete_all_material_expressions(asset)
    return asset


class Graph:
    def __init__(self, asset):
        self.asset, self.y = asset, -600

    def node(self, cls, x=-400, **props):
        self.y += 90
        expr = EDIT.create_material_expression(self.asset, cls, x, self.y)
        for key, value in props.items():
            expr.set_editor_property(key, value)
        return expr

    def const(self, value, x=-400):
        return self.node(unreal.MaterialExpressionConstant, x, r=value)

    def vec(self, x, y, z, px=-400):
        return self.node(unreal.MaterialExpressionConstant3Vector, px, constant=unreal.LinearColor(x, y, z, 1))

    def scalar(self, name, value, x=-800):
        return self.node(unreal.MaterialExpressionScalarParameter, x, parameter_name=name, default_value=value)

    def color(self, name, r, g, b, x=-800):
        return self.node(unreal.MaterialExpressionVectorParameter, x, parameter_name=name, default_value=unreal.LinearColor(r, g, b, 1))

    def op(self, cls, a, b, x=-200, a_out='', b_out='', **props):
        expr = self.node(cls, x, **props)
        if a is not None:
            EDIT.connect_material_expressions(a, a_out, expr, 'A')
        if b is not None:
            EDIT.connect_material_expressions(b, b_out, expr, 'B')
        return expr

    def mul(self, a, b, **kw):
        return self.op(unreal.MaterialExpressionMultiply, a, b, **kw)

    def add(self, a, b, **kw):
        return self.op(unreal.MaterialExpressionAdd, a, b, **kw)

    def sub(self, a, b, **kw):
        return self.op(unreal.MaterialExpressionSubtract, a, b, **kw)

    def noise(self, position, scale, levels, out_min, out_max, turbulence=False, x=-400):
        expr = self.node(unreal.MaterialExpressionNoise, x)
        fast = next(n for n in dir(unreal.NoiseFunction) if 'GRADIENT' in n and '3' in n)
        for key, value in {'noise_function': getattr(unreal.NoiseFunction, fast), 'scale': scale, 'quality': 1,
                           'levels': levels, 'output_min': out_min, 'output_max': out_max, 'turbulence': turbulence,
                           'level_scale': 2.2}.items():
            expr.set_editor_property(key, value)
        EDIT.connect_material_expressions(position, '', expr, 'Position')
        return expr

    def lerp(self, a, b, alpha, x=-100):
        expr = self.node(unreal.MaterialExpressionLinearInterpolate, x)
        EDIT.connect_material_expressions(a, '', expr, 'A')
        EDIT.connect_material_expressions(b, '', expr, 'B')
        EDIT.connect_material_expressions(alpha, '', expr, 'Alpha')
        return expr

    def out(self, expr, prop):
        EDIT.connect_material_property(expr, '', getattr(unreal.MaterialProperty, prop))

    def mask(self, expr, r=False, g=False, b=False, x=-500):
        m = self.node(unreal.MaterialExpressionComponentMask, x, r=r, g=g, b=b, a=False)
        EDIT.connect_material_expressions(expr, '', m, '')
        return m

    def lattice(self, world, plane):
        cell = self.mul(self.mask(world, *plane), self.const(1 / 20.0))
        frac = self.node(unreal.MaterialExpressionFrac, -300)
        EDIT.connect_material_expressions(cell, '', frac, '')
        centered = self.sub(frac, self.const(0.5))
        dist = self.node(unreal.MaterialExpressionDistance, -200)
        EDIT.connect_material_expressions(centered, '', dist, 'A')
        EDIT.connect_material_expressions(self.const(0), '', dist, 'B')
        hole = self.node(unreal.MaterialExpressionSmoothStep, -100)
        EDIT.connect_material_expressions(dist, '', hole, 'Value')
        EDIT.connect_material_expressions(self.const(0.05), '', hole, 'Min')
        EDIT.connect_material_expressions(self.const(0.065), '', hole, 'Max')
        return hole

    def tnut_holes(self, world):
        """1 everywhere, 0 inside 2cm t-nut holes on a 20cm lattice, projected along the panel's dominant axis."""
        facing_x = self.lattice(world, (False, True, True))
        facing_y = self.lattice(world, (True, False, True))
        normal = self.node(unreal.MaterialExpressionVertexNormalWS, -700)
        abs_normal = self.node(unreal.MaterialExpressionAbs, -600)
        EDIT.connect_material_expressions(normal, '', abs_normal, '')
        pick = self.node(unreal.MaterialExpressionIf, -50)
        EDIT.connect_material_expressions(self.mask(abs_normal, r=True), '', pick, 'A')
        EDIT.connect_material_expressions(self.mask(abs_normal, g=True), '', pick, 'B')
        EDIT.connect_material_expressions(facing_x, '', pick, 'A > B')
        EDIT.connect_material_expressions(facing_x, '', pick, 'A == B')
        EDIT.connect_material_expressions(facing_y, '', pick, 'A < B')
        return pick


def build_panel():
    asset = material('M_Panel')
    g = Graph(asset)
    world = g.node(unreal.MaterialExpressionWorldPosition, -1200)
    base = g.color('Color', .55, .43, .27)
    grain_amount = g.scalar('Grain', 1.0)
    rough_base = g.scalar('Roughness', .62)
    # long vertical birch grain (fine across, very long along Z), a coarse blotch so panels are not uniform, and a sanded speckle
    stretched = g.mul(world, g.vec(.16, .16, .004, -1000), x=-900)
    grain = g.noise(stretched, 1.0, 4, .74, 1.14, turbulence=True, x=-700)
    blotch = g.noise(g.mul(world, g.vec(.009, .009, .009, -1000), x=-900), 1.0, 2, .9, 1.06, x=-700)
    speckle = g.noise(g.mul(world, g.vec(2.5, 2.5, 2.5, -1000), x=-900), 1.0, 1, .93, 1.03, x=-700)
    tone = g.mul(g.mul(grain, blotch, x=-500), speckle, x=-450)
    grain_mix = g.lerp(g.const(1), tone, grain_amount, x=-350)
    holes = g.tnut_holes(world)
    hole_tint = g.lerp(g.const(.12), g.const(1), holes, x=-300)
    albedo = g.mul(g.mul(base, grain_mix, x=-200), hole_tint, x=-80)
    g.out(albedo, 'MP_BASE_COLOR')
    rough_noise = g.noise(g.mul(world, g.vec(.5, .5, .5, -1000), x=-900), 1.0, 1, -.08, .08, x=-700)
    rough = g.add(rough_base, rough_noise, x=-150)
    g.out(g.lerp(g.const(.95), rough, holes, x=-60), 'MP_ROUGHNESS')
    g.out(g.const(.35, -100), 'MP_SPECULAR')
    return asset


def build_rubber():
    asset = material('M_Rubber')
    g = Graph(asset)
    world = g.node(unreal.MaterialExpressionWorldPosition, -1200)
    base = g.color('Color', .035, .035, .04)
    fine = g.noise(g.mul(world, g.vec(1.2, 1.2, 1.2, -1000), x=-900), 1.0, 2, .8, 1.15, x=-700)
    g.out(g.mul(base, fine, x=-200), 'MP_BASE_COLOR')
    rough_noise = g.noise(g.mul(world, g.vec(.08, .08, .08, -1000), x=-900), 1.0, 2, .7, .9, x=-700)
    g.out(rough_noise, 'MP_ROUGHNESS')
    g.out(g.const(.3, -100), 'MP_SPECULAR')
    return asset


def build_hold():
    asset = material('M_HoldGrit')
    g = Graph(asset)
    world = g.node(unreal.MaterialExpressionWorldPosition, -1200)
    base = g.color('Color', .42, .42, .42)
    grit = g.noise(g.mul(world, g.vec(7, 7, 7, -1000), x=-900), 1.0, 1, .82, 1.1, x=-700)
    tone = g.noise(g.mul(world, g.vec(.4, .4, .4, -1000), x=-900), 1.0, 2, .9, 1.06, x=-700)
    g.out(g.mul(g.mul(base, grit, x=-300), tone, x=-150), 'MP_BASE_COLOR')
    rough = g.noise(g.mul(world, g.vec(3, 3, 3, -1000), x=-900), 1.0, 1, .72, .92, x=-700)
    g.out(rough, 'MP_ROUGHNESS')
    g.out(g.const(.3, -100), 'MP_SPECULAR')
    return asset


for built in (build_panel(), build_rubber(), build_hold()):
    built.set_editor_property('used_with_instanced_static_meshes', True)
    EDIT.recompile_material(built)
    if not unreal.EditorAssetLibrary.save_loaded_asset(built, False):
        raise RuntimeError('Could not save ' + built.get_name())
    unreal.log('Built ' + built.get_name())
