# Builds the dash's UI files from the game's own stock files:
#   ui/lcd_display.idf    the wheel LCD layout (UILayout)
#   ui/race_assets_f1.ui  the race HUD template that feeds it (UIDF)
# and erp/race_dash.json, the launcher ops that swap them into the game's packages.
#
# Usage: python tools/build_ui.py ["<F1 25 install folder>"]
# Reads the untouched originals from <install>/Original_GameData, falling back to the live packages.
import copy, json, os, random, sys, uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import erp
import pkgfmt
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
    package a dash addon has written into is skipped."""
    for base in (GAME, os.path.join(GAME, 'Original_GameData')):
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
                print(f'{resource.rsplit("/", 1)[-1]} from {path}')
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

    def add_rect(self, name, x, y, w, h, layer=12, like=None):
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

    def add_group(self, name, nodes, dbentry=None, negate=False, colour=None):
        """A group widget (UIGP) over nodes: shown while the bool at dbentry is true (false with negate), always
        without one, and tinted with colour (colourOverride). Shapes are the white divider mesh, so the
        override is their colour."""
        w = copy.deepcopy(self.find_any('ers_bar_deploy_used_bg'))
        props = D([('id', V('ui32', self._new_id())), ('version', V('ui32', 0)), ('type', V('ui32', 1346849109))])
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

    # ---- 0.1.0: the dash layout ("race" view)
    # The screen is about x -2.56..2.56, y -1.5..1.5 (centre 0, 0). Text is centred on its position. Colours are
    # colourOverride on the widgets; shapes are the white divider mesh. Values starting with '#ui_menu_data.hud_'
    # are written by the plugin; the others are the game's own LCD data (no UDP needed for those).
    WHITE, GREY, LABEL = (235, 235, 235), (120, 120, 126), (150, 150, 158)
    GREEN, RED, YELLOW = (40, 225, 110), (245, 45, 45), (250, 215, 40)
    LINE = (185, 185, 192)  # 0.1.3's (95, 95, 102) at 0.02 thick was all but invisible on the wheel
    H = '#ui_menu_data.hud_rdash_'

    # The game's own elements stay where they are, under an opaque backdrop: the dash draws on layers above
    # it. (0.1.0 / 0.1.1 moved 23 stock groups off the screen, the mods' mask and window elements among them,
    # and the game crashed loading a session; Wheel Dash only ever moved plain texts and lines.)
    L_BACK, L_SHAPE, L_SEG, L_TEXT = 50, 55, 56, 60
    lcd.add_rect('rd_backdrop', -3.0, -1.75, 6.0, 3.5, layer=L_BACK)
    tpl.add_group('rd_backdrop_group', ['rd_backdrop'], colour=(8, 8, 10))

    # A text's scale grows it rightwards from its box's left edge, so its centre moves right by
    # box * (scale - 1) / 2: Wheel Dash's placements fit that (SOC value box 1.0 x1.7 placed 0.33 left of where
    # it shows, gear 1.4 x1.55 0.34, lap time 2.72 x1.3 0.46, delta 2.72 x1.55 0.69). Each text is placed that
    # much to the left, so x is where it shows. (0.1.x's scaled values sat right of centre; a flat nudge didn't
    # fit them all.)
    def centred(x, width, scale):
        return x - width * (scale - 1.0) / 2.0

    NUDGE = 0.0

    def text(name, x, y, dbentry, colour=WHITE, scale=1.0, style='OSD_24_bold', width=1.0, value='',
             visible=None, nudge=True):
        lcd.add_text(name, 'speedo_value', centred(x, width, scale), y, width=width, layer=L_TEXT, style=style)
        if scale != 1.0:
            lcd.move(name, sx=scale, sy=scale)
        tpl.add_label(f'{name}_w', name, dbentry, value=value, colour=colour, visible=visible)

    def label(name, x, y, value, visible=None):
        text(name, x, y, H + 'none', colour=LABEL, style='OSD_14_regular', value=value, width=1.0, nudge=False,
             visible=visible)

    def line(name, x0, y0, x1, y1, t=0.035):
        if x1 - x0 >= y1 - y0:  # horizontal
            lcd.add_rect(name, x0, y0 - t / 2, x1 - x0, t, layer=L_SHAPE)
        else:
            lcd.add_rect(name, x0 - t / 2, y0, t, y1 - y0, layer=L_SHAPE)
        tpl.add_group(f'{name}_group', [name], colour=LINE)

    # columns: left info | tyres | gear | tyres | right info; rows: top strip, two middle rows, bottom row,
    # SOC bar
    X = [-2.50, -1.40, -0.80, 0.80, 1.40, 2.50]
    Y_TOP, Y_M1, Y_M2, Y_BOT, Y_BAR = 0.95, 0.25, -0.45, -1.05, -1.42
    cx = [(X[i] + X[i + 1]) / 2 for i in range(5)]

    # grid
    line('rd_h_top', X[0], Y_TOP, X[5], Y_TOP)
    line('rd_h_m1l', X[0], Y_M1, X[2], Y_M1)
    line('rd_h_m1r', X[3], Y_M1, X[5], Y_M1)
    line('rd_h_m2', X[0], Y_M2, X[5], Y_M2)
    line('rd_h_bot', X[0], Y_BOT, X[5], Y_BOT)
    line('rd_v_t1', X[2], Y_TOP, X[2], 1.42)
    line('rd_v_t2', X[3], Y_TOP, X[3], 1.42)
    # all four verticals run down to the bottom row (the info panel's pop-up box covers the inner two)
    for i in (1, 2, 3, 4):
        line(f'rd_v_{i}', X[i], Y_BOT, X[i], Y_TOP)

    # top strip: last lap | RPM | delta to the best lap
    text('rd_last', (X[0] + X[2]) / 2, 1.17, H + 'last', GREEN, scale=1.15, width=1.9, value='--:--.---')
    text('rd_rpm', cx[2], 1.17, H + 'rpm', RED, scale=1.15, width=1.6, value='0')
    dx = (X[3] + X[5]) / 2
    text('rd_delta_g', dx, 1.17, H + 'delta', GREEN, scale=1.15, width=1.9, visible=H + 'delta_neg')
    text('rd_delta_r', dx, 1.17, H + 'delta', RED, scale=1.15, width=1.9, visible=H + 'delta_pos')
    text('rd_delta_w', dx, 1.17, H + 'delta', GREY, scale=1.15, width=1.9, value='--.--',
         visible=H + 'delta_none')

    # left column: SPEED, LAP (the lap you're on), SOC %
    label('rd_l_speed', cx[0], 0.84, 'SPEED')
    text('rd_speed', cx[0], 0.52, H + 'speed', WHITE, scale=1.3, width=1.0, value='0')
    label('rd_l_lap', cx[0], 0.14, 'LAP')
    text('rd_lap', cx[0], -0.18, H + 'lap', WHITE, scale=1.3, width=1.0, value='0')
    label('rd_l_soc', cx[0], -0.56, 'SOC')
    text('rd_soc_g', cx[0], -0.80, H + 'soc', GREEN, scale=1.3, width=1.0, visible=H + 'soc_ok')
    text('rd_soc_r', cx[0], -0.80, H + 'soc', RED, scale=1.3, width=1.0, visible=H + 'soc_low')

    # tyre boxes (front row 1, rear row 2), each with its label, the game's own formatted temperature (text
    # bound exactly as the stock tyre labels bind it; needs no UDP) and the wear % under it (from the telemetry,
    # as Wheel Dash had it: shown while the game's UDP output is on)
    for corner, x, y_top, db in (('fl', cx[1], Y_TOP, 'front_left_tyre_temp_formatted'),
                                 ('fr', cx[3], Y_TOP, 'front_right_tyre_temp_formatted'),
                                 ('rl', cx[1], Y_M1, 'rear_left_tyre_temp_formatted'),
                                 ('rr', cx[3], Y_M1, 'rear_right_tyre_temp_formatted')):
        label(f'rd_l_t{corner}', x, y_top - 0.11, 'TYRE')
        text(f'rd_t_{corner}', x, y_top - 0.36, db, (255, 120, 70), scale=0.85, width=0.6, value='--')
        text(f'rd_w_{corner}', x, y_top - 0.57, H + f'w_{corner}', LABEL, scale=0.75, width=0.6, value='--')

    # centre: the stock gear digit (its widget and the game's gear value stay), big, over both middle rows
    lcd.move('gear_number', x=0.0, y=-0.05, sx=1.45, sy=1.45)
    Layout.param(lcd.node('gear_number'), 'eLayer.layer', V('si32', L_TEXT))
    label('rd_l_gear', cx[2], 0.84, 'GEAR')
    # bottom centre: the info panel across the three middle columns. Normally the ERS deploy mode; a box pops up
    # inside it: blue for a change (ERS mode), amber while a safety car or VSC is out, red under a red flag.
    # (Wheel Dash popped its box up over the middle of the screen.)
    px0, px1, pcx = X[1], X[4], (X[1] + X[4]) / 2
    # normally: fuel mix | ERS deploy mode | brake bias (mix and bias come from the game's UDP packets)
    label('rd_l_mix', cx[1], -0.56, 'MIX', visible=H + 'ers_show')
    text('rd_mix', cx[1], -0.80, H + 'mix', WHITE, scale=0.9, width=0.6, value='-', visible=H + 'ers_show')
    label('rd_l_ers', cx[2], -0.56, 'ERS MODE', visible=H + 'ers_show')
    text('rd_ers_mode', cx[2], -0.80, H + 'ers_mode', YELLOW, scale=1.05, width=1.6, value='-', visible=H + 'ers_show')
    label('rd_l_bbal', cx[3], -0.56, 'BBAL', visible=H + 'ers_show')
    text('rd_bbal', cx[3], -0.80, H + 'bbal', WHITE, scale=0.9, width=0.6, value='-', visible=H + 'ers_show')
    L_POP, L_POPTEXT = 58, 62
    for tag, colour in (('blue', (10, 92, 255)), ('amber', (235, 160, 0)), ('red', (215, 0, 0))):
        nm = f'rd_pop_{tag}'
        lcd.add_rect(nm, px0 + 0.03, Y_BOT + 0.03, px1 - px0 - 0.06, Y_M2 - Y_BOT - 0.06, layer=L_POP)
        tpl.add_group(f'{nm}_group', [nm], H + f'pop_{tag}', colour=colour)
    for nm, y, scale, style, dbe in (('rd_pop_title', -0.58, 1.0, 'OSD_14_regular', 'pop_title'),
                                     ('rd_pop_value', -0.82, 1.15, 'OSD_24_bold', 'pop_value')):
        lcd.add_text(nm, 'speedo_value', centred(pcx, 2.6, scale), y, width=2.6, layer=L_POPTEXT, style=style)
        lcd.move(nm, sx=scale, sy=scale)
        tpl.add_label(f'{nm}_w', nm, H + dbe, colour=WHITE, visible=H + 'pop_on')

    # right column: position, fuel margin in laps, gaps to the cars ahead and behind
    label('rd_l_pos', cx[4], 0.84, 'POS')
    text('rd_pos', cx[4], 0.52, H + 'pos', WHITE, scale=1.3, width=1.0, value='-')
    label('rd_l_fuel', cx[4], 0.14, 'FUEL')
    # the game's own fuel margin: 'laps_of_fuel_format', the text the stock fuel line puts in its "laps"
    # replacement (a string there, default '0.0'). Needs no UDP. ('laps_of_fuel_text' itself is a localisation
    # key, not plain text.)
    text('rd_fuel', cx[4], -0.18, 'laps_of_fuel_format', WHITE, scale=1.15, width=1.0, value='-')
    label('rd_l_gap', cx[4], -0.56, 'GAP')
    text('rd_gap_a', cx[4], -0.75, H + 'gap_a', GREEN, scale=0.8, width=1.0, value='-')
    text('rd_gap_b', cx[4], -0.94, H + 'gap_b', RED, scale=0.8, width=1.0, value='-')

    # SOC bar: 20 segments of 5 %, the first 3 red; unlit ones show the dark strip behind
    seg, gap = 0.23, 0.02
    lcd.add_rect('rd_soc_strip', X[0], Y_BAR, X[5] - X[0], 0.26, layer=L_SHAPE)
    tpl.add_group('rd_soc_strip_group', ['rd_soc_strip'], colour=(38, 38, 42))
    for i in range(20):
        nm = f'rd_soc_{i + 1:02d}'
        lcd.add_rect(nm, X[0] + gap / 2 + i * (seg + gap), Y_BAR + 0.03, seg, 0.20, layer=L_SEG)
        tpl.add_group(f'{nm}_group', [nm], f'{H}soc{i + 1:02d}', colour=RED if i < 3 else GREEN)

    os.makedirs(os.path.join(ROOT, 'ui', 'clips'), exist_ok=True)
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
    os.makedirs(os.path.join(ROOT, 'erp'), exist_ok=True)
    json.dump({'schema_version': 1, 'ops': ops}, open(os.path.join(ROOT, 'erp', 'race_dash.json'), 'w'), indent=2)
    # sanity: both files read back
    pkgfmt.read(lcd.encode())
    pkgfmt.read(tpl.encode())
    print('wrote ui/lcd_display.idf, ui/race_assets_f1.ui, erp/race_dash.json')


if __name__ == '__main__':
    main()
