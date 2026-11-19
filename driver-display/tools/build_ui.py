# Builds the dash's UI files from the game's own stock files:
#   ui/lcd_display.idf    the wheel LCD layout (UILayout)
#   ui/race_assets_f1.ui  the race HUD template that feeds it (UIDF)
# and erp/driver_display.json, the launcher ops that swap them into the game's packages.
#
# Usage: python tools/build_ui.py ["<F1 25 install folder>"]
# Reads the untouched originals from <install>/Original_GameData, falling back to the live packages.
import copy, json, math, os, random, sys, uuid

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
def near_white(c):
    return min(c[:3]) >= 220


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
        # white text needs no override (the game colours only the first ~128 widgets: see order_widgets)
        props[:] = [(k, v) for k, v in props if k != 'colourOverride']
        if colour is not None and not near_white(colour):
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
        if colour is not None and not near_white(colour):  # the divider mesh is white already
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


def order_widgets(tpl):
    """The game applies colourOverride to the first ~128 widgets of the template only (0.4.33: the BEHIND gap at
    widget 122 coloured, the tyre temperatures from 126 on and everything after white). The game's own widgets
    stay first; then ours with a colour (the race page's before the rest as built, every car's before the Ferrari
    copies), then the rest."""
    ws = list(tpl.lcd[1])
    ours = [w for w in ws if w[0].get('name') is not None and w[0].get('name').v.startswith('rd_')]
    stock = [w for w in ws if not any(w is o for o in ours)]

    def coloured(w):
        return w[0].get('colourOverride') is not None

    def ferrari_copy(w):
        return w[0].get('name').v.endswith('_f_w')
    col = [w for w in ours if coloured(w) and not ferrari_copy(w)]
    col_f = [w for w in ours if coloured(w) and ferrari_copy(w)]
    rest = [w for w in ours if not coloured(w)]
    tpl.lcd[1][:] = stock + col + col_f + rest
    last = len(stock) + len(col) - 1
    last_f = last + len(col_f)
    print(f'colour widgets: every car\'s up to widget {last}, the Ferrari copies up to {last_f} (colour reaches ~128)')
    if last >= 120:
        raise SystemExit('the coloured widgets reach past the ~128 the game colours: cut some')


def order_nodes(lcd, tpl):
    """Colour overrides reach the first nodes of the layout only, about the stock layout's 133 (0.4.36: the BEHIND
    gap at node 131 coloured, the tyre temperatures from node 135 white, whatever the widget order). The nodes the
    coloured widgets drive come first, in the widgets' order (every car's, then the Ferrari copies)."""
    kids = list(lcd.scene.get('children'))
    by_name = {k.get('name').v: k for k in kids}
    first, seen = [], set()
    last_every = -1
    for w in tpl.lcd[1]:
        if w[0].get('colourOverride') is None or w[0].get('name') is None:
            continue
        for path in w[4]:
            for d in path:
                nm = d.get('debugName').v
                if nm in by_name and nm not in seen:
                    seen.add(nm)
                    first.append(by_name[nm])
                    if not w[0].get('name').v.endswith('_f_w'):
                        last_every = len(first)
    stock = [k for k in kids if not k.get('name').v.startswith('rd_')]
    rest = [k for k in kids if k.get('name').v.startswith('rd_') and k.get('name').v not in seen]
    lcd.scene.set('children', L(stock + first + rest))
    print(f'colour nodes: every car\'s up to node {len(stock) + last_every - 1}, the Ferrari copies up to '
          f'{len(stock) + len(first) - 1} (colour reaches 128: nodes 0-127)')
    # The limit taken as 128 (a power of two; 2026-11-19 photos: node 113 coloured, 135 white, in the file as
    # written). Before 1.0.19 this printout described an order the written file never had.
    if len(stock) + len(first) - 1 >= 128:
        raise SystemExit('the coloured nodes (the Ferrari copies too) reach past node 127: cut some')


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

    # ---- the 2026 Ferrari's wheel: its screen bends the display (two panels each way, hinged at the centre
    # lines). There the text and lines are drawn from copies placed against the bend (tools/ferrari_2026_warp.json,
    # measured from a calibration grid screenshot): every text has a copy at its corrected spot, every switch over
    # them comes in two, <name>_n (any other car) and <name>_f (the Ferrari), which the plugin writes from <name>.
    warp = json.load(open(os.path.join(ROOT, 'tools', 'ferrari_2026_warp.json')))
    wxs, wys, wdx, wdy = warp['xs'], warp['ys'], warp['dx'], warp['dy']
    import bisect

    def ferrari(px, py):
        """Where to draw a canvas point on the Ferrari's screen (bilinear in the table)."""
        px, py = min(max(px, 0.0), 1670.0), min(max(py, 0.0), 942.0)
        i = min(max(bisect.bisect_right(wxs, px) - 1, 0), len(wxs) - 2)
        k = min(max(bisect.bisect_right(wys, py) - 1, 0), len(wys) - 2)
        tx = (px - wxs[i]) / (wxs[i + 1] - wxs[i])
        ty = (py - wys[k]) / (wys[k + 1] - wys[k])

        def at(t):
            return ((1 - ty) * ((1 - tx) * t[k][i] + tx * t[k][i + 1]) +
                    ty * ((1 - tx) * t[k + 1][i] + tx * t[k + 1][i + 1]))
        return px + at(wdx), py + at(wdy)

    split = set()  # the switches the plugin writes as <name>_n / <name>_f

    def split_leaf(visible):
        leaf = (visible or H + 'all')[len(H):]
        assert (visible or H).startswith(H), visible
        split.add(leaf)
        return H + leaf + '_n', H + leaf + '_f'

    def text(name, px, py, dbentry, colour=WHITE, scale=1.0, style='OSD_24_bold', wpx=330, value='',
             visible=None, page=None, layer=None):
        if page is not None and visible is None:
            visible = H + f'p{page}'  # that page only (a value with its own switch has the page in it)
        if colour == GREY:  # the "unknown" grey: plain white text (no colour override to spend)
            colour = None
        w = W(wpx)
        vis_n, vis_f = split_leaf(visible)
        for nm, (x, y), vis in ((name, (px, py), vis_n), (name + '_f', ferrari(px, py), vis_f)):
            n = lcd.add_text(nm, 'speedo_value', centred(X(x), w, scale), Y(y), width=w,
                             layer=L_TEXT if layer is None else layer, style=style)
            if scale != 1.0:
                lcd.move(nm, sx=scale, sy=scale)
            if nm.endswith('_f'):
                # the Ferrari copy turned against the screen's tilt where it sits (each of the wheel's four screen
                # panels is tilted a little): the tilt of a level line through the text's middle, across its width
                # (a text over the centre crease gets the average). Its scale grows it from its box's left edge,
                # taken as the turn's pivot too: the node moves so the text's middle stays put.
                half = min(wpx * scale, 500) / 2
                (ax, ay), (bx, by) = ferrari(px - half, py), ferrari(px + half, py)
                theta = -math.atan2(by - ay, bx - ax)  # canvas y runs down, the node's up
                if abs(theta) > 0.002:
                    n.set('rotation', V('quat', (0.0, 0.0, math.sin(theta / 2), math.cos(theta / 2), )))
                    r = w * scale / 2
                    p = list(n.get('position').v)
                    p[0] += r * (1 - math.cos(theta))
                    p[1] -= r * math.sin(theta)
                    n.set('position', V('vec3', tuple(p)))
            tpl.add_label(f'{nm}_w', nm, dbentry, value=value, colour=colour, visible=vis)

    def label(name, px, py, value, page=None, visible=None, scale=1.0, colour=LABEL):
        text(name, px, py, H + 'none', colour=colour, style='OSD_14_regular', value=value, wpx=326, scale=scale,
             visible=visible, page=page)

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

    part_stems = []  # the damage parts, for the grey base car

    def car_part(stem, key, layer):
        """A damage part: a copy per colour, each shown by its own leaf (c<g|a|r>_<key>). While its damage is
        unknown none shows, and the grey base car under the parts shows through (1.0.15: a grey copy of each part
        took 22 of the display's 512 nodes)."""
        tag, view_res, box = car_texture(stem)
        part_stems.append(stem)
        for c, colour in (('g', PART_G), ('a', PART_A), ('r', PART_R)):
            car_quad(f'{tag}_{c}', view_res, box, colour, H + f'c{c}_{key}', layer)

    def car_fixed(stem, colour, layer):
        """A part not coloured by damage: shown on the car page."""
        tag, view_res, box = car_texture(stem)
        car_quad(f'{tag}_f', view_res, box, colour, H + 'p1', layer)

    def overlay_texture(tag, alpha):
        """A full-canvas texture from an alpha picture (836 x 472, half the canvas: 4 px blocks), named tag (11
        characters, so its view path has the stock texture's 46)."""
        assert len(tag) == 11 and alpha.size == (836, 472)
        view_res = f'eaid://ui_package/art/textures/{tag}.tif'
        image_res = view_res + '.image'
        rel = 'ui/tex/' + tag
        files[rel + '.head'] = texture.surface_head(836, 472)
        files[rel + '.pixels'] = texture.bc7_white(alpha.tobytes(), 836, 472)
        files[rel + '.view'] = texture.view(image_res)
        textures.append((image_res, 'GfxSurfaceRes', [rel + '.head', rel + '.pixels']))
        textures.append((view_res, 'GfxSRVResource', [rel + '.view']))
        return view_res

    def white_texture():
        """An 8 x 8 white texture (once), for the boxes: a material-coloured quad over it draws a box."""
        tag = 'rd_ovl_wht0'
        view_res = f'eaid://ui_package/art/textures/{tag}.tif'
        if not any(t[0] == view_res for t in textures):
            image_res = view_res + '.image'
            rel = 'ui/tex/' + tag
            files[rel + '.head'] = texture.surface_head(8, 8)
            files[rel + '.pixels'] = texture.bc7_white(bytes([255]) * 64, 8, 8)
            files[rel + '.view'] = texture.view(image_res)
            textures.append((image_res, 'GfxSurfaceRes', [rel + '.head', rel + '.pixels']))
            textures.append((view_res, 'GfxSRVResource', [rel + '.view']))
        return view_res

    def box(name, x0, y0, x1, y1, colour, visible=None, layer=None):
        """A box: a quad over the white texture with its colour in the material (1.0.16; a rectangle with a
        colourOverride took one of the ~133 nodes the override reaches)."""
        car_quad(name, white_texture(), (x0, y0, x1, y1), colour, visible, L_PART if layer is None else layer)

    # the lines: drawn into textures per group (shared, each page, the badge outline), one straight and one
    # against the Ferrari's bend, instead of a rectangle each (49 rectangles: the LCD holds 512 nodes at most)
    lines = {'sha': [], 'pg0': [], 'pgr': [], 'pgq': [], 'pg1': [], 'pg2': [], 'bdg': []}

    def line(name, x0, y0, x1, y1, page=None, t=10):
        group = 'sha' if page is None else f'pg{page}'
        if x1 - x0 >= y1 - y0:
            lines[group].append((x0, y0, x1, y0, t))
        else:
            lines[group].append((x0, y0, x0, y1, t))

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
    lines['bdg'] += [(622, 815, 1100, 815, 6), (622, 878, 1100, 878, 6), (625, 812, 625, 881, 6),
                     (1097, 812, 1097, 881, 6)]  # the badge's grey outline (badge_n)
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

    # under a safety car / VSC, after its full screen: the bottom row turns amber with what is out (SAFETY CAR, VSC,
    # VSC ENDING, SC IN THIS LAP, SLOW DOWN), so gear, speed and the SC delta stay readable above it (1.0.4)
    box('rd_sc_bar', 30, 595, 1640, 905, (255, 205, 0), H + 'sc_bar', L_POPTEXT + 1)
    text('rd_sc_bar_text', 835, 750, H + 'sc_bar_text', (10, 10, 10), scale=1.9, wpx=1500, value='SAFETY CAR',
         visible=H + 'sc_bar', layer=L_POPTEXT + 2)

    # race messages across the bottom row (1.0.22: backmarker, pit window, rain, retirements, contact; the ERS SOC
    # cell's pop-up was too narrow for them and stays for setting changes): blue with white text for information,
    # amber with black text for incidents
    box('rd_row_blue', 30, 595, 1640, 905, (10, 92, 255), H + 'row_blue', L_POPTEXT + 1)
    box('rd_row_amber', 30, 595, 1640, 905, (240, 160, 0), H + 'row_amber', L_POPTEXT + 1)
    text('rd_row_text_l', 835, 750, H + 'row_text', WHITE, scale=1.35, wpx=1500, value='-',
         visible=H + 'row_light', layer=L_POPTEXT + 2)
    text('rd_row_text_d', 835, 750, H + 'row_text', (10, 10, 10), scale=1.35, wpx=1500, value='-',
         visible=H + 'row_dark', layer=L_POPTEXT + 2)

    # the fastest lap of the race (1.0.21): the bottom row purple with the driver and the time for 5 s
    box('rd_fl_bar', 30, 595, 1640, 905, (150, 60, 225), H + 'fl_bar', L_POPTEXT + 1)
    text('rd_fl_text', 835, 750, H + 'fl_text', WHITE, scale=1.35, wpx=1500, value='FASTEST LAP',
         visible=H + 'fl_bar', layer=L_POPTEXT + 2)

    # flags over the whole screen (as Wheel Dash had them): red flag, safety car / VSC / formation (amber), yellow,
    # blue flag; black text on amber and yellow, white on red and blue
    L_FULL, L_FULLTEXT = 70, 72
    for tag, colour in (('red', (215, 0, 0)), ('amber', (255, 205, 0)), ('yellow', (250, 225, 0)),
                        ('blue', (20, 100, 255)), ('green', (0, 165, 60)), ('white', (235, 235, 240)),
                        ('chq', (10, 10, 10)), ('black', (2, 2, 2))):
        box(f'rd_full_{tag}', 0, 0, 1670, 942, colour, H + f'full_{tag}', L_FULL)
    # the chequered flag: the squares are one texture (drawn with the car's textures, below), a black band for the
    # text across the middle over them
    box('rd_chq_band', 0, 300, 1670, 680, (10, 10, 10), H + 'full_chq', L_FULL + 2)
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
    # Two quads a slice: the copy (rd_logo_s, the game's own set as shipped) underneath, and over it a view of the
    # set by its own name (rd_logo_g), which the game resolves to the loaded one: a logo mod's set when one is in
    # (the launcher's importer writes them into vfx_package/common.erp, which the copy never sees: 0.4.29 showed
    # the stock Cadillac under a Corvette mod), else the game's. A logo card is opaque, so the by-name one covers
    # the copy wherever it resolves.
    for k, key in enumerate(logo_keys):
        for kind, image, layer in (('s', LOGO_IMAGE, L_FULL + 6), ('g', LOGO_SOURCE, L_FULL + 7)):
            tag = f'rd_logo_{kind}{k:02d}'
            view_res = f'eaid://ui_package/art/textures/{tag}.tif'
            mat_res = f'eaid://ui_package/art/osd/materials/steering_lcd_display/{tag}.material'
            emb_res = f'eaid://ui_package/art/osd/components/steering_lcd_display/{tag}.emb?context=default'
            files[f'ui/tex/{tag}.view'] = texture.view(image, k)
            files[f'ui/tex/{tag}.material'] = texture.material(stock_mat, view_res, (255, 255, 255))
            files[f'ui/tex/{tag}.emb'] = texture.binding([('mod_icon_bg', mat_res)])
            boot_extra.append((view_res, 'GfxSRVResource', [f'ui/tex/{tag}.view']))
            boot_extra.append((mat_res, 'GfxMaterialRes', [f'ui/tex/{tag}.material']))
            boot_extra.append((emb_res, 'GfxMatBindRes', [f'ui/tex/{tag}.emb']))
            n = lcd.add_rect(tag, X(385), Y(729), W(900), Y(213) - Y(729), layer=layer, like='mod_1_icon_bg')
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

    # the chequered flag's own title and line, lower than the other full screens' (1.0.6: at 400 the title sat
    # against the top squares; the band runs 300-680)
    text('rd_chq_title', 835, 440, H + 'full_title', WHITE, scale=1.9, wpx=1500, visible=H + 'full_chq_text',
         layer=L_FULLTEXT)
    text('rd_chq_sub', 835, 610, H + 'full_sub', WHITE, scale=1.6, style='OSD_14_regular', wpx=1500,
         visible=H + 'full_chq_text', layer=L_FULLTEXT)

    # the start lights (1.0.21): black over everything, the five lights coming on one by one (the game's start light
    # events), then a green GO (the full screen, from the plugin) at lights out. One circle texture, five quads.
    def circle_texture():
        from PIL import ImageDraw
        tag = 'rd_ovl_cir0'
        view_res = f'eaid://ui_package/art/textures/{tag}.tif'
        image_res = view_res + '.image'
        rel = 'ui/tex/' + tag
        size = 128
        disc = Image.new('L', (size * 4, size * 4), 0)
        ImageDraw.Draw(disc).ellipse([8, 8, size * 4 - 9, size * 4 - 9], fill=255)
        disc = disc.resize((size, size), Image.LANCZOS)
        files[rel + '.head'] = texture.surface_head(size, size)
        files[rel + '.pixels'] = texture.bc7_white(disc.tobytes(), size, size)
        files[rel + '.view'] = texture.view(image_res)
        textures.append((image_res, 'GfxSurfaceRes', [rel + '.head', rel + '.pixels']))
        textures.append((view_res, 'GfxSRVResource', [rel + '.view']))
        return view_res
    box('rd_lt_bg', 0, 0, 1670, 942, (0, 0, 0), H + 'lights', L_FULL + 4)
    circle = circle_texture()
    for i in range(5):
        cx = 835 + (i - 2) * 300
        car_quad(f'rd_lt{i + 1}', circle, (cx - 120, 351, cx + 120, 591), (235, 20, 20), H + f'lt{i + 1}', L_FULL + 5)

    # the flag strip across the top edge, blinking: yellow (a yellow flag, SC, VSC), blue (blue flag), red (red flag)
    # (one texture a colour with both bars since 1.0.17: two boxes a colour took 3 more of the 512 nodes)
    from PIL import ImageDraw
    bars = Image.new('L', (836, 472), 0)
    dd = ImageDraw.Draw(bars)
    dd.rectangle([15, 4, 819, 14], fill=255)    # canvas 30..1640 x 8..30, halved
    dd.rectangle([15, 456, 819, 466], fill=255)  # canvas 30..1640 x 912..934, halved
    for tag, colour in (('yellow', (250, 210, 0)), ('blue', (20, 110, 255)), ('red', (225, 0, 0))):
        stag = f'rd_ovl_st{tag[0]}0'
        car_quad(f'rd_strip_{tag}', overlay_texture(stag, bars), (0, 0, 1670, 942), colour, H + f'strip_{tag}', L_POP)

    # ---- race page (page 0)
    line('rd_v_t1', 618, 45, 618, 570, 0)
    line('rd_v_t2', 1053, 45, 1053, 570, 0)
    line('rd_h_tl', 35, 300, 605, 300, 0)
    line('rd_v_tl', 297, 325, 297, 565, 0)
    line('rd_h_g', 650, 395, 1020, 395, 0)
    # page 0 has two layouts since 1.0.16: race ('r') and practice / qualifying / time trial ('q'), the same lines
    # but the right column's (the race: the delta over the gaps; qualifying: three sectors evenly spaced)
    line('rd_h_tr1', 1065, 300, 1635, 300, 'r')
    line('rd_h_tr2', 1078, 428, 1635, 428, 'r')
    line('rd_h_tq1', 1075, 218, 1635, 218, 'q')
    line('rd_h_tq2', 1075, 385, 1635, 385, 'q')
    # lap time (running; the finished lap's for 3 s after the line), delta to the reference lap
    label('rd_l_lap_time', 181, 95, 'LAP TIME', page=0)
    text('rd_lap_time', 325, 207, H + 'cur', WHITE, scale=1.39, wpx=560, value='-:--.---', page=0)
    text('rd_l_delta', 1245, 95, H + 'delta_label', LABEL, style='OSD_14_regular',
         value='PREV LAP', visible=H + 'p0r', scale=1.0, wpx=326)
    for tag, colour, vis in (('g', GREEN, 'delta_neg'), ('r', RED, 'delta_pos')):  # no delta: "--.--" in green
        text(f'rd_delta_{tag}', 1330, 207, H + 'delta', colour, scale=1.5, wpx=460, value='--.--',
             visible=H + vis)
    # position, lap
    label('rd_l_pos', 106, 355, 'POS', page=0)
    text('rd_pos', 162, 460, H + 'pos', WHITE, scale=1.4, wpx=250, value='-', page=0)
    label('rd_l_lap', 385, 355, 'LAP', visible=H + 'p0r')
    text('rd_lap', 462, 460, H + 'lap', WHITE, scale=1.25, wpx=300, value='-', visible=H + 'p0r')
    # gear, rpm, speed
    label('rd_l_gear', 838, 62, 'GEAR', page=0)
    text('rd_gear', 838, 245, H + 'gear', WHITE, scale=0.85, style='OSD_80_bold', wpx=330, value='N', page=0)
    text('rd_rpm', 838, 372, H + 'rpm', WHITE, scale=0.8, style='OSD_19_bold', wpx=330, value='0 RPM', page=0)
    text('rd_speed', 838, 505, H + 'speed', WHITE, scale=1.5, wpx=330, value='0', page=0)
    # gaps
    text('rd_l_ahead', 1205, 368, H + 'code_a', LABEL, style='OSD_19_bold', scale=0.9, wpx=240, value='AHEAD',
         visible=H + 'p0r')
    text('rd_gap_a', 1480, 368, H + 'gap_a', YELLOW, scale=0.95, wpx=330, value='-', visible=H + 'gap_a_on')
    text('rd_l_behind', 1205, 492, H + 'code_b', LABEL, style='OSD_19_bold', scale=0.9, wpx=240, value='BEHIND',
         visible=H + 'p0r')
    text('rd_gap_b', 1480, 492, H + 'gap_b', GREEN, scale=0.95, wpx=330, value='-', visible=H + 'gap_b_on')

    # practice / qualifying / time trial (1.0.16, the user's mock-up): sector times on the right (purple the best of
    # every car, green the player's best, yellow neither), the delta to the best lap under the lap time where the
    # race has LAP n/total, and the session's time left where the race has the fuel margin
    PURPLE = (175, 85, 245)
    for k, sy in ((1, 140), (2, 300), (3, 465)):
        text(f'rd_l_s{k}', 1133, sy, H + 'none', WHITE, style='OSD_19_bold', scale=1.4, wpx=200, value=f'S{k}',
             visible=H + 'p0q')
        for tag, colour in (('p', PURPLE), ('g', GREEN), ('y', YELLOW)):
            text(f'rd_s{k}_{tag}', 1452, sy + 10, H + f's{k}', colour, scale=1.5, wpx=400, value='-',
                 visible=H + f's{k}_{tag}')
    label('rd_l_qd', 430, 355, 'TO PB', visible=H + 'p0q')
    for tag, colour, vis in (('g', GREEN, 'qd_neg'), ('r', RED, 'qd_pos')):  # no best lap: "--.--" in green
        text(f'rd_qd_{tag}', 455, 460, H + 'qdelta', colour, scale=1.05, wpx=290, value='--.--', visible=H + vis)
    text('rd_l_sess', 1383, 630, H + 'sess_label', LABEL, style='OSD_14_regular', value='TIME LEFT', wpx=460,
         visible=H + 'p0q')
    text('rd_sess', 1383, 765, H + 'sess_left', WHITE, scale=1.7, wpx=330, value='--:--', visible=H + 'p0q')
    # tyres: a box each, the corner and its temperature: blue cold / green / red hot from the game's packets,
    # else the game's own formatted temperature (white)
    # bigger since 0.4.34 (the degree sign was hard to read): the boxes fill the bottom left cell. White since 1.0:
    # the compound windows are a guide's guess until the launcher exposes the game's own (1.10), see tyre_window
    for corner, x0, y0, x1, y1 in (('fl', 40, 618, 310, 755), ('fr', 326, 618, 588, 755),
                                   ('rl', 40, 768, 310, 900), ('rr', 326, 768, 588, 900)):
        outline(f'rd_tbox_{corner}', x0, y0, x1, y1, 0)
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2 + 12  # centred, no corner label; 12 px low (1.0.3: it touched the top line)
        text(f'rd_t_{corner}', cx, cy, H + f't_{corner}', WHITE, scale=1.55, style='OSD_19_bold', wpx=200,
             value='--', page=0)
    # fuel margin (the game's packets), else the game's own value; white (1.0)
    label('rd_l_fuel', 1267, 630, 'FUEL', visible=H + 'p0r')
    text('rd_fuel', 1390, 765, 'laps_of_fuel_format', WHITE, scale=1.7, wpx=330, value='-', visible=H + 'fuel_game')
    text('rd_fuel_g', 1390, 765, H + 'fuel', WHITE, scale=1.7, wpx=330, value='-', visible=H + 'fuel_ok')
    # the margin shared over the laps left (1.0.21): what can be spent (+) or must be saved (-) each lap
    text('rd_fuel_pl', 1390, 862, H + 'fuel_pl', WHITE, style='OSD_14_regular', wpx=330, value='-',
         visible=H + 'fuel_pl_on')

    # ---- car page (page 1): the user's top-down car (art/car: white parts on a 1671 x 941 canvas, the mock-up's
    # pixels), each part coloured by its damage (green, amber, red; grey until known), the tyres by their wear, the
    # brakes by their damage; the chassis, cockpit and suspension grey.
    # A part is a copy of the stock 2026 mod icon's quad (mesh slot "mod_icon_bg"), stretched over the part's box
    # and given a material binding (.emb) of its own: that slot -> a copy of the stock lcd_default.material with the
    # part's texture and its colour in me_colour, one per colour (the group widgets only show the right one). The
    # launcher's UI art docs give this route; 0.4.9/0.4.10 switched textures with textureOverride, which the LCD
    # ignored (stretched white boxes).

    from PIL import ImageDraw
    # the chequered flag's squares: 16 x 9, white ones over the black screen (one quad: 0.4.6-0.4.22 drew 72)
    chq = Image.new('L', (836, 472), 0)
    d = ImageDraw.Draw(chq)
    for row in range(9):
        for col in range(16):
            if (row + col) % 2 == 0:
                d.rectangle([round(col * 836 / 16), round(row * 472 / 9), round((col + 1) * 836 / 16) - 1,
                             round((row + 1) * 472 / 9) - 1], fill=255)
    car_quad('rd_ovl_chq0', overlay_texture('rd_ovl_chq0', chq), (0, 0, 1670, 942), (240, 240, 240), H + 'full_chq',
             L_FULL + 1)

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
    # the grey base car: every damage part's shape in one texture (at the canvas's size, then halved), grey, under
    # all the parts; a part's coloured copy covers its own shape once its damage is known
    from PIL import ImageChops
    union = Image.new('L', (1672, 944), 0)
    for stem in part_stems:
        a = Image.open(os.path.join(ROOT, 'art', 'car', stem + '.png')).convert('RGBA').getchannel('A')
        canvas = Image.new('L', (1672, 944), 0)
        canvas.paste(a, (0, 0))
        union = ImageChops.lighter(union, canvas)
    car_quad('rd_ovl_car0', overlay_texture('rd_ovl_car0', union.resize((836, 472), Image.LANCZOS)),
             (0, 0, 1670, 942), PART_N, H + 'p1', 51)

    # bottom left: fuel mode | differential; bottom right: brake bias
    line('rd_v_cb', 330, 650, 330, 850, 1)
    label('rd_l_mix', 180, 678, 'FUEL MODE', page=1)
    text('rd_mix', 170, 785, H + 'mix', WHITE, scale=1.35, wpx=280, value='-', page=1)
    label('rd_l_diff', 465, 678, 'DIFF', page=1)
    text('rd_diff', 470, 785, H + 'diff', WHITE, scale=1.35, wpx=260, value='-', page=1)
    label('rd_l_bbal', 1383, 640, 'BRAKE BIAS', page=1)
    text('rd_bbal', 1383, 765, H + 'bbal', WHITE, scale=1.8, wpx=300, value='-', page=1)
    label('rd_l_bbal_front', 1383, 862, 'FRONT', page=1)
    # brake temperatures in the space beside the car: the rears left of it, the fronts right (the nose points right;
    # the car's left side is up)
    for corner, cx, ly in (('rl', 150, 150), ('rr', 150, 380), ('fl', 1520, 150), ('fr', 1520, 380)):
        label(f'rd_l_bt_{corner}', cx, ly, f'{corner.upper()} BRAKE', page=1)
        text(f'rd_bt_{corner}', cx, ly + 75, H + f'bt_{corner}', WHITE, scale=1.1, wpx=230, value='-', page=1)

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
                        ('w', (40, 120, 255))):  # unknown: "-" in the hard's white copy
        text(f'rd_tyre_{tag}', 1240, 190, H + 'tyre', colour, scale=1.6, wpx=760, value='-', visible=H + f'ty_{tag}')
    text('rd_tyre_age', 1240, 282, H + 'tyre_age', WHITE, style='OSD_19_bold', wpx=600, value='-', page=2)
    label('rd_l_rejoin', 290, 368, 'REJOIN POSITION', page=2)
    text('rd_rejoin', 440, 475, H + 'rejoin', WHITE, scale=1.8, wpx=400, value='-', page=2)
    label('rd_l_wing', 1110, 368, 'FRONT WING DAMAGE', page=2)
    label('rd_l_wing_lr', 1240, 418, 'LEFT / RIGHT', page=2, colour=GREY)
    for tag, colour in (('g', GREEN), ('a', AMBER), ('r', RED)):  # unknown: "-" in green
        text(f'rd_wing_{tag}', 1240, 500, H + 'wing', colour, scale=1.3, wpx=760, value='-', visible=H + f'wing_{tag}')
    # tyre wear: a box per corner, the % coloured by the wear limits in the settings
    for corner, x0, y0, x1, y1 in (('fl', 40, 618, 310, 755), ('fr', 326, 618, 588, 755),
                                   ('rl', 40, 768, 310, 900), ('rr', 326, 768, 588, 900)):
        outline(f'rd_wbox_{corner}', x0, y0, x1, y1, 2)
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2 + 12  # as the race page's tyre temperatures: centred, no label
        for tag, colour in (('g', GREEN), ('a', YELLOW), ('r', RED)):  # unknown: "--" in green
            text(f'rd_w{tag}_{corner}', cx, cy, H + f'w_{corner}', colour, scale=1.55, style='OSD_19_bold',
                 wpx=200, value='--', visible=H + f'w{tag}_{corner}')
    # the last pit stop's time stationary
    label('rd_l_pst', 1383, 640, 'PIT STOP TIME', page=2)
    text('rd_pst', 1360, 765, H + 'pst', WHITE, scale=1.7, wpx=330, value='-', page=2)
    label('rd_l_pst_s', 1590, 800, 's', page=2, scale=1.4, visible=H + 'pst_on')

    # ---- the calibration grid (the "Calibration grid" setting): black over everything, then straight lines across
    # the whole display, every 83.5 px across (21) and 78.5 px down (13), the centre ones red; a screenshot of it on a
    # wheel shows how that wheel's screen bends the display (the 2026 Ferrari's). 0.4.21's coarser grid over the
    # pages mixed with the pages' own lines and text.
    def draw_lines(segs, bend):
        """An 836 x 472 alpha picture of line segments (x0, y0, x1, y1, width; canvas px), each point moved by bend:
        drawn at the canvas's size and halved (smooth edges)."""
        big = Image.new('L', (1672, 944), 0)
        d2 = ImageDraw.Draw(big)
        for x0, y0, x1, y1, t in segs:
            n = max(2, int(max(abs(x1 - x0), abs(y1 - y0)) / 8) + 1)
            pts = [bend(x0 + (x1 - x0) * u / (n - 1), y0 + (y1 - y0) * u / (n - 1)) for u in range(n)]
            d2.line(pts, fill=255, width=max(2, round(t)), joint='curve')
        return big.resize((836, 472), Image.LANCZOS)

    def straight(x, y):
        return x, y

    # page 0's lines: the shared ones in each of its two layouts (one texture each, not three)
    lines['pgr'] = lines['pg0'] + lines['pgr']
    lines['pgq'] = lines['pg0'] + lines['pgq']
    lines['pg0'] = []
    for group, segs in lines.items():
        if not segs:
            continue
        vis = {'sha': None, 'pg0': H + 'p0', 'pgr': H + 'p0r', 'pgq': H + 'p0q', 'pg1': H + 'p1', 'pg2': H + 'p2',
               'bdg': H + 'badge_n'}[group]
        colour = (150, 150, 158) if group == 'bdg' else LINE
        vis_n, vis_f = split_leaf(vis)
        for v, bend, vv in (('n', straight, vis_n), ('f', ferrari, vis_f)):
            tag = f'rd_ln_{group}_{v}'
            car_quad(tag, overlay_texture(tag, draw_lines(segs, bend)), (0, 0, 1670, 942), colour, vv, L_SHAPE)

    # (two textures since 0.4.23: 0.4.22's 35 rectangles took the display past 512 nodes and it drew nothing; a
    # Ferrari copy of each since 0.4.24, bent the other way: on that wheel it should show straight)
    # Out of the build since 1.0.15 (it took 5 of the display's 512 nodes; the setting was hidden in 1.0.2). To measure
    # a new wheel: CALIBRATION = True here, the "calibration" toggle back in swofty.json and the plugin's
    # put_flag("calib", ...) back.
    CALIBRATION = False
    if CALIBRATION:
        L_CALIB = L_FULL + 9
        box('rd_cal_bg', 0, 0, 1670, 942, (0, 0, 0), H + 'calib', L_CALIB - 1)
        grid_segs = [(i * 83.5, 0, i * 83.5, 942, 6) for i in range(21) if i != 10] + \
                    [(0, j * 78.5, 1670, j * 78.5, 6) for j in range(13) if j != 6]
        cross_segs = [(835, 0, 835, 942, 6), (0, 471, 1670, 471, 6)]
        cal_n, cal_f = split_leaf(H + 'calib')
        for v, bend, vv in (('0', straight, cal_n), ('2', ferrari, cal_f)):
            car_quad(f'rd_ovl_grd{v}', overlay_texture(f'rd_ovl_grd{v}', draw_lines(grid_segs, bend)), (0, 0, 1670, 942),
                     (255, 255, 255), vv, L_CALIB)
        for v, bend, vv in (('1', straight, cal_n), ('3', ferrari, cal_f)):
            car_quad(f'rd_ovl_grd{v}', overlay_texture(f'rd_ovl_grd{v}', draw_lines(cross_segs, bend)),
                     (0, 0, 1670, 942), (255, 40, 40), vv, L_CALIB + 1)

    # the switches the plugin splits (names after ui_menu_data.hud_rdash_)
    hdr = ['// Written by tools/build_ui.py: the switches drawn as two copies, <name>_n on any other car and',
           '// <name>_f on the 2026 Ferrari (its wheel bends the display; the _f copies are placed against the bend).',
           '#pragma once', '', 'inline constexpr const char *kSplitLeaves[] = {']
    hdr += [f'    "{leaf}",' for leaf in sorted(split)] + ['};', '']
    open(os.path.join(ROOT, 'native', 'src', 'split_leaves.hpp'), 'w', encoding='utf-8').write('\n'.join(hdr))

    os.makedirs(os.path.join(ROOT, 'ui', 'clips'), exist_ok=True)
    os.makedirs(os.path.join(ROOT, 'ui', 'tex'), exist_ok=True)
    order_widgets(tpl)
    order_nodes(lcd, tpl)
    # written after order_nodes (1.0.19): until 1.0.18 it was written before, so the coloured nodes never came first
    # in the game's file and every colour past about node 113 of the creation order showed white (the gaps, the
    # sector times, the pit page's)
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
    json.dump({'schema_version': 1, 'ops': ops}, open(os.path.join(ROOT, 'erp', 'driver_display.json'), 'w'), indent=2)
    # sanity: both files read back
    pkgfmt.read(lcd.encode())
    # the LCD holds at most 512 nodes: 0.4.22 had 520 and the display drew nothing at all
    n_nodes = len(lcd.scene0.get('nodeidlookup'))
    print(f'lcd nodes: {n_nodes} of 512')
    if n_nodes > 512:
        raise SystemExit(f'the LCD layout has {n_nodes} nodes: the game draws nothing past 512')
    pkgfmt.read(tpl.encode())
    print('wrote ui/lcd_display.idf, ui/race_assets_f1.ui, erp/driver_display.json')


if __name__ == '__main__':
    main()
