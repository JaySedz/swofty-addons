# Builds the dash's UI files from the game's own stock files:
#   ui/lcd_display.idf    the wheel LCD layout (UILayout)
#   ui/race_assets_f1.ui  the race HUD template that feeds it (UIDF)
# and erp/cockpit_hud.json, the launcher ops that swap them into the game's packages.
#
# Usage: python tools/build_ui.py ["<F1 25 install folder>"]
# Reads the untouched originals from <install>/Original_GameData, falling back to the live packages.
import copy, json, os, random, sys, uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import erp
import pkgfmt
import texture
from pkgfmt import D, L, V

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = sys.argv[1] if len(sys.argv) > 1 else r'E:\Steam\steamapps\common\F1 25'
PKGS = ['2025_asset_groups/ui_package/flow_race_assets_f1.erp', '2025_asset_groups/ui_package/flow_options.erp']
LCD = 'eaid://ui_package/art/osd/components/steering_lcd_display/lcd_display.idf'
TPL = 'eaid://ui_package/templates/race_assets_f1.ui'
rng = random.Random(0x5ED2)  # stable ids from build to build
SHOW_LEAF = '#ui_menu_data.hud_rdash_show'  # true while the dash is up (set by the plugin)


def djb2(s):
    h = 5381
    for c in s.encode():
        h = (h * 33 + c) & 0xffffffff
    return h


MODDED = (b'rd_backdrop', b'rdash_', b'wd_popup', b'wd_rpm')  # a dash addon's elements: not the game's own file


def stock(resource, rtype):
    """The game's current file. The live package comes first: the launcher's Original_GameData backup can predate
    game patches (its race template was the release one, without the June patch's overtake tooltip entries). A live
    package a dash addon has written into is skipped. A clean live file is kept in build/stock (ignored by git:
    game files stay out of the repository), and that copy is used while the live package holds a dash addon's
    elements, before falling back to Original_GameData."""
    short = resource.rsplit("/", 1)[-1]
    cache = os.path.join(ROOT, 'build', 'stock', short)
    for base in (GAME, 'cache', os.path.join(GAME, 'Original_GameData')):
        if base == 'cache':
            if os.path.exists(cache):
                print(f'{short} from {cache} (the clean live copy)')
                return open(cache, 'rb').read()
            continue
        path = os.path.join(base, PKGS[0])
        if not os.path.exists(path):
            continue
        f = open(path, 'rb').read()
        for name, t, frags in erp.entries(f):
            if name == resource and t == rtype:
                data = erp.fragment(f, frags[0])
                if any(m in data for m in MODDED):
                    print(f'{path}: {resource.rsplit("/", 1)[-1]} holds a dash addon\'s elements, skipped')
                    break
                print(f'{short} from {path}')
                if base == GAME:
                    os.makedirs(os.path.dirname(cache), exist_ok=True)
                    open(cache, 'wb').write(data)
                return data
    raise SystemExit(f'{resource} not found under {GAME}')


# ------------------------------------------------------------------ layout (.idf)
class Layout:
    def __init__(self, data):
        self.root = pkgfmt.read(data)
        self.scene0 = self.root.get('scenelist')[0]
        self.scene = self.scene0.get('scene')

    def nodes(self, n=None):
        n = n or self.scene
        yield n
        for c in n.get('children'):
            yield from self.nodes(c)

    def node(self, name):
        for n in self.nodes():
            if n.get('name').v == name:
                return n
        raise KeyError(name)

    @staticmethod
    def param(n, key, value=None):
        for p in n.get('params'):
            if p.get('name').v == key:
                if value is not None:
                    p.set('value', value)
                return p.get('value')
        if value is not None:
            n.get('params').append(D([('value', value), ('name', V('str', key))]))
        return value

    def hide(self, name):
        """Moves a stock element (and its children) far off the screen. Its widgets keep running harmlessly."""
        self.move(name, y=60.0)

    def move(self, name, x=None, y=None, sx=None, sy=None):
        n = self.node(name)
        p = list(n.get('position').v)
        s = list(n.get('scale').v)
        if x is not None: p[0] = x
        if y is not None: p[1] = y
        if sx is not None: s[0] = sx
        if sy is not None: s[1] = sy
        n.set('position', V('vec3', tuple(p)))
        n.set('scale', V('vec3', tuple(s)))

    def add_text(self, name, like, x, y, style=None, width=None, layer=None, keep_anim=False, halign=None):
        """A new text element cloned from an existing one. Its id is djb2(name), as the game's top-level
        nodes are, and the name hash joins the scene's sorted lookup."""
        src = self.node(like)
        n = copy.deepcopy(src)
        n.set('name', V('str', name))
        n.set('elementId', V('ui32', djb2(name)))
        n.set('children', L())
        if not keep_anim:
            n.set('anims', L())
        self.param(n, 'guid', V('str', str(uuid.UUID(int=rng.getrandbits(128), version=4))))
        if style: self.param(n, 'style', V('str', style))
        if width is not None: self.param(n, 'boxWidth', V('fp32', float(width)))
        if layer is not None: self.param(n, 'eLayer.layer', V('si32', int(layer)))
        if halign: self.param(n, 'halign', V('str', halign))
        p = list(n.get('position').v)
        p[0], p[1] = x, y
        n.set('position', V('vec3', tuple(p)))
        self._register(n)
        return n

    def _register(self, n):
        name = n.get('name').v
        if any(m.get('name').v == name for m in self.nodes()):
            raise ValueError(f'node {name} exists')
        self.scene.get('children').append(n)
        look = self.scene0.get('nodeidlookup')
        vals = sorted({x.v for x in look} | {djb2(name)})
        self.scene0.set('nodeidlookup', L(V('ui32', v) for v in vals))

    # The plain rectangle every box is drawn with: the stock ERS bar background's mesh, 5.12 x 0.30, lying in
    # the mesh's x/z plane; the node's -90 degree turn about x stands it up, so the rectangle runs from the
    # node's position right by 5.12 and up by 0.30 (the node is its bottom left corner) and the node's scale
    # x / z stretch it. Its colour comes from the node's animation clip (the animated OSD material).
    # The rectangle: the stock white divider line (static lcd_display material, 7.0 x 0.02), stretched. Its
    # colour is the colourOverride of the group widget over it. 0.0.4-0.0.11 used the ERS bar background with
    # an animation clip, but that mesh's own yellow green multiplies the clip colour (blue came out green).
    RECT_LIKE = 'divider_horizontal_4'

    def add_rect(self, name, x, y, w, h, layer=12, like=None, keep_anim=False):
        """A new rectangle with its bottom left corner at (x, y), w wide and h high, drawn with the mesh of the
        stock node `like`. Meshes lie in their x/z plane and the node's turn about x stands them up, so a mesh
        spanning x [x0, x1] and z [z0, z1] covers screen x [px + x0 sx, px + x1 sx], y [py - z1 sz, py - z0 sz]."""
        src = self.node(like or self.RECT_LIKE)
        g = src.get('geoms')[0]
        mn, mx = g.get('extentsmin').v, g.get('extentsmax').v
        n = copy.deepcopy(src)
        n.set('name', V('str', name))
        n.set('elementId', V('ui32', djb2(name)))
        n.set('children', L())
        if not keep_anim:
            n.set('anims', L())
        self.param(n, 'guid', V('str', str(uuid.UUID(int=rng.getrandbits(128), version=4))))
        self.param(n, 'eLayer.layer', V('si32', int(layer)))
        sx, sz = w / (mx[0] - mn[0]), h / (mx[2] - mn[2])
        p = list(n.get('position').v)
        p[0], p[1] = x - mn[0] * sx, y + mx[2] * sz
        n.set('position', V('vec3', tuple(p)))
        s = list(n.get('scale').v)
        s[0], s[2] = sx, sz
        n.set('scale', V('vec3', tuple(s)))
        self._register(n)
        return n

    def encode(self):
        return pkgfmt.assemble(self.root)


# ------------------------------------------------------------------ template (.ui)
class Template:
    def __init__(self, data):
        self.root = pkgfmt.read(data)
        self.lcd = next(t for t in self.root.get('Templates') if t[0].get('name').v == 'lcd_modern')
        self.ids = set()
        self._collect(self.root)

    def _collect(self, c):
        for k, b in (c if isinstance(c, D) else enumerate(c)):
            if isinstance(b, (D, L)):
                self._collect(b)
            elif k == 'id' and b.t == 'ui32':
                self.ids.add(b.v)

    def widget(self, name):
        for w in self.lcd[1]:
            if w[0].get('name') is not None and w[0].get('name').v == name:
                return w
        raise KeyError(name)

    def bind_text(self, widget, dbentry, value=''):
        props = self.widget(widget)[0]
        props.set('text', D([('value', V('str', value)), ('dbentry', V('str', dbentry))]))

    @staticmethod
    def rgba(colour):
        """(r, g, b[, a]) as 0..1 floats or 0..255 ints -> the widget property colourOverride."""
        floats = all(isinstance(x, float) for x in colour)
        c = list(colour) + [1.0 if floats else 255] * (4 - len(colour))
        if floats:
            c = [round(x * 255) for x in c]
        return D([('value', V('rgba', tuple(int(x) for x in c)))])

    def add_label(self, name, node, dbentry, like='speedo_speed', value='', colour=None, visible=None):
        """A label widget (UILB) that shows the text at dbentry in node; colour sets its colourOverride (the
        game's own way to colour an element, used by 313 of its templates); visible a bool leaf."""
        w = copy.deepcopy(self.widget(like))
        props = w[0]
        props.set('id', V('ui32', self._new_id()))
        props.set('name', V('str', name))
        props.set('nameId', V('ui32', djb2(name)))
        props.set('text', D([('value', V('str', value)), ('dbentry', V('str', dbentry))]))
        if colour is not None:
            props.set('colourOverride', self.rgba(colour))
        if visible is not None:
            props.set('visible', D([('value', V('bool', 0)), ('dbentry', V('str', visible))]))
        w[4] = L([L([D([('nodeId', V('ui32', djb2(node))), ('debugName', V('str', node))])])])
        self.lcd[1].append(w)
        return w

    def find_any(self, name):
        """A widget anywhere in the file (any template), by name."""
        def walk(c):
            if isinstance(c, L) and len(c) >= 5 and isinstance(c[0], D) and c[0].get('name') is not None \
                    and c[0].get('name').v == name and c[0].get('type') is not None:
                return c
            for x in (c if isinstance(c, L) else [v for _, v in c]):
                if isinstance(x, (D, L)):
                    r = walk(x)
                    if r is not None:
                        return r
            return None
        r = walk(self.root)
        if r is None:
            raise KeyError(name)
        return r

    def _new_id(self):
        nid = rng.getrandbits(32)
        while nid in self.ids:
            nid = rng.getrandbits(32)
        self.ids.add(nid)
        return nid

    def add_group(self, name, nodes, dbentry=None, negate=False, colour=None, texture=None, texture_db=None):
        """A group widget (UIGP) over nodes: shown while the bool at dbentry is true (false with negate), always
        without one, and tinted with colour (colourOverride). Shapes are the white divider mesh, so the
        override is their colour."""
        w = copy.deepcopy(self.find_any('ers_bar_deploy_used_bg'))
        props = D([('id', V('ui32', self._new_id())), ('version', V('ui32', 0)), ('type', V('ui32', 1346849109))])
        if texture is not None:  # a fixed texture for its nodes (ignored on the LCD: 0.4.10)
            props.insert(0, ('textureOverride', D([('value', V('str', texture))])))
        if texture_db is not None:  # the texture from a leaf, as the stock LCD's 2026 mod icons get theirs
            props.insert(0, ('textureOverride', D([('value', V('str', '')), ('dbentry', V('str', texture_db))])))
        if colour is not None:
            props.append(('colourOverride', self.rgba(colour)))
        if dbentry is None:
            vis = [('value', V('bool', 1))]
        else:
            vis = [('value', V('bool', 0)), ('dbentry', V('str', dbentry))]
        if negate:
            vis.insert(0, ('dbentrynegate', V('bool', 1)))
        props.append(('visible', D(vis)))
        props.append(('nameId', V('ui32', djb2(name))))
        props.append(('name', V('str', name)))
        w[0] = props
        # [4] is a list of node paths (each a list from a top-level node down); one path per node here
        w[4] = L([L([D([('nodeId', V('ui32', djb2(n))), ('debugName', V('str', n))])]) for n in nodes])
        self.lcd[1].append(w)
        return w

    def encode(self):
        return pkgfmt.assemble(self.root)


def stock_frags(resource, rtype):
    for base in (os.path.join(GAME, 'Original_GameData'), GAME):
        path = os.path.join(base, PKGS[0])
        if not os.path.exists(path):
            continue
        f = open(path, 'rb').read()
        for name, t, frags in erp.entries(f):
            if name == resource and t == rtype:
                return [erp.fragment(f, fr) for fr in frags]
    raise SystemExit(f'{resource} not found under {GAME}')


# ------------------------------------------------------------------ the 2026 team logo set
LOGO_FROM = '2025_asset_groups/ui_package/flow_frontend.erp'
LOGO_SOURCE = 'eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.team_logos.f1_2026.texarray.image'
LOGO_IMAGE = 'eaid://ui_package/art/textures/rd_team_logos.tif.image'


def team_logo_keys():
    """The 2026 team logo set's keys in slice order, from the game's overrides.uidb (databases.erp)."""
    path = os.path.join(GAME, '2025_asset_groups', 'ui_package', 'databases.erp')
    f = open(path, 'rb').read()
    for name, t, frags in erp.entries(f):
        if name.endswith('/overrides.uidb'):
            root = pkgfmt.read(b''.join(erp.fragment(f, fr) for fr in frags))
            break
    else:
        raise SystemExit('overrides.uidb not found in ' + path)

    def child(container, name):  # Contents is keyed by name at the top, a list of named entries further in
        c = container.get('Contents')
        if isinstance(c, D):
            return c.get(name)
        return next(x for x in c if x.get('Name').v == name)
    group = child(child(root.get('overrides'), 'team_logos'), 'f1_2026')
    assert group.get('TextureArrayName').v.endswith('overrides.team_logos.f1_2026.texarray')
    return [e.get('Name').v for e in group.get('Contents')]


# ------------------------------------------------------------------ animation clips (colour)
# A clip is an AnimClip resource with two fragments: "temp" (a !pkg ANIM tree: packets of channels such as
# Alpha and DiffuseColour, each pointing into the key data) and "syst" (the key data: little-endian floats).
# The stock static-colour clips (info_lap_time, speedo_value, ...) hold, in order: Alpha times [0, 1], Alpha
# keys, DiffuseColour times [0, 1], DiffuseColour keys. A key component is 5 floats: value, in-handle
# (value, time), out-handle (value, time); a colour key is R, G, B, A components.
CLIP_LIKE = LCD + '/info_lap_time?context=animation'


def clip_keys(rgba, alpha=1.0):
    import struct

    def comp(v, t):  # a flat Bezier key at time t of a 0..1 clip
        return [v, v, max(0.0, t - 1 / 3), v, min(1.0, t + 1 / 3)] if t else [v, v, 0.0, v, 1 / 3]

    def key2(v):  # keys at t=0 and t=1
        return [v, v, 0.0, v, 0.3333], [v, v, 0.6667, v, 1.0]

    fl = [0.0, 1.0]
    a0, a1 = key2(alpha)
    fl += a0 + a1
    fl += [0.0, 1.0]
    k0, k1 = [], []
    for v in rgba:
        x, y = key2(v)
        k0 += x
        k1 += y
    fl += k0 + k1
    assert len(fl) == 54
    return struct.pack('<54f', *fl)


def main():
    lcd = Layout(stock(LCD, 'UILayout'))
    tpl = Template(stock(TPL, 'UIDF'))
    clip_temp, clip_syst = stock_frags(CLIP_LIKE, 'AnimClip')
    assert len(clip_syst) == 216
    clips = {}  # resource -> (temp, syst) to add
    files = {}

    def colour_clip(node, rgba):
        res = f'{LCD}/{node}?context=animation'
        clips[res] = (clip_temp, clip_keys(rgba))
        lcd.node(node).set('anims', L([D([('asset', V('str', res))])]))

    # ---- 0.4.0: the dash layout, from the user's mock-ups (1670 x 942 px; race page and car page)
    # The screen is about x -2.56..2.56, y -1.5..1.5. Positions are given in the mock-up's pixels and converted.
    # Text is centred on its position; a text's scale grows it right from its box's left edge, so centred() places
    # it that much to the left. Colours are colourOverride on the widgets; shapes are the white divider mesh.
    # Values starting with '#ui_menu_data.hud_' are written by the plugin.
    WHITE, GREY, LABEL = (240, 240, 240), (120, 120, 126), (235, 235, 235)
    GREEN, RED, YELLOW = (40, 225, 60), (245, 45, 45), (250, 230, 40)
    BLUE, CYAN, AMBER = (40, 140, 255), (40, 230, 255), (240, 160, 20)
    LINE = (185, 185, 192)
    H = '#ui_menu_data.hud_rdash_'

    def X(px):
        return (px - 835) * 0.003066

    def Y(py):
        return (471 - py) * 0.003185

    def W(px):
        return px * 0.003066

    # The game's own elements stay where they are, under an opaque backdrop: the dash draws on layers above it.
    # (Moving the game's mask and window elements off the screen is avoided; the stock gear digit stays under the
    # backdrop too, the dash has its own.)
    L_BACK, L_SHAPE, L_PART, L_TEXT, L_POP, L_POPTEXT = 50, 55, 56, 60, 64, 66
    lcd.add_rect('rd_backdrop', -3.0, -1.75, 6.0, 3.5, layer=L_BACK)
    tpl.add_group('rd_backdrop_group', ['rd_backdrop'], colour=(6, 6, 8))
    lcd.add_rect('rd_tint_sc', -3.0, -1.75, 6.0, 3.5, layer=L_BACK + 1)
    tpl.add_group('rd_tint_sc_group', ['rd_tint_sc'], '#ui_menu_data.hud_rdash_tint_sc', colour=(70, 54, 0))
    lcd.add_rect('rd_tint_red', -3.0, -1.75, 6.0, 3.5, layer=L_BACK + 1)
    tpl.add_group('rd_tint_red_group', ['rd_tint_red'], '#ui_menu_data.hud_rdash_tint_red', colour=(70, 8, 8))

    def centred(x, width, scale):
        return x - width * (scale - 1.0) / 2.0

    def text(name, px, py, dbentry, colour=WHITE, scale=1.0, style='OSD_24_bold', wpx=330, value='',
             visible=None, page=None, layer=None):
        if page is not None and visible is None:
            visible = H + f'p{page}'  # that page only (a value with its own switch has the page in it)
        w = W(wpx)
        lcd.add_text(name, 'speedo_value', centred(X(px), w, scale), Y(py), width=w,
                     layer=L_TEXT if layer is None else layer, style=style)
        if scale != 1.0:
            lcd.move(name, sx=scale, sy=scale)
        tpl.add_label(f'{name}_w', name, dbentry, value=value, colour=colour, visible=visible)

    def label(name, px, py, value, page=None, visible=None, scale=1.0, colour=LABEL):
        text(name, px, py, H + 'none', colour=colour, style='OSD_14_regular', value=value, wpx=326, scale=scale,
             visible=visible, page=page)

    def box(name, x0, y0, x1, y1, colour, visible=None, layer=None):
        lcd.add_rect(name, X(x0), Y(y1), W(x1 - x0), Y(y0) - Y(y1), layer=L_PART if layer is None else layer)
        tpl.add_group(f'{name}_group', [name], visible, colour=colour)

    def line(name, x0, y0, x1, y1, page=None, t=10):
        vis = H + f'p{page}' if page is not None else None
        if x1 - x0 >= y1 - y0:
            box(name, x0, y0 - t / 2, x1, y0 + t / 2, LINE, vis, L_SHAPE)
        else:
            box(name, x0 - t / 2, y0, x0 + t / 2, y1, LINE, vis, L_SHAPE)

    def outline(name, x0, y0, x1, y1, page=None):
        line(f'{name}_t', x0, y0, x1, y0, page, 6)
        line(f'{name}_b', x0, y1, x1, y1, page, 6)
        line(f'{name}_l', x0, y0, x0, y1, page, 6)
        line(f'{name}_r', x1, y0, x1, y1, page, 6)

    # ---- shared: the bottom row's frame, ERS SOC with the mode badge in the middle cell
    line('rd_h_mid', 30, 590, 1640, 590)
    line('rd_v_b1', 596, 595, 596, 905)
    line('rd_v_b2', 1126, 595, 1126, 905)
    label('rd_l_soc', 733, 630, 'ERS SOC', visible=H + 'mid_show')
    text('rd_soc_y', 865, 735, H + 'soc', YELLOW, scale=1.6, wpx=420, visible=H + 'soc_ok')
    text('rd_soc_r', 865, 735, H + 'soc', RED, scale=1.6, wpx=420, visible=H + 'soc_low')
    text('rd_soc_b', 865, 735, H + 'soc', BLUE, scale=1.6, wpx=420, visible=H + 'soc_b')
    # the mode badge: OVERTAKE (blue, 2026), STRAIGHT MODE (yellow, 2026), DRS (green ready / yellow open, 2025)
    box('rd_badge_g', 625, 815, 1097, 878, GREEN, H + 'badge_g')
    box('rd_badge_p', 625, 815, 1097, 878, (165, 60, 235), H + 'badge_p')
    for nm, x0, y0, x1, y1 in (('t', 625, 812, 1097, 818), ('b', 625, 875, 1097, 881), ('l', 622, 812, 628, 881),
                               ('r', 1094, 812, 1100, 881)):
        box(f'rd_badge_n_{nm}', x0, y0, x1, y1, (150, 150, 158), H + 'badge_n')
    text('rd_badge_text_d', 861, 848, H + 'badge_text', (10, 10, 10), scale=1.05, style='OSD_19_bold', wpx=470,
         visible=H + 'badge_dark', layer=L_TEXT + 1)
    text('rd_badge_text_l', 861, 848, H + 'badge_text', WHITE, scale=1.05, style='OSD_19_bold', wpx=470,
         visible=H + 'badge_light', layer=L_TEXT + 1)

    # the pop-up box, in the bottom middle cell on both pages: blue for a change (ERS mode, fuel mode, brake bias,
    # differential), amber under a safety car / VSC / formation lap and for a yellow flag, red under a red flag
    for tag, colour in (('blue', (10, 92, 255)), ('amber', (240, 160, 0)), ('red', (215, 0, 0))):
        box(f'rd_pop_{tag}', 602, 597, 1120, 902, colour, H + f'pop_{tag}', L_POP)
    for tone, colour in (('light', WHITE), ('dark', (10, 10, 10))):
        text(f'rd_pop_title_{tone}', 861, 660, H + 'pop_title', colour, scale=1.25, style='OSD_14_regular', wpx=500,
             visible=H + f'pop_{tone}', layer=L_POPTEXT)
        text(f'rd_pop_value_{tone}', 861, 775, H + 'pop_value', colour, scale=1.0, style='OSD_24_bold', wpx=500,
             visible=H + f'pop_{tone}', layer=L_POPTEXT)

    # flags over the whole screen (as Wheel Dash had them): red flag, safety car / VSC / formation (amber), yellow,
    # blue flag; black text on amber and yellow, white on red and blue
    L_FULL, L_FULLTEXT = 70, 72
    for tag, colour in (('red', (215, 0, 0)), ('amber', (255, 205, 0)), ('yellow', (250, 225, 0)),
                        ('blue', (20, 100, 255)), ('green', (0, 165, 60)), ('white', (235, 235, 240)),
                        ('chq', (10, 10, 10))):
        box(f'rd_full_{tag}', 0, 0, 1670, 942, colour, H + f'full_{tag}', L_FULL)
    # the chequered flag: 16 x 9 squares, the white ones over the black screen, a black band for the text across
    # the middle. Each square has its own group, as every other shape: 0.4.6/0.4.7 put all 72 in one group and the
    # game drew them grey and all the time (the group's switch and colour reached none of them)
    cw, ch = 1670 / 16, 942 / 9
    for row in range(9):
        for col in range(16):
            if (row + col) % 2 == 0:
                box(f'rd_chq_{row}_{col}', col * cw, row * ch, (col + 1) * cw, (row + 1) * ch, (240, 240, 240),
                    H + 'full_chq', L_FULL + 1)
    box('rd_chq_band', 0, 300, 1670, 680, (10, 10, 10), H + 'full_chq', L_FULL + 1)
    boot_extra = []  # the boot screen's views, materials and bindings (resource, type, [source files])
    L_FULLTEXT = L_FULL + 3  # over the chequered band
    # the boot screen when the player gets the car (3 s): the team's logo on black
    box('rd_boot_bg', 0, 0, 1670, 942, (0, 0, 0), H + 'boot', L_FULL + 5)
    # The team logo: the game's own 2026 team logo set (overrides.team_logos.f1_2026, 272 x 156 a logo, in
    # flow_frontend.erp), copied into the LCD's packages by the launcher (copy-resource: from the player's game,
    # with whatever logos replaced it; nothing of it ships here). One view per slice (an array view of that one
    # slice), a material and binding each (the car's route), one quad each; the plugin shows the player's team's.
    # The slice order is the set's own list in the game's overrides.uidb, read here and written for the plugin.
    # (0.4.15-0.4.19 switched textures with textureOverride, which the LCD ignores in every form.)
    logo_keys = team_logo_keys()
    stock_mat = stock_frags('eaid://ui_package/art/materials/lcd_default.material', 'GfxMaterialRes')[0]
    for k, key in enumerate(logo_keys):
        tag = f'rd_logo_s{k:02d}'
        view_res = f'eaid://ui_package/art/textures/{tag}.tif'
        mat_res = f'eaid://ui_package/art/osd/materials/steering_lcd_display/{tag}.material'
        emb_res = f'eaid://ui_package/art/osd/components/steering_lcd_display/{tag}.emb?context=default'
        files[f'ui/tex/{tag}.view'] = texture.view(LOGO_IMAGE, k)
        files[f'ui/tex/{tag}.material'] = texture.material(stock_mat, view_res, (255, 255, 255))
        files[f'ui/tex/{tag}.emb'] = texture.binding([('mod_icon_bg', mat_res)])
        boot_extra.append((view_res, 'GfxSRVResource', [f'ui/tex/{tag}.view']))
        boot_extra.append((mat_res, 'GfxMaterialRes', [f'ui/tex/{tag}.material']))
        boot_extra.append((emb_res, 'GfxMatBindRes', [f'ui/tex/{tag}.emb']))
        n = lcd.add_rect(tag, X(385), Y(729), W(900), Y(213) - Y(729), layer=L_FULL + 6, like='mod_1_icon_bg')
        n.get('geoms')[0].set('materialid', V('str', emb_res))
        tpl.add_group(f'{tag}_group', [tag], H + f'boot_s{k}')
    hdr = ['// Written by tools/build_ui.py from the game\'s overrides.uidb: the 2026 team logo set\'s keys in slice',
           '// order (overrides.team_logos.f1_2026). The boot screen shows the slice of the player\'s team.',
           '#pragma once', '', 'inline constexpr const char *kTeamLogoKeys[] = {']
    hdr += [f'    "{key}",' for key in logo_keys] + ['};', '']
    open(os.path.join(ROOT, 'native', 'src', 'team_logos.hpp'), 'w', encoding='utf-8').write('\n'.join(hdr))
    for tone, colour in (('dark', (10, 10, 10)), ('light', WHITE)):
        text(f'rd_full_title_{tone}', 835, 400, H + 'full_title', colour, scale=1.9, wpx=1500,
             visible=H + f'full_{tone}', layer=L_FULLTEXT)
        text(f'rd_full_sub_{tone}', 835, 600, H + 'full_sub', colour, scale=1.6, style='OSD_14_regular', wpx=1500,
             visible=H + f'full_{tone}', layer=L_FULLTEXT)

    # the flag strip across the top edge, blinking: yellow (a yellow flag, SC, VSC), blue (blue flag), red (red flag)
    for tag, colour in (('yellow', (250, 210, 0)), ('blue', (20, 110, 255)), ('red', (225, 0, 0))):
        box(f'rd_strip_{tag}', 30, 8, 1640, 30, colour, H + f'strip_{tag}', L_POP)
        box(f'rd_strip_b_{tag}', 30, 912, 1640, 934, colour, H + f'strip_{tag}', L_POP)

    # ---- race page (page 0)
    line('rd_v_t1', 618, 45, 618, 570, 0)
    line('rd_v_t2', 1053, 45, 1053, 570, 0)
    line('rd_h_tl', 35, 300, 605, 300, 0)
    line('rd_v_tl', 297, 325, 297, 565, 0)
    line('rd_h_g', 650, 395, 1020, 395, 0)
    line('rd_h_tr1', 1065, 300, 1635, 300, 0)
    line('rd_h_tr2', 1078, 428, 1635, 428, 0)
    # lap time (running; the finished lap's for 3 s after the line), delta to the reference lap
    label('rd_l_lap_time', 181, 95, 'LAP TIME', page=0)
    text('rd_lap_time', 325, 207, H + 'cur', WHITE, scale=1.39, wpx=560, value='-:--.---', page=0)
    text('rd_l_delta', 1245, 95, H + 'delta_label', LABEL, style='OSD_14_regular',
         value='PREV LAP', page=0, scale=1.0, wpx=326)
    for tag, colour, vis in (('g', GREEN, 'delta_neg'), ('r', RED, 'delta_pos'), ('w', GREY, 'delta_none')):
        text(f'rd_delta_{tag}', 1330, 207, H + 'delta', colour, scale=1.5, wpx=460, value='--.--',
             visible=H + vis)
    # position, lap
    label('rd_l_pos', 106, 355, 'POS', page=0)
    text('rd_pos', 162, 460, H + 'pos', WHITE, scale=1.4, wpx=250, value='-', page=0)
    label('rd_l_lap', 385, 355, 'LAP', page=0)
    text('rd_lap', 462, 460, H + 'lap', WHITE, scale=1.25, wpx=300, value='-', page=0)
    # gear, rpm, speed
    label('rd_l_gear', 838, 62, 'GEAR', page=0)
    text('rd_gear', 838, 245, H + 'gear', WHITE, scale=0.85, style='OSD_80_bold', wpx=330, value='N', page=0)
    text('rd_rpm', 838, 372, H + 'rpm', WHITE, scale=0.8, style='OSD_19_bold', wpx=330, value='0 RPM', page=0)
    text('rd_speed', 838, 505, H + 'speed', WHITE, scale=1.5, wpx=330, value='0', page=0)
    # gaps
    text('rd_l_ahead', 1180, 368, H + 'code_a', LABEL, style='OSD_19_bold', wpx=240, value='AHEAD', page=0)
    text('rd_gap_a', 1480, 368, H + 'gap_a', YELLOW, scale=0.95, wpx=330, value='-', page=0)
    text('rd_l_behind', 1180, 492, H + 'code_b', LABEL, style='OSD_19_bold', wpx=240, value='BEHIND', page=0)
    text('rd_gap_b', 1480, 492, H + 'gap_b', GREEN, scale=0.95, wpx=330, value='-', page=0)
    # tyres: a box each, the corner and its temperature: blue cold / green / red hot from the game's packets,
    # else the game's own formatted temperature (white)
    T_COLD, T_OK, T_HOT = (70, 150, 255), (60, 225, 110), (245, 60, 50)
    for corner, x0, y0, x1, y1, db in (('fl', 47, 650, 303, 745, 'front_left_tyre_temp_formatted'),
                                       ('fr', 323, 650, 570, 745, 'front_right_tyre_temp_formatted'),
                                       ('rl', 47, 765, 303, 860, 'rear_left_tyre_temp_formatted'),
                                       ('rr', 323, 765, 570, 860, 'rear_right_tyre_temp_formatted')):
        outline(f'rd_tbox_{corner}', x0, y0, x1, y1, 0)
        cy = (y0 + y1) / 2
        label(f'rd_l_t{corner}', x0 + 48, cy, corner.upper(), page=0, scale=1.3)
        text(f'rd_t_{corner}', x0 + 165, cy, H + f't_{corner}', GREY, scale=1.2, style='OSD_19_bold', wpx=200,
             value='--',
             visible=H + f'tg_{corner}')
        for tag, colour in (('c', T_COLD), ('n', T_OK), ('h', T_HOT)):
            text(f'rd_t{tag}_{corner}', x0 + 165, cy, H + f't_{corner}', colour, scale=1.2, style='OSD_19_bold',
                 wpx=200, value='--',
                 visible=H + f't{tag}_{corner}')
    # fuel margin: green to spare, red when fuel needs saving (the game's packets), else the game's own value
    label('rd_l_fuel', 1267, 630, 'FUEL', page=0)
    text('rd_fuel', 1390, 765, 'laps_of_fuel_format', WHITE, scale=1.7, wpx=330, value='-', visible=H + 'fuel_game')
    text('rd_fuel_g', 1390, 765, H + 'fuel', GREEN, scale=1.7, wpx=330, value='-', visible=H + 'fuel_ok')
    text('rd_fuel_r', 1390, 765, H + 'fuel', RED, scale=1.7, wpx=330, value='-', visible=H + 'fuel_low')

    # ---- car page (page 1): the user's top-down car (art/car: white parts on a 1671 x 941 canvas, the mock-up's
    # pixels), each part coloured by its damage (green, amber, red; grey until known), the tyres by their wear, the
    # brakes by their damage; the chassis, cockpit and suspension grey.
    # A part is a copy of the stock 2026 mod icon's quad (mesh slot "mod_icon_bg"), stretched over the part's box
    # and given a material binding (.emb) of its own: that slot -> a copy of the stock lcd_default.material with the
    # part's texture and its colour in me_colour, one per colour (the group widgets only show the right one). The
    # launcher's UI art docs give this route; 0.4.9/0.4.10 switched textures with textureOverride, which the LCD
    # ignored (stretched white boxes).
    PART_G, PART_A, PART_R, PART_N = (70, 230, 70), (255, 170, 30), (245, 50, 50), (120, 120, 128)
    BODY, SUSP = (150, 150, 158), (120, 120, 128)
    QUAD, SLOT = 'mod_1_icon_bg', 'mod_icon_bg'
    MAT_DIR = 'eaid://ui_package/art/osd/materials/steering_lcd_display/'
    EMB_DIR = 'eaid://ui_package/art/osd/components/steering_lcd_display/'
    stock_material = stock_frags('eaid://ui_package/art/materials/lcd_default.material', 'GfxMaterialRes')[0]
    from PIL import Image
    textures, materials, bindings = [], [], []  # (resource, type, [source files])

    def car_texture(stem):
        """The part's texture from art/car/<stem>.png, cropped to its shape (2 px spare, 4 px blocks), named so
        its view path has the stock texture's 46 characters; its box in the canvas's pixels."""
        im = Image.open(os.path.join(ROOT, 'art', 'car', stem + '.png')).convert('RGBA')
        alpha = im.getchannel('A')
        bx0, by0, bx1, by1 = alpha.getbbox()
        x0, y0 = max(0, bx0 - 2), max(0, by0 - 2)
        w, h = (bx1 + 2 - x0 + 3) // 4 * 4, (by1 + 2 - y0 + 3) // 4 * 4
        crop = alpha.crop((x0, y0, x0 + w, y0 + h))  # past the canvas edge reads as 0
        tag = f'rd_car_p_{len(textures) // 2:02d}'
        view_res = f'eaid://ui_package/art/textures/{tag}.tif'
        image_res = view_res + '.image'
        rel = 'ui/tex/' + tag
        files[rel + '.head'] = texture.surface_head(w, h)
        files[rel + '.pixels'] = texture.bc7_white(crop.tobytes(), w, h)
        files[rel + '.view'] = texture.view(image_res)
        textures.append((image_res, 'GfxSurfaceRes', [rel + '.head', rel + '.pixels']))
        textures.append((view_res, 'GfxSRVResource', [rel + '.view']))
        return tag, view_res, (x0, y0, x0 + w, y0 + h)

    def car_quad(name, view_res, box, colour, visible, layer):
        """A quad over box drawing the texture in colour, shown by the leaf visible."""
        x0, y0, x1, y1 = box
        mat_res = MAT_DIR + name + '.material'
        emb_res = EMB_DIR + name + '.emb?context=default'
        files[f'ui/tex/{name}.material'] = texture.material(stock_material, view_res, colour)
        files[f'ui/tex/{name}.emb'] = texture.binding([(SLOT, mat_res)])
        materials.append((mat_res, 'GfxMaterialRes', [f'ui/tex/{name}.material']))
        bindings.append((emb_res, 'GfxMatBindRes', [f'ui/tex/{name}.emb']))
        n = lcd.add_rect(name, X(x0), Y(y1), W(x1 - x0), Y(y0) - Y(y1), layer=layer, like=QUAD)
        n.get('geoms')[0].set('materialid', V('str', emb_res))
        tpl.add_group(f'{name}_group', [name], visible)

    def car_part(stem, key, layer):
        """A damage part: a copy per colour, each shown by its own leaf (c<g|a|r|n>_<key>)."""
        tag, view_res, box = car_texture(stem)
        for c, colour in (('g', PART_G), ('a', PART_A), ('r', PART_R), ('n', PART_N)):
            car_quad(f'{tag}_{c}', view_res, box, colour, H + f'c{c}_{key}', layer)

    def car_fixed(stem, colour, layer):
        """A part not coloured by damage: shown on the car page."""
        tag, view_res, box = car_texture(stem)
        car_quad(f'{tag}_f', view_res, box, colour, H + 'p1', layer)

    # bottom to top
    car_fixed('suspension', SUSP, 52)
    for side in ('left', 'right'):
        car_part(f'floor_edge_{side}', 'floor', 53)
        car_part(f'aero_vanes_{side}', 'floor', 54)
        car_part(f'sidepod_{side}', 'side', 54)
        car_part(f'sidepod_inlet_{side}', 'side', 55)
    car_part('engine_cover', 'eng', 55)
    car_part('gearbox', 'gbox', 55)
    car_part('rear_diffuser', 'diff', 55)
    car_fixed('chassis_and_nose', BODY, 55)
    car_fixed('cockpit_halo', BODY, 56)
    car_part('rear_wing', 'rw', 56)
    car_part('front_wing_left', 'fwl', 56)
    car_part('front_wing_right', 'fwr', 56)
    for corner, stem in (('fl', 'front_left'), ('fr', 'front_right'), ('rl', 'rear_left'), ('rr', 'rear_right')):
        car_part(f'tyre_{stem}', f't{corner}', 56)
        car_part(f'brake_{stem}', f'b{corner}', 57)
    # bottom left: fuel mode | differential; bottom right: brake bias
    line('rd_v_cb', 330, 650, 330, 850, 1)
    label('rd_l_mix', 180, 678, 'FUEL MODE', page=1)
    text('rd_mix', 170, 785, H + 'mix', WHITE, scale=1.35, wpx=280, value='-', page=1)
    label('rd_l_diff', 465, 678, 'DIFF', page=1)
    text('rd_diff', 470, 785, H + 'diff', WHITE, scale=1.35, wpx=260, value='-', page=1)
    label('rd_l_bbal', 1312, 640, 'BRAKE BIAS', page=1)
    text('rd_bbal', 1290, 765, H + 'bbal', WHITE, scale=1.8, wpx=300, value='-', page=1)
    label('rd_l_bbal_front', 1290, 862, 'FRONT', page=1)

    # ---- pit page (page 2), the user's mock-up: pit window | current tyres; rejoin position | front wing damage;
    # bottom left the tyre wear, bottom right the last pit stop's time. (The mock-up's next tyres, downforce level
    # and wing repair are the MFD's pit plan, which nothing outside the MFD can read.)
    line('rd_p2_v', 835, 30, 835, 570, 2)
    line('rd_p2_h', 35, 323, 1635, 323, 2)
    label('rd_l_pw', 250, 62, 'PIT WINDOW', page=2)
    label('rd_l_pw_laps', 440, 122, 'LAPS', page=2)
    text('rd_pw_w', 440, 225, H + 'pw', WHITE, scale=1.8, wpx=620, value='-', visible=H + 'pw_n')
    text('rd_pw_g', 440, 225, H + 'pw', GREEN, scale=1.8, wpx=620, value='-', visible=H + 'pw_g')
    label('rd_l_tyre', 1075, 62, 'CURRENT TYRES', page=2)
    for tag, colour in (('s', (245, 45, 45)), ('m', (250, 225, 0)), ('h', WHITE), ('i', (60, 200, 80)),
                        ('w', (40, 120, 255)), ('n', GREY)):
        text(f'rd_tyre_{tag}', 1240, 190, H + 'tyre', colour, scale=1.6, wpx=760, value='-', visible=H + f'ty_{tag}')
    text('rd_tyre_age', 1240, 282, H + 'tyre_age', WHITE, style='OSD_19_bold', wpx=600, value='-', page=2)
    label('rd_l_rejoin', 290, 368, 'REJOIN POSITION', page=2)
    text('rd_rejoin', 440, 475, H + 'rejoin', WHITE, scale=1.8, wpx=400, value='-', page=2)
    label('rd_l_wing', 1110, 368, 'FRONT WING DAMAGE', page=2)
    label('rd_l_wing_lr', 1240, 418, 'LEFT / RIGHT', page=2, colour=GREY)
    for tag, colour in (('g', GREEN), ('a', AMBER), ('r', RED), ('n', GREY)):
        text(f'rd_wing_{tag}', 1240, 500, H + 'wing', colour, scale=1.3, wpx=760, value='-', visible=H + f'wing_{tag}')
    # tyre wear: a box per corner, the % coloured by the wear limits in the settings
    for corner, x0, y0, x1, y1 in (('fl', 47, 650, 303, 745), ('fr', 323, 650, 570, 745),
                                   ('rl', 47, 765, 303, 860), ('rr', 323, 765, 570, 860)):
        outline(f'rd_wbox_{corner}', x0, y0, x1, y1, 2)
        cy = (y0 + y1) / 2
        label(f'rd_l_w{corner}', x0 + 48, cy, corner.upper(), page=2, scale=1.3)
        for tag, colour in (('g', GREEN), ('a', YELLOW), ('r', RED), ('n', GREY)):
            text(f'rd_w{tag}_{corner}', x0 + 170, cy, H + f'w_{corner}', colour, scale=0.78, wpx=180, value='--',
                 visible=H + f'w{tag}_{corner}')
    # the last pit stop's time stationary
    label('rd_l_pst', 1383, 640, 'PIT STOP TIME', page=2)
    text('rd_pst', 1360, 765, H + 'pst', WHITE, scale=1.7, wpx=330, value='-', page=2)
    label('rd_l_pst_s', 1590, 800, 's', page=2, scale=1.4, visible=H + 'pst_on')

    os.makedirs(os.path.join(ROOT, 'ui', 'clips'), exist_ok=True)
    os.makedirs(os.path.join(ROOT, 'ui', 'tex'), exist_ok=True)
    open(os.path.join(ROOT, 'ui', 'lcd_display.idf'), 'wb').write(lcd.encode())
    open(os.path.join(ROOT, 'ui', 'race_assets_f1.ui'), 'wb').write(tpl.encode())
    for rel, data in files.items():
        open(os.path.join(ROOT, rel), 'wb').write(data)
    for res, (temp, syst) in clips.items():
        stem = res.split('/')[-1].split('?')[0]
        open(os.path.join(ROOT, 'ui', 'clips', stem + '.temp'), 'wb').write(temp)
        open(os.path.join(ROOT, 'ui', 'clips', stem + '.syst'), 'wb').write(syst)
    ops = []
    for pkg in PKGS:
        ops.append({'op': 'replace-resource', 'erp': pkg, 'resource': LCD, 'source': 'ui/lcd_display.idf'})
        ops.append({'op': 'replace-resource', 'erp': pkg, 'resource': TPL, 'source': 'ui/race_assets_f1.ui'})
        for res in clips:
            stem = res.split('/')[-1].split('?')[0]
            ops.append({'op': 'add-resource', 'erp': pkg, 'resource': res, 'type': 'AnimClip',
                        'sources': [f'ui/clips/{stem}.temp', f'ui/clips/{stem}.syst']})
        # the team logo set, copied from the player's game (before the views on it)
        ops.append({'op': 'copy-resource', 'erp': pkg, 'from_erp': LOGO_FROM, 'resource': LOGO_SOURCE,
                    'as': LOGO_IMAGE})
        # the car: textures (image, view), materials, material bindings, each after a stock resource of its type
        for res, rtype, sources in textures + materials + bindings + boot_extra:
            ops.append({'op': 'add-resource', 'erp': pkg, 'resource': res, 'type': rtype, 'sources': sources})
    os.makedirs(os.path.join(ROOT, 'erp'), exist_ok=True)
    json.dump({'schema_version': 1, 'ops': ops}, open(os.path.join(ROOT, 'erp', 'cockpit_hud.json'), 'w'), indent=2)
    # sanity: both files read back
    pkgfmt.read(lcd.encode())
    pkgfmt.read(tpl.encode())
    print('wrote ui/lcd_display.idf, ui/race_assets_f1.ui, erp/cockpit_hud.json')


if __name__ == '__main__':
    main()
