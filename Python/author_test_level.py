"""Builds a level demonstrating a surface recipe's master, one feature per object.

Everything here uses the neutral placeholder textures the plugin ships and separates the layers by
tint, so it needs no art. That costs the demo the detail a real texture would bring - with a flat
cavity there is nothing for the height blend to interlock along - so layering is shown by bringing
the layers in one sphere at a time rather than by one blended sphere that would just read as a
single flat colour.

Run from the Mob toolbar menu, or:

    import author_test_level as t
    t.build('/Game/Path/MR_MySurface')
"""

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

SPHERE = '/Engine/BasicShapes/Sphere'
CUBE = '/Engine/BasicShapes/Cube'


def _log(msg):
    unreal.log('[MobMasterMaterial] ' + str(msg))


def _tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def _instance(path, name, parent):
    full = path + '/' + name
    mi = (unreal.load_asset(full) if EAL.does_asset_exist(full)
          else _tools().create_asset(name, path, unreal.MaterialInstanceConstant,
                                     unreal.MaterialInstanceConstantFactoryNew()))
    MEL.set_material_instance_parent(mi, parent)
    return mi


def _setup(mi, switches=None, scalars=None, vectors=None, textures=None):
    # Every switch the master carries is set explicitly, off unless asked for, so a demo shows one
    # feature and reruns cannot leave a stale switch on. Reading them off the master rather than a
    # list here is what stops a new feature from being silently undemoable.
    switches = switches or {}
    for key in MEL.get_static_switch_parameter_names(mi.get_editor_property('parent')):
        MEL.set_material_instance_static_switch_parameter_value(
            mi, key, bool(switches.get(str(key), False)))
    for key, value in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, key, float(value))
    for key, (r, g, b) in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(mi, key, unreal.LinearColor(r, g, b, 1.0))
    for key, path in (textures or {}).items():
        asset = unreal.load_asset(path)
        if asset:
            MEL.set_material_instance_texture_parameter_value(mi, key, asset)
    EAL.save_loaded_asset(mi, only_if_is_dirty=False)
    return mi


def build(recipe):
    """Authors the demo instances and a level laying them out. Returns the level's path."""
    import mob_recipe
    import importlib as _il
    _il.reload(mob_recipe)

    recipe = mob_recipe.load(recipe)
    if recipe is None:
        raise RuntimeError('no recipe')
    if mob_recipe.kind(recipe) != mob_recipe.SURFACE:
        raise RuntimeError('test levels are only built for surface recipes')

    root = str(recipe.get_editor_property('output_path').get_editor_property('path')).rstrip('/')
    name = str(recipe.get_editor_property('asset_name'))
    master = unreal.load_asset(root + '/M_' + name)
    if master is None:
        raise RuntimeError('generate %s first' % ('M_' + name))

    tests = root + '/Tests'

    # Tints stand in for texture sets, so a blend is visible without any art.
    STONE = (0.62, 0.63, 0.66)
    EARTH = (0.52, 0.36, 0.22)
    MOSS = (0.32, 0.46, 0.22)

    demos = [
        ('Plain', 'the base layer alone. Three texture samples, everything else compiled out',
         {}, {}, {'Layer0_Tint': STONE}),
        ('Layer1', 'the second layer brought in over the base, by scalar weight',
         {'bLayer1': True}, {'Layer1Weight': 0.65, 'BlendHeightAmount': 1.0, 'BlendContrast': 0.25},
         {'Layer0_Tint': STONE, 'Layer1_Tint': EARTH}),
        ('Layer2', 'and the third over that. Same master, two switches',
         {'bLayer1': True, 'bLayer2': True},
         {'Layer1Weight': 0.65, 'Layer2Weight': 0.6, 'BlendHeightAmount': 1.0, 'BlendContrast': 0.25},
         {'Layer0_Tint': STONE, 'Layer1_Tint': EARTH, 'Layer2_Tint': MOSS}),
        ('VertexPaint', 'the same three layers, driven by vertex colour. Fill white, paint black',
         {'bLayer1': True, 'bLayer2': True, 'bVertexPaint': True},
         {'BlendHeightAmount': 1.0, 'BlendContrast': 0.25},
         {'Layer0_Tint': STONE, 'Layer1_Tint': EARTH, 'Layer2_Tint': MOSS}),
        ('Triplanar', 'world-aligned projection. Scale the actor and the texture does not stretch',
         {'Layer0_Triplanar': True}, {'Layer0_TriplanarScale': 0.01}, {'Layer0_Tint': STONE}),
        ('Wetness', 'drive the recipe weather collection 0 to 1 and this one darkens and shines',
         {'bWetness': True},
         {'Wetness_PuddleDepth': 0.55, 'Wetness_PuddleFacing': 0.55, 'Wetness_PuddleRoughness': 0.02},
         {'Layer0_Tint': EARTH}),
        ('Variation', 'one instance, five actors. Each hashes its own position',
         {'bColorVariation': True},
         {'Variation_HueRange': 18.0, 'Variation_SaturationRange': 0.25, 'Variation_ValueRange': 0.3},
         {'Layer0_Tint': EARTH}),
        ('Emissive', 'a masked glow. The mask defaults to black, so the demo assigns a white one',
         {'bEmissive': True}, {'EmissiveIntensity': 4.0},
         {'Layer0_Tint': STONE, 'EmissiveColor': (1.0, 0.45, 0.1)},
         {'EmissiveMask': '/MobMasterMaterial/Textures/T_BaseWhite'}),
    ]

    # Everything past the original set depends on a recipe option, so each is offered only when the
    # master actually carries its switch. A demo for a feature that was not authored would be a
    # sphere that silently looks like the plain one.
    optional = [
        ('Detail', 'a fine normal over the base, faded out with distance. One extra sample',
         {'bDetail': True}, {'DetailScale': 8.0, 'DetailAmount': 1.0}, {'Layer0_Tint': STONE}),
        ('TileBreak', 'a second incommensurate tiling faded in with distance. Walk backwards',
         {'Layer0_TileBreak': True},
         {'Layer0_TileBreakScale': 0.37, 'Layer0_TileBreakStart': 200.0,
          'Layer0_TileBreakAmount': 1.0}, {'Layer0_Tint': STONE}),
        ('Parallax', 'the cheap offset mode. Depth without touching the silhouette',
         {'Layer0_Parallax': True}, {'Layer0_ParallaxAmount': 0.04}, {'Layer0_Tint': STONE}),
        ('Accumulation', 'a covering settling by which way the surface faces. Drive the amount',
         {'bAccumulation': True},
         {'Accumulation_Amount': 0.6, 'Accumulation_Facing': 0.5,
          'Accumulation_CavityBias': 0.5, 'Accumulation_CoverRoughness': 0.85},
         {'Layer0_Tint': EARTH, 'Accumulation_Colour': (0.92, 0.94, 1.0)}),
        ('Ripples', 'rain on standing water. Needs the collection wetness up, and a ripple normal',
         {'bWetness': True, 'bRipples': True},
         {'Wetness_PuddleDepth': 0.7, 'Wetness_PuddleFacing': 0.4,
          'Wetness_PuddleRoughness': 0.02, 'RippleScale': 12.0, 'RippleStrength': 1.0},
         {'Layer0_Tint': EARTH}),
        ('Debug', 'layer weights sent to emissive with base colour blacked out',
         {'bLayer1': True, 'bLayer2': True, 'bDebug': True},
         {'Layer1Weight': 0.5, 'Layer2Weight': 0.3, 'DebugMode': 1.0},
         {'Layer0_Tint': STONE, 'Layer1_Tint': EARTH, 'Layer2_Tint': MOSS}),
    ]

    available = set(str(n) for n in MEL.get_static_switch_parameter_names(master))
    for entry in optional:
        if all(switch in available for switch in entry[2]):
            demos.append(entry)
        else:
            _log('skipping the %s demo - not in this master' % entry[0])

    built = {}
    for entry in demos:
        key, _tip, switches, scalars, vectors = entry[:5]
        textures = entry[5] if len(entry) > 5 else None
        built[key] = _setup(_instance(tests, 'MI_%s_%s' % (name, key), master),
                            switches, scalars, vectors, textures)

    # A fresh level, so nothing here can land in whatever the user had open.
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    sphere = unreal.load_asset(SPHERE)

    def place(label, mesh, mi, x, y, z, scale):
        a = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, z),
                                          unreal.Rotator(0.0, 0.0, 0.0))
        a.set_actor_label(label)
        a.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        a.static_mesh_component.set_static_mesh(mesh)
        a.static_mesh_component.set_material(0, mi)
        return a

    spacing = 300.0
    row = [e[0] for e in demos if e[0] != 'Variation']
    for i, key in enumerate(row):
        place('Mob_' + key, sphere, built[key],
              (i - (len(row) - 1) * 0.5) * spacing, 0.0, 150.0, 1.2)

    # Five copies of one instance: the tint spread is the per-object hash, nothing per actor.
    for i in range(5):
        place('Mob_Variation_%d' % i, sphere, built['Variation'],
              (i - 2) * spacing, 420.0, 150.0, 1.2)

    # Per-instance data: one material instance, three actors, three tints. No new permutation.
    if 'bPrimitiveData' in available:
        prim = _setup(_instance(tests, 'MI_%s_PrimitiveData' % name, master),
                      {'bPrimitiveData': True}, {}, {'Layer0_Tint': STONE})
        for i, tint in enumerate(((0.8, 0.3, 0.25), (0.3, 0.55, 0.8), (0.85, 0.75, 0.3))):
            a = place('Mob_PrimitiveData_%d' % i, sphere, prim, (i - 1) * spacing, 780.0, 150.0, 1.2)
            # The default variant, not the runtime one: only that is saved with the level.
            a.static_mesh_component.set_default_custom_primitive_data_vector3(
                0, unreal.Vector(tint[0], tint[1], tint[2]))

    floor = place('Mob_Floor', unreal.load_asset(CUBE), built['Plain'], 0.0, 150.0, 0.0, 1.0)
    floor.set_actor_scale3d(unreal.Vector(24.0, 12.0, 0.5))

    light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 600.0),
                                          unreal.Rotator(0.0, -40.0, 140.0))
    light.set_actor_label('Mob_Light')
    light.light_component.set_intensity(6.0)

    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0.0, 0.0, 700.0))
    sky.set_actor_label('Mob_Sky')
    sky.light_component.set_intensity(1.5)

    level = '%s/L_%s_Test' % (tests, name)
    unreal.EditorLoadingAndSavingUtils.save_map(
        unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(), level)

    _log('test level built: %s' % level)
    for entry in demos:
        _log('  %-12s %s' % (entry[0], entry[1]))
    return level
