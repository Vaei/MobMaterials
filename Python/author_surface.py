"""Authors the surface master material and its material functions.

Covers props, environment pieces and buildings. Terrain has its own master; see
author_landscape.py.

Run from the editor's Python console:

    import sys, importlib
    sys.path.append('<PluginDir>/Python')
    import author_surface as asm
    importlib.reload(asm)
    asm.build_all()

Every phase is idempotent: an existing asset is emptied and rebuilt in place so material
instances keep their references.

The heavy maths lives in Shaders/Public/MobSurface.ush and is reached from Custom nodes through
the /MobMasterMaterial mapping the module registers, so the graphs here stay thin. Feature gating
is done with static bools inside the functions rather than switches in the master: a discarded
material function input is never compiled, so a layer that is switched off takes its texture
samples and its grade with it.
"""

import sys

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

ROOT = '/MobMasterMaterial/Surface'

# Base name for the authored assets: M_<MASTER_NAME>, MI_<MASTER_NAME>_<Preset>. A recipe overrides it.
MASTER_NAME = 'MobSurface'
FN_ROOT = ROOT + '/Functions'

INCLUDES = ['/MobMasterMaterial/Public/MobSurface.ush']

# The pack is BaseColor + Normal + CRM (Cavity, Roughness, Metallic). Placeholders have to match
# the sampler type they stand in for or the material will not compile.
BASE_TEX_BC = '/MobMasterMaterial/Textures/T_BaseGrey'
BASE_TEX_NRM = '/MobMasterMaterial/Textures/T_BaseNormal'
BASE_TEX_CRM = '/MobMasterMaterial/Textures/T_BaseCRM'
BASE_TEX_NOISE = '/MobMasterMaterial/Textures/T_BaseLinear'
BASE_TEX_EMISSIVE = '/MobMasterMaterial/Textures/T_BaseBlack'
BASE_TEX_DETAIL = '/MobMasterMaterial/Textures/T_BaseNormal'
# The opacity mask is read from alpha rather than a colour channel: alpha skips the sRGB decode, so
# the threshold an artist sets is the threshold the clip uses, and a masked prop can point this
# straight at its own BaseColor. The placeholder compresses without alpha, which samples as 1, so an
# instance that never assigns a mask clips nothing.
BASE_TEX_MASK = '/MobMasterMaterial/Textures/T_BaseWhite'

WEATHER_MPC = '/MobMasterMaterial/MPC_MobWeather'
WEATHER_PARAM = 'Wetness'

# Set by mob_recipe so a newly created collection can be written back onto the recipe.
RECIPE = None

# Recipe options. A feature left out here is not in the master at all, rather than present and
# switched off, so it costs no parameters and no graph.
INCLUDE_DETAIL = True
INCLUDE_TILE_BREAK = True
INCLUDE_PARALLAX = False
INCLUDE_PRIMITIVE_DATA = True
INCLUDE_DEBUG = True

# Foliage is a whole different master, not a switch: see build_master_material.
FOLIAGE = False
INCLUDE_ACCUMULATION = True
INCLUDE_RIPPLES = True

LAYERS = ['Layer0', 'Layer1', 'Layer2']

FIT = unreal.FunctionInputType
CMOT = unreal.CustomMaterialOutputType
ST = unreal.MaterialSamplerType
MP = unreal.MaterialProperty


# ---------------------------------------------------------------------------
# Asset plumbing
# ---------------------------------------------------------------------------

def _tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def _log(msg):
    unreal.log('[MobMasterMaterial] ' + str(msg))


def _clear_function(fn):
    """Empties a MaterialFunction.

    delete_all_material_expressions_in_function leaves nodes behind, so re-running a builder
    silently accumulates duplicate function inputs and the caller's connections then land on
    whichever copy the name lookup hits first. Loop until the graph is actually empty.
    """
    for _ in range(16):
        exprs = MEL.get_material_function_expressions(fn)
        if not exprs:
            return
        for e in exprs:
            MEL.delete_material_expression_in_function(fn, e)
    raise RuntimeError('could not empty material function %s' % fn.get_name())


def _clear_material(mat):
    """Empties a Material. Same caveat as _clear_function."""
    for _ in range(16):
        exprs = MEL.get_material_expressions(mat)
        if not exprs:
            return
        for e in exprs:
            MEL.delete_material_expression(mat, e)
    raise RuntimeError('could not empty material %s' % mat.get_name())


def get_or_create_function(name, description=''):
    path = FN_ROOT + '/' + name
    if EAL.does_asset_exist(path):
        fn = unreal.load_asset(path)
        _clear_function(fn)
    else:
        fn = _tools().create_asset(name, FN_ROOT, unreal.MaterialFunction,
                                   unreal.MaterialFunctionFactoryNew())
    fn.set_editor_property('description', description)
    fn.set_editor_property('expose_to_library', True)
    fn.set_editor_property('library_categories_text', ['MobMasterMaterial', 'Surface'])
    return fn


def get_or_create_material(package_path, name):
    path = package_path + '/' + name
    if EAL.does_asset_exist(path):
        mat = unreal.load_asset(path)
        _clear_material(mat)
    else:
        mat = _tools().create_asset(name, package_path, unreal.Material,
                                    unreal.MaterialFactoryNew())
    return mat


def save(asset):
    EAL.save_loaded_asset(asset, only_if_is_dirty=False)


# ---------------------------------------------------------------------------
# Expression helpers
# ---------------------------------------------------------------------------

# Node positions below are authored on a loose grid for readability in the script. Material nodes
# are far wider than they are tall - a function call with twenty pins, a parameter with a long name
# - so authored columns can sit closer together than the nodes in them actually are. Rather than
# guess a scale, the graph is relaid out once it is built: distinct x positions become evenly
# spaced columns, which keeps the left-to-right order and guarantees nothing overlaps.
COLUMN_PITCH = 460


def _expr(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, int(x), y)


def _fnexpr(fn, cls, x, y):
    return MEL.create_material_expression_in_function(fn, cls, int(x), y)


def _spread(exprs):
    columns = sorted({e.get_editor_property('material_expression_editor_x') for e in exprs})
    placement = {x: i * COLUMN_PITCH for i, x in enumerate(columns)}
    for e in exprs:
        e.set_editor_property('material_expression_editor_x',
                              placement[e.get_editor_property('material_expression_editor_x')])


def _finish_fn(fn):
    """Lays the graph out, then commits it."""
    _spread(MEL.get_material_function_expressions(fn))
    MEL.update_material_function(fn)


def link(src, src_out, dst, dst_in):
    if not MEL.connect_material_expressions(src, src_out, dst, dst_in):
        raise RuntimeError('failed to connect %s.%s -> %s.%s'
                           % (src.get_name(), src_out, dst.get_name(), dst_in))


def _vec4(x, y, z, w):
    """PreviewValue is an FVector4f, whose Python binding takes no constructor arguments."""
    v = unreal.Vector4f()
    v.set_editor_property('x', float(x))
    v.set_editor_property('y', float(y))
    v.set_editor_property('z', float(z))
    v.set_editor_property('w', float(w))
    return v


def fn_input(fn, name, input_type, x, y, sort, default=None, description=''):
    e = _fnexpr(fn, unreal.MaterialExpressionFunctionInput, x, y)
    e.set_editor_property('input_name', name)
    e.set_editor_property('input_type', input_type)
    e.set_editor_property('sort_priority', sort)
    e.set_editor_property('description', description)
    if default is not None:
        if isinstance(default, (int, float)):
            default = (float(default), 0.0, 0.0, 0.0)
        while len(default) < 4:
            default = tuple(default) + (0.0,)
        e.set_editor_property('preview_value', _vec4(*default[:4]))
        e.set_editor_property('use_preview_value_as_default', True)
    return e


def fn_output(fn, name, x, y, sort):
    e = _fnexpr(fn, unreal.MaterialExpressionFunctionOutput, x, y)
    e.set_editor_property('output_name', name)
    e.set_editor_property('sort_priority', sort)
    return e


def custom(owner, code, output_type, input_names, extra_outputs, x, y, description):
    """Creates a Custom node inside a MaterialFunction (owner) or Material."""
    if isinstance(owner, unreal.MaterialFunction):
        e = _fnexpr(owner, unreal.MaterialExpressionCustom, x, y)
    else:
        e = _expr(owner, unreal.MaterialExpressionCustom, x, y)
    e.set_editor_property('code', code)
    e.set_editor_property('output_type', output_type)
    e.set_editor_property('description', description)
    # FCustomInput/FCustomOutput are plain USTRUCTs, so they take no constructor kwargs.
    ins = []
    for n in input_names:
        s = unreal.CustomInput()
        s.set_editor_property('input_name', n)
        ins.append(s)
    outs = []
    for n, t in extra_outputs:
        s = unreal.CustomOutput()
        s.set_editor_property('output_name', n)
        s.set_editor_property('output_type', t)
        outs.append(s)
    e.set_editor_property('inputs', ins)
    e.set_editor_property('additional_outputs', outs)
    e.set_editor_property('include_file_paths', INCLUDES)
    return e


def _fn_const(fn, value, x, y):
    e = _fnexpr(fn, unreal.MaterialExpressionConstant, x, y)
    e.set_editor_property('r', float(value))
    return e


def _fn_const3(fn, rgb, x, y):
    e = _fnexpr(fn, unreal.MaterialExpressionConstant3Vector, x, y)
    e.set_editor_property('constant', unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return e


def _fn_switch(fn, value_expr, true_src, true_out, false_src, false_out, x, y):
    sw = _fnexpr(fn, unreal.MaterialExpressionStaticSwitch, x, y)
    link(true_src, true_out, sw, 'True')
    link(false_src, false_out, sw, 'False')
    link(value_expr, '', sw, 'Value')
    return sw


def _fn_sample(fn, tex_input, sampler_type, uv_src, uv_out, x, y):
    """One TextureSample on the shared wrap sampler.

    Shared:Wrap collapses every sample in the material onto one sampler state, which is what keeps
    a three layer triplanar material inside the 16 sampler limit at ES3.1. Derivatives are
    implicit: unlike the landscape's hex cells, these coordinates are continuous across the
    surface, so there is nothing for explicit gradients to fix.
    """
    s = _fnexpr(fn, unreal.MaterialExpressionTextureSample, x, y)
    s.set_editor_property('sampler_source', unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    s.set_editor_property('sampler_type', sampler_type)
    s.set_editor_property('automatic_view_mip_bias', False)
    link(tex_input, '', s, 'Tex')
    link(uv_src, uv_out, s, 'UVs')
    return s


# ---------------------------------------------------------------------------
# MF_MobSurfaceLayer
# ---------------------------------------------------------------------------

_CODE_LAYER_UV = """
return MobPrepMeshUV(UV, UVScale, UVRotation, float2(UVOffsetU, UVOffsetV));
"""

# Kept apart from the mesh UV so a layer that is not triplanar prunes this node with its taps. A
# Custom node emits its whole body whether or not an output is read, so folding both into one
# would leave the projection maths running on every layer that never uses it.
_CODE_LAYER_TRIPLANAR = """
float2 UVX, UVY, UVZ;
MobTriplanarCoords(WorldPos, TriplanarScale, float2(UVOffsetU, UVOffsetV), UVX, UVY, UVZ);
OutUVY = UVY;
OutUVZ = UVZ;
Weights = MobTriplanarWeights(WorldNormal, TriplanarSharpness);
return UVX;
"""

# A second tiling of the same texture, at a scale and rotation incommensurate with the first so the
# two repeats never line up.
_CODE_LAYER_UV2 = """
return MobPrepMeshUV(UV, UVScale * TileBreakScale, UVRotation + 37.0f,
                     float2(UVOffsetU, UVOffsetV) + 0.37f);
"""

_CODE_VIEW_TANGENT = """
return MobWorldToTangent(CameraVector, Parameters.TangentToWorld);
"""

_CODE_PARALLAX_CHEAP = """
return MobParallaxOffset(UV, ViewTangent, Height, Amount);
"""

# The one place a Custom node samples a texture itself: a raymarch cannot be expressed as graph
# taps. It brings its own sampler with it, so this is also the one thing here that spends a
# sampler slot.
_CODE_PARALLAX_POM = """
return MobParallaxOcclusion(HeightTex, HeightTexSampler, UV, ViewTangent, Amount, (int)Steps, 0.0f);
"""

_CODE_TILEBREAK = """
return MobTileBreak(Base, Second,
    MobTileBreakAmount(PixelDepth, TileBreakStart, TileBreakFalloff, TileBreakAmount));
"""

_CODE_TILEBREAK_NORMAL = """
return MobTileBreakNormal(Base, Second,
    MobTileBreakAmount(PixelDepth, TileBreakStart, TileBreakFalloff, TileBreakAmount));
"""

_CODE_TRI_COMBINE = """
return MobTriplanarCombine(X, Y, Z, Weights);
"""

_CODE_TRI_NORMAL = """
float3 WorldN = MobTriplanarNormalCombine(NX, NY, NZ, GeoNormal, Weights);
return MobWorldToTangent(WorldN, Parameters.TangentToWorld);
"""

_CODE_LAYER_GRADE = """
float3 Col = MobContrast(MobApplyHSV(BC.rgb, HueShift, Saturation, Value), Contrast, 0.5f) * Tint;

OutNormal = MobAdjustNormal(NRM.rgb, NormalIntensity, NormalFlipY);
OutRoughness = saturate(MobRemap(CRM.g, RoughnessMin, RoughnessMax));
OutMetallic = saturate(CRM.b * MetallicScale);
OutCavity = MobShapeCavity(CRM.r, CavityContrast);

// Height for the layer blend is the raw cavity, not the shaded one: contrast is a look control
// and the blend wants the surface as the sculpt left it.
OutHeight = saturate(CRM.r + HeightBias);

return max(Col, 0.0f);
"""

# name, type, default, sort, description
_LAYER_CONTROLS = [
    ('UVScale', FIT.FUNCTION_INPUT_SCALAR, 1.0, 10, 'Mesh UV tiling'),
    ('UVRotation', FIT.FUNCTION_INPUT_SCALAR, 0.0, 11, 'Degrees'),
    ('UVOffsetU', FIT.FUNCTION_INPUT_SCALAR, 0.0, 12, ''),
    ('UVOffsetV', FIT.FUNCTION_INPUT_SCALAR, 0.0, 13, ''),
    ('TriplanarScale', FIT.FUNCTION_INPUT_SCALAR, 0.01, 14, 'World units to UV, used when bTriplanar is on'),
    ('TriplanarSharpness', FIT.FUNCTION_INPUT_SCALAR, 4.0, 15, 'How narrowly the three projections cross-fade'),
    ('TileBreakScale', FIT.FUNCTION_INPUT_SCALAR, 0.37, 16, 'Second tiling, relative to the first'),
    ('TileBreakStart', FIT.FUNCTION_INPUT_SCALAR, 1500.0, 17, 'Where the second tiling starts coming in'),
    ('TileBreakFalloff', FIT.FUNCTION_INPUT_SCALAR, 4000.0, 18, ''),
    ('TileBreakAmount', FIT.FUNCTION_INPUT_SCALAR, 0.7, 19, ''),
    ('ParallaxAmount', FIT.FUNCTION_INPUT_SCALAR, 0.04, 25, 'Depth in UV units. Small: 0.02 to 0.06'),
    ('ParallaxSteps', FIT.FUNCTION_INPUT_SCALAR, 16.0, 26, 'Raymarch steps, occlusion mode only'),
    ('NormalIntensity', FIT.FUNCTION_INPUT_SCALAR, 1.0, 20, ''),
    ('NormalFlipY', FIT.FUNCTION_INPUT_SCALAR, 0.0, 21, '1 for a DirectX-convention normal map'),
    ('RoughnessMin', FIT.FUNCTION_INPUT_SCALAR, 0.0, 30, ''),
    ('RoughnessMax', FIT.FUNCTION_INPUT_SCALAR, 1.0, 31, ''),
    ('MetallicScale', FIT.FUNCTION_INPUT_SCALAR, 1.0, 32, ''),
    ('CavityContrast', FIT.FUNCTION_INPUT_SCALAR, 1.0, 33, ''),
    ('HeightBias', FIT.FUNCTION_INPUT_SCALAR, 0.0, 34, 'Pushes this layer up or down the blend'),
    ('HueShift', FIT.FUNCTION_INPUT_SCALAR, 0.0, 40, 'Degrees'),
    ('Saturation', FIT.FUNCTION_INPUT_SCALAR, 1.0, 41, ''),
    ('Value', FIT.FUNCTION_INPUT_SCALAR, 1.0, 42, ''),
    ('Contrast', FIT.FUNCTION_INPUT_SCALAR, 1.0, 43, ''),
]

_LAYER_OUTPUTS = ['BaseColor', 'Normal', 'Roughness', 'Metallic', 'Cavity', 'Height']


def build_layer_function():
    """MF_MobSurfaceLayer: one layer, sampled and graded.

    Projection is chosen by the bTriplanar static bool. Triplanar costs three taps per texture
    instead of one, so it is off by default and belongs on rock and terrain-adjacent geometry
    rather than on anything that is already unwrapped.
    """
    fn = get_or_create_function(
        'MF_MobSurfaceLayer',
        'Samples and grades one surface layer from a BaseColor + Normal + CRM pack. Projection '
        'is chosen by the bTriplanar static bool; everything else is a scalar the master feeds '
        'from uniquely named parameters.')

    ins = {}
    ins['BC'] = fn_input(fn, 'BC', FIT.FUNCTION_INPUT_TEXTURE2D, -1600, -600, 0,
                         description='Base colour')
    ins['NRM'] = fn_input(fn, 'NRM', FIT.FUNCTION_INPUT_TEXTURE2D, -1600, -520, 1,
                          description='Tangent normal map')
    ins['CRM'] = fn_input(fn, 'CRM', FIT.FUNCTION_INPUT_TEXTURE2D, -1600, -440, 2,
                          description='R cavity, G roughness, B metallic')
    ins['UV'] = fn_input(fn, 'UV', FIT.FUNCTION_INPUT_VECTOR2, -1600, -360, 3,
                         default=(0.0, 0.0), description='Mesh UV0')
    ins['WorldPos'] = fn_input(fn, 'WorldPos', FIT.FUNCTION_INPUT_VECTOR3, -1600, -300, 4,
                               default=(0.0, 0.0, 0.0))
    ins['WorldNormal'] = fn_input(fn, 'WorldNormal', FIT.FUNCTION_INPUT_VECTOR3, -1600, -240, 5,
                                  default=(0.0, 0.0, 1.0))
    ins['Tint'] = fn_input(fn, 'Tint', FIT.FUNCTION_INPUT_VECTOR3, -1600, -180, 6,
                           default=(1.0, 1.0, 1.0))

    y = -100
    for name, in_type, default, sort, desc in _LAYER_CONTROLS:
        ins[name] = fn_input(fn, name, in_type, -1600, y, sort, default=default, description=desc)
        y += 50

    ins['PixelDepth'] = fn_input(fn, 'PixelDepth', FIT.FUNCTION_INPUT_SCALAR, -1600, y, 8, default=0.0)
    y += 50
    ins['bTriplanar'] = fn_input(fn, 'bTriplanar', FIT.FUNCTION_INPUT_STATIC_BOOL, -1600, y, 90)
    ins['bTileBreak'] = fn_input(fn, 'bTileBreak', FIT.FUNCTION_INPUT_STATIC_BOOL, -1600, y + 50, 91)
    ins['bParallax'] = fn_input(fn, 'bParallax', FIT.FUNCTION_INPUT_STATIC_BOOL, -1600, y + 100, 92)
    ins['bParallaxOcclusion'] = fn_input(fn, 'bParallaxOcclusion', FIT.FUNCTION_INPUT_STATIC_BOOL,
                                         -1600, y + 150, 93)
    ins['CameraVector'] = fn_input(fn, 'CameraVector', FIT.FUNCTION_INPUT_VECTOR3, -1600, y + 200, 9,
                                   default=(0.0, 0.0, 1.0))

    uv_inputs = ['UV', 'UVScale', 'UVRotation', 'UVOffsetU', 'UVOffsetV']
    coords = custom(fn, _CODE_LAYER_UV, CMOT.CMOT_FLOAT2, uv_inputs, [],
                    -1200, -560, 'Mesh UV')
    for pin in uv_inputs:
        link(ins[pin], '', coords, pin)

    tri_inputs = ['WorldPos', 'WorldNormal', 'UVOffsetU', 'UVOffsetV',
                  'TriplanarScale', 'TriplanarSharpness']
    tri = custom(fn, _CODE_LAYER_TRIPLANAR, CMOT.CMOT_FLOAT2, tri_inputs,
                 [('OutUVY', CMOT.CMOT_FLOAT2), ('OutUVZ', CMOT.CMOT_FLOAT2),
                  ('Weights', CMOT.CMOT_FLOAT3)],
                 -1200, -440, 'Triplanar coordinates')
    for pin in tri_inputs:
        link(ins[pin], '', tri, pin)

    # Height has to be known before the offset can be applied, so the depth channel is tapped once
    # at the unshifted coordinate. Both parallax nodes hang off that, and both prune with it when
    # the layer does not ask for parallax.
    view_tangent = custom(fn, _CODE_VIEW_TANGENT, CMOT.CMOT_FLOAT3, ['CameraVector'], [],
                          -1100, -700, 'View direction in tangent space')
    link(ins['CameraVector'], '', view_tangent, 'CameraVector')

    height_tap = _fn_sample(fn, ins['CRM'], ST.SAMPLERTYPE_MASKS, coords, '', -1050, -620)

    cheap = custom(fn, _CODE_PARALLAX_CHEAP, CMOT.CMOT_FLOAT2,
                   ['UV', 'ViewTangent', 'Height', 'Amount'], [], -900, -700, 'Parallax offset')
    link(coords, '', cheap, 'UV')
    link(view_tangent, '', cheap, 'ViewTangent')
    link(height_tap, 'R', cheap, 'Height')
    link(ins['ParallaxAmount'], '', cheap, 'Amount')

    pom = custom(fn, _CODE_PARALLAX_POM, CMOT.CMOT_FLOAT2,
                 ['HeightTex', 'UV', 'ViewTangent', 'Amount', 'Steps'], [],
                 -900, -560, 'Parallax occlusion')
    link(ins['CRM'], '', pom, 'HeightTex')
    link(coords, '', pom, 'UV')
    link(view_tangent, '', pom, 'ViewTangent')
    link(ins['ParallaxAmount'], '', pom, 'Amount')
    link(ins['ParallaxSteps'], '', pom, 'Steps')

    shifted = _fn_switch(fn, ins['bParallaxOcclusion'], pom, '', cheap, '', -750, -640)
    planar_uv = _fn_switch(fn, ins['bParallax'], shifted, '', coords, '', -650, -640)

    uv2_inputs = ['UV', 'UVScale', 'UVRotation', 'UVOffsetU', 'UVOffsetV', 'TileBreakScale']
    coords2 = custom(fn, _CODE_LAYER_UV2, CMOT.CMOT_FLOAT2, uv2_inputs, [],
                     -1200, -320, 'Second tiling')
    for pin in uv2_inputs:
        link(ins[pin], '', coords2, pin)

    textures = (('BC', ST.SAMPLERTYPE_COLOR), ('NRM', ST.SAMPLERTYPE_NORMAL),
                ('CRM', ST.SAMPLERTYPE_MASKS))

    taps = {}
    y_tap = -900
    for tex_name, sampler_type in textures:
        for suffix, uv_src, uv_out in (('Planar', planar_uv, ''), ('Break', coords2, ''),
                                       ('X', tri, ''),
                                       ('Y', tri, 'OutUVY'), ('Z', tri, 'OutUVZ')):
            taps[tex_name + suffix] = _fn_sample(fn, ins[tex_name], sampler_type,
                                                 uv_src, uv_out, -900, y_tap)
            y_tap += 160

    # Colour and CRM combine the same way; the normal has to be reoriented before it can be.
    finals = {}
    y_comb = -900
    for tex_name in ('BC', 'CRM'):
        combine = custom(fn, _CODE_TRI_COMBINE, CMOT.CMOT_FLOAT4,
                         ['X', 'Y', 'Z', 'Weights'], [], -500, y_comb,
                         'Triplanar blend')
        for axis in ('X', 'Y', 'Z'):
            link(taps[tex_name + axis], 'RGBA', combine, axis)
        link(tri, 'Weights', combine, 'Weights')

        # Planar only: triplanar already samples three ways, and breaking that too would be nine
        # taps to solve a repeat the projection has largely hidden.
        broke = custom(fn, _CODE_TILEBREAK, CMOT.CMOT_FLOAT4,
                       ['Base', 'Second', 'PixelDepth', 'TileBreakStart', 'TileBreakFalloff',
                        'TileBreakAmount'], [], -400, y_comb + 380, 'Distance tiling break')
        link(taps[tex_name + 'Planar'], 'RGBA', broke, 'Base')
        link(taps[tex_name + 'Break'], 'RGBA', broke, 'Second')
        for pin in ('PixelDepth', 'TileBreakStart', 'TileBreakFalloff', 'TileBreakAmount'):
            link(ins[pin], '', broke, pin)
        planar = _fn_switch(fn, ins['bTileBreak'], broke, '',
                            taps[tex_name + 'Planar'], 'RGBA', -300, y_comb + 380)

        finals[tex_name] = _fn_switch(fn, ins['bTriplanar'], combine, '', planar, '', -250, y_comb)
        y_comb += 700

    nrm_combine = custom(fn, _CODE_TRI_NORMAL, CMOT.CMOT_FLOAT3,
                         ['NX', 'NY', 'NZ', 'GeoNormal', 'Weights'], [], -500, y_comb,
                         'Triplanar normal blend')
    for axis, pin in (('X', 'NX'), ('Y', 'NY'), ('Z', 'NZ')):
        link(taps['NRM' + axis], 'RGB', nrm_combine, pin)
    link(ins['WorldNormal'], '', nrm_combine, 'GeoNormal')
    link(tri, 'Weights', nrm_combine, 'Weights')
    nrm_broke = custom(fn, _CODE_TILEBREAK_NORMAL, CMOT.CMOT_FLOAT3,
                       ['Base', 'Second', 'PixelDepth', 'TileBreakStart', 'TileBreakFalloff',
                        'TileBreakAmount'], [], -400, y_comb + 380, 'Distance tiling break')
    link(taps['NRMPlanar'], 'RGB', nrm_broke, 'Base')
    link(taps['NRMBreak'], 'RGB', nrm_broke, 'Second')
    for pin in ('PixelDepth', 'TileBreakStart', 'TileBreakFalloff', 'TileBreakAmount'):
        link(ins[pin], '', nrm_broke, pin)
    nrm_planar = _fn_switch(fn, ins['bTileBreak'], nrm_broke, '', taps['NRMPlanar'], 'RGB',
                            -300, y_comb + 380)

    finals['NRM'] = _fn_switch(fn, ins['bTriplanar'], nrm_combine, '', nrm_planar, '', -250, y_comb)

    grade_inputs = ['BC', 'NRM', 'CRM', 'Tint',
                    'NormalIntensity', 'NormalFlipY',
                    'RoughnessMin', 'RoughnessMax', 'MetallicScale', 'CavityContrast', 'HeightBias',
                    'HueShift', 'Saturation', 'Value', 'Contrast']
    grade = custom(fn, _CODE_LAYER_GRADE, CMOT.CMOT_FLOAT3, grade_inputs,
                   [('OutNormal', CMOT.CMOT_FLOAT3), ('OutRoughness', CMOT.CMOT_FLOAT1),
                    ('OutMetallic', CMOT.CMOT_FLOAT1), ('OutCavity', CMOT.CMOT_FLOAT1),
                    ('OutHeight', CMOT.CMOT_FLOAT1)],
                   100, -300, 'Grade the sampled layer')
    for pin in grade_inputs:
        if pin in finals:
            link(finals[pin], '', grade, pin)
        else:
            link(ins[pin], '', grade, pin)

    outs = ['', 'OutNormal', 'OutRoughness', 'OutMetallic', 'OutCavity', 'OutHeight']
    for i, name in enumerate(_LAYER_OUTPUTS):
        link(grade, outs[i], fn_output(fn, name, 500, -300 + 60 * i, i), '')

    _finish_fn(fn)
    save(fn)
    _log('built MF_MobSurfaceLayer')
    return fn


# ---------------------------------------------------------------------------
# MF_MobLayerBlend3
# ---------------------------------------------------------------------------

_CODE_BLEND3 = """
float3 W = MobLayerWeights3(Height0, Height1, Height2, Weight1, Weight2, Contrast, HeightAmount);

OutNormal = MobBlendNormal3(Normal0, Normal1, Normal2, W);
OutRoughness = saturate(dot(float3(Roughness0, Roughness1, Roughness2), W));
OutMetallic = saturate(dot(float3(Metallic0, Metallic1, Metallic2), W));
OutCavity = saturate(dot(float3(Cavity0, Cavity1, Cavity2), W));
OutHeight = saturate(dot(float3(Height0, Height1, Height2), W));
OutWeights = W;

return MobBlend3(BaseColor0, BaseColor1, BaseColor2, W);
"""


def build_blend_function():
    """MF_MobLayerBlend3: folds three graded layers into one set of attributes.

    bLayer1 and bLayer2 route the unused layer's inputs back to layer 0. A material function input
    that no live branch reads is never compiled, so switching a layer off takes its twelve texture
    samples and its grade with it rather than just multiplying the result by zero.
    """
    fn = get_or_create_function(
        'MF_MobLayerBlend3',
        'Blends three graded surface layers by two paint weights, resolved against each layer\'s '
        'cavity so the layers interlock instead of cross-fading. Layers 1 and 2 are gated by '
        'static bools and cost nothing when off.')

    ins = {}
    y = -700
    for i, layer in enumerate(LAYERS):
        for j, attr in enumerate(_LAYER_OUTPUTS):
            name = attr + str(i)
            in_type = (FIT.FUNCTION_INPUT_VECTOR3 if attr in ('BaseColor', 'Normal')
                       else FIT.FUNCTION_INPUT_SCALAR)
            default = ((0.5, 0.5, 0.5) if attr == 'BaseColor' else
                       (0.0, 0.0, 1.0) if attr == 'Normal' else
                       0.5 if attr in ('Roughness', 'Cavity', 'Height') else 0.0)
            ins[name] = fn_input(fn, name, in_type, -900, y, i * 10 + j, default=default)
            y += 50
        y += 30

    ins['Weight1'] = fn_input(fn, 'Weight1', FIT.FUNCTION_INPUT_SCALAR, -900, y, 60, default=0.0)
    ins['Weight2'] = fn_input(fn, 'Weight2', FIT.FUNCTION_INPUT_SCALAR, -900, y + 50, 61, default=0.0)
    ins['Contrast'] = fn_input(fn, 'Contrast', FIT.FUNCTION_INPUT_SCALAR, -900, y + 100, 62,
                               default=0.5, description='Width of the band the layers interlock over')
    ins['HeightAmount'] = fn_input(fn, 'HeightAmount', FIT.FUNCTION_INPUT_SCALAR, -900, y + 150, 63,
                                   default=0.5,
                                   description='0 is a plain cross-fade, 1 is a pure height interlock')
    ins['bLayer1'] = fn_input(fn, 'bLayer1', FIT.FUNCTION_INPUT_STATIC_BOOL, -900, y + 200, 90)
    ins['bLayer2'] = fn_input(fn, 'bLayer2', FIT.FUNCTION_INPUT_STATIC_BOOL, -900, y + 250, 91)

    zero = _fn_const(fn, 0.0, -600, y + 300)

    # Layer 0 stands in for a disabled layer so the disabled branch has nothing left to compile.
    routed = {}
    sy = -700
    for i, gate in ((1, 'bLayer1'), (2, 'bLayer2')):
        for attr in _LAYER_OUTPUTS:
            routed[attr + str(i)] = _fn_switch(fn, ins[gate],
                                               ins[attr + str(i)], '',
                                               ins[attr + '0'], '', -600, sy)
            sy += 50
        sy += 30
    for attr in _LAYER_OUTPUTS:
        routed[attr + '0'] = ins[attr + '0']

    routed['Weight1'] = _fn_switch(fn, ins['bLayer1'], ins['Weight1'], '', zero, '', -600, sy)
    routed['Weight2'] = _fn_switch(fn, ins['bLayer2'], ins['Weight2'], '', zero, '', -600, sy + 50)

    blend_inputs = [attr + str(i) for i in range(3) for attr in _LAYER_OUTPUTS]
    blend_inputs += ['Weight1', 'Weight2', 'Contrast', 'HeightAmount']
    blend = custom(fn, _CODE_BLEND3, CMOT.CMOT_FLOAT3, blend_inputs,
                   [('OutNormal', CMOT.CMOT_FLOAT3), ('OutRoughness', CMOT.CMOT_FLOAT1),
                    ('OutMetallic', CMOT.CMOT_FLOAT1), ('OutCavity', CMOT.CMOT_FLOAT1),
                    ('OutHeight', CMOT.CMOT_FLOAT1), ('OutWeights', CMOT.CMOT_FLOAT3)],
                   -200, -300, 'Height-aware three way blend')
    for pin in blend_inputs:
        src = routed.get(pin, ins.get(pin))
        link(src, '', blend, pin)

    outs = ['', 'OutNormal', 'OutRoughness', 'OutMetallic', 'OutCavity', 'OutHeight']
    for i, name in enumerate(_LAYER_OUTPUTS):
        link(blend, outs[i], fn_output(fn, name, 200, -300 + 60 * i, i), '')
    # Only the blend knows these, and a debug view of a weight is the whole point of having one.
    link(blend, 'OutWeights', fn_output(fn, 'Weights', 200, 60, len(_LAYER_OUTPUTS)), '')

    _finish_fn(fn)
    save(fn)
    _log('built MF_MobLayerBlend3')
    return fn


# ---------------------------------------------------------------------------
# MF_MobColorVariation
# ---------------------------------------------------------------------------

# Per instance, set on the component, so one material instance serves thousands of actors that all
# look different. A hash of position cannot do that: it gives every copy a different tint, but
# never the tint somebody chose.
_CODE_ACCUMULATION = """
float Mask = MobAccumulation(WorldNormalZ, Cavity, Noise, Amount, Facing, CavityBias, NoiseAmount);
float3 Col = MobApplyAccumulation(BaseColor, Normal, Roughness, Colour, CoverRoughness, Mask,
                                  OutNormal, OutRoughness);
OutMask = Mask;
return Col;
"""

_CODE_RIPPLE_UV = """
float2 A, B;
MobRippleCoords(UV, Time, Scale, Speed, A, B);
OutB = B;
return A;
"""

_CODE_RIPPLES = """
return MobRainRipples(Normal, RippleA, RippleB, Puddle, Strength);
"""

_CODE_WIND = """
return MobWind(WorldPos, ObjectPos, Direction, Time, Strength, Speed,
               FlutterStrength, FlutterSpeed, Weight);
"""

_CODE_DEBUG = """
return MobDebugView((int)Mode, Weights, Cavity, Normal, Wetness, Height, VertexColour);
"""

_CODE_PRIMITIVE = """
float3 Col = BaseColor * lerp(1.0f.xxx, Tint, saturate(TintAmount));
OutRoughness = saturate(Roughness + RoughnessOffset);
OutWetness = saturate(Wetness + WetnessOffset);
return max(Col, 0.0f);
"""

_CODE_VARIATION = """
float3 Col = MobColorVariation(BaseColor, MobHash13(ObjectPos), HueRange, SaturationRange, ValueRange);
return MobMacroVariation(Col, MacroNoise, TintA, TintB, MacroTintAmount, MacroValueAmount);
"""


def build_variation_function():
    """MF_MobColorVariation: breaks up a repeated asset.

    Two independent sources. The per-object hash gives every copy of a mesh its own tint, which is
    what stops a row of identical crates reading as one object. The macro noise varies across a
    single mesh, which is the only thing that helps on a long wall or a building.
    """
    fn = get_or_create_function(
        'MF_MobColorVariation',
        'Per-object hue, saturation and value variation, plus optional low frequency world-space '
        'noise. Both are gated by static bools.')

    ins = {}
    ins['BaseColor'] = fn_input(fn, 'BaseColor', FIT.FUNCTION_INPUT_VECTOR3, -900, -400, 0,
                                default=(0.5, 0.5, 0.5))
    ins['ObjectPos'] = fn_input(fn, 'ObjectPos', FIT.FUNCTION_INPUT_VECTOR3, -900, -340, 1,
                                default=(0.0, 0.0, 0.0))
    ins['WorldPos'] = fn_input(fn, 'WorldPos', FIT.FUNCTION_INPUT_VECTOR3, -900, -280, 2,
                               default=(0.0, 0.0, 0.0))
    ins['NoiseTexture'] = fn_input(fn, 'NoiseTexture', FIT.FUNCTION_INPUT_TEXTURE2D, -900, -220, 3)
    ins['TintA'] = fn_input(fn, 'TintA', FIT.FUNCTION_INPUT_VECTOR3, -900, -160, 4,
                            default=(1.0, 1.0, 1.0))
    ins['TintB'] = fn_input(fn, 'TintB', FIT.FUNCTION_INPUT_VECTOR3, -900, -100, 5,
                            default=(1.0, 1.0, 1.0))

    scalars = [('HueRange', 8.0, 10), ('SaturationRange', 0.1, 11), ('ValueRange', 0.1, 12),
               ('NoiseScale', 0.002, 20), ('MacroTintAmount', 0.5, 21), ('MacroValueAmount', 0.25, 22)]
    y = -40
    for name, default, sort in scalars:
        ins[name] = fn_input(fn, name, FIT.FUNCTION_INPUT_SCALAR, -900, y, sort, default=default)
        y += 50

    ins['bEnabled'] = fn_input(fn, 'bEnabled', FIT.FUNCTION_INPUT_STATIC_BOOL, -900, y, 90)
    ins['bMacro'] = fn_input(fn, 'bMacro', FIT.FUNCTION_INPUT_STATIC_BOOL, -900, y + 50, 91)

    noise_uv = custom(fn, 'return WorldPos.xy * Scale;', CMOT.CMOT_FLOAT2,
                      ['WorldPos', 'Scale'], [], -600, -220, 'Macro noise coordinates')
    link(ins['WorldPos'], '', noise_uv, 'WorldPos')
    link(ins['NoiseScale'], '', noise_uv, 'Scale')
    noise = _fn_sample(fn, ins['NoiseTexture'], ST.SAMPLERTYPE_MASKS,
                       noise_uv, '', -400, -220)

    flat = _fn_const(fn, 0.5, -400, -120)
    noise_value = _fn_switch(fn, ins['bMacro'], noise, 'R', flat, '', -200, -220)

    var_inputs = ['BaseColor', 'ObjectPos', 'MacroNoise', 'TintA', 'TintB',
                  'HueRange', 'SaturationRange', 'ValueRange',
                  'MacroTintAmount', 'MacroValueAmount']
    var = custom(fn, _CODE_VARIATION, CMOT.CMOT_FLOAT3, var_inputs, [], 0, -400, 'Colour variation')
    for pin in var_inputs:
        if pin == 'MacroNoise':
            link(noise_value, '', var, pin)
        else:
            link(ins[pin], '', var, pin)

    result = _fn_switch(fn, ins['bEnabled'], var, '', ins['BaseColor'], '', 250, -400)
    link(result, '', fn_output(fn, 'BaseColor', 450, -400, 0), '')

    _finish_fn(fn)
    save(fn)
    _log('built MF_MobColorVariation')
    return fn


# ---------------------------------------------------------------------------
# MF_MobSurfaceWetness
# ---------------------------------------------------------------------------

_CODE_WETNESS = """
float3 Col = MobWetness(
    BaseColor, Normal, Roughness, Specular, Cavity, WorldNormalZ,
    saturate(Amount * LocalAmount + PaintWeight),
    Darkening, RoughnessTarget, NormalFlatten, SpecularTarget,
    PorosityAmount, PuddleDepth, PuddleRoughness, PuddleFacing,
    OutNormal, OutRoughness, OutSpecular, OutMask, OutPuddle);
return Col;
"""

_WETNESS_OUTPUTS = ['BaseColor', 'Normal', 'Roughness', 'Specular', 'Mask', 'Puddle']


def build_wetness_function():
    """MF_MobSurfaceWetness: darkens and smooths a wet surface.

    Amount is the global weather value, so the whole world goes wet together. LocalAmount lets an
    interior instance opt out without needing its own permutation, and PaintWeight lets a mesh
    hold water where the artist painted it.
    """
    fn = get_or_create_function(
        'MF_MobSurfaceWetness',
        'Cavity-driven wetness. Water reaches the crevices first, standing water flattens the '
        'surface out completely. Gated by a static bool.')

    ins = {}
    ins['BaseColor'] = fn_input(fn, 'BaseColor', FIT.FUNCTION_INPUT_VECTOR3, -900, -500, 0,
                                default=(0.5, 0.5, 0.5))
    ins['Normal'] = fn_input(fn, 'Normal', FIT.FUNCTION_INPUT_VECTOR3, -900, -440, 1,
                             default=(0.0, 0.0, 1.0))
    ins['Roughness'] = fn_input(fn, 'Roughness', FIT.FUNCTION_INPUT_SCALAR, -900, -380, 2, default=0.5)
    ins['Specular'] = fn_input(fn, 'Specular', FIT.FUNCTION_INPUT_SCALAR, -900, -330, 3, default=0.5)
    ins['Cavity'] = fn_input(fn, 'Cavity', FIT.FUNCTION_INPUT_SCALAR, -900, -280, 4, default=1.0)
    ins['WorldNormalZ'] = fn_input(fn, 'WorldNormalZ', FIT.FUNCTION_INPUT_SCALAR, -900, -230, 5,
                                   default=1.0, description='World normal Z; gates standing water to up-facing surfaces')
    ins['Amount'] = fn_input(fn, 'Amount', FIT.FUNCTION_INPUT_SCALAR, -900, -180, 6, default=0.0,
                             description='Global weather wetness')
    ins['LocalAmount'] = fn_input(fn, 'LocalAmount', FIT.FUNCTION_INPUT_SCALAR, -900, -130, 7, default=1.0)
    ins['PaintWeight'] = fn_input(fn, 'PaintWeight', FIT.FUNCTION_INPUT_SCALAR, -900, -80, 8, default=0.0)

    scalars = [('Darkening', 0.55, 20), ('RoughnessTarget', 0.25, 21), ('NormalFlatten', 0.4, 22),
               ('SpecularTarget', 0.5, 23), ('PorosityAmount', 1.0, 24),
               ('PuddleDepth', 0.15, 30), ('PuddleRoughness', 0.05, 31), ('PuddleFacing', 0.8, 32)]
    y = -20
    for name, default, sort in scalars:
        ins[name] = fn_input(fn, name, FIT.FUNCTION_INPUT_SCALAR, -900, y, sort, default=default)
        y += 50

    ins['bEnabled'] = fn_input(fn, 'bEnabled', FIT.FUNCTION_INPUT_STATIC_BOOL, -900, y, 90)

    wet_inputs = ['BaseColor', 'Normal', 'Roughness', 'Specular', 'Cavity', 'WorldNormalZ',
                  'Amount', 'LocalAmount', 'PaintWeight'] + [s[0] for s in scalars]
    wet = custom(fn, _CODE_WETNESS, CMOT.CMOT_FLOAT3, wet_inputs,
                 [('OutNormal', CMOT.CMOT_FLOAT3), ('OutRoughness', CMOT.CMOT_FLOAT1),
                  ('OutSpecular', CMOT.CMOT_FLOAT1), ('OutMask', CMOT.CMOT_FLOAT1),
                  ('OutPuddle', CMOT.CMOT_FLOAT1)],
                 -400, -400, 'Wetness')
    for pin in wet_inputs:
        link(ins[pin], '', wet, pin)

    zero = _fn_const(fn, 0.0, -400, 200)
    dry = [(ins['BaseColor'], ''), (ins['Normal'], ''), (ins['Roughness'], ''),
           (ins['Specular'], ''), (zero, ''), (zero, '')]
    wet_out = ['', 'OutNormal', 'OutRoughness', 'OutSpecular', 'OutMask', 'OutPuddle']

    for i, name in enumerate(_WETNESS_OUTPUTS):
        sw = _fn_switch(fn, ins['bEnabled'], wet, wet_out[i], dry[i][0], dry[i][1], -100, -400 + 60 * i)
        link(sw, '', fn_output(fn, name, 150, -400 + 60 * i, i), '')

    _finish_fn(fn)
    save(fn)
    _log('built MF_MobSurfaceWetness')
    return fn


# ---------------------------------------------------------------------------
# MF_MobSurfaceEmissive
# ---------------------------------------------------------------------------

def build_emissive_function():
    """MF_MobSurfaceEmissive: a masked glow."""
    fn = get_or_create_function(
        'MF_MobSurfaceEmissive',
        'Emissive colour from a mask texture. Gated by a static bool, so a material that does not '
        'glow neither samples the mask nor writes the pin.')

    tex = fn_input(fn, 'MaskTexture', FIT.FUNCTION_INPUT_TEXTURE2D, -700, -300, 0)
    uv = fn_input(fn, 'UV', FIT.FUNCTION_INPUT_VECTOR2, -700, -240, 1, default=(0.0, 0.0))
    color = fn_input(fn, 'Color', FIT.FUNCTION_INPUT_VECTOR3, -700, -180, 2, default=(1.0, 1.0, 1.0))
    intensity = fn_input(fn, 'Intensity', FIT.FUNCTION_INPUT_SCALAR, -700, -120, 3, default=1.0)
    enabled = fn_input(fn, 'bEnabled', FIT.FUNCTION_INPUT_STATIC_BOOL, -700, -60, 90)

    sample = _fn_sample(fn, tex, ST.SAMPLERTYPE_COLOR, uv, '', -400, -300)
    glow = custom(fn, 'return Mask.rgb * Color * Intensity;', CMOT.CMOT_FLOAT3,
                  ['Mask', 'Color', 'Intensity'], [], -150, -300, 'Emissive')
    link(sample, 'RGBA', glow, 'Mask')
    link(color, '', glow, 'Color')
    link(intensity, '', glow, 'Intensity')

    off = _fn_const3(fn, (0.0, 0.0, 0.0), -150, -180)
    result = _fn_switch(fn, enabled, glow, '', off, '', 100, -300)
    link(result, '', fn_output(fn, 'Emissive', 300, -300, 0), '')

    _finish_fn(fn)
    save(fn)
    _log('built MF_MobSurfaceEmissive')
    return fn


# ---------------------------------------------------------------------------
# MF_MobSurfaceFinalise
# ---------------------------------------------------------------------------

_CODE_DETAIL = """
return MobDetailNormal(Normal, Detail.rgb, Strength, MobDistanceFade(PixelDepth, FadeStart, FadeLength));
"""

_CODE_FINALISE = """
float3 Col = MobApplyCavity(BaseColor, Cavity, CavityColorAmount);
Col *= lerp(1.0f, VertexShade, saturate(VertexShadeAmount));

float Rough;
float3 N = MobDistanceClamp(Normal, Roughness, PixelDepth, FadeStart, FadeLength,
                            DistanceNormalFlatten, DistanceRoughnessFloor, Rough);

OutNormal = N;
OutRoughness = Rough;
OutSpecular = saturate(Specular * lerp(1.0f, Cavity, saturate(CavitySpecularAmount)));
return max(Col, 0.0f);
"""


def build_finalise_function():
    """MF_MobSurfaceFinalise: cavity into the shading, then the distance clamp.

    Cavity is micro shadowing: it multiplies BaseColor and Specular and never touches the AO pin,
    which is what keeps it from doubling up with the renderer's own occlusion.

    The distance clamp is not optional polish. The project ships FXAA with no temporal filter, so
    normal and specular detail smaller than a pixel has nothing to resolve it and crawls; taking it
    out in the material is the only place that can be fixed.
    """
    fn = get_or_create_function(
        'MF_MobSurfaceFinalise',
        'Applies cavity to base colour and specular, then flattens normals and floors roughness '
        'with distance so sub-pixel detail stops aliasing under FXAA.')

    ins = {}
    ins['BaseColor'] = fn_input(fn, 'BaseColor', FIT.FUNCTION_INPUT_VECTOR3, -700, -400, 0,
                                default=(0.5, 0.5, 0.5))
    ins['Normal'] = fn_input(fn, 'Normal', FIT.FUNCTION_INPUT_VECTOR3, -700, -340, 1,
                             default=(0.0, 0.0, 1.0))
    ins['Roughness'] = fn_input(fn, 'Roughness', FIT.FUNCTION_INPUT_SCALAR, -700, -280, 2, default=0.5)
    ins['Specular'] = fn_input(fn, 'Specular', FIT.FUNCTION_INPUT_SCALAR, -700, -230, 3, default=0.5)
    ins['Cavity'] = fn_input(fn, 'Cavity', FIT.FUNCTION_INPUT_SCALAR, -700, -180, 4, default=1.0)
    ins['VertexShade'] = fn_input(fn, 'VertexShade', FIT.FUNCTION_INPUT_SCALAR, -700, -130, 5,
                                  default=1.0, description='Painted darkening, vertex colour alpha')
    ins['PixelDepth'] = fn_input(fn, 'PixelDepth', FIT.FUNCTION_INPUT_SCALAR, -700, -80, 6, default=0.0)
    ins['DetailTexture'] = fn_input(fn, 'DetailTexture', FIT.FUNCTION_INPUT_TEXTURE2D, -700, -30, 7)
    ins['DetailUV'] = fn_input(fn, 'DetailUV', FIT.FUNCTION_INPUT_VECTOR2, -700, 20, 8,
                               default=(0.0, 0.0))
    ins['bDetail'] = fn_input(fn, 'bDetail', FIT.FUNCTION_INPUT_STATIC_BOOL, -700, 70, 90)

    scalars = [('DetailStrength', 0.5, 15),
               ('CavityColorAmount', 0.5, 10), ('CavitySpecularAmount', 0.5, 11),
               ('VertexShadeAmount', 1.0, 12),
               ('FadeStart', 2000.0, 20), ('FadeLength', 6000.0, 21),
               ('DistanceNormalFlatten', 0.6, 22), ('DistanceRoughnessFloor', 0.4, 23)]
    y = -20
    for name, default, sort in scalars:
        ins[name] = fn_input(fn, name, FIT.FUNCTION_INPUT_SCALAR, -700, y, sort, default=default)
        y += 50

    # One tap for the whole material rather than one per layer: a detail normal is the same
    # high-frequency break whichever layer it lands on, and three of them would cost three samples
    # to say the same thing.
    detail_sample = _fn_sample(fn, ins['DetailTexture'], ST.SAMPLERTYPE_NORMAL,
                               ins['DetailUV'], '', -520, 60)
    detailed = custom(fn, _CODE_DETAIL, CMOT.CMOT_FLOAT3,
                      ['Normal', 'Detail', 'Strength', 'PixelDepth', 'FadeStart', 'FadeLength'],
                      [], -420, 60, 'Detail normal')
    link(ins['Normal'], '', detailed, 'Normal')
    link(detail_sample, 'RGB', detailed, 'Detail')
    link(ins['DetailStrength'], '', detailed, 'Strength')
    for pin in ('PixelDepth', 'FadeStart', 'FadeLength'):
        link(ins[pin], '', detailed, pin)
    normal_in = _fn_switch(fn, ins['bDetail'], detailed, '', ins['Normal'], '', -360, 60)

    final_inputs = ['BaseColor', 'Normal', 'Roughness', 'Specular', 'Cavity', 'VertexShade',
                    'PixelDepth'] + [s[0] for s in scalars]
    final = custom(fn, _CODE_FINALISE, CMOT.CMOT_FLOAT3, final_inputs,
                   [('OutNormal', CMOT.CMOT_FLOAT3), ('OutRoughness', CMOT.CMOT_FLOAT1),
                    ('OutSpecular', CMOT.CMOT_FLOAT1)],
                   -300, -300, 'Finalise')
    for pin in final_inputs:
        link(normal_in if pin == 'Normal' else ins[pin], '', final, pin)

    for i, (name, out) in enumerate((('BaseColor', ''), ('Normal', 'OutNormal'),
                                     ('Roughness', 'OutRoughness'), ('Specular', 'OutSpecular'))):
        link(final, out, fn_output(fn, name, 0, -300 + 60 * i, i), '')

    _finish_fn(fn)
    save(fn)
    _log('built MF_MobSurfaceFinalise')
    return fn


def build_functions():
    build_layer_function()
    build_blend_function()
    build_variation_function()
    build_wetness_function()
    build_emissive_function()
    build_finalise_function()
    _log('functions ready')


# ---------------------------------------------------------------------------
# Weather parameters
# ---------------------------------------------------------------------------

def ensure_weather_parameters():
    """Creates the weather collection if absent and makes sure it carries the wetness scalars.

    Point WEATHER_MPC at a collection the project already uses if there is one: each collection
    costs a uniform buffer, so two floats do not deserve their own.
    """
    if EAL.does_asset_exist(WEATHER_MPC):
        mpc = unreal.load_asset(WEATHER_MPC)
    else:
        package, _, name = WEATHER_MPC.rpartition('/')
        mpc = _tools().create_asset(name, package, unreal.MaterialParameterCollection,
                                    unreal.MaterialParameterCollectionFactoryNew())
    if mpc is None:
        raise RuntimeError('could not create %s' % WEATHER_MPC)

    existing = list(mpc.get_editor_property('scalar_parameters'))
    names = [str(p.get_editor_property('parameter_name')) for p in existing]
    added = []
    for name, default in ((WEATHER_PARAM, 0.0), ('PuddleAmount', 1.0)):
        if name in names:
            continue
        p = unreal.CollectionScalarParameter()
        p.set_editor_property('parameter_name', name)
        p.set_editor_property('default_value', float(default))
        existing.append(p)
        added.append(name)

    if added:
        mpc.set_editor_property('scalar_parameters', existing)
        save(mpc)

    # So the toolbar can find it, and so a second generate reuses this one rather than making
    # another beside the next master.
    if RECIPE is not None and RECIPE.get_editor_property('weather_collection') is None:
        RECIPE.set_editor_property('weather_collection', mpc)
        save(RECIPE)
        _log('recipe now points at %s' % WEATHER_MPC)
    _log('weather parameters ready%s' % (' (added %s)' % ', '.join(added) if added else ''))
    return mpc


# ---------------------------------------------------------------------------
# Master material
# ---------------------------------------------------------------------------

def _param_scalar(mat, name, default, group, x, y, sort=0):
    e = _expr(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property('parameter_name', name)
    e.set_editor_property('default_value', float(default))
    e.set_editor_property('group', group)
    e.set_editor_property('sort_priority', sort)
    return e


def _param_vector(mat, name, rgb, group, x, y, sort=0):
    e = _expr(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property('parameter_name', name)
    e.set_editor_property('default_value', unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    e.set_editor_property('group', group)
    e.set_editor_property('sort_priority', sort)
    return e


def _param_texture(mat, name, texture_path, group, x, y, sort=0):
    e = _expr(mat, unreal.MaterialExpressionTextureObjectParameter, x, y)
    e.set_editor_property('parameter_name', name)
    e.set_editor_property('texture', unreal.load_asset(texture_path))
    e.set_editor_property('group', group)
    e.set_editor_property('sort_priority', sort)
    return e


def _param_static_bool(mat, name, default, group, x, y, sort=0):
    e = _expr(mat, unreal.MaterialExpressionStaticBoolParameter, x, y)
    e.set_editor_property('parameter_name', name)
    e.set_editor_property('default_value', bool(default))
    e.set_editor_property('group', group)
    e.set_editor_property('sort_priority', sort)
    return e


def _fn_call(mat, function_path, x, y):
    e = _expr(mat, unreal.MaterialExpressionMaterialFunctionCall, x, y)
    e.set_editor_property('material_function', unreal.load_asset(function_path))
    return e


def _switch_param(mat, name, true_src, true_out, false_src, false_out, group, x, y, default=False):
    """A StaticSwitchParameter. Several may share a name; they all follow one toggle on the instance."""
    sw = _expr(mat, unreal.MaterialExpressionStaticSwitchParameter, x, y)
    sw.set_editor_property('parameter_name', name)
    sw.set_editor_property('default_value', bool(default))
    sw.set_editor_property('group', group)
    link(true_src, true_out, sw, 'True')
    link(false_src, false_out, sw, 'False')
    return sw


GROUP_GLOBAL = '00 - Global'
GROUP_BLEND = '10 - Blending'
GROUP_WETNESS = '30 - Wetness'
GROUP_ACCUM = '35 - Accumulation'
GROUP_VARIATION = '40 - Variation'
GROUP_PRIMITIVE = '05 - Per Instance'
GROUP_DETAIL = '45 - Detail'
GROUP_EMISSIVE = '50 - Emissive'
GROUP_DISTANCE = '60 - Distance'
GROUP_FOLIAGE = '70 - Foliage'
GROUP_DEBUG = '90 - Debug'

_LAYER_GROUPS = ['01 - Layer 0', '02 - Layer 1', '03 - Layer 2']

# Defaults the master hands each layer. Layer 1 and 2 tile a little differently out of the box so a
# freshly enabled layer is visible rather than a perfect copy of the base.
_LAYER_DEFAULTS = {
    'UVScale': [1.0, 1.0, 1.0],
    'UVRotation': [0.0, 0.0, 0.0],
    'UVOffsetU': [0.0, 0.0, 0.0],
    'UVOffsetV': [0.0, 0.0, 0.0],
    'TriplanarScale': [0.01, 0.01, 0.01],
    'TriplanarSharpness': [4.0, 4.0, 4.0],
    'TileBreakScale': [0.37, 0.41, 0.53],
    'TileBreakStart': [1500.0, 1500.0, 1500.0],
    'TileBreakFalloff': [4000.0, 4000.0, 4000.0],
    'TileBreakAmount': [0.7, 0.7, 0.7],
    'ParallaxAmount': [0.04, 0.04, 0.04],
    'ParallaxSteps': [16.0, 16.0, 16.0],
    'NormalIntensity': [1.0, 1.0, 1.0],
    'NormalFlipY': [0.0, 0.0, 0.0],
    'RoughnessMin': [0.0, 0.0, 0.0],
    'RoughnessMax': [1.0, 1.0, 1.0],
    'MetallicScale': [1.0, 1.0, 1.0],
    'CavityContrast': [1.0, 1.0, 1.0],
    'HeightBias': [0.0, 0.0, 0.0],
    'HueShift': [0.0, 0.0, 0.0],
    'Saturation': [1.0, 1.0, 1.0],
    'Value': [1.0, 1.0, 1.0],
    'Contrast': [1.0, 1.0, 1.0],
}


def _build_layer_block(mat, index, shared, x, y):
    """One layer: its three texture parameters, its controls and the layer function call."""
    layer = LAYERS[index]
    group = _LAYER_GROUPS[index]
    call = _fn_call(mat, FN_ROOT + '/MF_MobSurfaceLayer', x + 700, y)

    for pin, default_path, sort in (('BC', BASE_TEX_BC, 0), ('NRM', BASE_TEX_NRM, 1),
                                    ('CRM', BASE_TEX_CRM, 2)):
        tex = _param_texture(mat, '%s_%s' % (layer, pin), default_path, group, x, y + 80 * sort, sort)
        link(tex, '', call, pin)

    link(shared['uv'], '', call, 'UV')
    link(shared['worldpos'], '', call, 'WorldPos')
    link(shared['worldnormal'], '', call, 'WorldNormal')
    link(_param_vector(mat, layer + '_Tint', (1.0, 1.0, 1.0), group, x, y + 250, 5), '', call, 'Tint')

    yy = y + 320
    for name, _in_type, _default, sort, _desc in _LAYER_CONTROLS:
        # A control for a feature the recipe left out would be a parameter that does nothing.
        if name.startswith('TileBreak') and not INCLUDE_TILE_BREAK:
            continue
        if name.startswith('Parallax') and not INCLUDE_PARALLAX:
            continue
        value = _LAYER_DEFAULTS[name][index]
        link(_param_scalar(mat, '%s_%s' % (layer, name), value, group, x, yy, sort), '', call, name)
        yy += 50

    link(_param_static_bool(mat, layer + '_Triplanar', False, group, x, yy, 90), '', call, 'bTriplanar')
    link(shared['depth'], '', call, 'PixelDepth')
    link(shared['camera'], '', call, 'CameraVector')

    # A static bool function input has no usable default, so it is driven either way.
    if INCLUDE_TILE_BREAK:
        link(_param_static_bool(mat, layer + '_TileBreak', False, group, x, yy + 50, 91),
             '', call, 'bTileBreak')
    else:
        off = _expr(mat, unreal.MaterialExpressionStaticBool, x, yy + 50)
        off.set_editor_property('value', False)
        link(off, '', call, 'bTileBreak')

    if INCLUDE_PARALLAX:
        link(_param_static_bool(mat, layer + '_Parallax', False, group, x, yy + 100, 92),
             '', call, 'bParallax')
        link(_param_static_bool(mat, layer + '_ParallaxOcclusion', False, group, x, yy + 150, 93),
             '', call, 'bParallaxOcclusion')
    else:
        for pin in ('bParallax', 'bParallaxOcclusion'):
            off = _expr(mat, unreal.MaterialExpressionStaticBool, x, yy + 100)
            off.set_editor_property('value', False)
            link(off, '', call, pin)
    return call


def build_master_material():
    """M_MobSurface.

    Opaque and one-sided. Masked and two-sided are per-instance base property overrides rather
    than switches, so the common path pays nothing for them; note that r.EarlyZPass is 0 in this
    project, which makes masked geometry cost real overdraw.
    """
    mat = get_or_create_material(ROOT, 'M_' + MASTER_NAME)

    # Shading model, two-sidedness and blend mode are material properties, not switches, so a
    # foliage master is a different material rather than a toggle on this one. That is the right
    # shape anyway: foliage wants different defaults the whole way down.
    if FOLIAGE:
        mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
        mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
        mat.set_editor_property('two_sided', True)
    else:
        mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
        mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
        mat.set_editor_property('two_sided', False)

    # --- shared inputs ----------------------------------------------------
    uv = _expr(mat, unreal.MaterialExpressionTextureCoordinate, -3600, -1200)
    uv.set_editor_property('coordinate_index', 0)

    worldpos = _expr(mat, unreal.MaterialExpressionWorldPosition, -3600, -1140)
    worldnormal = _expr(mat, unreal.MaterialExpressionVertexNormalWS, -3600, -1080)
    objectpos = _expr(mat, unreal.MaterialExpressionObjectPositionWS, -3600, -1020)
    depth = _expr(mat, unreal.MaterialExpressionPixelDepth, -3600, -960)
    vcol = _expr(mat, unreal.MaterialExpressionVertexColor, -3600, -900)
    camera = _expr(mat, unreal.MaterialExpressionCameraVectorWS, -3600, -840)

    normal_z = _expr(mat, unreal.MaterialExpressionComponentMask, -3400, -1080)
    normal_z.set_editor_property('r', False)
    normal_z.set_editor_property('g', False)
    normal_z.set_editor_property('b', True)
    normal_z.set_editor_property('a', False)
    link(worldnormal, '', normal_z, '')

    shared = {'uv': uv, 'worldpos': worldpos, 'worldnormal': worldnormal, 'depth': depth,
              'camera': camera}

    # --- vertex paint -----------------------------------------------------
    # R and G are the two layer weights, B boosts wetness, A darkens. Black adds and white is
    # neutral on every channel: a mesh with no colour buffer gets white from the vertex factory,
    # so neutral has to be white or anything that reaches this material unpainted arrives as the
    # top layer, soaking wet, with nothing to say so. Alpha multiplies already, so it needs no
    # inversion to read that way.
    def paint(channel, param_name, default, y, sort, invert=True):
        fallback = _param_scalar(mat, param_name, default, GROUP_BLEND, -3400, y, sort)
        src, src_out = vcol, channel
        if invert:
            inv = _expr(mat, unreal.MaterialExpressionOneMinus, -3300, y)
            link(vcol, channel, inv, '')
            src, src_out = inv, ''
        return _switch_param(mat, 'bVertexPaint', src, src_out, fallback, '',
                             GROUP_BLEND, -3200, y, default=True)

    weight1 = paint('R', 'Layer1Weight', 0.0, -900, 0)
    weight2 = paint('G', 'Layer2Weight', 0.0, -850, 1)
    wet_paint = paint('B', 'WetnessPaint', 0.0, -800, 2)
    vertex_shade = paint('A', 'VertexShade', 1.0, -750, 3, invert=False)

    # --- layers -----------------------------------------------------------
    calls = []
    x = -3000
    y = -600
    for i in range(3):
        calls.append(_build_layer_block(mat, i, shared, x, y))
        y += 1500

    # --- blend ------------------------------------------------------------
    blend = _fn_call(mat, FN_ROOT + '/MF_MobLayerBlend3', -1600, -600)
    for i, call in enumerate(calls):
        for attr in _LAYER_OUTPUTS:
            link(call, attr, blend, attr + str(i))
    link(weight1, '', blend, 'Weight1')
    link(weight2, '', blend, 'Weight2')
    link(_param_scalar(mat, 'BlendContrast', 0.5, GROUP_BLEND, -1900, -600, 10), '', blend, 'Contrast')
    link(_param_scalar(mat, 'BlendHeightAmount', 0.5, GROUP_BLEND, -1900, -550, 11), '', blend, 'HeightAmount')
    link(_param_static_bool(mat, 'bLayer1', False, GROUP_BLEND, -1900, -500, 90), '', blend, 'bLayer1')
    link(_param_static_bool(mat, 'bLayer2', False, GROUP_BLEND, -1900, -450, 91), '', blend, 'bLayer2')

    # --- colour variation -------------------------------------------------
    variation = _fn_call(mat, FN_ROOT + '/MF_MobColorVariation', -1100, -600)
    link(blend, 'BaseColor', variation, 'BaseColor')
    link(objectpos, '', variation, 'ObjectPos')
    link(worldpos, '', variation, 'WorldPos')
    link(_param_texture(mat, 'MacroNoiseTexture', BASE_TEX_NOISE, GROUP_VARIATION, -1400, -600, 0),
         '', variation, 'NoiseTexture')
    link(_param_vector(mat, 'MacroTintA', (1.0, 1.0, 1.0), GROUP_VARIATION, -1400, -520, 1),
         '', variation, 'TintA')
    link(_param_vector(mat, 'MacroTintB', (0.85, 0.88, 0.95), GROUP_VARIATION, -1400, -470, 2),
         '', variation, 'TintB')
    vy = -420
    for name, default, sort in (('HueRange', 8.0, 10), ('SaturationRange', 0.1, 11),
                                ('ValueRange', 0.1, 12), ('NoiseScale', 0.002, 20),
                                ('MacroTintAmount', 0.5, 21), ('MacroValueAmount', 0.25, 22)):
        link(_param_scalar(mat, 'Variation_' + name, default, GROUP_VARIATION, -1400, vy, sort),
             '', variation, name)
        vy += 50
    link(_param_static_bool(mat, 'bColorVariation', False, GROUP_VARIATION, -1400, vy, 90),
         '', variation, 'bEnabled')
    link(_param_static_bool(mat, 'bMacroVariation', False, GROUP_VARIATION, -1400, vy + 50, 91),
         '', variation, 'bMacro')

    # --- wetness ----------------------------------------------------------
    weather = _expr(mat, unreal.MaterialExpressionCollectionParameter, -900, -300)
    weather.set_editor_property('collection', unreal.load_asset(WEATHER_MPC))
    weather.set_editor_property('parameter_name', WEATHER_PARAM)

    # --- per-instance overrides -------------------------------------------
    # Sits between variation and wetness so an instance can be told to be wetter, and so its tint
    # multiplies whatever the layers and the variation already decided.
    colour_src, colour_out = variation, 'BaseColor'
    rough_src, rough_out = blend, 'Roughness'
    wet_local_src, wet_local_out = None, ''

    if INCLUDE_PRIMITIVE_DATA:
        # Custom primitive data is a flag on a parameter rather than a node of its own: the
        # parameter keeps its default when nothing sets the data, so an actor that was never told
        # anything looks like the instance says it should.
        tint = _param_vector(mat, 'PrimitiveTint', (1.0, 1.0, 1.0), GROUP_PRIMITIVE, -1250, -360, 0)
        tint.set_editor_property('use_custom_primitive_data', True)
        tint.set_editor_property('primitive_data_index', 0)

        rough_offset = _param_scalar(mat, 'PrimitiveRoughness', 0.0, GROUP_PRIMITIVE, -1250, -300, 1)
        rough_offset.set_editor_property('use_custom_primitive_data', True)
        rough_offset.set_editor_property('primitive_data_index', 4)

        wet_offset = _param_scalar(mat, 'PrimitiveWetness', 0.0, GROUP_PRIMITIVE, -1250, -250, 2)
        wet_offset.set_editor_property('use_custom_primitive_data', True)
        wet_offset.set_editor_property('primitive_data_index', 5)

        local_wet = _param_scalar(mat, 'Wetness_LocalAmount', 1.0, GROUP_WETNESS, -1250, -200, 0)

        prim = custom(mat, _CODE_PRIMITIVE, CMOT.CMOT_FLOAT3,
                      ['BaseColor', 'Roughness', 'Wetness', 'Tint', 'RoughnessOffset',
                       'WetnessOffset', 'TintAmount'],
                      [('OutRoughness', CMOT.CMOT_FLOAT1), ('OutWetness', CMOT.CMOT_FLOAT1)],
                      -950, -300, 'Per-instance overrides')
        link(variation, 'BaseColor', prim, 'BaseColor')
        link(blend, 'Roughness', prim, 'Roughness')
        link(local_wet, '', prim, 'Wetness')
        link(tint, '', prim, 'Tint')
        link(rough_offset, '', prim, 'RoughnessOffset')
        link(wet_offset, '', prim, 'WetnessOffset')
        link(_param_scalar(mat, 'PrimitiveTintAmount', 1.0, GROUP_PRIMITIVE, -1250, -150, 3),
             '', prim, 'TintAmount')

        sw_colour = _switch_param(mat, 'bPrimitiveData', prim, '', variation, 'BaseColor',
                                  GROUP_PRIMITIVE, -800, -360)
        sw_rough = _switch_param(mat, 'bPrimitiveData', prim, 'OutRoughness', blend, 'Roughness',
                                 GROUP_PRIMITIVE, -800, -300)
        sw_wet = _switch_param(mat, 'bPrimitiveData', prim, 'OutWetness', local_wet, '',
                               GROUP_PRIMITIVE, -800, -240)
        colour_src, colour_out = sw_colour, ''
        rough_src, rough_out = sw_rough, ''
        wet_local_src, wet_local_out = sw_wet, ''

    wet = _fn_call(mat, FN_ROOT + '/MF_MobSurfaceWetness', -600, -600)
    link(colour_src, colour_out, wet, 'BaseColor')
    link(blend, 'Normal', wet, 'Normal')
    link(rough_src, rough_out, wet, 'Roughness')
    link(_param_scalar(mat, 'Specular', 0.5, GROUP_GLOBAL, -900, -900, 0), '', wet, 'Specular')
    link(blend, 'Cavity', wet, 'Cavity')
    link(normal_z, '', wet, 'WorldNormalZ')
    link(weather, '', wet, 'Amount')
    link(wet_paint, '', wet, 'PaintWeight')
    wy = -250
    for name, default, sort in (('Darkening', 0.55, 20),
                                ('RoughnessTarget', 0.25, 21), ('NormalFlatten', 0.4, 22),
                                ('SpecularTarget', 0.5, 23), ('PorosityAmount', 1.0, 24),
                                ('PuddleDepth', 0.15, 30), ('PuddleRoughness', 0.05, 31),
                                ('PuddleFacing', 0.8, 32)):
        link(_param_scalar(mat, 'Wetness_' + name, default, GROUP_WETNESS, -900, wy, sort),
             '', wet, name)
        wy += 50
    if wet_local_src is not None:
        link(wet_local_src, wet_local_out, wet, 'LocalAmount')
    else:
        link(_param_scalar(mat, 'Wetness_LocalAmount', 1.0, GROUP_WETNESS, -900, wy, 0),
             '', wet, 'LocalAmount')
    link(_param_static_bool(mat, 'bWetness', False, GROUP_WETNESS, -900, wy, 90), '', wet, 'bEnabled')

    # --- rain ripples -----------------------------------------------------
    shaded_colour, shaded_colour_out = wet, 'BaseColor'
    shaded_normal, shaded_normal_out = wet, 'Normal'
    shaded_rough, shaded_rough_out = wet, 'Roughness'

    if INCLUDE_RIPPLES:
        rip_time = _expr(mat, unreal.MaterialExpressionTime, -500, 300)
        rip_uv = custom(mat, _CODE_RIPPLE_UV, CMOT.CMOT_FLOAT2,
                        ['UV', 'Time', 'Scale', 'Speed'], [('OutB', CMOT.CMOT_FLOAT2)],
                        -400, 300, 'Ripple coordinates')
        link(uv, '', rip_uv, 'UV')
        link(rip_time, '', rip_uv, 'Time')
        link(_param_scalar(mat, 'RippleScale', 12.0, GROUP_WETNESS, -600, 300, 40), '', rip_uv, 'Scale')
        link(_param_scalar(mat, 'RippleSpeed', 0.25, GROUP_WETNESS, -600, 350, 41), '', rip_uv, 'Speed')

        rip_tex = _param_texture(mat, 'RippleNormal', BASE_TEX_DETAIL, GROUP_WETNESS, -600, 400, 42)
        rip_a = _expr(mat, unreal.MaterialExpressionTextureSample, -250, 260)
        rip_b = _expr(mat, unreal.MaterialExpressionTextureSample, -250, 360)
        for node, src, out in ((rip_a, rip_uv, ''), (rip_b, rip_uv, 'OutB')):
            node.set_editor_property('sampler_source',
                                     unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
            node.set_editor_property('sampler_type', ST.SAMPLERTYPE_NORMAL)
            node.set_editor_property('automatic_view_mip_bias', False)
            link(rip_tex, '', node, 'Tex')
            link(src, out, node, 'UVs')

        ripples = custom(mat, _CODE_RIPPLES, CMOT.CMOT_FLOAT3,
                         ['Normal', 'RippleA', 'RippleB', 'Puddle', 'Strength'], [],
                         -100, 300, 'Rain ripples')
        link(wet, 'Normal', ripples, 'Normal')
        link(rip_a, 'RGB', ripples, 'RippleA')
        link(rip_b, 'RGB', ripples, 'RippleB')
        link(wet, 'Puddle', ripples, 'Puddle')
        link(_param_scalar(mat, 'RippleStrength', 0.6, GROUP_WETNESS, -600, 450, 43),
             '', ripples, 'Strength')

        sw = _switch_param(mat, 'bRipples', ripples, '', wet, 'Normal', GROUP_WETNESS, 50, 300)
        shaded_normal, shaded_normal_out = sw, ''

    # --- accumulation -----------------------------------------------------
    if INCLUDE_ACCUMULATION:
        acc = custom(mat, _CODE_ACCUMULATION, CMOT.CMOT_FLOAT3,
                     ['BaseColor', 'Normal', 'Roughness', 'WorldNormalZ', 'Cavity', 'Noise',
                      'Colour', 'CoverRoughness', 'Amount', 'Facing', 'CavityBias', 'NoiseAmount'],
                     [('OutNormal', CMOT.CMOT_FLOAT3), ('OutRoughness', CMOT.CMOT_FLOAT1),
                      ('OutMask', CMOT.CMOT_FLOAT1)],
                     -100, 600, 'Accumulation')
        link(shaded_colour, shaded_colour_out, acc, 'BaseColor')
        link(shaded_normal, shaded_normal_out, acc, 'Normal')
        link(shaded_rough, shaded_rough_out, acc, 'Roughness')
        link(normal_z, '', acc, 'WorldNormalZ')
        link(blend, 'Cavity', acc, 'Cavity')
        link(_param_scalar(mat, 'Accumulation_Noise', 0.5, GROUP_ACCUM, -600, 700, 5),
             '', acc, 'Noise')
        link(_param_vector(mat, 'Accumulation_Colour', (0.86, 0.89, 0.94), GROUP_ACCUM, -600, 600, 0),
             '', acc, 'Colour')
        for nm, dv, so in (('Amount', 0.0, 1), ('Facing', 0.55, 2), ('CavityBias', 0.35, 3),
                           ('NoiseAmount', 0.4, 4), ('CoverRoughness', 0.85, 6)):
            link(_param_scalar(mat, 'Accumulation_' + nm, dv, GROUP_ACCUM, -600, 750 + so * 50, so),
                 '', acc, nm)

        sw_c = _switch_param(mat, 'bAccumulation', acc, '', shaded_colour, shaded_colour_out,
                             GROUP_ACCUM, 100, 600)
        sw_n = _switch_param(mat, 'bAccumulation', acc, 'OutNormal', shaded_normal, shaded_normal_out,
                             GROUP_ACCUM, 100, 660)
        sw_r = _switch_param(mat, 'bAccumulation', acc, 'OutRoughness', shaded_rough, shaded_rough_out,
                             GROUP_ACCUM, 100, 720)
        shaded_colour, shaded_colour_out = sw_c, ''
        shaded_normal, shaded_normal_out = sw_n, ''
        shaded_rough, shaded_rough_out = sw_r, ''

    # --- finalise ---------------------------------------------------------
    final = _fn_call(mat, FN_ROOT + '/MF_MobSurfaceFinalise', 0, -600)
    link(shaded_colour, shaded_colour_out, final, 'BaseColor')
    link(shaded_normal, shaded_normal_out, final, 'Normal')
    link(shaded_rough, shaded_rough_out, final, 'Roughness')
    link(wet, 'Specular', final, 'Specular')
    link(blend, 'Cavity', final, 'Cavity')
    link(vertex_shade, '', final, 'VertexShade')
    link(depth, '', final, 'PixelDepth')

    # A static bool function input has no usable default, so it has to be driven either way: a
    # parameter when the feature is in, a constant false when the recipe left it out.
    if not INCLUDE_DETAIL:
        off = _expr(mat, unreal.MaterialExpressionStaticBool, -600, 420)
        off.set_editor_property('value', False)
        link(off, '', final, 'bDetail')
    else:
        detail_uv = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 500)
        link(uv, '', detail_uv, 'A')
        link(_param_scalar(mat, 'DetailScale', 8.0, GROUP_DETAIL, -600, 500, 1), '', detail_uv, 'B')
        link(detail_uv, '', final, 'DetailUV')
        link(_param_texture(mat, 'DetailNormal', BASE_TEX_DETAIL, GROUP_DETAIL, -600, 420, 0),
             '', final, 'DetailTexture')
        link(_param_scalar(mat, 'DetailStrength', 0.5, GROUP_DETAIL, -600, 560, 2),
             '', final, 'DetailStrength')
        link(_param_static_bool(mat, 'bDetail', False, GROUP_DETAIL, -600, 610, 90),
             '', final, 'bDetail')
    fy = -600
    for name, default, group, sort in (('CavityColorAmount', 0.5, GROUP_GLOBAL, 10),
                                       ('CavitySpecularAmount', 0.5, GROUP_GLOBAL, 11),
                                       ('VertexShadeAmount', 1.0, GROUP_GLOBAL, 12),
                                       ('FadeStart', 2000.0, GROUP_DISTANCE, 20),
                                       ('FadeLength', 6000.0, GROUP_DISTANCE, 21),
                                       ('DistanceNormalFlatten', 0.6, GROUP_DISTANCE, 22),
                                       ('DistanceRoughnessFloor', 0.4, GROUP_DISTANCE, 23)):
        link(_param_scalar(mat, name, default, group, -300, fy, sort), '', final, name)
        fy += 50

    # --- emissive ---------------------------------------------------------
    emissive = _fn_call(mat, FN_ROOT + '/MF_MobSurfaceEmissive', 0, 200)
    link(_param_texture(mat, 'EmissiveMask', BASE_TEX_EMISSIVE, GROUP_EMISSIVE, -300, 200, 0),
         '', emissive, 'MaskTexture')
    link(uv, '', emissive, 'UV')
    link(_param_vector(mat, 'EmissiveColor', (1.0, 0.85, 0.6), GROUP_EMISSIVE, -300, 280, 1),
         '', emissive, 'Color')
    link(_param_scalar(mat, 'EmissiveIntensity', 1.0, GROUP_EMISSIVE, -300, 330, 2),
         '', emissive, 'Intensity')
    link(_param_static_bool(mat, 'bEmissive', False, GROUP_EMISSIVE, -300, 380, 90),
         '', emissive, 'bEnabled')

    # --- opacity mask -----------------------------------------------------
    # Only compiled when an instance overrides the blend mode to Masked, so this needs no switch of
    # its own; an opaque instance never reads the pin.
    mask_tex = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, 0, 600)
    mask_tex.set_editor_property('parameter_name', 'OpacityMask')
    mask_tex.set_editor_property('texture', unreal.load_asset(BASE_TEX_MASK))
    mask_tex.set_editor_property('sampler_source', unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    mask_tex.set_editor_property('sampler_type', ST.SAMPLERTYPE_COLOR)
    mask_tex.set_editor_property('group', GROUP_GLOBAL)
    link(uv, '', mask_tex, 'UVs')

    # --- foliage ----------------------------------------------------------
    if FOLIAGE:
        time_node = _expr(mat, unreal.MaterialExpressionTime, -1000, 1200)
        wind = custom(mat, _CODE_WIND, CMOT.CMOT_FLOAT3,
                      ['WorldPos', 'ObjectPos', 'Direction', 'Time', 'Strength', 'Speed',
                       'FlutterStrength', 'FlutterSpeed', 'Weight'],
                      [], -500, 1200, 'Wind')
        link(worldpos, '', wind, 'WorldPos')
        link(objectpos, '', wind, 'ObjectPos')
        link(time_node, '', wind, 'Time')
        link(_param_vector(mat, 'WindDirection', (1.0, 0.0, 0.0), GROUP_FOLIAGE, -1000, 1000, 0),
             '', wind, 'Direction')
        for name, default, sort in (('WindStrength', 6.0, 1), ('WindSpeed', 1.2, 2),
                                    ('WindFlutterStrength', 2.5, 3), ('WindFlutterSpeed', 5.0, 4)):
            link(_param_scalar(mat, name, default, GROUP_FOLIAGE, -1000, 1050 + sort * 50, sort),
                 '', wind, name.replace('Wind', '').replace('Flutter', 'Flutter'))
        # Painted red is the sway weight; unpainted white already means the tips move most.
        link(vcol, 'R', wind, 'Weight')

        gate = _switch_param(mat, 'bWind', wind, '',
                             _expr(mat, unreal.MaterialExpressionConstant3Vector, -500, 1320),
                             '', GROUP_FOLIAGE, -300, 1200)
        MEL.connect_material_property(gate, '', MP.MP_WORLD_POSITION_OFFSET)

        # Two-sided foliage reads this as the colour of light coming through a leaf.
        MEL.connect_material_property(
            _param_vector(mat, 'SubsurfaceColor', (0.15, 0.35, 0.08), GROUP_FOLIAGE, -1000, 1300, 10),
            '', MP.MP_SUBSURFACE_COLOR)

    # --- debug ------------------------------------------------------------
    base_src, base_out = final, 'BaseColor'
    emissive_src, emissive_out = emissive, 'Emissive'

    if INCLUDE_DEBUG:
        dbg = custom(mat, _CODE_DEBUG, CMOT.CMOT_FLOAT3,
                     ['Mode', 'Weights', 'Cavity', 'Normal', 'Wetness', 'Height', 'VertexColour'],
                     [], 300, 800, 'Debug view')
        link(_param_scalar(mat, 'DebugMode', 1.0, GROUP_DEBUG, 0, 800, 1), '', dbg, 'Mode')
        link(blend, 'Weights', dbg, 'Weights')
        link(blend, 'Cavity', dbg, 'Cavity')
        link(blend, 'Normal', dbg, 'Normal')
        link(wet, 'Mask', dbg, 'Wetness')
        link(blend, 'Height', dbg, 'Height')
        link(vcol, '', dbg, 'VertexColour')

        black = _expr(mat, unreal.MaterialExpressionConstant3Vector, 0, 900)
        black.set_editor_property('constant', unreal.LinearColor(0.0, 0.0, 0.0, 1.0))

        # Sent to emissive and the base colour blacked out, so what you see is the value itself
        # rather than the value times whatever the light happened to be doing.
        base_src = _switch_param(mat, 'bDebug', black, '', final, 'BaseColor', GROUP_DEBUG, 500, 900)
        base_out = ''
        emissive_src = _switch_param(mat, 'bDebug', dbg, '', emissive, 'Emissive', GROUP_DEBUG,
                                     500, 800)
        emissive_out = ''

    # --- outputs ----------------------------------------------------------
    MEL.connect_material_property(base_src, base_out, MP.MP_BASE_COLOR)
    MEL.connect_material_property(final, 'Normal', MP.MP_NORMAL)
    MEL.connect_material_property(final, 'Roughness', MP.MP_ROUGHNESS)
    MEL.connect_material_property(final, 'Specular', MP.MP_SPECULAR)
    MEL.connect_material_property(blend, 'Metallic', MP.MP_METALLIC)
    MEL.connect_material_property(emissive_src, emissive_out, MP.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(mask_tex, 'A', MP.MP_OPACITY_MASK)
    # Ambient occlusion is deliberately left at 1. Cavity is already in BaseColor and Specular, and
    # the renderer supplies its own occlusion; feeding a baked map here darkens twice.

    _spread(MEL.get_material_expressions(mat))
    errors = MEL.recompile_material(mat)
    save(mat)
    for e in errors:
        _log('COMPILE ERROR: ' + str(e))
    _log('built M_%s, %d node(s), %d error(s)'
         % (MASTER_NAME, MEL.get_num_material_expressions(mat), len(errors)))
    return mat, errors


# ---------------------------------------------------------------------------
# Material instances
# ---------------------------------------------------------------------------

def _get_or_create_instance(name, parent):
    path = ROOT + '/' + name
    if EAL.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        mi = _tools().create_asset(name, ROOT, unreal.MaterialInstanceConstant,
                                   unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent)
    return mi


# Every distinct set of static switches is a distinct shader map, and PSO.md records that PSOs are
# per vertex factory times material. Asset instances should parent to one of these rather than
# flipping their own switches, or the PSO count grows with the art.
_INSTANCE_PRESETS = [
    ('Prop', {}),
    ('Building', {'bLayer1': True, 'bVertexPaint': True, 'bWetness': True}),
    ('Rock', {'Layer0_Triplanar': True, 'bWetness': True}),
    ('Lamp', {'bEmissive': True}),
]

_SWITCH_DEFAULTS = {
    'bVertexPaint': True, 'bLayer1': False, 'bLayer2': False,
    'Layer0_Triplanar': False, 'Layer1_Triplanar': False, 'Layer2_Triplanar': False,
    'Layer0_TileBreak': False, 'Layer1_TileBreak': False, 'Layer2_TileBreak': False,
    'Layer0_Parallax': False, 'Layer1_Parallax': False, 'Layer2_Parallax': False,
    'Layer0_ParallaxOcclusion': False, 'Layer1_ParallaxOcclusion': False,
    'Layer2_ParallaxOcclusion': False,
    'bWetness': False, 'bColorVariation': False, 'bMacroVariation': False, 'bEmissive': False,
    'bDetail': False, 'bPrimitiveData': False, 'bDebug': False,
    'bAccumulation': False, 'bRipples': False, 'bWind': False,
}


def build_material_instances():
    master = unreal.load_asset(ROOT + '/M_' + MASTER_NAME)
    built = []
    for name, overrides in _INSTANCE_PRESETS:
        mi = _get_or_create_instance('MI_%s_%s' % (MASTER_NAME, name), master)
        for switch, default in _SWITCH_DEFAULTS.items():
            if switch == 'bDetail' and not INCLUDE_DETAIL:
                continue
            if switch.endswith('_TileBreak') and not INCLUDE_TILE_BREAK:
                continue
            if '_Parallax' in switch and not INCLUDE_PARALLAX:
                continue
            if switch == 'bPrimitiveData' and not INCLUDE_PRIMITIVE_DATA:
                continue
            if switch == 'bDebug' and not INCLUDE_DEBUG:
                continue
            if switch == 'bAccumulation' and not INCLUDE_ACCUMULATION:
                continue
            if switch == 'bRipples' and not INCLUDE_RIPPLES:
                continue
            if switch == 'bWind' and not FOLIAGE:
                continue
            MEL.set_material_instance_static_switch_parameter_value(
                mi, switch, bool(overrides.get(switch, default)))
        save(mi)
        built.append(mi)
    _log('built %d material instance(s)' % len(built))
    return built


def build_all(recipe=None):
    """Everything a recipe can author.

    Pass a recipe asset or its path; the Mob toolbar menu passes the one it was run from. Without
    one the module's own defaults stand, so this still runs from a bare Python console.
    """
    import mob_recipe
    import importlib as _il
    _il.reload(mob_recipe)
    me = sys.modules[__name__]
    recipe = mob_recipe.load(recipe)
    mob_recipe.apply_surface(me, recipe)

    ensure_weather_parameters()
    build_functions()

    # Siblings first, so the module is left describing the recipe that was asked for.
    for other in (mob_recipe.siblings(recipe, mob_recipe.SURFACE) if recipe else []):
        _log('rebuilding %s, which shares these functions' % other.get_name())
        mob_recipe.apply_surface(me, other)
        build_master_material()
        build_material_instances()

    if recipe:
        mob_recipe.apply_surface(me, recipe)
    mat, errors = build_master_material()
    build_material_instances()
    return mat, errors
