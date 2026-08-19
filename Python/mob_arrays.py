"""Packing a recipe's paint layers into texture arrays.

One array per channel, sliced in the recipe's layer order, so the material samples three texture
objects no matter how many layers there are. Run it before generating a landscape master with
Texture Array Layers on, and again whenever a layer's art changes:

    import mob_arrays
    mob_arrays.pack('/Game/Materials/MR_Landscape')

Every layer's textures have to agree - one resolution and one format per channel - because that is
what a texture array is. When they do not, this says which layer broke it rather than packing
something wrong.
"""

import unreal

import mob_recipe

REG = unreal.AssetRegistryHelpers.get_asset_registry()

# Array asset names, and the suffixes a layer's texture may carry for each. Matched
# case-insensitively against the end of the texture name.
CHANNELS = [
    ('BC', ('_bc', '_basecolor', '_basecolour', '_albedo', '_color', '_colour', '_d', '_diffuse')),
    ('NRM', ('_nrm', '_normal', '_norm', '_n')),
    ('HRC', ('_hrc', '_heigrougao', '_heightroughnesscavity', '_mask', '_masks', '_orm', '_mra')),
]


def _log(msg):
    unreal.log('[MobArrays] ' + str(msg))


def _textures_under(root):
    """Every Texture2D under a content path, by name."""
    found = {}
    for asset in REG.get_assets_by_path(root, recursive=True):
        if str(asset.asset_class_path.asset_name) != 'Texture2D':
            continue
        found[str(asset.asset_name)] = str(asset.package_name)
    return found


def _match(layer, suffixes, textures):
    """The texture for one layer and channel, or None.

    Prefers the longest suffix that matches, so a _basecolor never loses to a bare _c, and the
    shortest remaining name, so T_grass beats T_grass_dead_variant when both would do.
    """
    layer_key = layer.lower()
    hits = []
    for name, package in textures.items():
        lowered = name.lower()
        if layer_key not in lowered:
            continue
        for suffix in suffixes:
            if lowered.endswith(suffix):
                hits.append((len(suffix), -len(name), name, package))
                break
    if not hits:
        return None
    hits.sort(reverse=True)
    return hits[0][3]


def resolve(recipe=None):
    """What would be packed, per layer and channel, without packing it.

    Returns (layers, missing) where layers is a list of (layer, {channel: package path}).
    """
    import author_landscape as al
    mob_recipe.apply_landscape(al, recipe)

    root = getattr(al, 'LAYER_TEXTURE_ROOT', '')
    if not root:
        _log('no Layer Texture Root on the recipe - nothing to search')
        return [], ['Layer Texture Root']

    textures = _textures_under(root)
    _log('%d texture(s) under %s' % (len(textures), root))

    # Moss samples like a paint layer and so needs a slice of its own, after the painted ones.
    # The master assigns the indices in exactly this order.
    names = [layer for layer, _s, _p in al.LAYERS] + ['Moss']

    resolved, missing = [], []
    for layer in names:
        picked = {}
        for channel, suffixes in CHANNELS:
            package = _match(layer, suffixes, textures)
            if package is None:
                missing.append('%s %s' % (layer, channel))
            else:
                picked[channel] = package
        resolved.append((layer, picked))
    return resolved, missing


def pack(recipe=None):
    """Builds one texture array per channel from the recipe's layers.

    Written beside the master as TA_<AssetName>_<Channel>, rebuilt in place if already there, so
    a material already sampling them keeps its references across a repack.
    """
    import author_landscape as al

    resolved, missing = resolve(recipe)
    if not resolved:
        return {}

    for entry in missing:
        _log('MISSING  %s' % entry)

    built = {}
    for channel, _suffixes in CHANNELS:
        slices, gap = [], False
        for layer, picked in resolved:
            package = picked.get(channel)
            if package is None and layer == 'Moss':
                # Moss is the master's own overlay rather than a layer anyone asked for, so a
                # project with no moss art should not be unable to pack. It gets the first layer's
                # texture and a line saying so, since a wrong slice is otherwise invisible.
                package = resolved[0][1].get(channel)
                if package:
                    _log('%s: no moss texture, using %s' % (channel, package.rsplit('/', 1)[-1]))
            if package is None:
                gap = True
                break
            texture = unreal.load_asset(package)
            if texture is None:
                _log('could not load %s' % package)
                gap = True
                break
            slices.append(texture)
        if gap:
            _log('%s: skipped, a layer has no texture for it' % channel)
            continue

        path = '%s/TA_%s_%s' % (al.ROOT, al.MASTER_NAME, channel)
        array = unreal.MobLayerArrayLibrary.pack_layer_array(path, slices)
        if array is None:
            _log('%s: pack failed, see the log above for which slice' % channel)
            continue

        # The packed array itself, not its path: saving by path reloads the package to find what to
        # save, which is a load nobody asked for around an asset that was just built in memory.
        unreal.EditorAssetLibrary.save_loaded_asset(array, only_if_is_dirty=False)
        built[channel] = path
        _log('%s: %d slice(s) -> %s' % (channel, len(slices), path))

    for index, (layer, _picked) in enumerate(resolved):
        _log('   slice %d  %s' % (index, layer))

    if len(built) == len(CHANNELS):
        _log('packed. Generate the master to sample them.')
    else:
        _log('packed %d of %d channel(s). Fix the gaps above before generating.'
             % (len(built), len(CHANNELS)))
    return built
