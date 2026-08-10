"""Checks a generated master against the contract its documentation states.

Not a diff of the built asset against a golden copy: a binary diff of a material tells you
something changed without telling you whether it matters. These are the claims the README and the
reference docs actually make - what a feature costs, what it leaves alone - asserted one at a time
so a failure names the claim that stopped being true.

Run from the Mob toolbar menu, or:

    import mob_verify
    mob_verify.run('/Game/Path/MR_MySurface')

Every number here is the Material Editor's own estimate rather than a count of taps in the
compiled shader. The estimate over-reports - it counts the Texture2DSample overload declarations
and the engine's BRDF lookup - so these are baselines to hold steady, not ground truth. When one
moves, dump the shader and count before believing the stat.
"""

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

SCRATCH = '/Game/__MobVerify'

# switch -> what turning it on, alone, should cost. None means "not asserted, just recorded".
SURFACE_EXPECT = [
    ('everything off', [], dict(tex=3, samplers=4)),
    ('one extra layer', ['bLayer1'], dict(tex=6, samplers=4)),
    ('two extra layers', ['bLayer1', 'bLayer2'], dict(tex=9, samplers=4)),
    ('layer 0 triplanar', ['Layer0_Triplanar'], dict(samplers=4)),
    ('layer 0 tiling break', ['Layer0_TileBreak'], dict(samplers=4)),
    ('parallax offset', ['Layer0_Parallax'], dict(tex=4, samplers=4)),
    ('parallax occlusion', ['Layer0_Parallax', 'Layer0_ParallaxOcclusion'], dict(samplers=5)),
    ('detail', ['bDetail'], dict(tex=4, samplers=4)),
    ('wetness', ['bWetness'], dict(tex=3, samplers=4)),
    ('per-instance data', ['bPrimitiveData'], dict(tex=3, samplers=4)),
    ('debug', ['bDebug'], dict(samplers=4)),
    ('emissive', ['bEmissive'], dict(tex=4, samplers=4)),
    # Accumulation reuses the cavity and normal the material already has, so no extra taps.
    ('accumulation', ['bAccumulation'], dict(tex=3, samplers=4)),
    ('rain ripples', ['bWetness', 'bRipples'], dict(tex=5, samplers=4)),
]


def _log(msg):
    unreal.log('[MobVerify] ' + str(msg))


def _switch_names(mat):
    return [str(n) for n in MEL.get_static_switch_parameter_names(mat)]


def _probe(mat, name, on):
    """A scratch instance with exactly the named switches on, and its statistics."""
    path = SCRATCH + '/' + name
    mi = (unreal.load_asset(path) if EAL.does_asset_exist(path)
          else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
              name, SCRATCH, unreal.MaterialInstanceConstant,
              unreal.MaterialInstanceConstantFactoryNew()))
    MEL.set_material_instance_parent(mi, mat)
    for switch in _switch_names(mat):
        MEL.set_material_instance_static_switch_parameter_value(mi, switch, switch in on)
    MEL.update_material_instance(mi)
    s = MEL.get_statistics(mi)
    return {
        'ps': s.get_editor_property('num_pixel_shader_instructions'),
        'samplers': s.get_editor_property('num_samplers'),
        'tex': s.get_editor_property('num_pixel_texture_samples'),
    }


class _Result(object):
    def __init__(self):
        self.passed = 0
        self.failed = []

    def check(self, claim, ok, detail=''):
        if ok:
            self.passed += 1
            _log('  pass  %s%s' % (claim, (' - ' + detail) if detail else ''))
        else:
            self.failed.append(claim)
            _log('  FAIL  %s%s' % (claim, (' - ' + detail) if detail else ''))


def run(recipe):
    """Asserts the contract for whatever a recipe authored. Returns True when all claims hold."""
    import mob_recipe
    import importlib
    importlib.reload(mob_recipe)

    recipe = mob_recipe.load(recipe)
    if recipe is None:
        _log('no recipe')
        return False

    root = str(recipe.get_editor_property('output_path').get_editor_property('path')).rstrip('/')
    name = str(recipe.get_editor_property('asset_name'))
    kind = mob_recipe.kind(recipe)
    mat = unreal.load_asset('%s/M_%s' % (root, name))
    if mat is None:
        _log('M_%s not generated yet' % name)
        return False

    foliage = bool(recipe.get_editor_property('foliage')) if kind == mob_recipe.SURFACE else False
    _log('checking M_%s (%s)' % (name, 'foliage' if foliage else kind))
    r = _Result()

    # A master goes stale when a function it calls is rebuilt without it, and the errors land
    # nowhere near the function - so compiling is asserted before anything is measured.
    r.check('master compiles', len(MEL.recompile_material(mat)) == 0)

    # The AO pin has to stay free, or cavity doubles with the renderer's own occlusion. This is
    # the one claim that is about what the material does NOT do, and so the easiest to lose.
    ao = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION) \
        if hasattr(MEL, 'get_material_property_input_node') else None
    r.check('ambient occlusion pin left unwritten', ao is None, 'cavity multiplies instead')

    if kind == mob_recipe.LANDSCAPE:
        switches = _switch_names(mat)
        layers = sorted({n.rsplit('_', 1)[0] for n in switches if n.endswith('_HexTiling')})
        base = _probe(mat, 'L_off', set())
        r.check('sampler budget never approached', base['samplers'] <= 8,
                'all tiling off uses %s' % base['samplers'])

        if layers:
            first = layers[0]
            dual = _probe(mat, 'L_dual', {first + '_DualScale'})
            hexed = _probe(mat, 'L_hex', {first + '_HexTiling'})

            # Both tiers are extra taps on textures already bound, so neither may cost a sampler.
            r.check('dual scale costs taps, not samplers',
                    dual['tex'] > base['tex'] and dual['samplers'] == base['samplers'],
                    'tex %s -> %s' % (base['tex'], dual['tex']))
            r.check('hex tiling costs more than dual scale',
                    hexed['tex'] > dual['tex'] and hexed['samplers'] == base['samplers'],
                    'tex %s -> %s' % (dual['tex'], hexed['tex']))

            # Hex wins over dual where both are on, or a layer with both set would pay twice.
            both = _probe(mat, 'L_both', {first + '_DualScale', first + '_HexTiling'})
            r.check('hex tiling wins over dual scale', both['tex'] == hexed['tex'],
                    'tex=%s' % both['tex'])

    if kind == mob_recipe.SURFACE:
        switches = set(_switch_names(mat))
        baseline = None

        for claim, on, expect in SURFACE_EXPECT:
            if foliage:
                # A foliage master carries an opacity mask and no tiling break, so the surface tap
                # baselines do not describe it. Samplers still have to hold.
                expect = {'samplers': expect['samplers']} if 'samplers' in expect else {}
            missing = [s for s in on if s not in switches]
            if missing:
                _log('  skip  %s - not in this master' % claim)
                continue

            stats = _probe(mat, 'V_' + claim.replace(' ', '_').replace('-', '_'), set(on))
            if baseline is None and not on:
                baseline = stats

            detail = 'tex=%s samplers=%s ps=%s' % (stats['tex'], stats['samplers'], stats['ps'])
            ok = True
            for key, want in expect.items():
                if stats[key] != want:
                    ok = False
                    detail += ' (expected %s=%s)' % (key, want)
            r.check(claim, ok, detail)

        if baseline:
            r.check('16 sampler budget never approached', baseline['samplers'] <= 8,
                    'all off uses %s' % baseline['samplers'])

        # Per-instance data is a contract with whatever sets it, so the indices are not free to move.
        want = {'PrimitiveTint': 0, 'PrimitiveRoughness': 4, 'PrimitiveWetness': 5}
        found = {}
        for e in MEL.get_material_expressions(mat):
            if e.get_class().get_name() in ('MaterialExpressionScalarParameter',
                                            'MaterialExpressionVectorParameter'):
                if e.get_editor_property('use_custom_primitive_data'):
                    found[str(e.get_editor_property('parameter_name'))] = \
                        e.get_editor_property('primitive_data_index')
        if found:
            r.check('primitive data indices unchanged', found == want, str(found))

        if foliage:
            r.check('wind is on the foliage master', 'bWind' in switches)
            # An enum stringifies as "<BlendMode.BLEND_MASKED: 1>", so match on the name appearing.
            r.check('foliage is masked and two sided',
                    bool(mat.get_editor_property('two_sided'))
                    and 'MASKED' in str(mat.get_editor_property('blend_mode')).upper(),
                    str(mat.get_editor_property('shading_model')))

    _log('%d passed, %d failed' % (r.passed, len(r.failed)))
    for f in r.failed:
        _log('  failed: %s' % f)

    if EAL.does_directory_exist(SCRATCH):
        EAL.delete_directory(SCRATCH)

    return not r.failed
