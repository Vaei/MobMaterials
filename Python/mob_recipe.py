"""Reads a recipe asset and points a generator at it.

A recipe says what to author and where. Everything the generators used to hold as module
constants - output path, asset name, layer list, whether the project outputs are wired - comes
from here, so one project can carry as many masters as it needs and regenerate each on its own.

With no recipe the generators keep the defaults they were written with, so they still run straight
from the Python console.
"""

import unreal

EAL = unreal.EditorAssetLibrary

LANDSCAPE = 'Landscape'
SURFACE = 'Surface'


def load(recipe):
    """Takes a path or an asset and returns the asset, or None."""
    if recipe is None:
        return None
    if isinstance(recipe, str):
        return unreal.load_asset(recipe) if EAL.does_asset_exist(recipe) else None
    return recipe


def kind(recipe):
    """'Landscape' or 'Surface'.

    An enum stringifies as "<MobMaterialKind.SURFACE: 1>", so match on the name appearing rather
    than on either end of it.
    """
    return SURFACE if 'SURFACE' in str(recipe.get_editor_property('kind')).upper() else LANDSCAPE


def _dir(recipe, name, default=''):
    try:
        path = recipe.get_editor_property(name)
    except Exception:
        return default
    text = str(path.get_editor_property('path') if path else '').strip().rstrip('/')
    return text or default


def _apply_common(module, recipe):
    root = _dir(recipe, 'output_path', module.ROOT)
    module.ROOT = root

    # Functions live beside the master they serve. Two recipes sharing an output path share one
    # copy; two pointing elsewhere each get their own and stay independent.
    module.FN_ROOT = root + '/Functions'

    name = str(recipe.get_editor_property('asset_name') or '').strip()
    if name:
        module.MASTER_NAME = name
    return module


def apply_landscape(module, recipe):
    recipe = load(recipe)
    if recipe is None:
        module._log('no recipe, using script defaults')
        return module

    _apply_common(module, recipe)

    layers = recipe.get_editor_property('layers') or []
    parsed = []
    for entry in layers:
        layer = str(entry.get_editor_property('name'))
        if not layer or layer == 'None':
            continue
        parsed.append((layer,
                       str(entry.get_editor_property('physical_surface')),
                       str(entry.get_editor_property('physical_material'))))
    if parsed:
        module.LAYERS = parsed

    module.BUILD_PROJECT_OUTPUTS = bool(recipe.get_editor_property('build_project_outputs'))
    module.RVT_DIR = _dir(recipe, 'runtime_virtual_texture_path', module.RVT_DIR)
    module.PHYSMAT_DIR = _dir(recipe, 'physical_material_path', module.PHYSMAT_DIR)
    module.LAYERINFO_DIR = _dir(recipe, 'layer_info_path', module.LAYERINFO_DIR)
    module.GRASS_DIR = _dir(recipe, 'grass_type_path', module.GRASS_DIR)

    # Built from RVT_DIR when the module was imported, so they have to be rebuilt after it moves.
    module.RVT_TERRAIN = module.RVT_DIR + '/RVT_' + module.MASTER_NAME
    module.RVT_HEIGHT = module.RVT_DIR + '/RVT_' + module.MASTER_NAME + 'Height'

    module._log('%s: %d layer(s), %s, project outputs %s'
                % (module.MASTER_NAME, len(module.LAYERS), module.ROOT,
                   'on' if module.BUILD_PROJECT_OUTPUTS else 'off'))
    return module


def apply_surface(module, recipe):
    recipe = load(recipe)
    if recipe is None:
        module._log('no recipe, using script defaults')
        return module

    _apply_common(module, recipe)

    # The property resolves to the loaded collection, whose repr ends in its class name, so parsing
    # the string is a trap: ask the object for its path and drop the object suffix.
    collection = recipe.get_editor_property('weather_collection')
    path = ''
    if collection is not None:
        try:
            path = str(collection.get_path_name())
        except Exception:
            try:
                path = str(collection.to_soft_object_path().to_string())
            except Exception:
                path = ''
        path = path.split('.')[0].strip()
    if path:
        module.WEATHER_MPC = path
    else:
        # Unset: put one beside the master rather than in the plugin, and hand the recipe back so
        # the generator can fill the field in once it has created it.
        module.WEATHER_MPC = '%s/MPC_%sWeather' % (module.ROOT, module.MASTER_NAME)
    module.RECIPE = recipe
    module.INCLUDE_DETAIL = bool(recipe.get_editor_property('detail_maps'))

    module._log('%s: %s, weather %s, detail %s'
                % (module.MASTER_NAME, module.ROOT, module.WEATHER_MPC,
                   'on' if module.INCLUDE_DETAIL else 'off'))
    return module
