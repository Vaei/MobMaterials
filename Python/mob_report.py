"""What a project is actually spending on these materials.

Two things that only show up once a project has been built out for a while, and both of which are
discovered at cook time if nobody looks earlier:

  permutations()     how many distinct shader maps the instances add up to
  texture_memory()   how much texture a master's instances drag in

Run from the Mob toolbar menu, or:

    import mob_report
    mob_report.permutations()
    mob_report.texture_memory()
"""

import unreal

MEL = unreal.MaterialEditingLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()


def _log(msg):
    unreal.log('[MobReport] ' + str(msg))


def _masters():
    """Every material generated from a recipe in this project."""
    found = []
    recipes = REG.get_assets_by_class(
        unreal.TopLevelAssetPath('/Script/MobMasterMaterial', 'MobMaterialRecipe'), False)
    for a in recipes:
        recipe = unreal.load_asset(str(a.package_name))
        root = str(recipe.get_editor_property('output_path').get_editor_property('path')).rstrip('/')
        name = str(recipe.get_editor_property('asset_name'))
        mat = unreal.load_asset('%s/M_%s' % (root, name))
        if mat is not None:
            found.append((str(a.asset_name), mat))
    return found


def _instances_of(mat):
    """Every material instance whose parent chain reaches this master."""
    out = []
    for a in REG.get_assets_by_class(
            unreal.TopLevelAssetPath('/Script/Engine', 'MaterialInstanceConstant'), False):
        mi = unreal.load_asset(str(a.package_name))
        parent = mi.get_editor_property('parent') if mi else None
        while parent is not None:
            if parent == mat:
                out.append(mi)
                break
            parent = parent.get_editor_property('parent') \
                if parent.get_class().get_name() == 'MaterialInstanceConstant' else None
    return out


def permutations():
    """Distinct static-switch combinations in use, per master.

    Every distinct combination is another shader map, and PSOs are per vertex factory times
    material - so this number multiplies by however many mesh types wear it. Instances that share
    a combination cost nothing extra, which is the whole reason for parenting to presets.
    """
    total = 0
    for label, mat in _masters():
        switches = [str(n) for n in MEL.get_static_switch_parameter_names(mat)]
        combos = {}
        for mi in _instances_of(mat):
            key = tuple(MEL.get_material_instance_static_switch_parameter_value(mi, s)
                        for s in switches)
            combos.setdefault(key, []).append(mi.get_name())

        _log('%s: %d instance(s), %d distinct permutation(s)'
             % (mat.get_name(), sum(len(v) for v in combos.values()), len(combos)))
        for key, users in sorted(combos.items(), key=lambda kv: -len(kv[1])):
            on = [s for s, v in zip(switches, key) if v]
            _log('   %-2d x  %s' % (len(users), ', '.join(on) if on else 'everything off'))
            if len(users) <= 3:
                _log('         %s' % ', '.join(sorted(users)))
        total += len(combos)

    _log('%d permutation(s) across every master' % total)
    return total


# Bytes per pixel by compression, near enough to budget with. Block compression is the whole
# reason these differ: a normal map is twice an albedo for the same dimensions.
_BYTES_PER_PIXEL = {
    0: 0.5,    # Default, BC1
    1: 1.0,    # Normalmap, BC5
    2: 0.5,    # Masks, BC1
    3: 0.5,    # Grayscale, BC4
    5: 4.0,    # VectorDisplacement, RGBA8
    7: 4.0,    # EditorIcon
    8: 0.5,    # Alpha, BC4
    11: 1.0,   # BC7
    12: 8.0,   # HalfFloat
}


def _texture_parameters(mat):
    """Every texture parameter on a master, by name."""
    names = []
    for e in MEL.get_material_expressions(mat):
        cls = e.get_class().get_name()
        if cls in ('MaterialExpressionTextureObjectParameter',
                   'MaterialExpressionTextureSampleParameter2D'):
            names.append(str(e.get_editor_property('parameter_name')))
    return sorted(set(names))


def _dimensions(tex):
    """The texture's built size.

    Not blueprint_get_size_x: that reports whatever mip is resident right now, so a texture the
    editor has not streamed in measures 32x32 and one it has measures 2048, for the same asset.
    """
    try:
        size = tex.blueprint_get_built_texture_size()
        return int(size.x), int(size.y)
    except Exception:
        pass
    try:
        return int(tex.blueprint_get_size_x()), int(tex.blueprint_get_size_y())
    except Exception:
        return 0, 0


def _estimate_bytes(tex):
    """Resident size, estimated from dimensions and compression, mips included.

    The asset registry does not carry a usable size tag here and the loaded texture will not hand
    one over, so this is arithmetic rather than a measurement - good enough to rank by and to spot
    the 4K that should have been a 512, which is what anyone actually wants from it.
    """
    w, h = _dimensions(tex)
    if not w or not h:
        return 0
    try:
        setting = tex.get_editor_property('compression_settings')
    except Exception:
        return 0

    # The property is an enum, which will not int() - take its value, and fall back to reading the
    # name out of its repr rather than guessing a size.
    fmt = getattr(setting, 'value', None)
    if fmt is None:
        text = str(setting)
        fmt = int(text.rsplit(':', 1)[-1].strip(' >')) if ':' in text else -1
    bpp = _BYTES_PER_PIXEL.get(int(fmt), 1.0)
    # A full mip chain is a third again on top of mip 0.
    return int(w * h * bpp * 4.0 / 3.0)


def texture_memory():
    """Texture referenced by each master's instances.

    Worth watching wherever texture streaming is off, because then every referenced mip is
    resident whether or not anything is looking at it - the size below is what it costs to have
    the material in the level at all, not what it costs to see it.
    """
    grand = 0
    for label, mat in _masters():
        params = _texture_parameters(mat)
        seen = {}

        for mi in _instances_of(mat):
            for name in params:
                try:
                    tex = MEL.get_material_instance_texture_parameter_value(mi, name)
                except Exception:
                    tex = None
                if tex is None:
                    continue
                path = tex.get_path_name()
                if path not in seen:
                    w, h = _dimensions(tex)
                    seen[path] = (tex.get_name(), _estimate_bytes(tex), w, h)

        used = sum(v[1] for v in seen.values())
        grand += used
        _log('%s: %d texture(s), about %.1f MB resident'
             % (mat.get_name(), len(seen), used / (1024.0 * 1024.0)))
        for name, size, w, h in sorted(seen.values(), key=lambda v: -v[1])[:10]:
            _log('   %-44s %5dx%-5d %6.2f MB' % (name, w, h, size / (1024.0 * 1024.0)))

    _log('about %.1f MB across every master' % (grand / (1024.0 * 1024.0)))
    return grand


def run_all():
    permutations()
    texture_memory()
