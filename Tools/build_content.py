# Makes the game's Unreal assets from scratch (materials and the level), so
# nothing has to be made by hand in the editor. Run with Tools/build_content.sh.
import unreal

MATERIALS = '/Game/Ports/Materials'
LEVEL = '/Game/Ports/Maps/Main'

assets = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def new_material(name):
    path = f'{MATERIALS}/{name}'
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    return assets.create_asset(name, MATERIALS, unreal.Material, unreal.MaterialFactoryNew())


def constant(material, value, x, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionConstant, x, y)
    node.set_editor_property('r', value)
    return node


def finish(material):
    mel.recompile_material(material)
    eal.save_loaded_asset(material)
    unreal.log(f'Ports content: made {material.get_path_name()}')


# The board's surfaces: each point of the mesh carries its own colour.
m = new_material('M_PortsVertexColor')
colour = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -400, 0)
mel.connect_material_property(colour, '', unreal.MaterialProperty.MP_BASE_COLOR)
mel.connect_material_property(constant(m, 0.9, -400, 200), '', unreal.MaterialProperty.MP_ROUGHNESS)
mel.connect_material_property(constant(m, 0.0, -400, 300), '', unreal.MaterialProperty.MP_SPECULAR)
finish(m)

# Placeholder models: one flat colour, set from code through the "Color" parameter.
m = new_material('M_PortsColor')
colour = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -400, 0)
colour.set_editor_property('parameter_name', 'Color')
colour.set_editor_property('default_value', unreal.LinearColor(0.9, 0.8, 0.6, 1.0))
mel.connect_material_property(colour, '', unreal.MaterialProperty.MP_BASE_COLOR)
mel.connect_material_property(constant(m, 0.8, -400, 200), '', unreal.MaterialProperty.MP_ROUGHNESS)
finish(m)

# ---------- Textures (painted by Tools/make_textures.mjs) ----------
import os
TEXTURES = '/Game/Ports/Textures'
SOURCE = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), 'SourceArt', 'Textures')


def import_texture(name, normal=False, linear=False):
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', os.path.join(SOURCE, name + '.png'))
    task.set_editor_property('destination_path', TEXTURES)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', True)
    assets.import_asset_tasks([task])
    texture = unreal.load_asset(f'{TEXTURES}/{name}')
    if not texture:
        unreal.log_error(f'Ports content: could not import {name}')
        return None
    if normal:
        texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property('srgb', False)
    if linear:
        texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
        texture.set_editor_property('srgb', False)
    eal.save_loaded_asset(texture)
    unreal.log(f'Ports content: imported {name}')
    return texture


def world_uv(material, size, y):
    """Texture coordinates taken from the position on the board, so textures need no unwrapping."""
    where = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1400, y)
    flat = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -1200, y)
    flat.set_editor_property('r', True)
    flat.set_editor_property('g', True)
    flat.set_editor_property('b', False)
    flat.set_editor_property('a', False)
    mel.connect_material_expressions(where, '', flat, '')
    scaled = mel.create_material_expression(material, unreal.MaterialExpressionDivide, -1000, y)
    scaled.set_editor_property('const_b', size)
    mel.connect_material_expressions(flat, '', scaled, 'A')
    return scaled


def sample(material, texture, uv, x, y, normal=False):
    node = mel.create_material_expression(material, unreal.MaterialExpressionTextureSample, x, y)
    node.set_editor_property('texture', texture)
    if normal:
        node.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    mel.connect_material_expressions(uv, '', node, 'UVs')
    return node


def multiply(material, a, a_out, b, b_out, x, y, const=None):
    node = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, x, y)
    mel.connect_material_expressions(a, a_out, node, 'A')
    if b is not None:
        mel.connect_material_expressions(b, b_out, node, 'B')
    else:
        node.set_editor_property('const_b', const)
    return node


parchment = import_texture('T_Parchment')
parchment_n = import_texture('T_Parchment_N', normal=True)
wood = import_texture('T_Wood')
wood_n = import_texture('T_Wood_N', normal=True)

# Parchment: the mesh's own colours, tinted by the parchment texture at two
# sizes (so the repeat does not show) and given its fine relief.
if parchment and parchment_n:
    m = new_material('M_PortsParchment')
    paint = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -700, -200)
    wide = sample(m, parchment, world_uv(m, 11000.0, 0), -700, 0)
    fine = sample(m, parchment, world_uv(m, 3700.0, 250), -700, 250)
    grain = multiply(m, wide, 'RGB', fine, 'RGB', -450, 100)
    lifted = multiply(m, grain, '', None, None, -300, 100, const=1.27)
    colour = multiply(m, paint, '', lifted, '', -150, 0)
    mel.connect_material_property(colour, '', unreal.MaterialProperty.MP_BASE_COLOR)
    relief = sample(m, parchment_n, world_uv(m, 3700.0, 500), -700, 500, normal=True)
    mel.connect_material_property(relief, 'RGB', unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(constant(m, 0.88, -400, 700), '', unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(constant(m, 0.15, -400, 800), '', unreal.MaterialProperty.MP_SPECULAR)
    finish(m)

# The walnut table under the map. The mesh's colours darken it (used for the map's shadow).
if wood and wood_n:
    m = new_material('M_PortsTable')
    paint = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -700, -200)
    boards = sample(m, wood, world_uv(m, 16000.0, 0), -700, 0)
    colour = multiply(m, paint, '', boards, 'RGB', -300, 0)
    mel.connect_material_property(colour, '', unreal.MaterialProperty.MP_BASE_COLOR)
    relief = sample(m, wood_n, world_uv(m, 16000.0, 300), -700, 300, normal=True)
    mel.connect_material_property(relief, 'RGB', unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(constant(m, 0.5, -400, 600), '', unreal.MaterialProperty.MP_ROUGHNESS)
    finish(m)

# ---------- Map decorations (rendered from the web version's artwork by Tools/make_map_art.mjs) ----------
def import_art(name):
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', os.path.join(os.path.dirname(SOURCE), 'MapArt', name + '.png'))
    task.set_editor_property('destination_path', TEXTURES)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', True)
    assets.import_asset_tasks([task])
    texture = unreal.load_asset(f'{TEXTURES}/{name}')
    if texture:
        eal.save_loaded_asset(texture)
        unreal.log(f'Ports content: imported {name}')
    else:
        unreal.log_error(f'Ports content: could not import {name}')
    return texture


cartouche = import_art('T_MapCartouche')
compass = import_art('T_MapCompass')

# A picture painted on the map: lit like the parchment around it, see-through where the picture is.
if cartouche:
    m = new_material('M_PortsMapArt')
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    # Not shaded by the light: the picture keeps the colours it has on the website.
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    art = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -500, 0)
    art.set_editor_property('parameter_name', 'Art')
    art.set_editor_property('texture', cartouche)
    # A little warmth, to sit with the candle-lit parchment round it.
    warm = mel.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -500, 250)
    warm.set_editor_property('constant', unreal.LinearColor(1.62, 1.42, 1.04, 1.0))
    mel.connect_material_property(multiply(m, art, 'RGB', warm, '', -250, 0), '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(art, 'A', unreal.MaterialProperty.MP_OPACITY)
    finish(m)

# ---------- Water ----------
# The sea and the lakes. The game hands the material a picture of how far every point is
# from the coast ("Field"); from that it takes its depth of colour and draws surf rolling
# in to every shore. Ripples are two layers of one normal map drifting across each other.
def custom(material, code, output, names, x, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property('code', code)
    node.set_editor_property('output_type', output)
    inputs = []
    for name in names:
        one = unreal.CustomInput()
        one.set_editor_property('input_name', name)
        inputs.append(one)
    node.set_editor_property('inputs', inputs)
    return node


water_n = import_texture('T_Water_N', normal=True)
flat = import_texture('T_Flat', linear=True)
if water_n and flat:
    m = new_material('M_PortsWater')
    where = mel.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -1900, 0)
    clock = mel.create_material_expression(m, unreal.MaterialExpressionTime, -1900, 300)
    still = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -1900, 450)
    still.set_editor_property('parameter_name', 'Still')
    still.set_editor_property('default_value', 0.0)

    # Where this point is in the coast picture: the board is 1164 x 896 map pixels of 20 units,
    # and the picture reaches 420 pixels past it on every side.
    place = custom(m, 'float2 px = float2(WP.y / 20.0 + 582.0, 448.0 - WP.x / 20.0); return (px + 420.0) / float2(2004.0, 1736.0);',
                   unreal.CustomMaterialOutputType.CMOT_FLOAT2, ['WP'], -1600, 0)
    mel.connect_material_expressions(where, '', place, 'WP')
    field = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -1350, 0)
    field.set_editor_property('parameter_name', 'Field')
    field.set_editor_property('texture', flat)
    field.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    mel.connect_material_expressions(place, '', field, 'UVs')

    def ripples(size, speed_x, speed_y, y):
        uv = custom(m, f'return WP.xy / {size:.1f} + T * float2({speed_x}, {speed_y});', unreal.CustomMaterialOutputType.CMOT_FLOAT2, ['WP', 'T'], -1600, y)
        mel.connect_material_expressions(where, '', uv, 'WP')
        mel.connect_material_expressions(clock, '', uv, 'T')
        return sample(m, water_n, uv, -1350, y, normal=True)

    wide = ripples(2600.0, 0.011, 0.004, 300)
    fine = ripples(1150.0, -0.007, 0.013, 550)

    colour = custom(m, """
float d = (D - 0.5) * 128.0;
float depth = lerp(max(0.0, -d), 6.0, Still);
float3 shallow = float3(0.115, 0.400, 0.420);
float3 middle = float3(0.019, 0.188, 0.254);
float3 deep = float3(0.011, 0.105, 0.160);
float3 c = lerp(shallow, middle, smoothstep(0.0, 9.0, depth));
c = lerp(c, deep, smoothstep(9.0, 62.0, depth));
float wobble = N.x * 3.0 + N.y * 2.0;
float roll = sin(depth * 1.05 - T * 1.25 + wobble * 4.0);
float surf = smoothstep(0.70, 1.0, roll) * (1.0 - smoothstep(2.5, 12.0, depth));
float shore = 1.0 - smoothstep(0.2, 2.0, depth);
float foam = saturate(surf * 0.5 + shore * 0.8) * (1.0 - Still) * saturate(0.55 + wobble);
return lerp(c, float3(0.80, 0.90, 0.88), foam);
""", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ['D', 'T', 'N', 'Still'], -900, 0)
    mel.connect_material_expressions(field, 'R', colour, 'D')
    mel.connect_material_expressions(clock, '', colour, 'T')
    mel.connect_material_expressions(wide, 'RGB', colour, 'N')
    mel.connect_material_expressions(still, '', colour, 'Still')
    mel.connect_material_property(colour, '', unreal.MaterialProperty.MP_BASE_COLOR)

    surface = custom(m, 'return normalize(float3((A.xy + B.xy) * 0.55, 1.0));', unreal.CustomMaterialOutputType.CMOT_FLOAT3, ['A', 'B'], -900, 400)
    mel.connect_material_expressions(wide, 'RGB', surface, 'A')
    mel.connect_material_expressions(fine, 'RGB', surface, 'B')
    mel.connect_material_property(surface, '', unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(constant(m, 0.22, -400, 600), '', unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(constant(m, 0.55, -400, 700), '', unreal.MaterialProperty.MP_SPECULAR)
    finish(m)

# The houses' flags: the mesh's own colours, seen from both sides, and waving. Each point of the
# cloth carries how far it is from the pole (0 to 1), and waves that much.
m = new_material('M_PortsFlag')
m.set_editor_property('two_sided', True)
# Not shaded by the light: a flag facing away from the sun would otherwise turn black.
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
paint = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -500, 0)
mel.connect_material_property(multiply(m, paint, '', None, None, -250, 0, const=0.95), '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
where = mel.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, 500)
clock = mel.create_material_expression(m, unreal.MaterialExpressionTime, -900, 650)
along = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -900, 750)
wave = custom(m, """
float reach = UV.x;
float phase = T * 3.4 - reach * 5.5 + WP.y * 0.013;
return float3(-sin(phase) * reach * 13.0, 0.0, cos(phase * 0.8) * reach * 5.0);
""", unreal.CustomMaterialOutputType.CMOT_FLOAT3, ['WP', 'T', 'UV'], -600, 600)
mel.connect_material_expressions(where, '', wave, 'WP')
mel.connect_material_expressions(clock, '', wave, 'T')
mel.connect_material_expressions(along, '', wave, 'UV')
mel.connect_material_property(wave, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
finish(m)

# Thin washes of colour laid over the board (the plague's stain, foul air): the mesh's own
# colours, as see-through as each point says.
m = new_material('M_PortsGlaze')
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
m.set_editor_property('two_sided', True)
paint = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -500, 0)
mel.connect_material_property(paint, '', unreal.MaterialProperty.MP_BASE_COLOR)
mel.connect_material_property(paint, 'A', unreal.MaterialProperty.MP_OPACITY)
mel.connect_material_property(constant(m, 1.0, -400, 300), '', unreal.MaterialProperty.MP_ROUGHNESS)
mel.connect_material_property(constant(m, 0.0, -400, 400), '', unreal.MaterialProperty.MP_SPECULAR)
finish(m)

# Things that give their own light (embers, candle flames, the crown's rays): the mesh's
# own colours added to what is behind them, as strongly as each point's alpha says.
m = new_material('M_PortsGlow')
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('two_sided', True)
paint = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -500, 0)
mel.connect_material_property(multiply(m, paint, '', paint, 'A', -250, 0), '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
finish(m)

# ---------- Sound ----------
# The six songs of the web version (converted from its own files in assets/music) and its
# sound effects (recorded from its own code by Tools/make_sounds.mjs).
AUDIO = '/Game/Ports/Audio'


def import_sound(folder, name, loop=False):
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', os.path.join(os.path.dirname(SOURCE), folder, name + '.wav'))
    task.set_editor_property('destination_path', AUDIO)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', True)
    assets.import_asset_tasks([task])
    sound = unreal.load_asset(f'{AUDIO}/{name}')
    if not sound:
        unreal.log_error(f'Ports content: could not import {name}')
        return None
    if loop:
        sound.set_editor_property('looping', True)
    eal.save_loaded_asset(sound)
    return sound


for song in ['menu', 'trade_1', 'trade_2', 'trade_3', 'plague', 'ending']:
    import_sound('Music', 'music_' + song, loop=True)
for effect in ['click', 'dice', 'bell', 'knell', 'page', 'coin', 'sail', 'cart', 'fortune', 'misfortune', 'plague', 'fanfare', 'victory', 'drumroll', 'stamp', 'error', 'low']:
    import_sound('Sounds', 'sfx_' + effect)

# An empty level. The game mode builds the board, light and sky when play starts.
if not eal.does_asset_exist(LEVEL):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if levels and levels.new_level(LEVEL):
        levels.save_current_level()
        unreal.log(f'Ports content: made {LEVEL}')
    else:
        unreal.log_error('Ports content: could not make the level.')
