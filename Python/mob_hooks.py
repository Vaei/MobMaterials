"""Splices a project's own material functions into a generated master.

A generated material cannot be hand-edited and keep the edit: the next authoring run empties the
graph and rebuilds it. A hook is the way round that. The recipe names a function and a point, and
the generator wires it in every time.

Wiring is by name and nothing else. The master carries a dict of live signals - BaseColor, Normal,
Cavity, and so on - down the chain; a hook function's declared inputs are looked up in it, and its
declared outputs replace the entries of the same name. A signal the function has no output for
passes through untouched, so a function that reads five things and writes one costs nothing on the
other four.

Set HOOKS from the recipe (mob_recipe does this) and call apply at each point.
"""

import unreal

MEL = unreal.MaterialEditingLibrary

# Point names, matching EMobMaterialHookPoint. See the enum for what is live at each.
LAYERS = 'Layers'
INSTANCE = 'Instance'
WEATHER = 'Weather'
SHADING = 'Shading'
OUTPUT = 'Output'
WPO = 'WorldPositionOffset'

POINTS = [LAYERS, INSTANCE, WEATHER, SHADING, OUTPUT, WPO]

# Set by mob_recipe from the recipe asset. Each entry is a dict: function, point, switch,
# switch_default, switch_group.
HOOKS = []


def _log(msg):
    unreal.log('[MobMaterials] ' + str(msg))


def reset():
    """Called before a recipe is read, so a generator run without one splices nothing."""
    global HOOKS
    HOOKS = []


def point_from_enum(value):
    """The point name for an EMobMaterialHookPoint.

    An enum stringifies as "<MobMaterialHookPoint.WORLD_POSITION_OFFSET: 5>", so match on the
    UHT-cased name appearing rather than on either end of it.
    """
    text = str(value).upper()
    for name in POINTS:
        # WorldPositionOffset comes through as WORLD_POSITION_OFFSET.
        uht = ''.join(('_' + c if c.isupper() and i else c) for i, c in enumerate(name)).upper()
        if uht in text:
            return name
    return None


def declared_pins(function):
    """The input and output names a material function declares, read off its own graph.

    Read from the asset rather than from a call node: a freshly created call has not necessarily
    populated its pin arrays yet, and an empty read there would silently wire nothing.
    """
    ins, outs = [], []
    for e in MEL.get_material_function_expressions(function):
        if isinstance(e, unreal.MaterialExpressionFunctionInput):
            ins.append(str(e.get_editor_property('input_name')))
        elif isinstance(e, unreal.MaterialExpressionFunctionOutput):
            outs.append(str(e.get_editor_property('output_name')))
    return ins, outs


def _static_switch(mat, name, group, default, true_src, true_out, false_src, false_out, x, y):
    sw = MEL.create_material_expression(mat, unreal.MaterialExpressionStaticSwitchParameter,
                                        int(x), int(y))
    sw.set_editor_property('parameter_name', name)
    sw.set_editor_property('default_value', bool(default))
    sw.set_editor_property('group', str(group))
    _connect(true_src, true_out, sw, 'True')
    _connect(false_src, false_out, sw, 'False')
    return sw


def _connect(src, src_out, dst, dst_in):
    if not MEL.connect_material_expressions(src, src_out, dst, dst_in):
        raise RuntimeError('hook: failed to connect %s.%s -> %s.%s'
                           % (src.get_name(), src_out, dst.get_name(), dst_in))


def _zero(mat, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, int(x), int(y))
    e.set_editor_property('constant', unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
    return e


def apply(mat, point, signals, x=0, y=0):
    """Splices every hook sitting at this point, and returns the signals as they leave it.

    signals maps a name to an (expression, output name) pair. The dict is copied rather than
    written through, so a caller holding the previous one still sees what it had.
    """
    at = [h for h in HOOKS if h.get('point') == point]
    if not at:
        return signals

    signals = dict(signals)
    _log('hooks at %s: %s' % (point, ', '.join(h['function'].get_name() for h in at)))
    _log('  signals: ' + ', '.join(sorted(signals)))

    for index, hook in enumerate(at):
        function = hook['function']
        row = y + index * 400
        call = MEL.create_material_expression(mat, unreal.MaterialExpressionMaterialFunctionCall,
                                              int(x), int(row))
        call.set_editor_property('material_function', function)

        ins, outs = declared_pins(function)

        # An input naming nothing live is left on the function's own default rather than treated as
        # an error: a project function is written once and spliced at whichever point suits, and the
        # pools differ.
        unmatched = []
        for name in ins:
            source = signals.get(name)
            if source is None:
                unmatched.append(name)
                continue
            _connect(source[0], source[1], call, name)
        if unmatched:
            _log('  %s: no signal for %s' % (function.get_name(), ', '.join(unmatched)))

        switch = str(hook.get('switch') or '')
        group = hook.get('switch_group') or 'Project'
        default = bool(hook.get('switch_default'))

        for offset, name in enumerate(outs):
            previous = signals.get(name)

            # World position offset is added by the caller rather than replacing a signal, so it is
            # the one output allowed to name something that was not already there.
            if previous is None and not (point == WPO and name == WPO):
                _log('  %s: output %s replaces nothing at %s, ignored'
                     % (function.get_name(), name, point))
                continue

            source = (call, name)
            if switch:
                false_src = previous if previous is not None else (_zero(mat, x + 200, row), '')
                source = (_static_switch(mat, switch, group, default, call, name,
                                         false_src[0], false_src[1],
                                         x + 400, row + offset * 60), '')
            signals[name] = source

    return signals
