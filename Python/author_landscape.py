"""Authors the landscape master material and its material functions.

Run from the editor's Python console:

    import sys, importlib
    sys.path.append('<PluginDir>/Python')
    import author_landscape as alm
    importlib.reload(alm)
    alm.build_all()

Every phase is idempotent: an existing asset is emptied and rebuilt in place so material
instances and the landscape keep their references.

The heavy maths lives in Shaders/Public/Mob*.ush and is reached from Custom nodes through the
/MobMasterMaterial mapping the module registers, so the graphs here stay thin.

build_all() writes only into the plugin. The phases under PROJECT INTEGRATION below author
assets that belong to a game rather than to the plugin - physical materials, layer infos, grass
types, runtime virtual textures - so their paths are yours to set and they are opt-in.
"""

import sys

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

# Where the master and its instances are written. Point this into a project to author a variant
# that carries the project outputs below without dirtying the plugin's own copy.
ROOT = '/MobMasterMaterial/Landscape'

# Base name for the authored assets: M_<MASTER_NAME>, MI_<MASTER_NAME>. A recipe overrides it.
MASTER_NAME = 'MobLandscape'

# Where the material functions live. Leave this alone when ROOT moves: the functions are the same
# either way, and a project master reusing them keeps one copy of the maths.
FN_ROOT = '/MobMasterMaterial/Landscape/Functions'

INCLUDE_DEBUG = True

# Sample the layers out of three texture arrays rather than three textures each. Pack the arrays
# with mob_arrays.pack() before generating: the master samples whatever is at each slice index and
# has no way to notice that a slice is not the art the layer wanted.
TEXTURE_ARRAYS = False
LAYER_TEXTURE_ROOT = ''

INCLUDES = [
    '/MobMasterMaterial/Public/MobMaterialUtil.ush',
    '/MobMasterMaterial/Public/MobLandscapeBombing.ush',
]

# Base materials always default their texture parameters to these neutrals rather than to real
# art, so a base material never drags unused textures into memory.
# One per sampler type: the defaults have to match or the sampler type check fails.
BASE_TEX_BC = '/MobMasterMaterial/Textures/T_BaseGrey'      # sRGB colour
BASE_TEX_NRM = '/MobMasterMaterial/Textures/T_BaseNormal'   # normal map
BASE_TEX_HRC = '/MobMasterMaterial/Textures/T_BaseLinear'   # linear mask pack

# Paint layers, in blend order. Surface type and physical material are consumed by the
# LayerInfo/physmat phase, not by the material graph itself.
LAYERS = [
    ('Grass',       'SurfaceType1', 'PhysMat_Grass'),
    ('DryGrass',    'SurfaceType1', 'PhysMat_Grass'),
    ('Dirt',        'SurfaceType2', 'PhysMat_Ground'),
    ('PackedDirt',  'SurfaceType2', 'PhysMat_Ground'),
    ('GardenSoil',  'SurfaceType2', 'PhysMat_Ground'),
    ('Gravel',      'SurfaceType2', 'PhysMat_Gravel'),
    ('RakedGravel', 'SurfaceType2', 'PhysMat_Gravel'),
    ('StonePaving', 'SurfaceType8', 'PhysMat_Stone'),
    ('Rock',        'SurfaceType8', 'PhysMat_Stone'),
    ('ForestFloor', 'SurfaceType3', 'PhysMat_ForestFloor'),
    ('SandSilt',    'SurfaceType4', 'PhysMat_Sand'),
]

FIT = unreal.FunctionInputType
CMOT = unreal.CustomMaterialOutputType


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
    """Returns an emptied MaterialFunction, creating it if absent."""
    path = FN_ROOT + '/' + name
    if EAL.does_asset_exist(path):
        fn = unreal.load_asset(path)
        _clear_function(fn)
    else:
        fn = _tools().create_asset(name, FN_ROOT, unreal.MaterialFunction,
                                   unreal.MaterialFunctionFactoryNew())
    fn.set_editor_property('description', description)
    fn.set_editor_property('expose_to_library', True)
    fn.set_editor_property('library_categories_text', ['MobMasterMaterial', 'Landscape'])
    return fn


def get_or_create_material(package_path, name):
    """Returns an emptied Material, creating it if absent."""
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


# ---------------------------------------------------------------------------
# MF_MobLandscapeLayer
# ---------------------------------------------------------------------------

_CODE_COORDS = """
float2 T = MobPrepUV(UV, UVScale, UVRotation, UVOffset);
float2 dX = ddx(T);
float2 dY = ddy(T);

DXBase = dX;
DYBase = dY;

UVDual = T * SecondScale + 0.37f;
DXDual = dX * SecondScale;
DYDual = dY * SecondScale;

float3 W = 0;
MobHexCells(T, dX, dY, RotationAmount, BlendContrast,
           UVHex1, DXHex1, DYHex1,
           UVHex2, DXHex2, DYHex2,
           UVHex3, DXHex3, DYHex3, W);
Weights = W;

return T;
"""

_CODE_COMBINE_HEX = """
float4 A = MobHexCombine(Hex1, Hex2, Hex3, Weights);
A.rgb = MobRestoreVariance(A.rgb, Mean, Weights, VarianceRestore);
return A;
"""

_CODE_COMBINE_DUAL = """
return MobDualCombine(Base, Second, Amount);
"""

_CODE_SHADE = """
// BC: base colour.  NRM: tangent normal, already unpacked by the sampler.
// HRC: r height, g roughness, b cavity.
float3 Col = MobMacroModulate(BC.rgb, MacroNoise, TintA, TintB, TintAmount, MacroValueAmount);
Col = MobApplyHSV(Col, HueShift, Saturation, Value);
Col = MobContrast(Col, Contrast, 0.5f);

float3 N = MobAdjustNormal(NRM.rgb, NormalIntensity, NormalFlipY);
N = MobFlattenNormal(N, saturate(NormalFlatten + NormalFlattenFar * DistanceFade));

float Rough = MobRemap(HRC.g, RoughnessMin, RoughnessMax);
Rough = max(Rough, RoughnessMinFar * DistanceFade);

// Cavity is micro shadowing, so it multiplies base colour and specular further down the chain
// rather than reaching the AO pin. The renderer supplies its own occlusion and a baked one on top
// of it darkens twice.
float Cavity = saturate(pow(max(HRC.b, 0.0f), max(CavityContrast, MOB_EPS)));

OutNormal = N;
OutRoughness = saturate(Rough);
OutCavity = Cavity;
OutHeight = saturate(HRC.r * HeightContrast + HeightOffset);
return Col;
"""

# name, type, default, sort, description
_LAYER_CONTROLS = [
    ('UVScale',          FIT.FUNCTION_INPUT_SCALAR,  1.0,  10, 'Tiles per landscape UV unit'),
    ('UVRotation',       FIT.FUNCTION_INPUT_SCALAR,  0.0,  11, 'Degrees'),
    ('UVOffset',         FIT.FUNCTION_INPUT_VECTOR2, (0.0, 0.0), 12, ''),

    ('HueShift',         FIT.FUNCTION_INPUT_SCALAR,  0.0,  20, 'Degrees'),
    ('Saturation',       FIT.FUNCTION_INPUT_SCALAR,  1.0,  21, ''),
    ('Value',            FIT.FUNCTION_INPUT_SCALAR,  1.0,  22, ''),
    ('Contrast',         FIT.FUNCTION_INPUT_SCALAR,  1.0,  23, 'Pivots on mid grey'),
    ('TintA',            FIT.FUNCTION_INPUT_VECTOR3, (1.0, 1.0, 1.0), 24, 'Macro variation low end'),
    ('TintB',            FIT.FUNCTION_INPUT_VECTOR3, (1.0, 1.0, 1.0), 25, 'Macro variation high end'),
    ('TintAmount',       FIT.FUNCTION_INPUT_SCALAR,  0.0,  26, ''),
    ('MacroValueAmount', FIT.FUNCTION_INPUT_SCALAR,  0.0,  27, 'Brightness swing from macro noise'),

    ('NormalIntensity',  FIT.FUNCTION_INPUT_SCALAR,  1.0,  30, ''),
    ('NormalFlatten',    FIT.FUNCTION_INPUT_SCALAR,  0.0,  31, ''),
    ('NormalFlattenFar', FIT.FUNCTION_INPUT_SCALAR,  0.8,  32, 'Flatten added at full distance fade'),
    ('NormalFlipY',      FIT.FUNCTION_INPUT_SCALAR,  0.0,  33, '1 for DirectX convention source art'),

    ('RoughnessMin',     FIT.FUNCTION_INPUT_SCALAR,  0.0,  40, ''),
    ('RoughnessMax',     FIT.FUNCTION_INPUT_SCALAR,  1.0,  41, ''),
    ('RoughnessMinFar',  FIT.FUNCTION_INPUT_SCALAR,  0.45, 42, 'Floor at full distance fade, kills specular sparkle'),
    ('CavityContrast',   FIT.FUNCTION_INPUT_SCALAR,  1.0,  43, ''),

    ('HeightContrast',   FIT.FUNCTION_INPUT_SCALAR,  1.0,  50, ''),
    ('HeightOffset',     FIT.FUNCTION_INPUT_SCALAR,  0.0,  51, ''),

    ('BlendContrast',    FIT.FUNCTION_INPUT_SCALAR,  4.0,  60, 'Hex tier: weight sharpness'),
    ('RotationAmount',   FIT.FUNCTION_INPUT_SCALAR,  1.0,  61, 'Hex tier: per-cell rotation'),
    ('VarianceRestore',  FIT.FUNCTION_INPUT_SCALAR,  1.0,  62, 'Hex tier: undo blend wash-out'),
    ('MeanColor',        FIT.FUNCTION_INPUT_VECTOR3, (0.5, 0.5, 0.5), 63, 'Mean colour of the source art'),
    ('SecondScale',      FIT.FUNCTION_INPUT_SCALAR,  0.37, 64, 'Dual tier: second UV scale'),
    ('Amount',           FIT.FUNCTION_INPUT_SCALAR,  1.0,  65, 'Dual tier: overlay strength'),
]


def build_layer_function():
    """MF_MobLandscapeLayer: one paint layer, sampled and graded, as material attributes."""
    fn = get_or_create_function(
        'MF_MobLandscapeLayer',
        'Samples and grades one landscape paint layer. Tiling tier is chosen by the '
        'bHexTiling/bDualScale static bools; everything else is a scalar the master feeds '
        'from uniquely named parameters.')

    ins = {}

    tex_type = FIT.FUNCTION_INPUT_TEXTURE2D_ARRAY if TEXTURE_ARRAYS else FIT.FUNCTION_INPUT_TEXTURE2D
    ins['BC'] = fn_input(fn, 'BC', tex_type, -1400, -500, 0,
                         description='Base colour')
    ins['NRM'] = fn_input(fn, 'NRM', tex_type, -1400, -400, 1,
                          description='Tangent normal map')
    ins['HRC'] = fn_input(fn, 'HRC', tex_type, -1400, -300, 2,
                          description='R height, G roughness, B ambient occlusion')
    ins['UV'] = fn_input(fn, 'UV', FIT.FUNCTION_INPUT_VECTOR2, -1400, -200, 2,
                         default=(0.0, 0.0), description='Landscape layer coords')
    ins['MacroNoise'] = fn_input(fn, 'MacroNoise', FIT.FUNCTION_INPUT_SCALAR, -1400, -100, 3,
                                 default=0.5, description='Shared macro noise, sampled once by the master')
    ins['DistanceFade'] = fn_input(fn, 'DistanceFade', FIT.FUNCTION_INPUT_SCALAR, -1400, 0, 4,
                                   default=0.0, description='0 near, 1 far; drives normal flatten and roughness floor')

    y = 100
    for name, in_type, default, sort, desc in _LAYER_CONTROLS:
        ins[name] = fn_input(fn, name, in_type, -1400, y, sort, default=default, description=desc)
        y += 60

    ins['bHexTiling'] = fn_input(fn, 'bHexTiling', FIT.FUNCTION_INPUT_STATIC_BOOL, -1400, y, 70)
    ins['bDualScale'] = fn_input(fn, 'bDualScale', FIT.FUNCTION_INPUT_STATIC_BOOL, -1400, y + 60, 71)

    if TEXTURE_ARRAYS:
        ins['LayerIndex'] = fn_input(fn, 'LayerIndex', FIT.FUNCTION_INPUT_SCALAR, -1400, y + 120, 72,
                                     default=0.0, description='Which slice of the layer arrays this layer is')

    # Coordinate generation. Deliberately touches no texture: see MobLandscapeBombing.ush.
    coord_outputs = []
    for name in ('DXBase', 'DYBase', 'UVDual', 'DXDual', 'DYDual',
                 'UVHex1', 'DXHex1', 'DYHex1',
                 'UVHex2', 'DXHex2', 'DYHex2',
                 'UVHex3', 'DXHex3', 'DYHex3'):
        coord_outputs.append((name, CMOT.CMOT_FLOAT2))
    coord_outputs.append(('Weights', CMOT.CMOT_FLOAT3))

    coords = custom(fn, _CODE_COORDS, CMOT.CMOT_FLOAT2,
                    ['UV', 'UVScale', 'UVRotation', 'UVOffset',
                     'SecondScale', 'BlendContrast', 'RotationAmount'],
                    coord_outputs, -1000, -400, 'Tiling coordinates')
    for pin in ('UV', 'UVScale', 'UVRotation', 'UVOffset',
                'SecondScale', 'BlendContrast', 'RotationAmount'):
        link(ins[pin], '', coords, pin)

    # An array sample takes the slice as a third coordinate, and its gradients have to be the same
    # width as the coordinate they differentiate - the slice does not vary across a pixel, so its
    # derivative is zero. The appends feed taps, so a tier no switch selects prunes them too.
    sliced = {}
    if TEXTURE_ARRAYS:
        zero = _fnexpr(fn, unreal.MaterialExpressionConstant, -1000, -1600)
        zero.set_editor_property('r', 0.0)
        y_slice = -1560
        for out_name in ('', 'UVDual', 'UVHex1', 'UVHex2', 'UVHex3',
                         'DXBase', 'DYBase', 'DXDual', 'DYDual',
                         'DXHex1', 'DYHex1', 'DXHex2', 'DYHex2', 'DXHex3', 'DYHex3'):
            ap = _fnexpr(fn, unreal.MaterialExpressionAppendVector, -850, y_slice)
            link(coords, out_name, ap, 'A')
            link(ins['LayerIndex'] if out_name.startswith('UV') or out_name == '' else zero,
                 '', ap, 'B')
            sliced[out_name] = ap
            y_slice += 90

    def tap(texture_input, sampler_type, uv_out, ddx_out, ddy_out, x, y):
        """One TextureSample on the shared wrap sampler, with explicit gradients.

        The gradients keep mip selection correct across the hex cell boundaries, where the
        rotated coordinates are discontinuous and implicit derivatives would pick a wildly wrong
        mip on the seam.
        """
        s = _fnexpr(fn, unreal.MaterialExpressionTextureSample, x, y)
        s.set_editor_property('sampler_source', unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        s.set_editor_property('mip_value_mode', unreal.TextureMipValueMode.TMVM_DERIVATIVE)
        s.set_editor_property('sampler_type', sampler_type)
        s.set_editor_property('automatic_view_mip_bias', False)
        link(texture_input, '', s, 'Tex')
        if TEXTURE_ARRAYS:
            link(sliced[uv_out], '', s, 'UVs')
            link(sliced[ddx_out], '', s, 'DDX(UVs)')
            link(sliced[ddy_out], '', s, 'DDY(UVs)')
        else:
            link(coords, uv_out, s, 'UVs')
            link(coords, ddx_out, s, 'DDX(UVs)')
            link(coords, ddy_out, s, 'DDY(UVs)')
        return s

    ST = unreal.MaterialSamplerType
    textures = (('BC', ST.SAMPLERTYPE_COLOR), ('NRM', ST.SAMPLERTYPE_NORMAL), ('HRC', ST.SAMPLERTYPE_MASKS))
    taps = {}
    y_tap = -1400
    for tex_name, sampler_type in textures:
        for suffix, uv_out, dx_out, dy_out in (('Base', '', 'DXBase', 'DYBase'),
                                               ('Dual', 'UVDual', 'DXDual', 'DYDual'),
                                               ('Hex1', 'UVHex1', 'DXHex1', 'DYHex1'),
                                               ('Hex2', 'UVHex2', 'DXHex2', 'DYHex2'),
                                               ('Hex3', 'UVHex3', 'DXHex3', 'DYHex3')):
            taps[tex_name + suffix] = tap(ins[tex_name], sampler_type, uv_out, dx_out, dy_out,
                                          -700, y_tap)
            y_tap += 180

    # Combine per tier, then pick a tier. bHexTiling wins over bDualScale.
    finals = {}
    y_comb = -1400
    for tex_name, _sampler_type in textures:
        hex_combine = custom(fn, _CODE_COMBINE_HEX, CMOT.CMOT_FLOAT4,
                             ['Hex1', 'Hex2', 'Hex3', 'Weights', 'Mean', 'VarianceRestore'],
                             [], -400, y_comb, 'Hex weighted blend')
        for i in (1, 2, 3):
            link(taps['%sHex%d' % (tex_name, i)], 'RGBA', hex_combine, 'Hex%d' % i)
        link(coords, 'Weights', hex_combine, 'Weights')
        if tex_name == 'BC':
            link(ins['VarianceRestore'], '', hex_combine, 'VarianceRestore')
            link(ins['MeanColor'], '', hex_combine, 'Mean')
        else:
            # Restoring variance on a normal or a mask pack pushes values outside their valid
            # range, so only the base colour gets it back.
            off = _fnexpr(fn, unreal.MaterialExpressionConstant, -600, y_comb + 60)
            off.set_editor_property('r', 0.0)
            link(off, '', hex_combine, 'VarianceRestore')
            half = _fnexpr(fn, unreal.MaterialExpressionConstant3Vector, -600, y_comb + 110)
            half.set_editor_property('constant', unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
            link(half, '', hex_combine, 'Mean')

        dual_combine = custom(fn, _CODE_COMBINE_DUAL, CMOT.CMOT_FLOAT4,
                              ['Base', 'Second', 'Amount'], [], -400, y_comb + 200,
                              'Dual scale overlay')
        link(taps[tex_name + 'Base'], 'RGBA', dual_combine, 'Base')
        link(taps[tex_name + 'Dual'], 'RGBA', dual_combine, 'Second')
        link(ins['Amount'], '', dual_combine, 'Amount')

        cheap = _fnexpr(fn, unreal.MaterialExpressionStaticSwitch, -200, y_comb + 100)
        link(dual_combine, '', cheap, 'True')
        link(taps[tex_name + 'Base'], 'RGBA', cheap, 'False')
        link(ins['bDualScale'], '', cheap, 'Value')

        final = _fnexpr(fn, unreal.MaterialExpressionStaticSwitch, 0, y_comb + 100)
        link(hex_combine, '', final, 'True')
        link(cheap, '', final, 'False')
        link(ins['bHexTiling'], '', final, 'Value')
        finals[tex_name] = final
        y_comb += 500

    shade_inputs = ['BC', 'NRM', 'HRC', 'MacroNoise', 'DistanceFade',
                    'TintA', 'TintB', 'TintAmount', 'MacroValueAmount',
                    'HueShift', 'Saturation', 'Value', 'Contrast',
                    'NormalIntensity', 'NormalFlatten', 'NormalFlattenFar', 'NormalFlipY',
                    'RoughnessMin', 'RoughnessMax', 'RoughnessMinFar', 'CavityContrast',
                    'HeightContrast', 'HeightOffset']
    shade = custom(fn, _CODE_SHADE, CMOT.CMOT_FLOAT3, shade_inputs,
                   [('OutNormal', CMOT.CMOT_FLOAT3),
                    ('OutRoughness', CMOT.CMOT_FLOAT1),
                    ('OutCavity', CMOT.CMOT_FLOAT1),
                    ('OutHeight', CMOT.CMOT_FLOAT1)],
                   -100, -100, 'Grade the sampled layer')

    for tex_name, _sampler_type in textures:
        link(finals[tex_name], '', shade, tex_name)
    for pin in shade_inputs:
        if pin in ('BC', 'NRM', 'HRC'):
            continue
        link(ins[pin], '', shade, pin)

    out_color = fn_output(fn, 'BaseColor', 300, -300, 0)
    out_normal = fn_output(fn, 'Normal', 300, -200, 1)
    out_rough = fn_output(fn, 'Roughness', 300, -100, 2)
    out_cavity = fn_output(fn, 'Cavity', 300, 0, 3)
    out_height = fn_output(fn, 'Height', 300, 100, 4)

    link(shade, '', out_color, '')
    link(shade, 'OutNormal', out_normal, '')
    link(shade, 'OutRoughness', out_rough, '')
    link(shade, 'OutCavity', out_cavity, '')
    link(shade, 'OutHeight', out_height, '')

    _finish_fn(fn)
    save(fn)
    _log('built MF_MobLandscapeLayer')
    return fn


# ---------------------------------------------------------------------------
# MF_MobHeightBlendPair
# ---------------------------------------------------------------------------

_CODE_HEIGHT_BLEND = """
float Total = WeightA + WeightB;
float Split = WeightB / max(Total, MOB_EPS);
float T = MobHeightBlendAlpha(HeightA, HeightB, Split, Contrast, HeightAmount);
OutHeight = lerp(HeightA, HeightB, T);
OutWeight = Total;
return T;
"""


def build_height_blend_function():
    """MF_MobHeightBlendPair: folds one more layer into the running blend.

    Chaining pairs keeps the master a straight line of ten nodes instead of one Custom node
    with sixty-odd inputs, and the interlocking maths stays in a single HLSL function.
    """
    fn = get_or_create_function(
        'MF_MobHeightBlendPair',
        'Blends the next paint layer into the accumulated result using both layers heights, '
        'so transitions interlock along their detail instead of cross-fading.')

    a_color = fn_input(fn, 'BaseColorA', FIT.FUNCTION_INPUT_VECTOR3, -800, -400, 0, default=(0.0, 0.0, 0.0))
    a_normal = fn_input(fn, 'NormalA', FIT.FUNCTION_INPUT_VECTOR3, -800, -340, 1, default=(0.0, 0.0, 1.0))
    a_rough = fn_input(fn, 'RoughnessA', FIT.FUNCTION_INPUT_SCALAR, -800, -280, 2, default=0.5)
    a_cavity = fn_input(fn, 'CavityA', FIT.FUNCTION_INPUT_SCALAR, -800, -220, 3, default=1.0)
    a_height = fn_input(fn, 'HeightA', FIT.FUNCTION_INPUT_SCALAR, -800, -160, 4, default=0.0)
    a_weight = fn_input(fn, 'WeightA', FIT.FUNCTION_INPUT_SCALAR, -800, -100, 5, default=0.0)

    b_color = fn_input(fn, 'BaseColorB', FIT.FUNCTION_INPUT_VECTOR3, -800, 0, 6, default=(0.0, 0.0, 0.0))
    b_normal = fn_input(fn, 'NormalB', FIT.FUNCTION_INPUT_VECTOR3, -800, 60, 7, default=(0.0, 0.0, 1.0))
    b_rough = fn_input(fn, 'RoughnessB', FIT.FUNCTION_INPUT_SCALAR, -800, 120, 8, default=0.5)
    b_cavity = fn_input(fn, 'CavityB', FIT.FUNCTION_INPUT_SCALAR, -800, 180, 9, default=1.0)
    b_height = fn_input(fn, 'HeightB', FIT.FUNCTION_INPUT_SCALAR, -800, 240, 10, default=0.0)
    b_weight = fn_input(fn, 'WeightB', FIT.FUNCTION_INPUT_SCALAR, -800, 300, 11, default=0.0)

    contrast = fn_input(fn, 'Contrast', FIT.FUNCTION_INPUT_SCALAR, -800, 360, 12, default=0.5,
                        description='Width of the band the layers interlock over')
    height_amount = fn_input(fn, 'HeightAmount', FIT.FUNCTION_INPUT_SCALAR, -800, 420, 13, default=0.35,
                             description='0 is a plain gradient from the paint weight, 1 is fully height-interlocked')

    blend = custom(fn, _CODE_HEIGHT_BLEND, CMOT.CMOT_FLOAT1,
                   ['HeightA', 'WeightA', 'HeightB', 'WeightB', 'Contrast', 'HeightAmount'],
                   [('OutHeight', CMOT.CMOT_FLOAT1), ('OutWeight', CMOT.CMOT_FLOAT1)],
                   -400, 0, 'Height-aware blend alpha')

    link(a_height, '', blend, 'HeightA')
    link(a_weight, '', blend, 'WeightA')
    link(b_height, '', blend, 'HeightB')
    link(b_weight, '', blend, 'WeightB')
    link(contrast, '', blend, 'Contrast')
    link(height_amount, '', blend, 'HeightAmount')

    def lerp_node(src_a, src_b, x, y):
        node = _fnexpr(fn, unreal.MaterialExpressionLinearInterpolate, x, y)
        link(src_a, '', node, 'A')
        link(src_b, '', node, 'B')
        link(blend, '', node, 'Alpha')
        return node

    mix_color = lerp_node(a_color, b_color, -100, -300)
    mix_normal = lerp_node(a_normal, b_normal, -100, -150)
    mix_rough = lerp_node(a_rough, b_rough, -100, 0)
    mix_cavity = lerp_node(a_cavity, b_cavity, -100, 150)

    normalize = _fnexpr(fn, unreal.MaterialExpressionNormalize, 100, -150)
    link(mix_normal, '', normalize, '')

    link(mix_color, '', fn_output(fn, 'BaseColor', 400, -300, 0), '')
    link(normalize, '', fn_output(fn, 'Normal', 400, -200, 1), '')
    link(mix_rough, '', fn_output(fn, 'Roughness', 400, -100, 2), '')
    link(mix_cavity, '', fn_output(fn, 'Cavity', 400, 0, 3), '')
    link(blend, 'OutHeight', fn_output(fn, 'Height', 400, 100, 4), '')
    link(blend, 'OutWeight', fn_output(fn, 'Weight', 400, 200, 5), '')

    _finish_fn(fn)
    save(fn)
    _log('built MF_MobHeightBlendPair')
    return fn


# ---------------------------------------------------------------------------
# MF_MobLayerWeight
# ---------------------------------------------------------------------------

_CODE_LAYER_WEIGHT = """
float Slope = 1.0f - MobSlopeMask(WorldNormal, SlopeMin, SlopeMax);
float Altitude = MobAltitudeMask(WorldZ, AltitudeMin, AltitudeMax, AltitudeFeather);
return saturate(RawWeight * lerp(1.0f, Slope, SlopeAmount) * lerp(1.0f, Altitude, AltitudeAmount));
"""


def build_layer_weight_function():
    """MF_MobLayerWeight: gates a painted weight by slope and altitude.

    The master samples raw weights with LandscapeLayerSample rather than handing them to a
    LandscapeLayerBlend, because a blend node gives no access to the weight itself and these
    masks have to multiply into it.
    """
    fn = get_or_create_function(
        'MF_MobLayerWeight',
        'Masks one paint layer weight by slope and world height so a layer can refuse to '
        'appear on cliffs or below a waterline.')

    raw = fn_input(fn, 'RawWeight', FIT.FUNCTION_INPUT_SCALAR, -600, -200, 0, default=0.0)
    wnormal = fn_input(fn, 'WorldNormal', FIT.FUNCTION_INPUT_VECTOR3, -600, -140, 1, default=(0.0, 0.0, 1.0))
    wz = fn_input(fn, 'WorldZ', FIT.FUNCTION_INPUT_SCALAR, -600, -80, 2, default=0.0)
    smin = fn_input(fn, 'SlopeMin', FIT.FUNCTION_INPUT_SCALAR, -600, -20, 3, default=30.0)
    smax = fn_input(fn, 'SlopeMax', FIT.FUNCTION_INPUT_SCALAR, -600, 40, 4, default=55.0)
    samt = fn_input(fn, 'SlopeAmount', FIT.FUNCTION_INPUT_SCALAR, -600, 100, 5, default=0.0)
    amin = fn_input(fn, 'AltitudeMin', FIT.FUNCTION_INPUT_SCALAR, -600, 160, 6, default=-100000.0)
    amax = fn_input(fn, 'AltitudeMax', FIT.FUNCTION_INPUT_SCALAR, -600, 220, 7, default=100000.0)
    afeather = fn_input(fn, 'AltitudeFeather', FIT.FUNCTION_INPUT_SCALAR, -600, 280, 8, default=200.0)
    aamt = fn_input(fn, 'AltitudeAmount', FIT.FUNCTION_INPUT_SCALAR, -600, 340, 9, default=0.0)

    node = custom(fn, _CODE_LAYER_WEIGHT, CMOT.CMOT_FLOAT1,
                  ['RawWeight', 'WorldNormal', 'WorldZ', 'SlopeMin', 'SlopeMax', 'SlopeAmount',
                   'AltitudeMin', 'AltitudeMax', 'AltitudeFeather', 'AltitudeAmount'],
                  [], -200, 0, 'Slope and altitude gate')

    for pin, src in (('RawWeight', raw), ('WorldNormal', wnormal), ('WorldZ', wz),
                     ('SlopeMin', smin), ('SlopeMax', smax), ('SlopeAmount', samt),
                     ('AltitudeMin', amin), ('AltitudeMax', amax),
                     ('AltitudeFeather', afeather), ('AltitudeAmount', aamt)):
        link(src, '', node, pin)

    link(node, '', fn_output(fn, 'Weight', 200, 0, 0), '')

    _finish_fn(fn)
    save(fn)
    _log('built MF_MobLayerWeight')
    return fn


# ---------------------------------------------------------------------------
# Overlays
# ---------------------------------------------------------------------------

# Base attributes every overlay receives and hands back, and the matching overlay-side pins.
_ATTR = [
    ('BaseColor', FIT.FUNCTION_INPUT_VECTOR3, (0.0, 0.0, 0.0)),
    ('Normal',    FIT.FUNCTION_INPUT_VECTOR3, (0.0, 0.0, 1.0)),
    ('Roughness', FIT.FUNCTION_INPUT_SCALAR,  0.5),
    ('Cavity',    FIT.FUNCTION_INPUT_SCALAR,  1.0),
    ('Height',    FIT.FUNCTION_INPUT_SCALAR,  0.0),
]


def _attr_inputs(fn, suffix, x, sort_base):
    """Creates the five attribute inputs, suffixed so base and overlay sets do not collide."""
    out = {}
    y = -400
    for i, (name, in_type, default) in enumerate(_ATTR):
        out[name] = fn_input(fn, name + suffix, in_type, x, y, sort_base + i, default=default)
        y += 60
    return out


def _attr_outputs(fn, base, over, alpha_node, alpha_out, x):
    """Lerps every attribute from base to overlay by alpha and wires the function outputs."""
    y = -300
    for i, (name, _t, _d) in enumerate(_ATTR):
        mix = _fnexpr(
            fn, unreal.MaterialExpressionLinearInterpolate, x, y)
        link(base[name], '', mix, 'A')
        link(over[name], '', mix, 'B')
        link(alpha_node, alpha_out, mix, 'Alpha')
        src = mix
        if name == 'Normal':
            src = _fnexpr(
                fn, unreal.MaterialExpressionNormalize, x + 200, y)
            link(mix, '', src, '')
        link(src, '', fn_output(fn, name, x + 400, y, i), '')
        y += 120


_CODE_SLOPE_ROCK = """
float Slope = MobSlopeMask(WorldNormal, SlopeStart, SlopeEnd);
Slope = saturate(Slope + (MacroNoise - 0.5f) * NoiseAmount);
float T = MobHeightBlendAlpha(Height, RockHeight, Slope, HeightContrast, HeightAmount);
OutMask = T;
return T;
"""


def build_slope_rock_function():
    """MF_MobSlopeRock: replaces the painted result with rock as the ground turns vertical."""
    fn = get_or_create_function(
        'MF_MobSlopeRock',
        'Swaps the blended surface for the rock layer on steep ground. Runs after the paint '
        'blend so it costs one transition rather than a slot in every layer.')

    base = _attr_inputs(fn, '', -1000, 0)
    rock = _attr_inputs(fn, 'Rock', -1000, 10)
    wnormal = fn_input(fn, 'WorldNormal', FIT.FUNCTION_INPUT_VECTOR3, -1000, 300, 20, default=(0.0, 0.0, 1.0))
    noise = fn_input(fn, 'MacroNoise', FIT.FUNCTION_INPUT_SCALAR, -1000, 360, 21, default=0.5)
    s_start = fn_input(fn, 'SlopeStart', FIT.FUNCTION_INPUT_SCALAR, -1000, 420, 22, default=32.0)
    s_end = fn_input(fn, 'SlopeEnd', FIT.FUNCTION_INPUT_SCALAR, -1000, 480, 23, default=55.0)
    contrast = fn_input(fn, 'HeightContrast', FIT.FUNCTION_INPUT_SCALAR, -1000, 540, 24, default=0.5)
    rock_amount = fn_input(fn, 'HeightAmount', FIT.FUNCTION_INPUT_SCALAR, -1000, 570, 26, default=0.5)
    noise_amt = fn_input(fn, 'NoiseAmount', FIT.FUNCTION_INPUT_SCALAR, -1000, 600, 25, default=0.15,
                         description='Breaks the slope threshold so it does not read as a contour line')

    node = custom(fn, _CODE_SLOPE_ROCK, CMOT.CMOT_FLOAT1,
                  ['WorldNormal', 'MacroNoise', 'Height', 'RockHeight',
                   'SlopeStart', 'SlopeEnd', 'HeightContrast', 'HeightAmount', 'NoiseAmount'],
                  [('OutMask', CMOT.CMOT_FLOAT1)], -600, 0, 'Slope transition')

    link(wnormal, '', node, 'WorldNormal')
    link(noise, '', node, 'MacroNoise')
    link(base['Height'], '', node, 'Height')
    link(rock['Height'], '', node, 'RockHeight')
    link(s_start, '', node, 'SlopeStart')
    link(s_end, '', node, 'SlopeEnd')
    link(contrast, '', node, 'HeightContrast')
    link(rock_amount, '', node, 'HeightAmount')
    link(noise_amt, '', node, 'NoiseAmount')

    _attr_outputs(fn, base, rock, node, '', -200)
    link(node, 'OutMask', fn_output(fn, 'Mask', 200, 400, 5), '')

    _finish_fn(fn)
    save(fn)
    _log('built MF_MobSlopeRock')
    return fn


_CODE_MOSS = """
float Slope = MobSlopeMask(WorldNormal, SlopeMin, SlopeMax);
float SlopeTerm = lerp(1.0f - Slope, Slope, SlopeFavour);
float Cavity = MobMicroCavity(Height, CavityBias, CavityContrast);
float Shade = MobShadeMask(WorldNormal, SunDirection, ShadeSoftness);
float Noise = saturate((MacroNoise - NoiseBias) * NoiseContrast + 0.5f);

float M = Amount;
M *= lerp(1.0f, SlopeTerm, SlopeAmount);
M *= lerp(1.0f, Cavity, CavityAmount);
M *= lerp(1.0f, Shade, ShadeAmount);
M *= lerp(1.0f, Noise, NoiseAmount);
M = saturate(M + PaintWeight);

float T = MobHeightBlendAlpha(Height, MossHeight, M, HeightContrast, HeightAmount);
OutMask = T;
return T;
"""


def build_moss_function():
    """MF_MobTerrainMoss: procedural moss in the damp, shaded, crevice-heavy places."""
    fn = get_or_create_function(
        'MF_MobTerrainMoss',
        'Adds moss where the ground is shaded, concave and rough. SlopeFavour aimed at 1 puts '
        'it on rock faces, which is what turns the procedural slope rock into mossy rock.')

    base = _attr_inputs(fn, '', -1200, 0)
    moss = _attr_inputs(fn, 'Moss', -1200, 10)

    wnormal = fn_input(fn, 'WorldNormal', FIT.FUNCTION_INPUT_VECTOR3, -1200, 300, 20, default=(0.0, 0.0, 1.0))
    sun = fn_input(fn, 'SunDirection', FIT.FUNCTION_INPUT_VECTOR3, -1200, 360, 21, default=(0.0, 0.0, -1.0),
                   description='Points from sky to ground; drive from a parameter collection')
    noise = fn_input(fn, 'MacroNoise', FIT.FUNCTION_INPUT_SCALAR, -1200, 420, 22, default=0.5)
    paint = fn_input(fn, 'PaintWeight', FIT.FUNCTION_INPUT_SCALAR, -1200, 480, 23, default=0.0,
                     description='Painted Moss layer, added on top of the procedural mask')

    controls = [
        ('Amount',         0.0,  30, 'Global moss density'),
        ('SlopeMin',       20.0, 31, ''),
        ('SlopeMax',       60.0, 32, ''),
        ('SlopeFavour',    1.0,  33, '0 favours flat ground, 1 favours rock faces'),
        ('SlopeAmount',    1.0,  34, ''),
        ('CavityBias',     0.0,  35, ''),
        ('CavityContrast', 1.5,  36, ''),
        ('CavityAmount',   1.0,  37, ''),
        ('ShadeSoftness',  0.5,  38, ''),
        ('ShadeAmount',    1.0,  39, ''),
        ('NoiseBias',      0.5,  40, ''),
        ('NoiseContrast',  2.0,  41, ''),
        ('NoiseAmount',    1.0,  42, ''),
        ('HeightContrast', 0.4,  43, ''),
        ('HeightAmount',   0.4,  44, '0 is a plain gradient, 1 is fully height-interlocked'),
    ]
    ctl = {}
    y = 540
    for name, default, sort, desc in controls:
        ctl[name] = fn_input(fn, name, FIT.FUNCTION_INPUT_SCALAR, -1200, y, sort,
                             default=default, description=desc)
        y += 60

    pins = (['WorldNormal', 'SunDirection', 'MacroNoise', 'PaintWeight', 'Height', 'MossHeight']
            + [c[0] for c in controls])
    node = custom(fn, _CODE_MOSS, CMOT.CMOT_FLOAT1, pins,
                  [('OutMask', CMOT.CMOT_FLOAT1)], -700, 0, 'Moss mask')

    link(wnormal, '', node, 'WorldNormal')
    link(sun, '', node, 'SunDirection')
    link(noise, '', node, 'MacroNoise')
    link(paint, '', node, 'PaintWeight')
    link(base['Height'], '', node, 'Height')
    link(moss['Height'], '', node, 'MossHeight')
    for name, _d, _s, _desc in controls:
        link(ctl[name], '', node, name)

    _attr_outputs(fn, base, moss, node, '', -300)
    link(node, 'OutMask', fn_output(fn, 'Mask', 100, 400, 5), '')

    _finish_fn(fn)
    save(fn)
    _log('built MF_MobTerrainMoss')
    return fn


_CODE_WETNESS = """
float Cavity = MobMicroCavity(Height, CavityBias, CavityContrast);
float W = saturate(Amount * lerp(1.0f, Cavity, CavityAmount) + PaintWeight);

// Standing water sits in the deepest part of the cavity, so it needs a harder threshold than
// the damp darkening around it.
float Puddle = saturate((Cavity - (1.0f - PuddleDepth)) * 8.0f) * W;

float3 Col = BaseColor * lerp(1.0f, Darkening, W);
float Rough = lerp(Roughness, RoughnessTarget, W);
float3 N = MobFlattenNormal(Normal, saturate(NormalFlatten * W));

Rough = lerp(Rough, PuddleRoughness, Puddle);
N = MobFlattenNormal(N, Puddle);

OutNormal = N;
OutRoughness = saturate(Rough);
OutMask = W;
OutPuddle = Puddle;
return Col;
"""


def build_wetness_function():
    """MF_MobTerrainWetness: damp darkening plus standing water in the crevices."""
    fn = get_or_create_function(
        'MF_MobTerrainWetness',
        'Darkens and smooths the surface where it is wet, and flattens it out completely where '
        'water stands. Mask feeds the physical material output so wet dirt reads as mud.')

    base = _attr_inputs(fn, '', -900, 0)
    paint = fn_input(fn, 'PaintWeight', FIT.FUNCTION_INPUT_SCALAR, -900, 300, 20, default=0.0)

    controls = [
        ('Amount',          0.0,  30, 'Global wetness'),
        ('Darkening',       0.55, 31, 'Base colour multiplier when fully wet'),
        ('RoughnessTarget', 0.25, 32, ''),
        ('NormalFlatten',   0.4,  33, ''),
        ('CavityBias',      0.0,  34, ''),
        ('CavityContrast',  2.0,  35, ''),
        ('CavityAmount',    1.0,  36, ''),
        ('PuddleDepth',     0.15, 37, 'How deep a crevice has to be before water stands in it'),
        ('PuddleRoughness', 0.05, 38, ''),
    ]
    ctl = {}
    y = 360
    for name, default, sort, desc in controls:
        ctl[name] = fn_input(fn, name, FIT.FUNCTION_INPUT_SCALAR, -900, y, sort,
                             default=default, description=desc)
        y += 60

    pins = ['BaseColor', 'Normal', 'Roughness', 'Height', 'PaintWeight'] + [c[0] for c in controls]
    node = custom(fn, _CODE_WETNESS, CMOT.CMOT_FLOAT3, pins,
                  [('OutNormal', CMOT.CMOT_FLOAT3),
                   ('OutRoughness', CMOT.CMOT_FLOAT1),
                   ('OutMask', CMOT.CMOT_FLOAT1),
                   ('OutPuddle', CMOT.CMOT_FLOAT1)],
                  -400, 0, 'Wetness')

    for pin in ('BaseColor', 'Normal', 'Roughness', 'Height'):
        link(base[pin], '', node, pin)
    link(paint, '', node, 'PaintWeight')
    for name, _d, _s, _desc in controls:
        link(ctl[name], '', node, name)

    link(node, '', fn_output(fn, 'BaseColor', 0, -300, 0), '')
    link(node, 'OutNormal', fn_output(fn, 'Normal', 0, -200, 1), '')
    link(node, 'OutRoughness', fn_output(fn, 'Roughness', 0, -100, 2), '')
    link(base['Cavity'], '', fn_output(fn, 'Cavity', 0, 0, 3), '')
    link(base['Height'], '', fn_output(fn, 'Height', 0, 100, 4), '')
    link(node, 'OutMask', fn_output(fn, 'Mask', 0, 200, 5), '')
    link(node, 'OutPuddle', fn_output(fn, 'Puddle', 0, 300, 6), '')

    _finish_fn(fn)
    save(fn)
    _log('built MF_MobTerrainWetness')
    return fn


# ---------------------------------------------------------------------------
# PROJECT INTEGRATION: runtime virtual textures
# ---------------------------------------------------------------------------
#
# Off by default. On, the master gains its runtime virtual texture, physical material and grass
# outputs, which reference assets under the paths configured in this file. Leave it off and the
# material stays self-contained.

BUILD_PROJECT_OUTPUTS = False
#
# An RVT is game data, not plugin data: it is sized to a landscape and referenced by a level.
# Point this at wherever the project keeps its textures.

RVT_DIR = '/Game/Environment/Textures'
RVT_TERRAIN = RVT_DIR + '/RVT_MobTerrain'
RVT_HEIGHT = RVT_DIR + '/RVT_MobTerrainHeight'


def _get_or_create_rvt(package_path, name):
    path = package_path + '/' + name
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    return _tools().create_asset(name, package_path, unreal.RuntimeVirtualTexture,
                                 unreal.RuntimeVirtualTextureFactory())


def build_rvt_assets(tile_count=4, tile_size=2, tile_border=2):
    """The two runtime virtual textures the landscape writes.

    Both values are exponents: 4 and 2 mean 16 tiles of 256 pixels, so 4096 texels across the
    volume. Against L_Hub_Hermitage's 8000 unit landscape that is a shade under 2 cm per texel.
    Raise tile_count by one per doubling of landscape size to hold that density.
    """
    MT = unreal.RuntimeVirtualTextureMaterialType

    terrain = _get_or_create_rvt(RVT_DIR, 'RVT_MobTerrain')
    terrain.set_editor_property('material_type', MT.BASE_COLOR_NORMAL_ROUGHNESS)
    terrain.set_editor_property('tile_count', tile_count)
    terrain.set_editor_property('tile_size', tile_size)
    terrain.set_editor_property('tile_border_size', tile_border)
    terrain.set_editor_property('compress_textures', True)
    save(terrain)

    height = _get_or_create_rvt(RVT_DIR, 'RVT_MobTerrainHeight')
    height.set_editor_property('material_type', MT.WORLD_HEIGHT)
    height.set_editor_property('tile_count', max(tile_count - 2, 0))
    height.set_editor_property('tile_size', tile_size)
    height.set_editor_property('tile_border_size', tile_border)
    save(height)

    _log('built runtime virtual textures')
    return terrain, height


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


def _param_texture(mat, name, texture, group, x, y, sort=0):
    e = _expr(mat, unreal.MaterialExpressionTextureObjectParameter, x, y)
    e.set_editor_property('parameter_name', name)
    e.set_editor_property('texture', texture)
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


_ATTR_PINS = ['BaseColor', 'Normal', 'Roughness', 'Cavity', 'Height']

# Controls the master owns rather than the layer function.
_WEIGHT_CONTROLS = [
    ('SlopeMin', 30.0), ('SlopeMax', 55.0), ('SlopeAmount', 0.0),
    ('AltitudeMin', -100000.0), ('AltitudeMax', 100000.0),
    ('AltitudeFeather', 200.0), ('AltitudeAmount', 0.0),
]


def _build_layer_block(mat, layer, x, y, shared, layer_fn, weight_fn, sampled_weight=True,
                       layer_index=0):
    """One paint layer: texture parameters, controls, the layer function and its masked weight."""
    call = _fn_call(mat, layer_fn, x + 900, y)

    if TEXTURE_ARRAYS:
        for pin in ('BC', 'NRM', 'HRC'):
            link(shared['arrays'][pin], '', call, pin)
        index = _expr(mat, unreal.MaterialExpressionConstant, x, y)
        index.set_editor_property('r', float(layer_index))
        link(index, '', call, 'LayerIndex')
    else:
        for pin, default_path, sort in (('BC', BASE_TEX_BC, 0),
                                        ('NRM', BASE_TEX_NRM, 1),
                                        ('HRC', BASE_TEX_HRC, 2)):
            tex = _param_texture(mat, '%s_%s' % (layer, pin), unreal.load_asset(default_path),
                                 layer, x, y + 90 * sort, sort)
            link(tex, '', call, pin)
    link(shared['uv'], '', call, 'UV')
    link(shared['macro'], 'R', call, 'MacroNoise')
    link(shared['fade'], '', call, 'DistanceFade')

    yy = y + 190
    for name, in_type, default, sort, _desc in _LAYER_CONTROLS:
        if name == 'UVOffset':
            u = _param_scalar(mat, layer + '_UVOffsetU', 0.0, layer, x, yy, sort)
            v = _param_scalar(mat, layer + '_UVOffsetV', 0.0, layer, x, yy + 50, sort)
            ap = _expr(mat, unreal.MaterialExpressionAppendVector, x + 400, yy)
            link(u, '', ap, 'A')
            link(v, '', ap, 'B')
            link(ap, '', call, 'UVOffset')
            yy += 100
        elif in_type == FIT.FUNCTION_INPUT_VECTOR3:
            link(_param_vector(mat, layer + '_' + name, default, layer, x, yy, sort), '', call, name)
            yy += 50
        else:
            link(_param_scalar(mat, layer + '_' + name, default, layer, x, yy, sort), '', call, name)
            yy += 50

    link(_param_static_bool(mat, layer + '_HexTiling', False, layer, x, yy, 70), '', call, 'bHexTiling')
    link(_param_static_bool(mat, layer + '_DualScale', True, layer, x, yy + 50, 71), '', call, 'bDualScale')

    weight = None
    if sampled_weight:
        sample = _expr(
            mat, unreal.MaterialExpressionLandscapeLayerSample, x, y - 140)
        sample.set_editor_property('parameter_name', layer)
        sample.set_editor_property('preview_weight', 0.0)

        weight = _fn_call(mat, weight_fn, x + 900, y - 140)
        link(sample, '', weight, 'RawWeight')
        link(shared['normal'], '', weight, 'WorldNormal')
        link(shared['worldz'], '', weight, 'WorldZ')
        wy = y - 140
        for name, default in _WEIGHT_CONTROLS:
            link(_param_scalar(mat, layer + '_' + name, default, layer, x + 400, wy, 80), '', weight, name)
            wy += 50

    return call, weight


# Generated with one input per non-base layer; the names are the layer names.
_CODE_BASE_WEIGHT = """
float Painted = %s;
return max(Base, 1.0f - saturate(Painted));
"""


_CODE_DEBUG = """
return MobDebugView((int)Mode, Weights, Cavity, Normal, Wetness, Height, VertexColour);
"""

_CODE_GLOBAL_GRADE = """
float3 Col = MobApplyHSV(BaseColor, GlobalHue, GlobalSaturation, GlobalValue) * GlobalTint;
Col = lerp(Col, dot(Col, float3(0.299f, 0.587f, 0.114f)).xxx, saturate(DistanceDesaturation * DistanceFade));
Col = lerp(Col, DistanceColor, saturate(DistanceColorAmount * DistanceFade));

// Cavity lands here rather than on the AO pin: it darkens the albedo and takes the sheen out of
// the crevices, which is what micro shadowing actually does, and it cannot double with whatever
// occlusion the renderer is already applying.
Col = MobApplyCavity(Col, Cavity, CavityColorAmount);

OutNormal = MobFlattenNormal(Normal, saturate(GlobalNormalFlatten));
OutRoughness = saturate(max(Roughness, GlobalRoughnessMin));
OutSpecular = saturate(Specular * lerp(1.0f, Cavity, saturate(CavitySpecularAmount)));
return max(Col, 0.0f);
"""



def _landscape_debug(mat, blocks, acc, colour_src, normal_src, wet):
    """Routes a debug view over the finished terrain.

    The weights come from the first three paint layers rather than the blend, because a landscape
    blend is a chain of pairs and there is no single place holding a triple. Three is enough to
    read: it is the transitions between neighbouring layers that go wrong, not layer nine alone.
    """
    names = [layer for layer, _s, _p in LAYERS[:3]]
    weights = []
    for layer in names:
        weights.append((blocks[layer][1], 'Weight'))
    while len(weights) < 3:
        zero = _expr(mat, unreal.MaterialExpressionConstant, 1400, 1400)
        zero.set_editor_property('r', 0.0)
        weights.append((zero, ''))

    rg = _expr(mat, unreal.MaterialExpressionAppendVector, 1500, 1300)
    link(weights[0][0], weights[0][1], rg, 'A')
    link(weights[1][0], weights[1][1], rg, 'B')
    rgb = _expr(mat, unreal.MaterialExpressionAppendVector, 1600, 1300)
    link(rg, '', rgb, 'A')
    link(weights[2][0], weights[2][1], rgb, 'B')

    vcol = _expr(mat, unreal.MaterialExpressionVertexColor, 1400, 1500)

    dbg = custom(mat, _CODE_DEBUG, CMOT.CMOT_FLOAT3,
                 ['Mode', 'Weights', 'Cavity', 'Normal', 'Wetness', 'Height', 'VertexColour'],
                 [], 1700, 1300, 'Debug view')
    link(_param_scalar(mat, 'DebugMode', 1.0, 'Debug', 1400, 1200, 1), '', dbg, 'Mode')
    link(rgb, '', dbg, 'Weights')
    link(acc['Cavity'][0], acc['Cavity'][1], dbg, 'Cavity')
    link(acc['Normal'][0], acc['Normal'][1], dbg, 'Normal')
    link(wet, 'Mask', dbg, 'Wetness')
    link(acc['Height'][0], acc['Height'][1], dbg, 'Height')
    link(vcol, '', dbg, 'VertexColour')

    black = _expr(mat, unreal.MaterialExpressionConstant3Vector, 1700, 1450)
    black.set_editor_property('constant', unreal.LinearColor(0.0, 0.0, 0.0, 1.0))

    # Terrain has no emissive to borrow, so the view goes to base colour with the normal flattened:
    # an unlit read is the point, and a flat normal is the closest this gets to one.
    flat = _expr(mat, unreal.MaterialExpressionConstant3Vector, 1700, 1550)
    flat.set_editor_property('constant', unreal.LinearColor(0.0, 0.0, 1.0, 1.0))

    sw_c = _expr(mat, unreal.MaterialExpressionStaticSwitchParameter, 1900, 1300)
    sw_c.set_editor_property('parameter_name', 'bDebug')
    sw_c.set_editor_property('group', 'Debug')
    link(dbg, '', sw_c, 'True')
    link(colour_src, '', sw_c, 'False')

    sw_n = _expr(mat, unreal.MaterialExpressionStaticSwitchParameter, 1900, 1450)
    sw_n.set_editor_property('parameter_name', 'bDebug')
    sw_n.set_editor_property('group', 'Debug')
    link(flat, '', sw_n, 'True')
    link(normal_src, '', sw_n, 'False')

    return sw_c, sw_n


def build_master_material():
    """M_MobLandscape.

    The blend is a chain of MF_MobHeightBlendPair rather than a LandscapeLayerBlend, because a
    blend node exposes no weight to mask and this material has to gate layers by slope and
    altitude. LandscapeLayerSample registers the layer names with the landscape editor just the
    same.
    """
    # No landscape usage flag exists in UE5; the landscape vertex factory permutation comes from
    # the material being assigned to a landscape.
    mat = get_or_create_material(ROOT, 'M_' + MASTER_NAME)
    mat.set_editor_property('two_sided', False)

    noise_tex = unreal.load_asset(BASE_TEX_HRC)
    layer_fn = FN_ROOT + '/MF_MobLandscapeLayer'
    weight_fn = FN_ROOT + '/MF_MobLayerWeight'

    # --- shared inputs ----------------------------------------------------
    coords = _expr(mat, unreal.MaterialExpressionLandscapeLayerCoords, -3400, -1200)
    coords.set_editor_property('mapping_scale', 1.0)

    wnormal = _expr(mat, unreal.MaterialExpressionVertexNormalWS, -3400, -1100)

    wpos = _expr(mat, unreal.MaterialExpressionWorldPosition, -3400, -1000)
    worldz = _expr(mat, unreal.MaterialExpressionComponentMask, -3200, -1000)
    worldz.set_editor_property('r', False)
    worldz.set_editor_property('g', False)
    worldz.set_editor_property('b', True)
    worldz.set_editor_property('a', False)
    link(wpos, '', worldz, '')

    depth = _expr(mat, unreal.MaterialExpressionPixelDepth, -3400, -900)
    fade = custom(mat, 'return MobDistanceFade(Depth, Start, Falloff);', CMOT.CMOT_FLOAT1,
                  ['Depth', 'Start', 'Falloff'], [], -3000, -900, 'Distance fade')
    link(depth, '', fade, 'Depth')
    link(_param_scalar(mat, 'DistanceFadeStart', 3000.0, 'Global', -3400, -830), '', fade, 'Start')
    link(_param_scalar(mat, 'DistanceFadeFalloff', 8000.0, 'Global', -3400, -780), '', fade, 'Falloff')

    # One macro-variation fetch for the whole material rather than one per layer.
    macro_scale = _param_scalar(mat, 'MacroNoiseScale', 0.012, 'Global', -3400, -700)
    macro_uv = _expr(mat, unreal.MaterialExpressionMultiply, -3200, -700)
    link(coords, '', macro_uv, 'A')
    link(macro_scale, '', macro_uv, 'B')
    macro_tex = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -3000, -700)
    macro_tex.set_editor_property('parameter_name', 'MacroNoiseTexture')
    macro_tex.set_editor_property('texture', noise_tex)
    macro_tex.set_editor_property('sampler_source', unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    macro_tex.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    link(macro_uv, '', macro_tex, 'UVs')

    sun = _param_vector(mat, 'SunDirection', (0.0, 0.35, -0.94), 'Global', -3400, -620)

    shared = {'uv': coords, 'normal': wnormal, 'worldz': worldz,
              'fade': fade, 'macro': macro_tex, 'sun': sun}

    if TEXTURE_ARRAYS:
        # Three texture objects for the whole material, whatever the layer count. Sliced in the
        # order mob_arrays packs them, which is the recipe's layer order with moss last.
        shared['arrays'] = {}
        for i, (pin, default_path) in enumerate((('BC', BASE_TEX_BC), ('NRM', BASE_TEX_NRM),
                                                 ('HRC', BASE_TEX_HRC))):
            path = '%s/TA_%s_%s' % (ROOT, MASTER_NAME, pin)
            array = unreal.load_asset(path) or unreal.load_asset(default_path)
            shared['arrays'][pin] = _param_texture(mat, 'Layers_' + pin, array,
                                                   'Global', -3400, -560 + 60 * i, i)

    # --- paint layers -----------------------------------------------------
    blocks = {}
    x = -2600
    y = -400
    for index, (layer, _st, _pm) in enumerate(LAYERS):
        blocks[layer] = _build_layer_block(mat, layer, x, y, shared, layer_fn, weight_fn,
                                           layer_index=index)
        y += 2200

    # Moss and wetness are overlays: they sample like a layer but their weight is a painted
    # alpha rather than a weight-blended one, so they do not compete for the normalised weight.
    moss_call, _ = _build_layer_block(mat, 'Moss', x, y, shared, layer_fn, weight_fn,
                                      sampled_weight=False, layer_index=len(LAYERS))
    y += 2200
    moss_paint = _expr(mat, unreal.MaterialExpressionLandscapeLayerSample, x, y)
    moss_paint.set_editor_property('parameter_name', 'Moss')
    moss_paint.set_editor_property('preview_weight', 0.0)
    wet_paint = _expr(mat, unreal.MaterialExpressionLandscapeLayerSample, x, y + 100)
    wet_paint.set_editor_property('parameter_name', 'Wetness')
    wet_paint.set_editor_property('preview_weight', 0.0)

    # --- fold the layers together ----------------------------------------
    first_call, first_weight = blocks[LAYERS[0][0]]
    acc = {p: (first_call, p) for p in _ATTR_PINS}
    acc_weight = (first_weight, 'Weight')

    # The first layer is the base: it holds whatever weight the painted layers leave behind.
    # Without this, untouched ground shows the base layer at a painted weight of zero, so the
    # first brush stroke blends against nothing and lands as a hard edge.
    other_pins = [layer for layer, _s, _p in LAYERS[1:]]
    base_weight = custom(mat, _CODE_BASE_WEIGHT % ' + '.join(other_pins), CMOT.CMOT_FLOAT1,
                         ['Base'] + other_pins, [], -1600, -700, 'Base layer weight')
    link(first_weight, 'Weight', base_weight, 'Base')
    for layer in other_pins:
        link(blocks[layer][1], 'Weight', base_weight, layer)
    acc_weight = (base_weight, '')

    # Both blend controls are shared by every transition, so the whole terrain reads the same.
    blend_contrast = _param_scalar(mat, 'BlendHeightContrast', 0.5, 'Blending', -1600, -600)
    blend_amount = _param_scalar(mat, 'BlendHeightAmount', 0.35, 'Blending', -1600, -540)

    bx = -1400
    by = -400
    for layer, _st, _pm in LAYERS[1:]:
        call, weight = blocks[layer]
        pair = _fn_call(mat, FN_ROOT + '/MF_MobHeightBlendPair', bx, by)
        for pin in _ATTR_PINS:
            src, out = acc[pin]
            link(src, out, pair, pin + 'A')
            link(call, pin, pair, pin + 'B')
        link(acc_weight[0], acc_weight[1], pair, 'WeightA')
        link(weight, 'Weight', pair, 'WeightB')
        link(blend_contrast, '', pair, 'Contrast')
        link(blend_amount, '', pair, 'HeightAmount')
        acc = {p: (pair, p) for p in _ATTR_PINS}
        acc_weight = (pair, 'Weight')
        by += 400

    # --- slope rock -------------------------------------------------------
    # Slope rock takes over from whatever is painted past an angle, so it needs a layer to take
    # over with. A layer named Rock is the intent; the last one is the fallback, because the
    # alternative is a recipe that cannot be generated at all.
    rock_layer = next((layer for layer, _s, _p in LAYERS if layer.lower() == 'rock'), LAYERS[-1][0])
    if rock_layer != 'Rock':
        _log('no layer named Rock - slope rock will use %s' % rock_layer)
    rock_call, _ = blocks[rock_layer]
    slope = _fn_call(mat, FN_ROOT + '/MF_MobSlopeRock', -600, -400)
    for pin in _ATTR_PINS:
        src, out = acc[pin]
        link(src, out, slope, pin)
        link(rock_call, pin, slope, pin + 'Rock')
    link(wnormal, '', slope, 'WorldNormal')
    link(macro_tex, 'R', slope, 'MacroNoise')
    for name, default in (('SlopeStart', 32.0), ('SlopeEnd', 55.0),
                          ('HeightContrast', 0.5), ('HeightAmount', 0.5), ('NoiseAmount', 0.15)):
        link(_param_scalar(mat, 'SlopeRock_' + name, default, 'Slope Rock', -900, -400 + 50 * len(name)),
             '', slope, name)
    acc = {p: (slope, p) for p in _ATTR_PINS}

    # --- moss -------------------------------------------------------------
    moss = _fn_call(mat, FN_ROOT + '/MF_MobTerrainMoss', -300, -400)
    for pin in _ATTR_PINS:
        src, out = acc[pin]
        link(src, out, moss, pin)
        link(moss_call, pin, moss, pin + 'Moss')
    link(wnormal, '', moss, 'WorldNormal')
    link(sun, '', moss, 'SunDirection')
    link(macro_tex, 'R', moss, 'MacroNoise')
    link(moss_paint, '', moss, 'PaintWeight')
    moss_defaults = [('Amount', 0.0), ('SlopeMin', 20.0), ('SlopeMax', 60.0), ('SlopeFavour', 1.0),
                     ('SlopeAmount', 1.0), ('CavityBias', 0.0), ('CavityContrast', 1.5),
                     ('CavityAmount', 1.0), ('ShadeSoftness', 0.5), ('ShadeAmount', 1.0),
                     ('NoiseBias', 0.5), ('NoiseContrast', 2.0), ('NoiseAmount', 1.0),
                     ('HeightContrast', 0.4), ('HeightAmount', 0.4)]
    my = -400
    for name, default in moss_defaults:
        link(_param_scalar(mat, 'Moss_' + name, default, 'Moss', -600, my), '', moss, name)
        my += 50
    acc = {p: (moss, p) for p in _ATTR_PINS}

    # --- wetness ----------------------------------------------------------
    wet = _fn_call(mat, FN_ROOT + '/MF_MobTerrainWetness', 0, -400)
    for pin in _ATTR_PINS:
        src, out = acc[pin]
        link(src, out, wet, pin)
    link(wet_paint, '', wet, 'PaintWeight')
    wet_defaults = [('Amount', 0.0), ('Darkening', 0.55), ('RoughnessTarget', 0.25),
                    ('NormalFlatten', 0.4), ('CavityBias', 0.0), ('CavityContrast', 2.0),
                    ('CavityAmount', 1.0), ('PuddleDepth', 0.15), ('PuddleRoughness', 0.05)]
    wy = -400
    for name, default in wet_defaults:
        link(_param_scalar(mat, 'Wetness_' + name, default, 'Wetness', -300, wy), '', wet, name)
        wy += 50

    # --- global grade -----------------------------------------------------
    specular = _param_scalar(mat, 'Specular', 0.35, 'Global', 100, -500)

    grade = custom(mat, _CODE_GLOBAL_GRADE, CMOT.CMOT_FLOAT3,
                   ['BaseColor', 'Normal', 'Roughness', 'Cavity', 'Specular', 'DistanceFade',
                    'GlobalHue', 'GlobalSaturation', 'GlobalValue', 'GlobalTint',
                    'GlobalNormalFlatten', 'GlobalRoughnessMin',
                    'CavityColorAmount', 'CavitySpecularAmount',
                    'DistanceDesaturation', 'DistanceColor', 'DistanceColorAmount'],
                   [('OutNormal', CMOT.CMOT_FLOAT3), ('OutRoughness', CMOT.CMOT_FLOAT1),
                    ('OutSpecular', CMOT.CMOT_FLOAT1)],
                   400, -400, 'Global grade')
    link(wet, 'BaseColor', grade, 'BaseColor')
    link(wet, 'Normal', grade, 'Normal')
    link(wet, 'Roughness', grade, 'Roughness')
    link(acc['Cavity'][0], acc['Cavity'][1], grade, 'Cavity')
    link(specular, '', grade, 'Specular')
    link(fade, '', grade, 'DistanceFade')
    gy = -400
    for name, default in (('GlobalHue', 0.0), ('GlobalSaturation', 1.0), ('GlobalValue', 1.0),
                          ('GlobalNormalFlatten', 0.0), ('GlobalRoughnessMin', 0.0),
                          ('CavityColorAmount', 0.5), ('CavitySpecularAmount', 0.5),
                          ('DistanceDesaturation', 0.0), ('DistanceColorAmount', 0.0)):
        link(_param_scalar(mat, name, default, 'Global', 100, gy), '', grade, name)
        gy += 50
    link(_param_vector(mat, 'GlobalTint', (1.0, 1.0, 1.0), 'Global', 100, gy), '', grade, 'GlobalTint')
    link(_param_vector(mat, 'DistanceColor', (0.55, 0.62, 0.72), 'Global', 100, gy + 50),
         '', grade, 'DistanceColor')

    # --- runtime virtual texture -----------------------------------------
    # Only wired when the project asks for it. The RVT, the physical materials and the grass types
    # are all game assets, so referencing them unconditionally would leave the shipped material
    # pointing at packages that do not exist outside the project it was authored in.
    MP = unreal.MaterialProperty
    if not BUILD_PROJECT_OUTPUTS:
        MEL.connect_material_property(grade, '', MP.MP_BASE_COLOR)
        MEL.connect_material_property(grade, 'OutNormal', MP.MP_NORMAL)
        MEL.connect_material_property(grade, 'OutRoughness', MP.MP_ROUGHNESS)
        MEL.connect_material_property(grade, 'OutSpecular', MP.MP_SPECULAR)

        _spread(MEL.get_material_expressions(mat))
        errors = MEL.recompile_material(mat)
        save(mat)
        for e in errors:
            _log('COMPILE ERROR: ' + str(e))
        _log('built M_%s, %d node(s), %d error(s)'
             % (MASTER_NAME, MEL.get_num_material_expressions(mat), len(errors)))
        return mat, errors

    terrain_rvt, height_rvt = build_rvt_assets()

    rvt_out = _expr(mat, unreal.MaterialExpressionRuntimeVirtualTextureOutput, 1400, 200)
    link(grade, '', rvt_out, 'BaseColor')
    link(grade, 'OutNormal', rvt_out, 'Normal')
    link(grade, 'OutRoughness', rvt_out, 'Roughness')
    link(grade, 'OutSpecular', rvt_out, 'Specular')
    link(worldz, '', rvt_out, 'WorldHeight')

    rvt_sample = _expr(mat, unreal.MaterialExpressionRuntimeVirtualTextureSample, 900, -900)
    rvt_sample.set_editor_property('virtual_texture', terrain_rvt)
    rvt_sample.set_editor_property('material_type', unreal.RuntimeVirtualTextureMaterialType.BASE_COLOR_NORMAL_ROUGHNESS)

    use_rvt = _param_static_bool(mat, 'bUseRVT', True, 'Global', 900, -1100)

    def rvt_switch(rvt_out_name, blend_src, blend_out, x_pos, y_pos):
        sw = _expr(mat, unreal.MaterialExpressionStaticSwitchParameter, x_pos, y_pos)
        sw.set_editor_property('parameter_name', 'bUseRVT')
        sw.set_editor_property('default_value', True)
        sw.set_editor_property('group', 'Global')
        link(rvt_sample, rvt_out_name, sw, 'True')
        link(blend_src, blend_out, sw, 'False')
        return sw

    sw_color = rvt_switch('BaseColor', grade, '', 1200, -700)
    sw_normal = rvt_switch('Normal', grade, 'OutNormal', 1200, -550)
    sw_rough = rvt_switch('Roughness', grade, 'OutRoughness', 1200, -400)

    if INCLUDE_DEBUG:
        sw_color, sw_normal = _landscape_debug(mat, blocks, acc, sw_color, sw_normal, wet)

    MEL.connect_material_property(sw_color, '', MP.MP_BASE_COLOR)
    MEL.connect_material_property(sw_normal, '', MP.MP_NORMAL)
    MEL.connect_material_property(sw_rough, '', MP.MP_ROUGHNESS)
    MEL.connect_material_property(grade, 'OutSpecular', MP.MP_SPECULAR)

    # --- physical material output ----------------------------------------
    build_physical_materials()

    phys_pins = [(layer + 'W', blocks[layer][1], 'Weight') for layer, _s, _p in LAYERS]
    phys_pins += [('SlopeMask', slope, 'Mask'), ('MossMask', moss, 'Mask'), ('WetMask', wet, 'Mask')]

    phys = custom(mat, _CODE_PHYSMAT, CMOT.CMOT_FLOAT1, [p[0] for p in phys_pins],
                  [('OutSoil', CMOT.CMOT_FLOAT1), ('OutGravel', CMOT.CMOT_FLOAT1),
                   ('OutStone', CMOT.CMOT_FLOAT1), ('OutForest', CMOT.CMOT_FLOAT1),
                   ('OutSand', CMOT.CMOT_FLOAT1), ('OutMud', CMOT.CMOT_FLOAT1)],
                  700, 600, 'Footstep surfaces')
    for pin, src, out in phys_pins:
        link(src, out, phys, pin)

    phys_out = _expr(mat, unreal.MaterialExpressionLandscapePhysicalMaterialOutput, 1200, 600)
    phys_map = [('PhysMat_Grass', ''), ('PhysMat_Ground', 'OutSoil'), ('PhysMat_Gravel', 'OutGravel'),
                ('PhysMat_Stone', 'OutStone'), ('PhysMat_ForestFloor', 'OutForest'),
                ('PhysMat_Sand', 'OutSand'), ('PhysMat_Mud', 'OutMud')]
    entries = []
    for pm_name, _out in phys_map:
        entry = unreal.PhysicalMaterialInput()
        entry.set_editor_property('physical_material', unreal.load_asset(PHYSMAT_DIR + '/' + pm_name))
        entries.append(entry)
    phys_out.set_editor_property('inputs', entries)
    # Input pins are named after the physical material asset, so this has to run after the array
    # is assigned.
    for pm_name, out in phys_map:
        link(phys, out, phys_out, pm_name)

    # --- grass output -----------------------------------------------------
    grass_types = build_grass_types()
    if grass_types:
        grass_out = _expr(mat, unreal.MaterialExpressionLandscapeGrassOutput, 1200, 1000)
        grass_entries = []
        for layer in grass_types:
            entry = unreal.GrassInput()
            entry.set_editor_property('name', layer)
            entry.set_editor_property('grass_type', grass_types[layer])
            grass_entries.append(entry)
        grass_out.set_editor_property('grass_types', grass_entries)
        # Pin names come from the Name field, so the array has to be set first.
        for layer in grass_types:
            link(blocks[layer][1], 'Weight', grass_out, layer)

    _spread(MEL.get_material_expressions(mat))
    errors = MEL.recompile_material(mat)
    save(mat)
    for e in errors:
        _log('COMPILE ERROR: ' + str(e))
    _log('built M_%s, %d node(s), %d error(s)'
         % (MASTER_NAME, MEL.get_num_material_expressions(mat), len(errors)))
    return mat, errors


def _find_param(mat, name):
    for e in MEL.get_material_expressions(mat):
        if isinstance(e, unreal.MaterialExpressionScalarParameter):
            if str(e.get_editor_property('parameter_name')) == name:
                return e
    raise KeyError(name)


# ---------------------------------------------------------------------------
# PROJECT INTEGRATION: physical materials and layer infos
# ---------------------------------------------------------------------------
#
# Footstep surfaces and paint layer infos belong to the game. Set these to the project's own
# folders before running the phases below.

PHYSMAT_DIR = '/Game/VFX/Footsteps'
LAYERINFO_DIR = '/Game/Environment/Landscape/LayerInfo'

# Created if absent. Surface types come from DefaultEngine.ini's PhysicalSurfaces, so these names
# have to match what the project registered there.
NEW_PHYSMATS = [
    ('PhysMat_Stone', 'SURFACE_TYPE8'),
    ('PhysMat_ForestFloor', 'SURFACE_TYPE3'),
    ('PhysMat_Mud', 'SURFACE_TYPE9'),
]

# Overlay layers paint on top of the weight-blended set, so they must not take weight from it.
OVERLAY_LAYERS = ['Moss', 'Wetness']


def build_physical_materials():
    created = []
    for name, surface in NEW_PHYSMATS:
        path = PHYSMAT_DIR + '/' + name
        if EAL.does_asset_exist(path):
            pm = unreal.load_asset(path)
        else:
            pm = _tools().create_asset(name, PHYSMAT_DIR, unreal.PhysicalMaterial,
                                       unreal.PhysicalMaterialFactoryNew())
        pm.set_editor_property('surface_type', getattr(unreal.PhysicalSurface, surface))
        save(pm)
        created.append(pm)
    _log('physical materials ready')
    return created


def configure_layer_infos():
    """Configures the LandscapeLayerInfoObject assets the landscape editor created.

    The assets themselves cannot be authored here: ULandscapeLayerInfoObject::LayerName is
    VisibleAnywhere, so Python refuses to set it, and the only function that does
    (UE::Landscape::CreateTargetLayerInfo) is not exposed. Create them once from Landscape mode
    with the + next to each target layer, then run this to set blend method and physical
    material. Reports any layer it could not find.
    """
    BM = unreal.LandscapeTargetLayerBlendMethod
    found, missing = {}, []
    wanted = ([(layer, physmat, BM.FINAL_WEIGHT_BLENDING) for layer, _s, physmat in LAYERS]
              + [(layer, None, BM.NONE) for layer in OVERLAY_LAYERS])

    existing = _layer_info_assets()
    for layer, physmat_name, blend in wanted:
        info = existing.get(layer)
        if info is None:
            missing.append(layer)
            continue
        info.set_editor_property('blend_method', blend)
        if physmat_name:
            pm_path = PHYSMAT_DIR + '/' + physmat_name
            if EAL.does_asset_exist(pm_path):
                info.set_editor_property('phys_material', unreal.load_asset(pm_path))
        # Left dirty on purpose: saving a source-controlled asset raises a modal checkout prompt
        # that blocks the game thread, and this runs over a remote python channel.
        found[layer] = info

    _log('configured %d layer info(s)' % len(found))
    if missing:
        _log('MISSING layer infos, create them from Landscape mode: ' + ', '.join(missing))
    return found, missing


def _layer_info_assets():
    """Every layer info in the project, keyed by the layer name recorded on the asset."""
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    f = unreal.ARFilter(
        class_paths=[unreal.TopLevelAssetPath('/Script/Landscape', 'LandscapeLayerInfoObject')],
        recursive_classes=True)
    out = {}
    for asset in registry.get_assets(f):
        info = unreal.load_asset(str(asset.package_name))
        out[str(info.get_editor_property('layer_name'))] = info
    return out


# How the paint layers and procedural masks resolve to a footstep surface. Weights are summed
# per physical material and the engine takes the dominant one.
_CODE_PHYSMAT = """
float Grass = GrassW + DryGrassW;
float Soil = DirtW + PackedDirtW + GardenSoilW;
float Gravel = GravelW + RakedGravelW;
float Stone = StonePavingW + RockW;
float Forest = ForestFloorW;
float Sand = SandSiltW;

// Slope rock and moss are never painted, so without this they would inherit whatever layer
// happens to be underneath and a cliff face would sound like grass.
Stone = max(Stone, SlopeMask);
Grass = max(Grass, MossMask);

// Wet soil is mud. This is why Mud needs no paint layer of its own.
float Mud = Soil * WetMask;
Soil = Soil * (1.0f - WetMask);

OutSoil = saturate(Soil);
OutGravel = saturate(Gravel);
OutStone = saturate(Stone);
OutForest = saturate(Forest);
OutSand = saturate(Sand);
OutMud = saturate(Mud);
return saturate(Grass);
"""


# ---------------------------------------------------------------------------
# PROJECT INTEGRATION: landscape grass
# ---------------------------------------------------------------------------
#
# Grass types name meshes and materials the game owns, so this table is meant to be edited.

GRASS_DIR = '/Game/Environment/Foliage'

# The project ships FoliageType assets for these meshes but no LandscapeGrassType, which is what
# the grass output needs. Same meshes, driven by paint weight instead of hand placement.
GRASS_TYPES = [
    ('LGT_MobGrass', 'Grass', [
        ('/Game/Environment/Meshes/ST_Grass_Short', '/Game/Environment/Materials/MI_Grass_Short', 400.0),
        ('/Game/Environment/Meshes/ST_Grass_Mid', '/Game/Environment/Materials/MI_Grass_Mid', 250.0),
        ('/Game/Environment/Meshes/ST_Grass_Tall', '/Game/Environment/Materials/MI_Grass_Tall', 120.0),
    ]),
    ('LGT_MobDryGrass', 'DryGrass', [
        ('/Game/Environment/Meshes/ST_Grass_Short', '/Game/Environment/Materials/MI_Grass_Short', 260.0),
        ('/Game/Environment/Meshes/ST_Grass_Mid', '/Game/Environment/Materials/MI_Grass_Mid', 140.0),
    ]),
    ('LGT_MobUndergrowth', 'ForestFloor', [
        ('/Game/Environment/Meshes/ST_Grass_Short', '/Game/Environment/Materials/MI_Grass_Short', 320.0),
    ]),
]


def build_grass_types():
    built = {}
    for asset_name, layer, varieties in GRASS_TYPES:
        path = GRASS_DIR + '/' + asset_name
        if EAL.does_asset_exist(path):
            gt = unreal.load_asset(path)
        else:
            gt = _tools().create_asset(asset_name, GRASS_DIR, unreal.LandscapeGrassType,
                                       unreal.LandscapeGrassTypeFactory())
        entries = []
        for mesh_path, material_path, density in varieties:
            if not EAL.does_asset_exist(mesh_path):
                _log('missing grass mesh, skipped: ' + mesh_path)
                continue
            v = unreal.GrassVariety()
            v.set_editor_property('grass_mesh', unreal.load_asset(mesh_path))
            if EAL.does_asset_exist(material_path):
                v.set_editor_property('override_materials', [unreal.load_asset(material_path)])
            v.set_editor_property('grass_density', unreal.PerPlatformFloat(density))
            v.set_editor_property('start_cull_distance', unreal.PerPlatformInt(2000))
            v.set_editor_property('end_cull_distance', unreal.PerPlatformInt(4000))
            v.set_editor_property('random_rotation', True)
            v.set_editor_property('align_to_surface', True)
            v.set_editor_property('cast_dynamic_shadow', False)
            entries.append(v)
        gt.set_editor_property('grass_varieties', entries)
        save(gt)
        built[layer] = gt
    _log('built %d landscape grass type(s)' % len(built))
    return built


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


def build_material_instances():
    """The instance a landscape uses, plus a variant for targets without virtual texturing.

    Where r.VirtualTextures is off the RVT sample returns nothing, so bUseRVT off routes the base
    pass through the layer blend directly instead. Trim the layer set on that variant per map: the
    static switches compile unused layers out entirely. bUseRVT only exists when
    BUILD_PROJECT_OUTPUTS is on.
    """
    master = unreal.load_asset(ROOT + '/M_' + MASTER_NAME)

    base = _get_or_create_instance('MI_' + MASTER_NAME, master)
    if BUILD_PROJECT_OUTPUTS:
        MEL.set_material_instance_static_switch_parameter_value(base, 'bUseRVT', True)
    save(base)

    switch = _get_or_create_instance('MI_' + MASTER_NAME + '_Switch', master)
    if BUILD_PROJECT_OUTPUTS:
        MEL.set_material_instance_static_switch_parameter_value(switch, 'bUseRVT', False)
    for layer, _s, _p in LAYERS:
        MEL.set_material_instance_static_switch_parameter_value(switch, layer + '_HexTiling', False)
        MEL.set_material_instance_static_switch_parameter_value(switch, layer + '_DualScale', False)
    save(switch)

    _log('built material instances')
    return base, switch


# ---------------------------------------------------------------------------
# Level wiring
# ---------------------------------------------------------------------------

def wire_landscape_rvt(expand=0.0):
    """Points the open level's landscape at both RVTs and fits a volume to each.

    RuntimeVirtualTextureComponent exposes only VirtualTexture to Python - BoundsAlignActor and
    the Set Bounds button are editor-only C++ - so the volume transform is built the same way
    RuntimeVirtualTexture::SetBounds does it: the component's local box is the unit cube, so
    location is the bounds minimum and scale is the bounds size.
    """
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = editor.get_editor_world()

    landscapes = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Landscape)
    if not landscapes:
        raise RuntimeError('no Landscape actor in %s' % world.get_name())
    landscape = landscapes[0]

    terrain = unreal.load_asset(RVT_TERRAIN)
    height = unreal.load_asset(RVT_HEIGHT)
    rvts = [terrain, height]

    proxies = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LandscapeStreamingProxy)
    for proxy in [landscape] + list(proxies):
        proxy.set_editor_property('runtime_virtual_textures', rvts)
        proxy.set_editor_property('virtual_texture_render_pass_type',
                                  unreal.RuntimeVirtualTextureMainPassType.EXCLUSIVE
                                  if False else unreal.RuntimeVirtualTextureMainPassType.ALWAYS)

    # Union the landscape and every proxy, since a World Partition landscape carries its
    # geometry on the proxies rather than the parent actor.
    box_min = None
    box_max = None
    for actor in [landscape] + list(proxies):
        origin, extent = actor.get_actor_bounds(False)
        lo = unreal.Vector(origin.x - extent.x, origin.y - extent.y, origin.z - extent.z)
        hi = unreal.Vector(origin.x + extent.x, origin.y + extent.y, origin.z + extent.z)
        if box_min is None:
            box_min, box_max = lo, hi
        else:
            box_min = unreal.Vector(min(box_min.x, lo.x), min(box_min.y, lo.y), min(box_min.z, lo.z))
            box_max = unreal.Vector(max(box_max.x, hi.x), max(box_max.y, hi.y), max(box_max.z, hi.z))

    if expand > 0.0:
        box_min = unreal.Vector(box_min.x - expand, box_min.y - expand, box_min.z - expand)
        box_max = unreal.Vector(box_max.x + expand, box_max.y + expand, box_max.z + expand)

    size = unreal.Vector(max(box_max.x - box_min.x, 1.0),
                         max(box_max.y - box_min.y, 1.0),
                         max(box_max.z - box_min.z, 1.0))

    existing = {}
    for vol in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RuntimeVirtualTextureVolume):
        comp = vol.get_editor_property('virtual_texture_component')
        existing[comp.get_editor_property('virtual_texture')] = vol

    made = []
    for rvt in rvts:
        vol = existing.get(rvt)
        if vol is None:
            vol = actors.spawn_actor_from_class(unreal.RuntimeVirtualTextureVolume,
                                                box_min, unreal.Rotator(0.0, 0.0, 0.0))
            vol.get_editor_property('virtual_texture_component').set_editor_property('virtual_texture', rvt)
        vol.set_actor_label('RVTVolume_' + rvt.get_name())
        vol.set_actor_location(box_min, False, False)
        vol.set_actor_scale3d(size)
        made.append(vol)

    _log('RVT wired: bounds min %s size %s' % (box_min, size))
    return landscape, made


# ---------------------------------------------------------------------------
# Mesh blending
# ---------------------------------------------------------------------------

_CODE_RVT_BLEND = """
// Height above the terrain surface, so a mesh sunk into the ground reads 1 at its base and
// falls off to 0 once it clears BlendDistance.
float Above = WorldZ - TerrainHeight - BlendOffset;
float A = 1.0f - smoothstep(0.0f, max(BlendDistance, MOB_EPS), Above);
A = pow(saturate(A), max(BlendSharpness, MOB_EPS)) * saturate(Amount);
return A;
"""


def build_rvt_blend_function():
    """MF_MobRVTBlend: fades a mesh's surface into the terrain it is standing in.

    Reads the same two runtime virtual textures the landscape writes, so the rock meeting the
    ground shows the ground's own blended layers rather than a hard silhouette. Costs the mesh
    two virtual texture samples, so it sits behind a static switch on the mesh material.
    """
    fn = get_or_create_function(
        'MF_MobRVTBlend',
        'Blends a mesh into the landscape near its base using the terrain runtime virtual '
        'textures. Enable per material instance; it does nothing where r.VirtualTextures is off.')

    base = _attr_inputs(fn, '', -1200, 0)
    amount = fn_input(fn, 'Amount', FIT.FUNCTION_INPUT_SCALAR, -1200, 300, 20, default=1.0)
    distance = fn_input(fn, 'BlendDistance', FIT.FUNCTION_INPUT_SCALAR, -1200, 360, 21, default=60.0,
                        description='World units above the terrain over which the mesh takes over')
    offset = fn_input(fn, 'BlendOffset', FIT.FUNCTION_INPUT_SCALAR, -1200, 420, 22, default=0.0)
    sharpness = fn_input(fn, 'BlendSharpness', FIT.FUNCTION_INPUT_SCALAR, -1200, 480, 23, default=1.0)

    terrain = _fnexpr(
        fn, unreal.MaterialExpressionRuntimeVirtualTextureSample, -800, -300)
    terrain.set_editor_property('virtual_texture', unreal.load_asset(RVT_TERRAIN))
    terrain.set_editor_property('material_type',
                                unreal.RuntimeVirtualTextureMaterialType.BASE_COLOR_NORMAL_ROUGHNESS)

    height = _fnexpr(
        fn, unreal.MaterialExpressionRuntimeVirtualTextureSample, -800, 0)
    height.set_editor_property('virtual_texture', unreal.load_asset(RVT_HEIGHT))
    height.set_editor_property('material_type', unreal.RuntimeVirtualTextureMaterialType.WORLD_HEIGHT)

    wpos = _fnexpr(fn, unreal.MaterialExpressionWorldPosition, -1200, 560)
    worldz = _fnexpr(fn, unreal.MaterialExpressionComponentMask, -1000, 560)
    worldz.set_editor_property('r', False)
    worldz.set_editor_property('g', False)
    worldz.set_editor_property('b', True)
    worldz.set_editor_property('a', False)
    link(wpos, '', worldz, '')

    alpha = custom(fn, _CODE_RVT_BLEND, CMOT.CMOT_FLOAT1,
                   ['WorldZ', 'TerrainHeight', 'BlendDistance', 'BlendOffset', 'BlendSharpness', 'Amount'],
                   [], -500, 300, 'Terrain blend alpha')
    link(worldz, '', alpha, 'WorldZ')
    link(height, 'WorldHeight', alpha, 'TerrainHeight')
    link(distance, '', alpha, 'BlendDistance')
    link(offset, '', alpha, 'BlendOffset')
    link(sharpness, '', alpha, 'BlendSharpness')
    link(amount, '', alpha, 'Amount')

    # The terrain side of the lerp comes from the virtual texture rather than a second attribute
    # set, so it always matches whatever the landscape actually renders.
    pairs = (('BaseColor', 'BaseColor'), ('Normal', 'Normal'), ('Roughness', 'Roughness'))
    y = -300
    for attr, rvt_output in pairs:
        mix = _fnexpr(
            fn, unreal.MaterialExpressionLinearInterpolate, -200, y)
        link(base[attr], '', mix, 'A')
        link(terrain, rvt_output, mix, 'B')
        link(alpha, '', mix, 'Alpha')
        src = mix
        if attr == 'Normal':
            src = _fnexpr(fn, unreal.MaterialExpressionNormalize, 0, y)
            link(mix, '', src, '')
        link(src, '', fn_output(fn, attr, 300, y, len(pairs)), '')
        y += 150

    link(base['Cavity'], '', fn_output(fn, 'Cavity', 300, y, 3), '')
    link(alpha, '', fn_output(fn, 'Alpha', 300, y + 150, 4), '')

    _finish_fn(fn)
    save(fn)
    _log('built MF_MobRVTBlend')
    return fn


# ---------------------------------------------------------------------------
# Paint test
# ---------------------------------------------------------------------------

def _paint_blob_material():
    """A radial gradient in UI domain, the only domain DrawMaterialToRenderTarget accepts."""
    mat = get_or_create_material(ROOT + '/Tests', 'M_MobPaintBlob')
    mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI)

    uv = _expr(mat, unreal.MaterialExpressionTextureCoordinate, -600, 0)
    node = custom(mat, 'return 1.0f - smoothstep(Inner, Outer, length(UV - 0.5f));',
                  CMOT.CMOT_FLOAT1, ['UV', 'Inner', 'Outer'], [], -300, 0, 'radial blob')
    link(uv, '', node, 'UV')
    inner = _expr(mat, unreal.MaterialExpressionConstant, -600, 100)
    inner.set_editor_property('r', 0.12)
    outer = _expr(mat, unreal.MaterialExpressionConstant, -600, 160)
    outer.set_editor_property('r', 0.30)
    link(inner, '', node, 'Inner')
    link(outer, '', node, 'Outer')
    MEL.connect_material_property(node, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    save(mat)
    return mat


def paint_test_blob(layer='Gravel', size=1024):
    """Paints a soft circular patch of one layer, to exercise the blend for real.

    Goes through the landscape's weightmap import rather than the paint tool, which is the only
    route open to a script.
    """
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    landscape = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Landscape)[0]

    blob = _paint_blob_material()
    rt = _tools().create_asset('RT_MobPaintTest', ROOT + '/Tests', unreal.TextureRenderTarget2D,
                               unreal.TextureRenderTargetFactoryNew()) \
        if not EAL.does_asset_exist(ROOT + '/Tests/RT_MobPaintTest') \
        else unreal.load_asset(ROOT + '/Tests/RT_MobPaintTest')
    rt.set_editor_property('size_x', size)
    rt.set_editor_property('size_y', size)
    rt.set_editor_property('render_target_format', unreal.TextureRenderTargetFormat.RTF_RGBA8)

    unreal.RenderingLibrary.clear_render_target2d(world, rt, unreal.LinearColor(0, 0, 0, 1))
    unreal.RenderingLibrary.draw_material_to_render_target(world, rt, blob)

    ok = landscape.landscape_import_weightmap_from_render_target(rt, layer)
    _log('painted %s from render target: %s' % (layer, ok))
    return ok


# ---------------------------------------------------------------------------
# Test material
# ---------------------------------------------------------------------------

def build_layer_test_material(hex_tiling=True, dual_scale=False):
    """A single-layer material for eyeballing MF_MobLandscapeLayer on a plane.

    Compiling this is also the cheapest proof that the .ush chain resolves and that the layer
    stays inside the sampler budget.
    """
    mat = get_or_create_material(ROOT + '/Tests', 'M_MobLayerTest')
    def me(cls, x, y):
        return _expr(mat, cls, x, y)

    tex_params = {}
    y_tex = -400
    for pin, default_path in (('BC', BASE_TEX_BC), ('NRM', BASE_TEX_NRM), ('HRC', BASE_TEX_HRC)):
        if TEXTURE_ARRAYS:
            default = unreal.load_asset('%s/TA_%s_%s' % (ROOT, MASTER_NAME, pin))
            if default is None:
                _log('no TA_%s_%s to test against - pack the arrays first' % (MASTER_NAME, pin))
                return None, []
        else:
            default = unreal.load_asset(default_path)
        t = me(unreal.MaterialExpressionTextureObjectParameter, -900, y_tex)
        t.set_editor_property('parameter_name', pin)
        t.set_editor_property('texture', default)
        tex_params[pin] = t
        y_tex += 150

    uv = me(unreal.MaterialExpressionTextureCoordinate, -900, 100)
    sb_hex = me(unreal.MaterialExpressionStaticBool, -900, 250)
    sb_hex.set_editor_property('value', hex_tiling)
    sb_dual = me(unreal.MaterialExpressionStaticBool, -900, 350)
    sb_dual.set_editor_property('value', dual_scale)

    call = me(unreal.MaterialExpressionMaterialFunctionCall, -400, 0)
    call.set_editor_property('material_function', unreal.load_asset(FN_ROOT + '/MF_MobLandscapeLayer'))

    for src, pin in ((tex_params['BC'], 'BC'), (tex_params['NRM'], 'NRM'),
                     (tex_params['HRC'], 'HRC'), (uv, 'UV'),
                     (sb_hex, 'bHexTiling'), (sb_dual, 'bDualScale')):
        link(src, '', call, pin)

    MP = unreal.MaterialProperty
    MEL.connect_material_property(call, 'BaseColor', MP.MP_BASE_COLOR)
    MEL.connect_material_property(call, 'Normal', MP.MP_NORMAL)
    MEL.connect_material_property(call, 'Roughness', MP.MP_ROUGHNESS)

    _spread(MEL.get_material_expressions(mat))
    errors = MEL.recompile_material(mat)
    save(mat)
    for e in errors:
        _log('COMPILE ERROR: ' + str(e))
    _log('built M_MobLayerTest, %d error(s)' % len(errors))
    return mat, errors


# ---------------------------------------------------------------------------
# Entry points
# ---------------------------------------------------------------------------

def build_functions():
    build_layer_function()
    build_height_blend_function()
    build_layer_weight_function()
    build_slope_rock_function()
    build_moss_function()
    build_wetness_function()

    # The mesh-blend function samples the terrain's runtime virtual textures, so it can only exist
    # once those do.
    if BUILD_PROJECT_OUTPUTS:
        build_rvt_blend_function()
    _log('material functions done')


def build_all(recipe=None):
    """Everything a recipe can author without touching a level.

    Pass a recipe asset or its path; the Mob toolbar menu passes the one it was run from. Without
    one the module's own defaults stand, so this still runs from a bare Python console.
    """
    import mob_recipe
    import importlib as _il
    _il.reload(mob_recipe)
    me = sys.modules[__name__]
    recipe = mob_recipe.load(recipe)
    mob_recipe.apply_landscape(me, recipe)

    if BUILD_PROJECT_OUTPUTS:
        build_rvt_assets()

    build_functions()

    # Siblings first, so the module is left describing the recipe that was asked for.
    for other in (mob_recipe.siblings(recipe, mob_recipe.LANDSCAPE) if recipe else []):
        _log('rebuilding %s, which shares these functions' % other.get_name())
        mob_recipe.apply_landscape(me, other)
        build_master_material()
        build_material_instances()

    if recipe:
        mob_recipe.apply_landscape(me, recipe)
    mat, errors = build_master_material()
    build_material_instances()
    if errors:
        _log('%d compile error(s) in the master material' % len(errors))
    return errors
