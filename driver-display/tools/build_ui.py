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


def shared_stock_elements(lcd):
    """1.2.8: the game's wheel screen elements that other templates in the dash's archives lay out too. The F1 / F2
    HUD dials of flow_options.erp's hud_dynamic_templates.ui name nodes of the wheel screen (mod_1..mod_4,
    ers_null, ignore_null), and launcher 1.10.23 refuses a build whose archive loses a node a stock layout step
    names ("layout step ... was in the stock scenes of this archive and is in no edited scene's nodeidlookup"; a
    player's report: the game didn't start). Read from the game's own templates, so a game patch is followed."""
    top = list(lcd.scene.get('children'))
    owner = {}
    for k in top:
        for n in lcd.nodes(k):
            owner[djb2(n.get('name').v)] = k.get('name').v
    keep = set()

    def walk(x):
        if isinstance(x, D):
            nid = x.get('nodeId')
            if nid is not None and getattr(nid, 't', None) == 'ui32' and nid.v in owner:
                keep.add(owner[nid.v])
            for _, v in x:
                if isinstance(v, (D, L)):
                    walk(v)
        elif isinstance(x, L):
            for v in x:
                if isinstance(v, (D, L)):
                    walk(v)
    for pkg in PKGS:
        path = os.path.join(GAME, 'Original_GameData', pkg)
        if not os.path.exists(path):
            path = os.path.join(GAME, pkg)
        f = open(path, 'rb').read()
        for name, t, frags in erp.entries(f):
            if t != 'UIDF':
                continue
            root = pkgfmt.read(erp.fragment(f, frags[0]))
            for tplt in root.get('Templates') or []:
                tname = tplt[0].get('name').v if tplt[0].get('name') is not None else ''
                if name == TPL and tname == 'lcd_modern':
                    continue  # the dash's own template
                walk(tplt)
    return keep


def strip_stock(lcd, tpl, keep=()):
    """1.2.1: the game's own wheel screen out of the layout. Its 24 elements (133 nodes with their children) sat
    under the dash's backdrop, drawing nothing anyone saw, and took a quarter of the 512 nodes and the first places
    of the colour order. The dash's elements were cloned from some of them, which happens before this. The game's
    15 widgets stay (the template's enabled / disabled signals name two of them), with no nodes to drive: a widget
    with no node is one the game ships (animations, fuel, mods)."""
    kids = list(lcd.scene.get('children'))
    gone = [k for k in kids if not k.get('name').v.startswith('rd_') and k.get('name').v not in keep]
    hashes = set()
    for k in gone:
        hashes |= {djb2(n.get('name').v) for n in lcd.nodes(k)}
    lcd.scene.set('children', L([k for k in kids if not any(k is g for g in gone)]))
    lcd.scene0.set('nodeidlookup', L(V('ui32', v) for v in sorted({djb2(n.get('name').v) for n in lcd.nodes()})))
    dropped = 0

    def walk(c):
        nonlocal dropped
        items = list(c) if isinstance(c, L) else [v for _, v in c]
        if isinstance(c, L) and items and all(isinstance(x, L) for x in items) and \
                any(isinstance(d, D) and d.get('nodeId') is not None for x in items for d in x):
            keep = [x for x in items if not any(isinstance(d, D) and d.get('nodeId') is not None and
                                                d.get('nodeId').v in hashes for d in x)]
            dropped += len(items) - len(keep)
            c[:] = keep
            return
        for x in items:
            if isinstance(x, (D, L)):
                walk(x)
    for w in tpl.lcd[1]:
        if w[0].get('name') is None or not w[0].get('name').v.startswith('rd_'):
            walk(w)
    print(f'the game\'s wheel screen: {len(gone)} elements ({len(hashes)} nodes) out, {dropped} widget node path(s) '
          f'with them; kept for other templates: {", ".join(sorted(keep)) or "none"}')


def archive_check(lcd_data, tpl_data):
    """1.2.8: the launcher's rule for an edited archive (packages/core/src/games/f1_25/ui/check/archives.rs, from
    launcher 1.10.21): every node a template's layout step names that some scene of the archive had before the edit
    must still be in some scene's nodeidlookup after it, or the launcher refuses to start the game ("UI edit(s)
    break rules the game's UI loader enforces"). Checked here for both archives the dash edits, with every one of
    their templates (ours as built), so a layout that breaks it never ships (1.2.1-1.2.7 did: a player's report)."""
    def lookup(data):
        try:
            root = pkgfmt.read(data)
        except Exception:
            return set()
        return {x.v for sc in (root.get('scenelist') or []) for x in (sc.get('nodeidlookup') or [])}

    def node_ids(x, out):
        if isinstance(x, D):
            nid = x.get('nodeId')
            if nid is not None and getattr(nid, 't', None) == 'ui32':
                dn = x.get('debugName')
                out[nid.v] = dn.v if dn is not None else '?'
            for _, v in x:
                if isinstance(v, (D, L)):
                    node_ids(v, out)
        elif isinstance(x, L):
            for v in x:
                if isinstance(v, (D, L)):
                    node_ids(v, out)

    ours = lookup(lcd_data)
    problems = []
    for pkg in PKGS:
        path = os.path.join(GAME, 'Original_GameData', pkg)
        if not os.path.exists(path):
            path = os.path.join(GAME, pkg)
        f = open(path, 'rb').read()
        before, after, templates = set(), set(), []
        for name, t, frags in erp.entries(f):
            if t == 'UILayout':
                ids = lookup(erp.fragment(f, frags[0]))
                before |= ids
                after |= ours if name == LCD else ids
            elif t == 'UIDF':
                templates.append((name, tpl_data if name == TPL else erp.fragment(f, frags[0])))
        for name, data in templates:
            refs = {}
            node_ids(pkgfmt.read(data), refs)
            lost = sorted({refs[i] for i in refs if i in before and i not in after})
            if lost:
                problems.append(f'{pkg.rsplit("/", 1)[-1]}: {name.rsplit("/", 1)[-1]} names {", ".join(lost)}, which no '
                                f'scene of its archive has any more')
    if problems:
        raise SystemExit('the launcher would refuse this layout (a template names a node the build removed):\n  ' +
                         '\n  '.join(problems))
    print(f'archive check: every template of {len(PKGS)} archive(s) finds its nodes (the launcher\'s rule)')


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
    # 1.2.1: the game's widgets last (strip_stock left them no nodes), so ours take the first places
    tpl.lcd[1][:] = col + col_f + rest + stock
    last = len(col) - 1
    last_f = last + len(col_f)
    print(f'colour widgets: every car\'s up to widget {last}, the Ferrari copies up to {last_f} (colour reaches widget 98, tested)')
    # 2026-11-25 colour tests: red came out up to widget 98 / node 104 and white from widget 104 / node 110 (the
    # same with the game's HUD on or off); 1.2.18's Ferrari copies reached exactly widget 98 / node 104 and worked
    if last_f > 98:
        raise SystemExit(f'the coloured widgets reach widget {last_f}: the game colours up to widget 98 (tested)')


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
          f'{len(stock) + len(first) - 1} (colour reaches node 104, tested)')
    # The limit taken as 128 (a power of two; 2026-11-19 photos: node 113 coloured, 135 white, in the file as
    # written). Before 1.0.19 this printout described an order the written file never had.
    if len(stock) + len(first) - 1 > 104:  # tested 2026-11-25: node 104 coloured, node 110 white
        raise SystemExit('the coloured nodes (the Ferrari copies too) reach past node 104, the tested limit: cut some')


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
LIST = L  # the list helper, for code inside main (where L is a local label size)


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
        lcd.node(node).set('anims', LIST([D([('asset', V('str', res))])]))  # LIST: main's sections use L as a size

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
    # 1.3.11: rd_backdrop stays (its name marks the file as a dash addon's; test pads copy it) but hidden: the
    # backdrop and the SC / red flag tints are material-coloured boxes (made below, after box()), which free the
    # three colour slots their colourOverride took
    lcd.add_rect('rd_backdrop', -3.0, -1.75, 6.0, 3.5, layer=L_BACK)
    tpl.add_group('rd_backdrop_group', ['rd_backdrop'], H + 'none')

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
        """Where to draw a canvas point on the Ferrari's screen. 1.2.20: where it is on every other car (the
        archive op from ferrari_screen.py maps that screen straight); the bend table is kept for reference."""
        return px, py
        # until 1.2.19: bilinear in the measured table
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
    DEFAULT_FER = [None]  # 1.2.22: 'none' while the pages the Ferrari draws its own way are built

    def team_leaf(visible, suf):
        """1.2.27: a split switch's copy for one team layout set: 't' any team's pages, 'f' Ferrari, 'm' McLaren,
        'e' Mercedes (1.2.31)."""
        leaf = (visible or H + 'all')[len(H):]
        split.add(leaf)
        return H + leaf + '_' + suf

    def split_leaf(visible):
        leaf = (visible or H + 'all')[len(H):]
        assert (visible or H).startswith(H), visible
        split.add(leaf)
        return H + leaf + '_n', H + leaf + '_f'

    def text(name, px, py, dbentry, colour=WHITE, scale=1.0, style='OSD_24_bold', wpx=330, value='',
             visible=None, page=None, layer=None, fer=None):
        """fer (1.2.18, the Ferrari race page): None places the Ferrari copy against the bend at the same spot;
        'none' makes no Ferrari copy (a page 0 value the Ferrari page doesn't show); a dict (x, y, and optionally
        scale, wpx) places the Ferrari copy there instead, on the Ferrari's own page 0 layout."""
        if page is not None and visible is None:
            visible = H + f'p{page}'  # that page only (a value with its own switch has the page in it)
        if colour == GREY:  # the "unknown" grey: plain white text (no colour override to spend)
            colour = None
        if fer is None:
            fer = DEFAULT_FER[0]
        if fer is None:  # 1.2.20: one copy for every car, the 2026 Ferrari's screen is straight now
            vis_n, vis_f = visible or H + 'all', None
        else:
            vis_n, vis_f = split_leaf(visible)
        fx, fy, fscale, fwpx = px, py, scale, wpx
        if isinstance(fer, dict) and fer.get('suf'):
            vis_f = team_leaf(visible, fer['suf'])
        if isinstance(fer, dict):
            fx, fy = fer['x'], fer['y']
            fscale, fwpx = fer.get('scale', scale), fer.get('wpx', wpx)
        copies = [(name, (px, py), vis_n, scale, wpx)]
        fcolour = fer.get('colour', colour) if isinstance(fer, dict) else colour  # 1.2.24
        if fer is not None and fer != 'none':
            copies.append((name + '_f', ferrari(fx, fy), vis_f, fscale, fwpx))
        for nm, (x, y), vis, scale, wpx in copies:
            w = W(wpx)
            px, py = (fx, fy) if nm.endswith('_f') else (px, py)
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
            db = fer['db'] if nm.endswith('_f') and isinstance(fer, dict) and 'db' in fer else dbentry
            tpl.add_label(f'{nm}_w', nm, db, value=value, colour=fcolour if nm.endswith('_f') else colour,
                          visible=vis)

    def label(name, px, py, value, page=None, visible=None, scale=1.0, colour=LABEL, fer=None):
        text(name, px, py, H + 'none', colour=colour, style='OSD_14_regular', value=value, wpx=326, scale=scale,
             visible=visible, page=page, fer=fer)

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

    tex_crop = {}  # 1.3.12: a cropped picture's part of its full canvas (fractions), by view resource
    quad_mats = {}  # 1.3.12: material bytes -> its binding resource, shared by every quad that draws the same

    def car_quad(name, view_res, box, colour, visible, layer):
        """A quad over box drawing the texture in colour, shown by the leaf visible."""
        x0, y0, x1, y1 = box
        if view_res in tex_crop:  # 1.3.12: a cropped picture covers only its part of the box
            f0, g0, f1, g1 = tex_crop[view_res]
            x0, y0, x1, y1 = x0 + f0 * (box[2] - box[0]), y0 + g0 * (box[3] - box[1]), x0 + f1 * (box[2] - box[0]),                 y0 + g1 * (box[3] - box[1])
        # 1.3.12: quads with the same texture and colour share one material and binding (1.3.11 made 1,520 of each
        # an archive, only 283 different, and the game aborted opening the garage screen; 1.3.7's 1,222 had worked)
        mat = texture.material(stock_material, view_res, colour)
        emb_res = quad_mats.get(mat)
        if emb_res is None:
            mat_res = MAT_DIR + name + '.material'
            emb_res = EMB_DIR + name + '.emb?context=default'
            files[f'ui/tex/{name}.material'] = mat
            files[f'ui/tex/{name}.emb'] = texture.binding([(SLOT, mat_res)])
            materials.append((mat_res, 'GfxMaterialRes', [f'ui/tex/{name}.material']))
            bindings.append((emb_res, 'GfxMatBindRes', [f'ui/tex/{name}.emb']))
            quad_mats[mat] = emb_res
        n = lcd.add_rect(name, X(x0), Y(y1), W(x1 - x0), Y(y0) - Y(y1), layer=layer, like=QUAD)
        n.get('geoms')[0].set('materialid', V('str', emb_res))
        tpl.add_group(f'{name}_group', [name], visible)

    part_stems = []  # the damage parts, for the grey base car
    # 1.2.22: the Ferrari car page draws the same car smaller, in its box (370..1518 x 175..560, inset)
    _cb = None
    for f in os.listdir(os.path.join(ROOT, 'art', 'car')):
        bb = Image.open(os.path.join(ROOT, 'art', 'car', f)).convert('RGBA').getchannel('A').getbbox()
        if bb:
            _cb = bb if _cb is None else (min(_cb[0], bb[0]), min(_cb[1], bb[1]), max(_cb[2], bb[2]),
                                           max(_cb[3], bb[3]))
    _tx0, _ty0, _tx1, _ty1 = 392, 192, 1496, 543
    _k = min((_tx1 - _tx0) / (_cb[2] - _cb[0]), (_ty1 - _ty0) / (_cb[3] - _cb[1]))
    _ox = (_tx0 + _tx1) / 2 - _k * (_cb[0] + _cb[2]) / 2
    _oy = (_ty0 + _ty1) / 2 - _k * (_cb[1] + _cb[3]) / 2

    _mx0, _my0, _mx1, _my1 = 140 * 1670 / 1280, 150 * 1670 / 1280, 1150 * 1670 / 1280, 503 * 1670 / 1280
    _mk = min((_mx1 - _mx0) / (_cb[2] - _cb[0]), (_my1 - _my0) / (_cb[3] - _cb[1]))
    _mox = (_mx0 + _mx1) / 2 - _mk * (_cb[0] + _cb[2]) / 2
    _moy = (_my0 + _my1) / 2 - _mk * (_cb[1] + _cb[3]) / 2

    _ex0, _ey0, _ex1, _ey1 = 125 * 1670 / 1280, 112 * 1670 / 1280, 1160 * 1670 / 1280, 478 * 1670 / 1280
    _ek = min((_ex1 - _ex0) / (_cb[2] - _cb[0]), (_ey1 - _ey0) / (_cb[3] - _cb[1]))
    _eox = (_ex0 + _ex1) / 2 - _ek * (_cb[0] + _cb[2]) / 2
    _eoy = (_ey0 + _ey1) / 2 - _ek * (_cb[1] + _cb[3]) / 2

    _bx0, _by0, _bx1, _by1 = 175 * 1670 / 1280, 15 * 1670 / 1280, 1140 * 1670 / 1280, 340 * 1670 / 1280
    _bk = min((_bx1 - _bx0) / (_cb[2] - _cb[0]), (_by1 - _by0) / (_cb[3] - _cb[1]))
    _box_ = (_bx0 + _bx1) / 2 - _bk * (_cb[0] + _cb[2]) / 2
    _boy = (_by0 + _by1) / 2 - _bk * (_cb[1] + _cb[3]) / 2

    _ax0, _ay0, _ax1, _ay1 = 120 * 1670 / 1280, 75 * 1670 / 1280, 1160 * 1670 / 1280, 400 * 1670 / 1280
    _ak = min((_ax1 - _ax0) / (_cb[2] - _cb[0]), (_ay1 - _ay0) / (_cb[3] - _cb[1]))
    _aox = (_ax0 + _ax1) / 2 - _ak * (_cb[0] + _cb[2]) / 2
    _aoy = (_ay0 + _ay1) / 2 - _ak * (_cb[1] + _cb[3]) / 2

    _wx0, _wy0, _wx1, _wy1 = 190 * 1670 / 1280, 25 * 1670 / 1280, 1095 * 1670 / 1280, 335 * 1670 / 1280
    _wk = min((_wx1 - _wx0) / (_cb[2] - _cb[0]), (_wy1 - _wy0) / (_cb[3] - _cb[1]))
    _wox = (_wx0 + _wx1) / 2 - _wk * (_cb[0] + _cb[2]) / 2
    _woy = (_wy0 + _wy1) / 2 - _wk * (_cb[1] + _cb[3]) / 2

    _ux0, _uy0, _ux1, _uy1 = 65 * 1670 / 1280, 30 * 1670 / 1280, 1225 * 1670 / 1280, 440 * 1670 / 1280
    _uk = min((_ux1 - _ux0) / (_cb[2] - _cb[0]), (_uy1 - _uy0) / (_cb[3] - _cb[1]))
    _uox = (_ux0 + _ux1) / 2 - _uk * (_cb[0] + _cb[2]) / 2
    _uoy = (_uy0 + _uy1) / 2 - _uk * (_cb[1] + _cb[3]) / 2

    _hx0, _hy0, _hx1, _hy1 = 150 * 1670 / 1280, 95 * 1670 / 1280, 1130 * 1670 / 1280, 495 * 1670 / 1280
    _hk = min((_hx1 - _hx0) / (_cb[2] - _cb[0]), (_hy1 - _hy0) / (_cb[3] - _cb[1]))
    _hox = (_hx0 + _hx1) / 2 - _hk * (_cb[0] + _cb[2]) / 2
    _hoy = (_hy0 + _hy1) / 2 - _hk * (_cb[1] + _cb[3]) / 2

    _lx0, _ly0, _lx1, _ly1 = 140 * 1670 / 1280, 125 * 1670 / 1280, 1240 * 1670 / 1280, 590 * 1670 / 1280
    _lk = min((_lx1 - _lx0) / (_cb[2] - _cb[0]), (_ly1 - _ly0) / (_cb[3] - _cb[1]))
    _lox = (_lx0 + _lx1) / 2 - _lk * (_cb[0] + _cb[2]) / 2
    _loy = (_ly0 + _ly1) / 2 - _lk * (_cb[1] + _cb[3]) / 2

    def al_car(box):  # 1.3.10
        x0, y0, x1, y1 = box
        return (_lox + _lk * x0, _loy + _lk * y0, _lox + _lk * x1, _loy + _lk * y1)

    def hs_car(box):  # 1.3.9
        x0, y0, x1, y1 = box
        return (_hox + _hk * x0, _hoy + _hk * y0, _hox + _hk * x1, _hoy + _hk * y1)

    def au_car(box):  # 1.3.7
        x0, y0, x1, y1 = box
        return (_uox + _uk * x0, _uoy + _uk * y0, _uox + _uk * x1, _uoy + _uk * y1)

    def wi_car(box):  # 1.3.4
        x0, y0, x1, y1 = box
        return (_wox + _wk * x0, _woy + _wk * y0, _wox + _wk * x1, _woy + _wk * y1)

    def am_car(box):  # 1.3.1
        x0, y0, x1, y1 = box
        return (_aox + _ak * x0, _aoy + _ak * y0, _aox + _ak * x1, _aoy + _ak * y1)

    def rb_car(box):  # 1.2.37
        x0, y0, x1, y1 = box
        return (_box_ + _bk * x0, _boy + _bk * y0, _box_ + _bk * x1, _boy + _bk * y1)

    def me_car(box):  # 1.2.31
        x0, y0, x1, y1 = box
        return (_eox + _ek * x0, _eoy + _ek * y0, _eox + _ek * x1, _eoy + _ek * y1)

    def mc_car(box):  # 1.2.27
        x0, y0, x1, y1 = box
        return (_mox + _mk * x0, _moy + _mk * y0, _mox + _mk * x1, _moy + _mk * y1)

    def fer_car(box):
        x0, y0, x1, y1 = box
        return (_ox + _k * x0, _oy + _k * y0, _ox + _k * x1, _oy + _k * y1)

    def car_part(stem, key, layer):
        """A damage part: a copy per colour, each shown by its own leaf (c<g|a|r>_<key>). While its damage is
        unknown none shows, and the grey base car under the parts shows through (1.0.15: a grey copy of each part
        took 22 of the display's 512 nodes)."""
        tag, view_res, box = car_texture(stem)
        part_stems.append(stem)
        for c, colour in (('g', PART_G), ('a', PART_A), ('r', PART_R)):
            vn, vf = split_leaf(H + f'c{c}_{key}')
            car_quad(f'{tag}_{c}', view_res, box, colour, vn, layer)
            car_quad(f'{tag}_{c}_fd', view_res, fer_car(box), colour, vf, layer)
            car_quad(f'{tag}_{c}_md', view_res, mc_car(box), colour, team_leaf(H + f'c{c}_{key}', 'm'), layer)
            car_quad(f'{tag}_{c}_ed', view_res, me_car(box), colour, team_leaf(H + f'c{c}_{key}', 'e'), layer)
            car_quad(f'{tag}_{c}_bd', view_res, rb_car(box), colour, team_leaf(H + f'c{c}_{key}', 'b'), layer)
            car_quad(f'{tag}_{c}_ad', view_res, am_car(box), colour, team_leaf(H + f'c{c}_{key}', 'am'), layer)
            car_quad(f'{tag}_{c}_wd', view_res, wi_car(box), colour, team_leaf(H + f'c{c}_{key}', 'wi'), layer)
            car_quad(f'{tag}_{c}_ud', view_res, au_car(box), colour, team_leaf(H + f'c{c}_{key}', 'au'), layer)
            car_quad(f'{tag}_{c}_hd', view_res, hs_car(box), colour, team_leaf(H + f'c{c}_{key}', 'hs'), layer)
            car_quad(f'{tag}_{c}_ld', view_res, al_car(box), colour, team_leaf(H + f'c{c}_{key}', 'al'), layer)

    def car_fixed(stem, colour, layer):
        """A part not coloured by damage: shown on the car page."""
        tag, view_res, box = car_texture(stem)
        vn, vf = split_leaf(H + 'p1')
        car_quad(f'{tag}_f', view_res, box, colour, vn, layer)
        car_quad(f'{tag}_fd', view_res, fer_car(box), colour, vf, layer)
        car_quad(f'{tag}_md', view_res, mc_car(box), colour, team_leaf(H + 'p1', 'm'), layer)
        car_quad(f'{tag}_ed', view_res, me_car(box), colour, team_leaf(H + 'p1', 'e'), layer)
        car_quad(f'{tag}_bd', view_res, rb_car(box), colour, team_leaf(H + 'p1', 'b'), layer)
        car_quad(f'{tag}_ad', view_res, am_car(box), colour, team_leaf(H + 'p1', 'am'), layer)
        car_quad(f'{tag}_wd', view_res, wi_car(box), colour, team_leaf(H + 'p1', 'wi'), layer)
        car_quad(f'{tag}_ud', view_res, au_car(box), colour, team_leaf(H + 'p1', 'au'), layer)
        car_quad(f'{tag}_hd', view_res, hs_car(box), colour, team_leaf(H + 'p1', 'hs'), layer)
        car_quad(f'{tag}_ld', view_res, al_car(box), colour, team_leaf(H + 'p1', 'al'), layer)

    def overlay_texture(tag, alpha):
        """A full-canvas texture from an alpha picture (836 x 472, half the canvas: 4 px blocks), named tag (11
        characters, so its view path has the stock texture's 46)."""
        assert len(tag) == 11 and alpha.size == (836, 472)
        view_res = f'eaid://ui_package/art/textures/{tag}.tif'
        image_res = view_res + '.image'
        rel = 'ui/tex/' + tag
        # 1.3.12: cropped to what it draws (4 px blocks): most pictures are a few names on an empty canvas, and the
        # full 836 x 472 of each came to 36 MB; car_quad draws a cropped texture over its part of the quad's box
        bb = alpha.getbbox() or (0, 0, 4, 4)
        cx0, cy0 = bb[0] // 4 * 4, bb[1] // 4 * 4
        cx1, cy1 = min(836, -(-bb[2] // 4) * 4), min(472, -(-bb[3] // 4) * 4)
        alpha = alpha.crop((cx0, cy0, cx1, cy1))
        tex_crop[view_res] = (cx0 / 836, cy0 / 472, cx1 / 836, cy1 / 472)
        files[rel + '.head'] = texture.surface_head(cx1 - cx0, cy1 - cy0)
        files[rel + '.pixels'] = texture.bc7_white(alpha.tobytes(), cx1 - cx0, cy1 - cy0)
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

    # 1.3.11: the backdrop and the SC / red flag tints (the canvas and past it: the layout's -3..3 x -1.75..1.75)
    BACK_BOX = (835 - 3.0 / 0.003066, 471 - 1.75 / 0.003185, 835 + 3.0 / 0.003066, 471 + 1.75 / 0.003185)
    box('rd_backdropq', *BACK_BOX, (6, 6, 8), None, L_BACK)
    box('rd_tint_scq', *BACK_BOX, (70, 54, 0), H + 'tint_sc', L_BACK + 1)
    box('rd_tint_redq', *BACK_BOX, (70, 8, 8), H + 'tint_red', L_BACK + 1)

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
    # 1.2.24: on the Ferrari pages (no ERS SOC cell) a box in the upper centre, white text (the user's choice)
    POP_F = (485, 200, 1185, 470)
    for tag, colour in (('blue', (10, 92, 255)), ('amber', (240, 160, 0)), ('red', (215, 0, 0))):
        vn, vf = split_leaf(H + f'pop_{tag}')[0], team_leaf(H + f'pop_{tag}', 't')
        box(f'rd_pop_{tag}', 602, 597, 1120, 902, colour, vn, L_POP)
        box(f'rd_pop_{tag}_f', *POP_F, colour, vf, L_POP)
    for tone, colour in (('light', WHITE), ('dark', (10, 10, 10))):
        text(f'rd_pop_title_{tone}', 861, 660, H + 'pop_title', colour, scale=1.25, style='OSD_14_regular', wpx=500,
             visible=H + f'pop_{tone}', layer=L_POPTEXT,
             fer=dict(x=835, y=292, scale=1.25, wpx=660, colour=WHITE, suf='t'))
        text(f'rd_pop_value_{tone}', 861, 775, H + 'pop_value', colour, scale=1.0, style='OSD_24_bold', wpx=500,
             visible=H + f'pop_{tone}', layer=L_POPTEXT,
             fer=dict(x=835, y=432, scale=1.5, wpx=660, colour=WHITE, suf='t'))

    # 1.2.13: the bottom row's bars on the 2026 Ferrari. A straight box came out curved there (its screen bends the
    # display, the user's photo: black corners at the top left and right of a bar); each bar gets a Ferrari copy over
    # one picture of the bar drawn against the bend, as the lines and the safety car panel have. The copies take
    # their colour from their material like every box: no colour override.
    from PIL import ImageDraw as _RD
    _row_tex = []

    def row_bar_texture():
        if not _row_tex:
            x0, y0, x1, y1 = 30, 595, 1640, 905
            pts = []
            for (ax, ay), (bx, by) in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)),
                                       ((x0, y1), (x0, y0))):
                n = max(2, int(max(abs(bx - ax), abs(by - ay)) / 8))
                pts += [ferrari(ax + (bx - ax) * u / n, ay + (by - ay) * u / n) for u in range(n)]
            big = Image.new('L', (1672, 944), 0)
            _RD.Draw(big).polygon(pts, fill=255)
            _row_tex.append(overlay_texture('rd_ovl_rowf', big.resize((836, 472), Image.LANCZOS)))
        return _row_tex[0]

    ROW_F = (135, 590, 1535, 820)  # 1.2.24: the Ferrari pages' banner (their layout has no bottom row)

    def row_box(name, colour, visible):
        """A bottom-row bar; on the Ferrari pages a banner across the lower centre (1.2.24)."""
        vn, vf = split_leaf(visible)[0], team_leaf(visible, 't')
        box(name, 30, 595, 1640, 905, colour, vn, L_POPTEXT + 1)
        box(name + '_f', *ROW_F, colour, vf, L_POPTEXT + 1)

    # under a safety car / VSC, after its full screen: the bottom row turns amber with what is out (SAFETY CAR, VSC,
    # VSC ENDING, SC IN THIS LAP, SLOW DOWN), so gear, speed and the SC delta stay readable above it (1.0.4)
    row_box('rd_sc_bar', (255, 205, 0), H + 'sc_bar')
    text('rd_sc_bar_text', 835, 750, H + 'sc_bar_text', (10, 10, 10), scale=1.9, wpx=1500, value='SAFETY CAR',
         visible=H + 'sc_bar', layer=L_POPTEXT + 2, fer=dict(x=835, y=745, scale=1.5, wpx=1340, colour=WHITE, suf='t'))

    # race messages across the bottom row (1.0.22: backmarker, pit window, rain, retirements, contact; the ERS SOC
    # cell's pop-up was too narrow for them and stays for setting changes): blue with white text for information,
    # amber with black text for incidents
    row_box('rd_row_blue', (10, 92, 255), H + 'row_blue')
    row_box('rd_row_amber', (240, 160, 0), H + 'row_amber')
    row_box('rd_row_red', (215, 0, 0), H + 'row_red')  # a puncture (1.1.6)
    text('rd_row_text_l', 835, 750, H + 'row_text', WHITE, scale=1.2, wpx=1500, value='-',
         visible=H + 'row_light', layer=L_POPTEXT + 2, fer=dict(x=835, y=735, scale=1.1, wpx=1340, suf='t'))
    text('rd_row_text_d', 835, 750, H + 'row_text', (10, 10, 10), scale=1.2, wpx=1500, value='-',
         visible=H + 'row_dark', layer=L_POPTEXT + 2, fer=dict(x=835, y=735, scale=1.1, wpx=1340, colour=WHITE, suf='t'))

    # the fastest lap of the race (1.0.21): the bottom row purple with the driver and the time for 5 s
    row_box('rd_fl_bar', (150, 60, 225), H + 'fl_bar')
    text('rd_fl_text', 835, 750, H + 'fl_text', WHITE, scale=1.2, wpx=1500, value='FASTEST LAP',
         visible=H + 'fl_bar', layer=L_POPTEXT + 2, fer=dict(x=835, y=735, scale=1.1, wpx=1340, suf='t'))

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

    # the pit limiter screen's speed: the game's speed text as on the race page (1.1.3)
    for tone, colour in (('dark', (10, 10, 10)), ('light', WHITE)):
        text(f'rd_lim_speed_{tone}', 835, 600, 'speed_str', colour, scale=1.6, wpx=1500, value='0',
             visible=H + f'lim_{tone}', layer=L_FULLTEXT)

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
    label('rd_l_lap_time', 181, 95, 'LAP TIME', page=0, fer='none')
    text('rd_lap_time', 325, 207, H + 'cur', WHITE, scale=1.39, wpx=560, value='-:--.---', page=0, fer='none')
    text('rd_l_delta', 1245, 95, H + 'delta_label', LABEL, style='OSD_14_regular',
         value='PREV LAP', visible=H + 'p0r', scale=1.0, wpx=326, fer='none')
    for tag, colour, vis in (('g', GREEN, 'delta_neg'), ('r', RED, 'delta_pos')):  # no delta: "--.--" in green
        text(f'rd_delta_{tag}', 1330, 207, H + 'delta', colour, scale=1.5, wpx=460, value='--.--',
             visible=H + vis, fer='none')
    # position, lap
    label('rd_l_pos', 106, 355, 'POS', page=0, fer='none')
    text('rd_pos', 162, 460, H + 'pos', WHITE, scale=1.4, wpx=250, value='-', page=0, fer='none')
    label('rd_l_lap', 385, 355, 'LAP', visible=H + 'p0r', fer='none')
    text('rd_lap', 462, 460, H + 'lap', WHITE, scale=1.25, wpx=300, value='-', visible=H + 'p0r', fer='none')
    # gear, rpm, speed
    label('rd_l_gear', 838, 62, 'GEAR', page=0, fer='none')
    text('rd_gear', 838, 245, H + 'gear', WHITE, scale=0.85, style='OSD_80_bold', wpx=330, value='N', page=0, fer='none')
    text('rd_rpm', 838, 372, H + 'rpm', WHITE, scale=0.8, style='OSD_19_bold', wpx=330, value='0 RPM', page=0, fer='none')
    # the game's own speed text (the stock speedo's speed_str): km/h or mph as set in the game's options (1.1.3;
    # before, the plugin's own km/h)
    text('rd_speed', 838, 505, 'speed_str', WHITE, scale=1.5, wpx=330, value='0', page=0, fer='none')
    # gaps
    text('rd_l_ahead', 1205, 368, H + 'code_a', LABEL, style='OSD_19_bold', scale=0.9, wpx=240, value='AHEAD',
         visible=H + 'p0r', fer='none')
    text('rd_gap_a', 1480, 368, H + 'gap_a', YELLOW, scale=0.95, wpx=330, value='-', visible=H + 'gap_a_on', fer='none')
    text('rd_l_behind', 1205, 492, H + 'code_b', LABEL, style='OSD_19_bold', scale=0.9, wpx=240, value='BEHIND',
         visible=H + 'p0r', fer='none')
    text('rd_gap_b', 1480, 492, H + 'gap_b', GREEN, scale=0.95, wpx=330, value='-', visible=H + 'gap_b_on', fer='none')

    # ---- the safety car screen (1.2.0, the user's mock-up): while a safety car / VSC is out (after its flag screen),
    # on the race page, the top two rows turn into a yellow panel with black writing: SC / VSC DELTA | GEAR over the
    # speed | EST. PIT EXIT POS; the bottom row stays the race layout's (tyres, ERS SOC with LAP n/total under it
    # in place of the mode badge, fuel). The panel and its words are drawn into textures (quads tinted by their
    # material: no colour override, whose ~128 nodes are nearly all taken), the four values are black game texts.
    # Bahnschrift Bold (Windows) draws the words; the drawn pictures ship, not the font.
    from PIL import ImageDraw as _ID, ImageFont as _IF
    SC_TOP = (22, 22, 1648, 584)
    L_SCY = L_TEXT + 2  # over the race page's texts and lines, under the pop-ups and the full screens

    def sc_font(size):
        f = _IF.truetype(os.path.join(os.environ.get('WINDIR', 'C:/Windows'), 'Fonts', 'bahnschrift.ttf'), size)
        f.set_variation_by_name('Bold')
        return f

    def sc_mask(draw_fn):
        big = Image.new('L', (1672, 944), 0)
        draw_fn(_ID.Draw(big))
        return big.resize((836, 472), Image.LANCZOS)

    def bent_poly(pts, bend):
        out = []
        for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]):
            n = max(2, int(max(abs(x1 - x0), abs(y1 - y0)) / 8))
            out += [bend(x0 + (x1 - x0) * u / n, y0 + (y1 - y0) * u / n) for u in range(n)]
        return out

    def sc_panel(bend):
        x0, y0, x1, y1 = SC_TOP
        return sc_mask(lambda d: d.polygon(bent_poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], bend), fill=255))

    def sc_words(delta_word, bend):
        def draw(d):
            for word, cx, cy, size in ((delta_word, 324, 118, 74), ('GEAR', 835, 92, 64),
                                       ('EST. PIT EXIT POS', 1346, 118, 66)):
                bx, by = bend(cx, cy)
                d.text((bx, by), word, font=sc_font(size), fill=255, anchor='mm')
            for x0, y0, x1, y1 in ((618, 60, 618, 548), (1053, 60, 1053, 548), (690, 392, 980, 392)):
                n = max(2, int(max(abs(x1 - x0), abs(y1 - y0)) / 8) + 1)
                pts = [bend(x0 + (x1 - x0) * u / (n - 1), y0 + (y1 - y0) * u / (n - 1)) for u in range(n)]
                d.line(pts, fill=255, width=8, joint='curve')
        return sc_mask(draw)

    def sc_screen():
        def straight(x, y):
            return x, y
        for v, bend, vis in (('n', straight, split_leaf(H + 'sc_scr')[0]),):
            tag = f'rd_ovl_scy{v}'
            car_quad(tag, overlay_texture(tag, sc_panel(bend)), (0, 0, 1670, 942), (250, 225, 0), vis, L_SCY)
        for kind, word in (('s', 'SC DELTA'), ('v', 'VSC DELTA')):
            for v, bend, vis in (('n', straight, split_leaf(H + f'sc_lbl_{kind}')[0]),):
                tag = f'rd_ovl_sc{kind}{v}'
                car_quad(tag, overlay_texture(tag, sc_words(word, bend)), (0, 0, 1670, 942), (12, 12, 12), vis,
                         L_SCY + 1)
        BLACK = (10, 10, 10)
        DEFAULT_FER[0] = 'none'
        vis = H + 'sc_scr'
        text('rd_scd_v', 324, 352, H + 'scd', BLACK, scale=0.62, style='OSD_80_bold', wpx=560, value='-', visible=vis,
             layer=L_SCY + 1)
        text('rd_scd_gear', 835, 290, H + 'gear', BLACK, scale=0.85, style='OSD_80_bold', wpx=330, value='N',
             visible=vis, layer=L_SCY + 1)
        text('rd_scd_spd', 838, 488, 'speed_str', BLACK, scale=1.5, wpx=330, value='0', visible=vis, layer=L_SCY + 1)
        text('rd_scd_pit', 1346, 352, H + 'pitnow', BLACK, scale=0.62, style='OSD_80_bold', wpx=560, value='-',
             visible=vis, layer=L_SCY + 1)
        # under the ERS SOC, where the mode badge is: the lap
        text('rd_sc_lap', 861, 848, H + 'sc_lap', WHITE, scale=1.05, style='OSD_19_bold', wpx=470, value='-',
             visible=vis, layer=L_TEXT + 1)

    sc_screen()
    DEFAULT_FER[0] = None

    # practice / qualifying / time trial (1.0.16, the user's mock-up): sector times on the right (purple the best of
    # every car, green the player's best, yellow neither), the delta to the best lap under the lap time where the
    # race has LAP n/total, and the session's time left where the race has the fuel margin
    PURPLE = (175, 85, 245)
    for k, sy in ((1, 140), (2, 300), (3, 465)):
        text(f'rd_l_s{k}', 1133, sy, H + 'none', WHITE, style='OSD_19_bold', scale=1.4, wpx=200, value=f'S{k}',
             visible=H + 'p0q', fer='none')
        for tag, colour in (('p', PURPLE), ('g', GREEN), ('y', YELLOW)):
            text(f'rd_s{k}_{tag}', 1405, sy + 10, H + f's{k}', colour, scale=1.4, wpx=400, value='-',
                 visible=H + f's{k}_{tag}', fer='none')
    label('rd_l_qd', 430, 355, 'TO PB', visible=H + 'p0q', fer='none')
    for tag, colour, vis in (('g', GREEN, 'qd_neg'), ('r', RED, 'qd_pos')):  # no best lap: "--.--" in green
        text(f'rd_qd_{tag}', 455, 460, H + 'qdelta', colour, scale=1.05, wpx=290, value='--.--', visible=H + vis, fer='none')
    text('rd_l_sess', 1383, 630, H + 'sess_label', LABEL, style='OSD_14_regular', value='TIME LEFT', wpx=460,
         visible=H + 'p0q', fer='none')
    text('rd_sess', 1383, 765, H + 'sess_left', WHITE, scale=1.7, wpx=330, value='--:--', visible=H + 'p0q', fer='none')
    # tyres: a box each, the corner and its temperature: blue cold / green / red hot from the game's packets,
    # else the game's own formatted temperature (white)
    # bigger since 0.4.34 (the degree sign was hard to read): the boxes fill the bottom left cell. White since 1.0:
    # the compound windows are a guide's guess until the launcher exposes the game's own (1.10), see tyre_window
    for corner, x0, y0, x1, y1 in (('fl', 40, 618, 310, 755), ('fr', 326, 618, 588, 755),
                                   ('rl', 40, 768, 310, 900), ('rr', 326, 768, 588, 900)):
        outline(f'rd_tbox_{corner}', x0, y0, x1, y1, 0)
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2 + 12  # centred, no corner label; 12 px low (1.0.3: it touched the top line)
        fcx, fcy = {'fl': (1115, 262), 'fr': (1335, 262), 'rl': (1115, 410), 'rr': (1335, 410)}[corner]
        text(f'rd_t_{corner}', cx, cy, H + f't_{corner}', WHITE, scale=1.55, style='OSD_19_bold', wpx=200,
             value='--', page=0, fer='none')
    # fuel margin (the game's packets), else the game's own value; white (1.0)
    label('rd_l_fuel', 1267, 630, 'FUEL', visible=H + 'p0r', fer='none')
    text('rd_fuel', 1390, 765, 'laps_of_fuel_format', WHITE, scale=1.7, wpx=330, value='-', visible=H + 'fuel_game', fer='none')
    text('rd_fuel_g', 1390, 765, H + 'fuel', WHITE, scale=1.7, wpx=330, value='-', visible=H + 'fuel_ok', fer='none')
    # the margin shared over the laps left (1.0.21): what can be spent (+) or must be saved (-) each lap
    text('rd_fuel_pl', 1390, 862, H + 'fuel_pl', WHITE, style='OSD_14_regular', wpx=330, value='-',
         visible=H + 'fuel_pl_on', fer='none')

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
    vn, vf = split_leaf(H + 'p1')
    car_view = overlay_texture('rd_ovl_car0', union.resize((836, 472), Image.LANCZOS))
    car_quad('rd_ovl_car0', car_view, (0, 0, 1670, 942), PART_N, vn, 51)
    car_quad('rd_ovl_car0_fd', car_view, fer_car((0, 0, 1670, 942)), PART_N, vf, 51)
    car_quad('rd_ovl_car0_md', car_view, mc_car((0, 0, 1670, 942)), PART_N, team_leaf(H + 'p1', 'm'), 51)
    car_quad('rd_ovl_car0_ed', car_view, me_car((0, 0, 1670, 942)), PART_N, team_leaf(H + 'p1', 'e'), 51)
    car_quad('rd_ovl_car0_bd', car_view, rb_car((0, 0, 1670, 942)), PART_N, team_leaf(H + 'p1', 'b'), 51)
    car_quad('rd_ovl_car0_ad', car_view, am_car((0, 0, 1670, 942)), PART_N, team_leaf(H + 'p1', 'am'), 51)
    car_quad('rd_ovl_car0_wd', car_view, wi_car((0, 0, 1670, 942)), PART_N, team_leaf(H + 'p1', 'wi'), 51)
    car_quad('rd_ovl_car0_ud', car_view, au_car((0, 0, 1670, 942)), PART_N, team_leaf(H + 'p1', 'au'), 51)
    car_quad('rd_ovl_car0_hd', car_view, hs_car((0, 0, 1670, 942)), PART_N, team_leaf(H + 'p1', 'hs'), 51)
    car_quad('rd_ovl_car0_ld', car_view, al_car((0, 0, 1670, 942)), PART_N, team_leaf(H + 'p1', 'al'), 51)

    # bottom left: fuel mode | differential; bottom right: brake bias
    DEFAULT_FER[0] = 'none'  # 1.2.22: the car and pit pages' values: the Ferrari draws its own
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
    text('rd_rejoin', 290, 475, H + 'rejoin', WHITE, scale=1.8, wpx=400, value='-', page=2)
    # the cars you'd come out between (1.2.2): the one ahead / the one behind
    label('rd_l_rejoin_b', 640, 420, 'BETWEEN', page=2)
    text('rd_rejoin_cars', 640, 490, H + 'rejoin_cars', WHITE, scale=1.15, style='OSD_19_bold', wpx=360, value='-',
         page=2)
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
    DEFAULT_FER[0] = None

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

    # ---- the 2026 Ferrari's own pages (1.2.18 race page; 1.2.22: every page, after the user's mock-ups). One frame on
    # all of them: a top row of three boxes, a left column of three boxes (SPEED / REV on top), a block left of the
    # gear, the gear in the centre (the damage page puts the car there), the tyre grid, the MGU-K bar on the right,
    # the mode bar under the gear and ERS SOC under that. Five layouts, by the plugin's Ferrari state: r race,
    # q practice / qualifying / time trial, s race under a safety car / VSC, d the car (damage) page, p the pit page.
    # An element shown in several layouts is one element on the leaf fp_<states> (kFpCombos, written by the plugin).
    # Every text is white (the wheel colours text only up to widget 98 / node 104); colour comes from boxes. Text
    # positions are baselines (the text sits above the y given). Canvas px; the screen is mapped straight (1.2.20).
    from PIL import ImageFont as _FF

    def fp_font(size):
        f = _FF.truetype(os.path.join(os.environ.get('WINDIR', 'C:/Windows'), 'Fonts', 'bahnschrift.ttf'), size)
        f.set_variation_by_name('SemiBold')
        return f

    fp_combos = set()

    def fp_leaf(states):
        if states in ('fp',) or states.startswith('fp_') or '_' in states:
            return states  # a leaf of its own (the plugin checks the layout)
        fp_combos.add(states)
        return 'fp_' + states

    # the frame
    TOP_L, TOP_B, TOP_R = (18, 48, 558, 146), (578, 48, 720, 146), (1180, 48, 1630, 146)
    LC1, LC2, LC3 = (18, 175, 338, 420), (18, 438, 338, 685), (18, 705, 338, 872)
    MID, GEAR, TYRES = (370, 175, 663, 560), (695, 175, 975, 560), (1007, 175, 1445, 468)
    MODE, MGUK = (592, 588, 1128, 690), (1555, 178, 1630, 655)
    SOC_BAR = (665, 767, 1230, 853)

    def fp_picture(tag, boxes, lines, labels):
        big = Image.new('L', (1672, 944), 0)
        d = ImageDraw.Draw(big)
        for b in boxes:
            d.rectangle(list(b), outline=255, width=5)
        for l in lines:
            d.line(list(l), fill=255, width=5)
        for word, x, y, size in labels:
            d.text((x, y), word, font=fp_font(size), fill=255, anchor='lm')
        car_quad(f'rd_fp_pic_{tag}_f', overlay_texture(f'rd_ovl_fp{tag}3', big.resize((836, 472), Image.LANCZOS)),
                 (0, 0, 1670, 942), LINE, H + fp_leaf(tag), L_SHAPE)

    def cell2(b):  # a box's two cells, top and bottom
        x0, y0, x1, y1 = b
        m = (y0 + y1) // 2
        return (x0, y0, x1, m), (x0, m, x1, y1)

    def half_lines(b):
        x0, y0, x1, y1 = b
        return [(x0, (y0 + y1) // 2, x1, (y0 + y1) // 2)]

    def lab(b, word, size=38):
        return (word, b[0] + 14, b[1] + 28, size)

    TY_LINES = [(1226, 175, 1226, 468), (1007, 321, 1445, 321)]
    TY_LABELS = [('FL', 1020, 200, 30), ('FR', 1239, 200, 30), ('RL', 1020, 346, 30), ('RR', 1239, 346, 30)]
    SOC_LABEL = [('SOC', 505, 742, 42)]
    lc1t, lc1b = cell2(LC1)
    lc2t, lc2b = cell2(LC2)
    lc3t, lc3b = cell2(LC3)
    midt, midb = cell2(MID)
    m3 = [(MID[0], MID[1] + i * (MID[3] - MID[1]) // 3, MID[2], MID[1] + (i + 1) * (MID[3] - MID[1]) // 3)
          for i in range(3)]
    common_boxes = [TOP_L, TOP_R, LC1, LC2]
    # r: the race page
    fp_picture('r', common_boxes + [LC3, MID, GEAR, TYRES],
               half_lines(LC1) + half_lines(LC2) + half_lines(LC3) + half_lines(MID) + TY_LINES,
               [lab(lc1t, 'SPEED'), lab(lc1b, 'REV'), lab(lc2t, 'LAP'), lab(lc2b, 'TO GO'), lab(lc3t, 'BB', 32),
                lab(lc3b, 'FUEL', 32), lab(midt, 'AHEAD'), lab(midb, 'BEHIND')] + TY_LABELS + SOC_LABEL)
    # q: practice / qualifying / time trial
    fp_picture('q', common_boxes + [LC3, MID, GEAR, TYRES],
               half_lines(LC1) + half_lines(LC2) + [(MID[0], m3[1][1], MID[2], m3[1][1]),
                                                    (MID[0], m3[2][1], MID[2], m3[2][1])] + TY_LINES,
               [('LAST LAP', 32, 97, 32), ('TO PB', 1194, 97, 32), lab(lc1t, 'SPEED'), lab(lc1b, 'REV'),
                lab(lc2t, 'STINT LAP'), lab(lc2b, 'TIME LEFT'), lab(LC3, 'PERSONAL BEST'), lab(m3[0], 'S1'),
                lab(m3[1], 'S2'), lab(m3[2], 'S3')] + TY_LABELS + SOC_LABEL)
    # s: the race under a safety car / VSC
    mid_s = ((MID[0], MID[1], MID[2], 388), (MID[0], 388, MID[2], MID[3]))
    fp_picture('s', [TOP_R, LC1, LC2, MID, GEAR, TYRES],
               half_lines(LC1) + half_lines(LC2) + [(MID[0], 388, MID[2], 388), (GEAR[0], 420, GEAR[2], 420)] +
               TY_LINES,
               [('DELTA', 1194, 97, 32), lab(lc1t, 'LAP'), lab(lc1b, 'TO GO'), lab(lc2t, 'BB'), lab(lc2b, 'FUEL'),
                lab(mid_s[0], 'EST. PIT EXIT'), lab(mid_s[1], 'PIT WINDOW'), ('SPEED', GEAR[0] + 14, 448, 34)] +
               TY_LABELS + SOC_LABEL)
    # d: the car (damage) page
    CAR = (MID[0], MID[1], 1518, MID[3])
    fp_picture('d', common_boxes + [LC3, CAR], half_lines(LC1) + half_lines(LC2) + half_lines(LC3),
               [lab(lc1t, 'SPEED'), lab(lc1b, 'REV'), lab(lc2t, 'FUEL MODE'), lab(lc2b, 'DIFF'), lab(lc3t, 'BB', 32),
                lab(lc3b, 'FUEL', 32)] + SOC_LABEL)
    # p: the pit page
    fp_picture('p', common_boxes + [LC3, MID, GEAR, TYRES], half_lines(LC1) + half_lines(LC2) + half_lines(MID) +
               TY_LINES,
               [('PIT WINDOW', 32, 97, 34), ('TYRES', 1194, 97, 32), lab(lc1t, 'SPEED'), lab(lc1b, 'REV'),
                lab(lc2t, 'LAP'), lab(lc2b, 'TO GO'), lab(LC3, 'PIT STOP TIME'), lab(midt, 'REJOIN'),
                lab(midb, 'REPAIR WING')] + TY_LABELS + SOC_LABEL)

    def fp_text(name, x, y, dbentry, states, scale=1.15, wpx=300, style='OSD_24_bold', value='-'):
        """A Ferrari page value, white, its baseline at y."""
        lw = W(wpx)
        nm = name + '_f'
        lcd.add_text(nm, 'speedo_value', centred(X(x), lw, scale), Y(y), width=lw, layer=L_TEXT, style=style)
        if scale != 1.0:
            lcd.move(nm, sx=scale, sy=scale)
        tpl.add_label(f'{nm}_w', nm, dbentry, value=value, colour=None, visible=H + fp_leaf(states))

    def fp_box(name, b, colour, states, layer=None):
        """A Ferrari page box, coloured by its material (no colour override)."""
        car_quad(name + '_f', white_texture(), b, colour, H + fp_leaf(states), L_PART if layer is None else layer)

    def inset(b, k=3):
        return (b[0] + k, b[1] + k, b[2] - k, b[3] - k)

    RED, GREEN, BLUE, GREY = (165, 25, 30), (35, 135, 55), (35, 100, 160), (70, 70, 76)
    PURPLE, AMBER, YELLOW_BOX = (110, 60, 200), (190, 140, 0), (205, 160, 0)
    CX = lambda b: (b[0] + b[2]) / 2  # noqa: E731
    LCX = (LC1[0] + LC1[2]) / 2 + 10

    # ---- the top row
    fp_text('rd_fp_lt', CX(TOP_L), 132, H + 'cur', 'rd', scale=1.25, wpx=520)           # the lap time
    fp_text('rd_fp_qlt', 380, 132, H + 'cur', 'q', scale=1.15, wpx=330)                 # last lap (practice / q)
    for tag, colour in (('p', PURPLE), ('g', GREEN), ('y', AMBER)):                    # the sector just finished
        fp_box(f'rd_fp_sec_{tag}', TOP_B, colour, f'fp_sec_{tag}')
    fp_text('rd_fp_sec', CX(TOP_B), 132, H + 'fp_sec', 'fp_sec_on', scale=1.3, wpx=135, value='S1')
    fp_box('rd_fp_sess', TOP_B, PURPLE, 'q')                                            # the session (FP2, Q1)
    fp_text('rd_fp_sesst', CX(TOP_B), 132, H + 'fp_sess', 'q', scale=1.1, wpx=135)
    for tag, colour in (('neg', GREEN), ('pos', RED), ('none', GREY), ('sc', YELLOW_BOX)):  # the delta's box
        fp_box(f'rd_fp_d{tag}', TOP_R, colour, f'fp_d{tag}')
    fp_text('rd_fp_delta', CX(TOP_R), 132, H + 'fp_delta', 'rd', scale=1.35, wpx=430)
    fp_text('rd_fp_delta2', 1480, 132, H + 'fp_delta', 'qs', scale=1.3, wpx=290)
    fp_box('rd_fp_scbanner', TOP_L, YELLOW_BOX, 's')                                    # SAFETY CAR / VSC
    fp_text('rd_fp_scname', CX(TOP_L), 130, H + 'fp_scname', 's', scale=1.05, wpx=520)
    fp_box('rd_fp_pwopen', inset(TOP_L), GREEN, 'pw_g_f')                               # the pit window, open
    fp_text('rd_fp_pw', 420, 132, H + 'pw', 'p', scale=1.35, wpx=260)
    for tag, colour in (('s', (200, 30, 35)), ('m', (205, 165, 0)), ('h', (120, 120, 126)), ('i', (40, 150, 60)),
                        ('w', (35, 90, 200))):                                          # the tyres on the car
        fp_box(f'rd_fp_ty{tag}', TOP_R, colour, f'ty_{tag}_f')
    fp_text('rd_fp_tyre', 1480, 132, H + 'tyre', 'p', scale=1.1, wpx=290)

    # ---- the left column (values sit on the bottom of their cells)
    fp_box('rd_fp_red1', inset(lc1b), RED, 'rdpqs')                                     # REV / TO GO under a SC
    fp_box('rd_fp_red2', inset(lc2b), RED, 'rdpq')                                      # TO GO / DIFF / TIME LEFT
    fp_text('rd_fp_spd', LCX, lc1t[3] - 14, H + 'fp_spd', 'rdpq', scale=1.2)
    fp_text('rd_fp_rev', LCX, lc1b[3] - 14, H + 'fp_rev', 'rdpq', scale=1.2)
    fp_text('rd_fp_lap', LCX, lc2t[3] - 14, H + 'fp_lap', 'rp', scale=1.2)
    fp_text('rd_fp_togo', LCX, lc2b[3] - 14, H + 'fp_togo', 'rpq', scale=1.2)
    fp_text('rd_fp_bb', LCX, lc3t[3] - 8, H + 'bbal', 'rd', scale=0.95)
    fp_text('rd_fp_fuel', LCX, lc3b[3] - 8, H + 'fuel', 'rd', scale=0.95)
    fp_text('rd_fp_mix', LCX, lc2t[3] - 14, H + 'mix', 'd', scale=1.2)
    fp_text('rd_fp_diff', LCX, lc2b[3] - 14, H + 'diff', 'd', scale=1.2)
    fp_text('rd_fp_pst', LCX, LC3[3] - 18, H + 'pst', 'p', scale=1.4)
    fp_text('rd_fp_stint', LCX, lc2t[3] - 14, H + 'fp_stint', 'q', scale=1.2)
    fp_text('rd_fp_pb', LCX, LC3[3] - 18, H + 'fp_pb', 'q', scale=1.15)
    fp_text('rd_fp_slap', LCX, lc1t[3] - 14, H + 'fp_lap', 's', scale=1.2)
    fp_text('rd_fp_stogo', LCX, lc1b[3] - 14, H + 'fp_togo', 's', scale=1.2)
    fp_text('rd_fp_sbb', LCX, lc2t[3] - 14, H + 'bbal', 's', scale=1.2)
    fp_text('rd_fp_sfuel', LCX, lc2b[3] - 14, H + 'fuel', 's', scale=1.2)

    # ---- the block left of the gear
    for i, (c, b) in enumerate(((midt, 'code_a'), (midb, 'code_b'))):                  # race: ahead / behind
        fp_text(f'rd_fp_code{i}', CX(MID), c[3] - 82, H + b, 'fp_gaps', scale=0.8, wpx=270, value='')  # 1.2.24
        fp_text(f'rd_fp_gap{i}', CX(MID), c[3] - 12, H + f'fp_gap{i}', 'fp_gaps', scale=1.05, wpx=280, value='')
    for k, b in enumerate(m3, 1):                                                       # practice / qualifying
        for tag, colour in (('p', PURPLE), ('g', GREEN), ('y', AMBER)):
            fp_box(f'rd_fp_s{k}{tag}', inset(b), colour, f's{k}_{tag}_f')
        fp_text(f'rd_fp_s{k}', CX(MID) + 20, b[3] - 16, H + f's{k}', 'q', scale=1.15, wpx=260)
    fp_text('rd_fp_pitexit', CX(MID), mid_s[0][3] - 20, H + 'pitnow', 's', scale=1.6, wpx=270)  # under a SC
    fp_text('rd_fp_spw', CX(MID), mid_s[1][3] - 20, H + 'pw', 's', scale=1.25, wpx=270)
    fp_text('rd_fp_rejoin', CX(MID), midt[3] - 18, H + 'rejoin', 'p', scale=1.6, wpx=270)  # the pit page
    fp_box('rd_fp_wingfix', inset(midb), RED, 'fp_wingfix')
    fp_text('rd_fp_wing', CX(MID), midb[3] - 18, H + 'fp_wing', 'p', scale=1.6, wpx=270)

    # ---- the gear (the race, practice / qualifying, the pit page); under a SC on yellow, the speed under it
    fp_text('rd_fp_gear', CX(GEAR), GEAR[3] - 70, H + 'gear', 'rqp', scale=1.25, wpx=270, style='OSD_80_bold',
            value='N')
    fp_box('rd_fp_sgearbox', (GEAR[0] + 3, GEAR[1] + 3, GEAR[2] - 3, 417), YELLOW_BOX, 's')
    fp_text('rd_fp_sgear', CX(GEAR), 390, H + 'gear', 's', scale=0.95, wpx=270, style='OSD_80_bold', value='N')
    fp_text('rd_fp_sspd', CX(GEAR), GEAR[3] - 16, H + 'fp_spd', 's', scale=1.3, wpx=270)

    # ---- the tyres: temperatures (race, practice / qualifying, SC), wear on its colour (the pit page)
    TCELL = {'fl': (1007, 175, 1226, 321), 'fr': (1226, 175, 1445, 321), 'rl': (1007, 321, 1226, 468),
             'rr': (1226, 321, 1445, 468)}
    for corner, b in TCELL.items():
        fp_text(f'rd_fp_t{corner}', CX(b) + 10, b[3] - 18, H + f't_{corner}', 'rqs', scale=1.3, wpx=200,
                style='OSD_19_bold', value='--')
        for tag, colour in (('g', GREEN), ('a', AMBER), ('r', RED)):
            fp_box(f'rd_fp_w{tag}{corner}', inset(b), colour, f'w{tag}_{corner}_f')
        fp_text(f'rd_fp_w{corner}', CX(b) + 10, b[3] - 18, H + f'w_{corner}', 'p', scale=1.3, wpx=200,
                style='OSD_19_bold', value='--')

    # ---- every page: the mode bar, the MGU-K bar (blue deploying, red harvesting), ERS SOC
    for tag, colour in (('p', PURPLE), ('g', GREEN), ('n', GREY)):
        fp_box(f'rd_fp_mode{tag}', MODE, colour, f'fp_mode_{tag}')
    fp_text('rd_fp_mode', CX(MODE), MODE[3] - 26, H + 'tm_badge', 'fp_mode', scale=1.15, wpx=520)
    fp_box('rd_fp_kdep', MGUK, (60, 150, 190), 'fp_k_dep')
    fp_box('rd_fp_khar', MGUK, (205, 30, 45), 'fp_k_har')
    fp_text('rd_fp_soc', 560, 848, H + 'soc_n', 'rqsdp', scale=1.35, wpx=150)
    SEG = (SOC_BAR[2] - SOC_BAR[0]) / 20
    for i in range(20):
        x0 = SOC_BAR[0] + i * SEG
        fp_box(f'rd_fp_seg{i:02d}', (x0 + 2, SOC_BAR[1], x0 + SEG - 2, SOC_BAR[3]), (79, 138, 69), f'fp_seg{i:02d}')
        fp_box(f'rd_fp_sob{i:02d}', (x0 + 2, SOC_BAR[1], x0 + SEG - 2, SOC_BAR[3]), (40, 110, 255), f'fp_sob{i:02d}')  # overtake

    # ---- the car (damage) page: the car, the other cars' parts copied into the box (car_part / car_fixed above)
    hdr = ['// Written by tools/build_ui.py: the Ferrari pages\' layout sets (fp_<states>: r race, q practice /',
           '// qualifying, s race under a safety car, d car page, p pit page); the plugin writes fp_<states> true',
           '// on the 2026 Ferrari when its current layout is one of them.', '#pragma once', '',
           'inline constexpr const char *kFpCombos[] = {']
    hdr += [f'    "{c}",' for c in sorted(fp_combos)] + ['};', '']
    open(os.path.join(ROOT, 'native', 'src', 'fp_combos.hpp'), 'w', encoding='utf-8').write('\n'.join(hdr))

    # ---- the 2026 McLaren's own pages (1.2.27, the user's mock-ups from McLaren's 2026 race screen). Minimal, no
    # boxes: grey labels and white values, the battery as a vertical bar on the left and the revs as one on the
    # right, the mode badge over a big gear in the centre with the MGU-K line under it (orange deploying, red
    # harvesting), tyre values round a car icon. The user chose coloured text here (green / red / yellow deltas,
    # the compound's colour, an orange YES; dark text on the safety car's yellow): about a dozen colour slots.
    # Layouts as the Ferrari's (r race, q practice / qualifying, s race under a SC, d car page, p pit page); an
    # element shown in several is one element on mc_<states> (kMcCombos). Positions are the mock-ups' 1280 x 720
    # pixels, scaled to the canvas; text y is a baseline.
    KM = 1670 / 1280
    mc_combos = set()

    def mc_leaf(states):
        if '_' in states:
            return states  # a leaf of its own (the plugin checks the layout and the team)
        mc_combos.add(states)
        return 'mc_' + states

    def M(v):
        return v * KM

    MC_LABEL = (150, 150, 155)
    MC_GREEN, MC_RED, MC_YELLOW, MC_ORANGE = (140, 200, 120), (235, 80, 70), (250, 215, 70), (215, 120, 80)
    MC_DARK = (20, 20, 22)

    def mc_text(name, x, y, dbentry, states, size=40, align='left', colour=None, value='-', style='OSD_24_bold',
                chars=5):
        """A McLaren page value: mock-up x (left edge, right edge or centre by align), baseline y, cap height
        size (mock-up px)."""
        scale = M(size) / 250.0 if style == 'OSD_80_bold' else size / 34.0  # OSD_80's glyphs ~250 px a unit
        wpx = 900
        lw = W(wpx)
        nm = name + '_m'
        if align == 'left':
            left = X(M(x))
        elif align == 'right':
            # 1.2.29: no right alignment in the game's text: 1.2.27 (box left = right edge - box width) drew the text
            # ending where the box began; 1.2.28 (box left = right edge) worked unscaled but pushed scaled text off
            # the screen. A right-hand value is left-aligned from its right edge less its likely width (chars at
            # ~45 canvas px each at scale 1; the photos: STRAIGHT ACTIVE at scale 0.88 was ~700 px)
            left = X(M(x) - chars * 45 * scale)
        else:
            left = centred(X(M(x)), lw, scale)
        lcd.add_text(nm, 'speedo_value', left, Y(M(y)), width=lw, layer=L_TEXT, style=style,
                     halign=None if align == 'centre' else 'left')
        if scale != 1.0:
            lcd.move(nm, sx=scale, sy=scale)
        tpl.add_label(f'{nm}_w', nm, dbentry, value=value, colour=colour, visible=H + mc_leaf(states))

    def mc_box(name, x0, y0, x1, y1, colour, states, layer=None):
        car_quad(name + '_m', white_texture(), (M(x0), M(y0), M(x1), M(y1)), colour, H + mc_leaf(states),
                 L_PART if layer is None else layer)

    SOC_SEGS = [(32, 398 + i * (258 / 13), 64, 398 + (i + 1) * (258 / 13) - 2) for i in range(13)]  # top to bottom
    RPM_SEGS = [(1229, 45 + i * (617 / 20), 1251, 45 + (i + 1) * (617 / 20) - 3) for i in range(20)]
    ICON = (601, 453, 679, 600)  # the car icon the tyre values sit round

    def mc_picture(tag, labels, lines=(), boxes=(), icon=True, colour=None, dark=False):
        big = Image.new('L', (1672, 944), 0)
        d = ImageDraw.Draw(big)
        for word, x, y, anchor, size in labels:
            d.text((M(x), M(y)), word, font=fp_font(int(M(size))), fill=255, anchor=anchor)
        for x0, y0, x1, y1 in lines:
            d.line([M(x0), M(y0), M(x1), M(y1)], fill=255, width=3)
        for x0, y0, x1, y1 in boxes:
            d.rectangle([M(x0), M(y0), M(x1), M(y1)], outline=255, width=2)
        if icon:  # the car icon: four wheels and an H frame
            ix0, iy0, ix1, iy1 = ICON
            for wx, wy in ((ix0, iy0), (ix1 - 20, iy0), (ix0, iy1 - 34), (ix1 - 20, iy1 - 34)):
                d.rectangle([M(wx), M(wy), M(wx + 20), M(wy + 34)], fill=255)
            d.rectangle([M(ix0 + 18), M(iy0 + 12), M(ix1 - 18), M(iy0 + 22)], fill=255)
            d.rectangle([M(ix0 + 18), M(iy1 - 22), M(ix1 - 18), M(iy1 - 12)], fill=255)
            d.rectangle([M(636), M(iy0 + 12), M(644), M(iy1 - 12)], fill=255)
        car_quad(f'rd_mc_pic_{tag}_m', overlay_texture(f'rd_ovl_mc{tag[0]}{"k" if dark else "0"}',
                                                       big.resize((836, 472), Image.LANCZOS)),
                 (0, 0, 1670, 942), colour or MC_LABEL, H + mc_leaf(tag.rstrip('k')), L_SHAPE + (2 if dark else 0))

    # every page: SOC and RPM (labels, segment outlines), drawn into each layout's picture
    COMMON = [('SOC', 49, 369, 'mm', 22), ('RPM', 1240, 683, 'mm', 22)]
    SEG_OUT = [s for s in SOC_SEGS] + [s for s in RPM_SEGS]
    INTERVAL_LINES = [(889, 423 + 61 * i, 1192, 423 + 61 * i) for i in range(4)]
    SECTOR_LINES = [(889, 459, 1192, 459), (889, 538, 1192, 538)]
    mc_picture('r', COMMON + [('LAP TIME', 107, 52, 'lm', 22), ('FUEL Δ', 107, 188, 'lm', 22), ('LAP', 107, 307, 'lm', 22),
                              ('POS', 107, 484, 'lm', 22), ('PREV LAP Δ', 1190, 52, 'rm', 22), ('SPEED', 1190, 263, 'rm', 22),
                              ('INTERVAL  /  s', 889, 333, 'lm', 22), ('TYRE TEMP', 640, 410, 'mm', 22)],
               INTERVAL_LINES, SEG_OUT)
    mc_picture('q', COMMON + [('LAST LAP', 107, 52, 'lm', 22), ('PERSONAL BEST', 107, 188, 'lm', 22),
                              ('STINT LAP', 107, 484, 'lm', 22), ('Δ TO PB', 1190, 52, 'rm', 22), ('SPEED', 1190, 263, 'rm', 22),
                              ('LAST LAP SECTORS', 889, 351, 'lm', 22), ('S1', 889, 418, 'lm', 28), ('S2', 889, 497, 'lm', 28),
                              ('S3', 889, 576, 'lm', 28), ('TYRE TEMP', 640, 410, 'mm', 22)], SECTOR_LINES, SEG_OUT)
    mc_picture('s', COMMON + [('LAP', 107, 419, 'lm', 22), ('FUEL Δ', 107, 561, 'lm', 22), ('PIT WINDOW', 889, 419, 'lm', 22),
                              ('BRAKE BIAS', 889, 561, 'lm', 22), ('TYRE TEMP', 640, 410, 'mm', 22)], (), SEG_OUT)
    mc_picture('sk', [('VSC DELTA', 107, 52, 'lm', 22), ('EST. PIT EXIT', 1190, 52, 'rm', 22)], [(545, 250, 735, 250)],
               icon=False, colour=(245, 245, 245), dark=True)  # 1.3.11: white on amber
    mc_picture('d', COMMON + [('LAP TIME', 107, 52, 'lm', 22), ('PREV LAP Δ', 1190, 52, 'rm', 22),
                              ('FUEL MODE', 109, 574, 'lm', 22), ('DIFF', 405, 574, 'lm', 22), ('BRAKE BIAS', 681, 574, 'lm', 22),
                              ('FUEL Δ', 994, 574, 'lm', 22)], (), SEG_OUT, icon=False)
    mc_picture('p', COMMON + [('PIT WINDOW', 107, 52, 'lm', 22), ('REJOIN', 107, 188, 'lm', 22), ('REPAIR WING', 107, 307, 'lm', 22),
                              ('LAP', 107, 484, 'lm', 22), ('TYRES', 1190, 52, 'rm', 22), ('SPEED', 1190, 263, 'rm', 22),
                              ('TYRE WEAR', 640, 410, 'mm', 22), ('PIT STOP TIME', 889, 414, 'lm', 22)], (), SEG_OUT)

    # ---- every page: the mode badge, the battery and the revs
    for tag, colour in (('p', (110, 60, 200)), ('g', (35, 135, 55)), ('n', (70, 70, 76))):  # 1.2.28: bigger
        mc_box(f'rd_mc_mode{tag}', 470, 34, 810, 98, colour, f'mc_mode_{tag}')
    mc_text('rd_mc_mode', 640, 76, H + 'tm_badge', 'mc_mode', size=20, align='centre', value='')
    for i, (x0, y0, x1, y1) in enumerate(SOC_SEGS):  # mc_seg00 is the bottom one
        mc_box(f'rd_mc_seg{12 - i:02d}', x0 + 1, y0 + 1, x1 - 1, y1 - 1, (160, 175, 130), f'mc_seg{12 - i:02d}')
        mc_box(f'rd_mc_sob{12 - i:02d}', x0 + 1, y0 + 1, x1 - 1, y1 - 1, (40, 110, 255), f'mc_sob{12 - i:02d}')
    mc_text('rd_mc_soc', 49, 692, H + 'soc_n', 'rqsdp', size=22, align='centre')
    for i, (x0, y0, x1, y1) in enumerate(RPM_SEGS):  # mc_rpm00 is the bottom one
        mc_box(f'rd_mc_rpm{19 - i:02d}', x0 + 1, y0 + 1, x1 - 1, y1 - 1, (220, 220, 214), f'mc_rpm{19 - i:02d}')
    # the MGU-K line under the gear: orange deploying, red harvesting, grey otherwise
    for tag, colour in (('i', (90, 90, 95)), ('d', MC_ORANGE), ('h', (200, 40, 45))):
        mc_box(f'rd_mc_k{tag}', 448, 356, 832, 369, colour, f'mc_k_{tag}')
        mc_box(f'rd_mc_kd{tag}', 448, 525, 832, 537, colour, f'mc_kd_{tag}')  # the car page's, lower

    # ---- the gear, the speed, the revs as text
    mc_text('rd_mc_gear', 640, 300, H + 'gear', 'rqp', size=165, align='centre', style='OSD_80_bold', value='N')
    mc_text('rd_mc_speed', 1172, 230, H + 'fp_spd', 'rqp', size=50, align='right', chars=3)  # 1.2.30: clear of the RPM bar
    mc_text('rd_mc_rpm', 107, 652, H + 'fp_rev', 'rqp', size=26, align='left')
    # ---- the left column
    mc_text('rd_mc_lt', 107, 130, H + 'cur', 'rqd', size=44)                        # lap time / last lap
    mc_text('rd_mc_fuel', 107, 262, H + 'fuel', 'r', size=44)
    mc_text('rd_mc_lap', 107, 383, H + 'lap', 'r', size=44)
    mc_text('rd_mc_pos', 107, 558, H + 'pos', 'r', size=44)
    mc_text('rd_mc_pb', 107, 262, H + 'fp_pb', 'q', size=44)
    mc_text('rd_mc_sessl', 107, 307 + 8, H + 'sess_label', 'q', size=20, value='TIME LEFT')
    mc_text('rd_mc_left', 107, 383, H + 'fp_togo', 'q', size=44)
    mc_text('rd_mc_stint', 107, 558, H + 'fp_stint', 'q', size=44)
    # ---- the delta (green gaining, red losing in a race, yellow behind your best in practice / qualifying)
    for tag, colour in (('dneg', MC_GREEN), ('dpos_r', MC_RED), ('dpos_q', MC_YELLOW), ('dnone', None)):
        mc_text(f'rd_mc_{tag}', 1190, 132, H + 'fp_delta', f'mc_{tag}', size=46, align='right', colour=colour)  # 1.2.30: 58 covered its label
    # ---- race: the interval list, two or three cars either side
    for i in range(5):
        y = 402 + 61 * i
        mc_text(f'rd_mc_code{i}', 889, y, H + f'mc_code{i}', f'mc_row{i}', size=30, value='')
        mc_text(f'rd_mc_gap{i}', 1190, y, H + f'mc_gap{i}', f'mc_row{i}', size=34, align='right', value='')
    # ---- practice / qualifying: last lap's sectors
    for k, y in ((1, 432), (2, 511), (3, 590)):
        mc_text(f'rd_mc_s{k}', 1190, y, H + f's{k}', 'q', size=36, align='right', chars=6)
    # ---- tyres round the car icon: temperatures (race, practice, SC), wear (pit page)
    for corner, (x, y) in {'fl': (545, 500), 'fr': (733, 500), 'rl': (545, 606), 'rr': (733, 606)}.items():
        mc_text(f'rd_mc_t{corner}', x, y, H + f't_{corner}', 'rqs', size=40, align='centre', value='--')
        mc_text(f'rd_mc_w{corner}', x, y, H + f'w_{corner}', 'p', size=40, align='centre', value='--')
    # ---- the safety car / VSC: the top half yellow, dark writing
    mc_box('rd_mc_scy', 0, 0, 1280, 345, (185, 138, 10), 's', L_SHAPE + 1)  # 1.3.11: amber, white text (no colour slots)
    mc_text('rd_mc_scd', 107, 130, H + 'fp_delta', 's', size=44)
    mc_text('rd_mc_scname', 107, 215, H + 'fp_scname', 's', size=30)
    mc_text('rd_mc_sgear', 640, 225, H + 'gear', 's', size=110, align='centre', style='OSD_80_bold', value='N')
    mc_text('rd_mc_sspd', 640, 320, H + 'fp_spd', 's', size=56, align='centre')
    mc_text('rd_mc_spit', 1190, 130, H + 'pitnow', 's', size=58, align='right', chars=3)
    mc_text('rd_mc_slap', 107, 495, H + 'lap', 's', size=44)
    mc_text('rd_mc_sfuel', 107, 635, H + 'fuel', 's', size=44)
    mc_text('rd_mc_spw', 889, 495, H + 'pw', 's', size=44)
    mc_text('rd_mc_sbb', 889, 635, H + 'bbal', 's', size=44)
    # ---- the car page: the car (car_part / car_fixed draw it into this box), four values under it
    mc_text('rd_mc_mix', 109, 645, H + 'mix', 'd', size=40)
    mc_text('rd_mc_diff', 405, 645, H + 'diff', 'd', size=40)
    mc_text('rd_mc_bb', 681, 645, H + 'bbal', 'd', size=40)
    mc_text('rd_mc_dfuel', 994, 645, H + 'fuel', 'd', size=40)
    # ---- the pit page
    mc_text('rd_mc_pw', 107, 130, H + 'pw', 'p', size=48)
    mc_text('rd_mc_rejoin', 107, 262, H + 'rejoin', 'p', size=44)
    mc_text('rd_mc_wingy', 107, 383, H + 'fp_wing', 'mc_wing_yes', size=44, colour=MC_ORANGE)
    mc_text('rd_mc_wingn', 107, 383, H + 'fp_wing', 'mc_wing_no', size=44)
    mc_text('rd_mc_plap', 107, 558, H + 'lap', 'p', size=44)
    for tag, colour in (('s', (235, 70, 65)), ('m', MC_YELLOW), ('h', None), ('i', (90, 200, 100)),
                        ('w', (80, 150, 255))):
        mc_text(f'rd_mc_ty{tag}', 1150, 132, H + 'tyre', f'ty_{tag}_m', size=46, align='right', colour=colour,
                chars={'s': 5.4, 'm': 7.4, 'h': 5.4, 'i': 6.2, 'w': 4.6}[tag])  # 1.2.32: letters are wider than digits
    mc_text('rd_mc_pst', 889, 485, H + 'pst', 'p', size=50)

    hdr = ['// Written by tools/build_ui.py: the McLaren pages\' layout sets (mc_<states>), as kFpCombos.', '#pragma once',
           '', 'inline constexpr const char *kMcCombos[] = {']
    hdr += [f'    "{c}",' for c in sorted(mc_combos)] + ['};', '']
    open(os.path.join(ROOT, 'native', 'src', 'mc_combos.hpp'), 'w', encoding='utf-8').write('\n'.join(hdr))

    # ---- the 2026 Mercedes's own pages (1.2.31, the user's mock-ups after Mercedes's 2025 screen). A grid of cells
    # with a header (the page's name, a 12-light RPM strip, the session's time), the gears as a column on the left
    # (the current one lit) and the battery as a column on the right. White text only (the user: Mercedes has boxes
    # to carry colour): the delta's cell tinted green / red, the tyres' cell in the compound's colour, REPAIR WING
    # in orange. Layouts as the Ferrari's and McLaren's (r q s d p), sets on me_<states> (kMeCombos); the mock-ups'
    # 1280 x 720 pixels scaled to the canvas; only left / centre aligned text (a right-hand value starts at its
    # right edge less its likely width).
    me_combos = set()

    def me_leaf(states):
        if '_' in states:
            return states
        me_combos.add(states)
        return 'me_' + states

    ME_LINE = (75, 80, 92)
    ME_LABEL = (170, 176, 188)

    def me_text(name, x, y, dbentry, states, size=40, align='left', value='-', style='OSD_24_bold', chars=5):
        scale = M(size) / 250.0 if style == 'OSD_80_bold' else size / 34.0
        lw = W(900)
        nm = name + '_e'
        if align == 'left':
            left = X(M(x))
        elif align == 'right':
            left = X(M(x) - chars * 45 * scale)
        else:
            left = centred(X(M(x)), lw, scale)
        lcd.add_text(nm, 'speedo_value', left, Y(M(y)), width=lw, layer=L_TEXT, style=style,
                     halign=None if align == 'centre' else 'left')
        if scale != 1.0:
            lcd.move(nm, sx=scale, sy=scale)
        tpl.add_label(f'{nm}_w', nm, dbentry, value=value, colour=None, visible=H + me_leaf(states))

    def me_box(name, x0, y0, x1, y1, colour, states, layer=None):
        car_quad(name + '_e', white_texture(), (M(x0), M(y0), M(x1), M(y1)), colour, H + me_leaf(states),
                 L_PART if layer is None else layer)

    GEAR_CELLS = [(20, 145 + i * 69, 55, 145 + i * 69 + 65) for i in range(8)]  # 8 at the top .. 1 at the bottom
    RPM_SEGS_E = [(484 + i * 26.8, 39, 484 + i * 26.8 + 23, 66) for i in range(12)]
    SOC_SEGS_E = [(1229, 150 + i * 24.75, 1256, 150 + i * 24.75 + 21) for i in range(20)]  # top .. bottom
    OUT = (68, 97, 1208, 696)
    V1, V2 = 489, 761

    def me_picture(tag, title, labels, lines, label_colour=None):
        big = Image.new('L', (1672, 944), 0)
        d = ImageDraw.Draw(big)
        d.rectangle([M(18), M(20), M(1262), M(86)], outline=255, width=2)            # the header
        d.text((M(49), M(49)), title, font=fp_font(int(M(30))), fill=255, anchor='lm')
        d.text((M(432), M(51)), 'RPM', font=fp_font(int(M(19))), fill=255, anchor='lm')
        d.text((M(37), M(115)), 'G', font=fp_font(int(M(19))), fill=255, anchor='mm')
        d.text((M(1244), M(114)), 'SOC', font=fp_font(int(M(19))), fill=255, anchor='mm')
        for i, (x0, y0, x1, y1) in enumerate(GEAR_CELLS):
            d.text((M((x0 + x1) / 2), M((y0 + y1) / 2)), str(8 - i), font=fp_font(int(M(22))), fill=255,
                   anchor='mm')
        for x0, y0, x1, y1 in RPM_SEGS_E + SOC_SEGS_E:
            d.rectangle([M(x0), M(y0), M(x1), M(y1)], outline=255, width=2)
        d.rectangle([M(OUT[0]), M(OUT[1]), M(OUT[2]), M(OUT[3])], outline=255, width=3)
        for x0, y0, x1, y1 in lines:
            d.line([M(x0), M(y0), M(x1), M(y1)], fill=255, width=3)
        for word, x, y, anchor, size in labels:
            d.text((M(x), M(y)), word, font=fp_font(int(M(size))), fill=255, anchor=anchor)
        car_quad(f'rd_me_pic_{tag}_e', overlay_texture(f'rd_ovl_me{tag}0', big.resize((836, 472), Image.LANCZOS)),
                 (0, 0, 1670, 942), label_colour or ME_LABEL, H + me_leaf(tag), L_SHAPE)

    # the gear cells' dark green, the header's cyan edge (every page)
    gbg = Image.new('L', (1672, 944), 0)
    gd = ImageDraw.Draw(gbg)
    for x0, y0, x1, y1 in GEAR_CELLS:
        gd.rectangle([M(x0), M(y0), M(x1), M(y1)], fill=255)
    car_quad('rd_me_gearbg_e', overlay_texture('rd_ovl_megb', gbg.resize((836, 472), Image.LANCZOS)),
             (0, 0, 1670, 942), (36, 52, 34), H + me_leaf('rqsdp'), L_BACK + 2)
    me_box('rd_me_edge', 18, 20, 24, 86, (140, 220, 235), 'rqsdp')

    FRAME = [(V1, OUT[1], V1, OUT[3]), (V2, OUT[1], V2, OUT[3]), (OUT[0], 253, OUT[2], 253), (OUT[0], 504, OUT[2], 504),
             (V1, 561, V2, 561)]
    TOPMID = [(V1, 175, V2, 175)]
    TYRE_SPLITS = [(86, 389, 472, 389), (778, 389, 1191, 389)]  # 1.2.35: midway between the values
    BOTTOM_SPLITS = [(86, 601, 472, 601), (778, 601, 1191, 601)]
    L = 19  # label size (mock-up px)
    TYRE_LABELS = [('FL', 93, 346, 'lm', 22), ('RL', 93, 445, 'lm', 22), ('FR', 785, 346, 'lm', 22),
                   ('RR', 785, 445, 'lm', 22)]
    me_picture('r', 'RACE', [('SESSION', 975, 51, 'mm', L), ('PREV LAP Δ', 92, 119, 'lm', L), ('POS', 503, 134, 'lm', L),
                             ('LAP', 503, 211, 'lm', L), ('LAP TIME', 785, 119, 'lm', L), ('TYRE TEMP', 89, 281, 'lm', L),
                             ('TYRE TEMP', 781, 281, 'lm', L), ('GEAR', 625, 278, 'mm', L), ('AHEAD', 93, 554, 'lm', L),
                             ('BEHIND', 93, 639, 'lm', L), ('SPEED', 625, 586, 'mm', L), ('FUEL Δ', 785, 554, 'lm', L),
                             ('BRAKE BIAS', 785, 639, 'lm', L)] + TYRE_LABELS,
               FRAME + TOPMID + TYRE_SPLITS + BOTTOM_SPLITS)
    me_picture('q', 'PRACTICE', [('TIME LEFT', 975, 51, 'mm', L), ('DELTA TO BEST', 92, 119, 'lm', L),
                                 ('POS', 503, 134, 'lm', L), ('STINT LAP', 503, 211, 'lm', L), ('LAST LAP', 785, 119, 'lm', L),
                                 ('LAST LAP SECTORS', 89, 281, 'lm', L), ('S1', 90, 337, 'lm', 22), ('S2', 90, 399, 'lm', 22),
                                 ('S3', 90, 461, 'lm', 22), ('TYRE TEMP', 781, 281, 'lm', L), ('FL', 781, 351, 'lm', 22),
                                 ('FR', 989, 351, 'lm', 22), ('RL', 781, 445, 'lm', 22), ('RR', 989, 445, 'lm', 22),
                                 ('GEAR', 625, 278, 'mm', L), ('PERSONAL BEST', 93, 525, 'lm', L), ('SPEED', 625, 586, 'mm', L),
                                 ('FUEL Δ', 785, 554, 'lm', L), ('BRAKE BIAS', 785, 639, 'lm', L)],
               FRAME + TOPMID + [(86, 371, 471, 371), (86, 433, 471, 433), (983, 310, 983, 490), (777, 400, 1192, 400),
                                 (778, 601, 1191, 601)])
    me_picture('s', '', [('SESSION', 975, 51, 'mm', L), ('TYRE TEMP', 89, 281, 'lm', L), ('TYRE TEMP', 781, 281, 'lm', L),
                         ('GEAR', 625, 278, 'mm', L), ('LAP', 93, 554, 'lm', L), ('PIT WINDOW', 93, 639, 'lm', L),
                         ('SPEED', 625, 586, 'mm', L), ('FUEL Δ', 785, 554, 'lm', L), ('BRAKE BIAS', 785, 639, 'lm', L),
                         ('VSC DELTA', 92, 123, 'lm', L), ('CURRENT POS', 625, 126, 'mm', L),
                         ('EST. PIT EXIT', 785, 123, 'lm', L)] + TYRE_LABELS,
               FRAME + TYRE_SPLITS + BOTTOM_SPLITS)
    me_picture('d', 'CAR DAMAGE', [('SESSION', 975, 51, 'mm', L), ('FUEL MODE', 93, 525, 'lm', L),
                                   ('DIFFERENTIAL', 625, 586, 'mm', L), ('BRAKE BIAS', 785, 525, 'lm', L)],
               [(OUT[0], 504, OUT[2], 504), (V1, 504, V1, OUT[3]), (V2, 504, V2, OUT[3]), (V1, 561, V2, 561)])
    me_picture('p', 'PIT INFO', [('SESSION', 975, 51, 'mm', L), ('PIT WINDOW', 92, 119, 'lm', L), ('POS', 503, 134, 'lm', L),
                                 ('LAP', 503, 211, 'lm', L), ('TYRES', 785, 119, 'lm', L), ('TYRE WEAR', 89, 281, 'lm', L),
                                 ('TYRE WEAR', 781, 281, 'lm', L), ('GEAR', 625, 278, 'mm', L), ('REJOIN', 93, 554, 'lm', L),
                                 ('REPAIR WING', 93, 639, 'lm', L), ('SPEED', 625, 586, 'mm', L),
                                 ('PIT STOP TIME', 785, 525, 'lm', L)] + TYRE_LABELS,
               FRAME + TOPMID + TYRE_SPLITS + [(86, 601, 472, 601)])

    # ---- every page: the header's RPM strip and session time, the gear column, the battery column, the mode bar
    for i, (x0, y0, x1, y1) in enumerate(RPM_SEGS_E):
        colour = (140, 220, 235) if i < 4 else (250, 185, 80) if i < 8 else (190, 170, 225)
        me_box(f'rd_me_rpm{i:02d}', x0, y0, x1, y1, colour, f'me_rpm{i:02d}')
    me_text('rd_me_sess', 1180, 64, H + 'sess_left', 'rqsdp', size=36, align='right', chars=5, value='--:--')
    me_text('rd_me_scname', 49, 64, H + 'fp_scname', 's', size=24, value='SAFETY CAR')
    for i, (x0, y0, x1, y1) in enumerate(GEAR_CELLS):
        me_box(f'rd_me_g{8 - i}', x0, y0, x1, y1, (180, 235, 90), f'me_g{8 - i}', L_BACK + 3)
    for i, (x0, y0, x1, y1) in enumerate(SOC_SEGS_E):  # me_seg00 at the bottom
        me_box(f'rd_me_seg{19 - i:02d}', x0 + 1, y0 + 1, x1 - 1, y1 - 1, (110, 190, 90), f'me_seg{19 - i:02d}')  # green
        me_box(f'rd_me_sob{19 - i:02d}', x0 + 1, y0 + 1, x1 - 1, y1 - 1, (40, 110, 255), f'me_sob{19 - i:02d}')
    me_text('rd_me_soc', 1244, 680, H + 'soc_n', 'rqsdp', size=18, align='centre')
    for tag, colour in (('p', (100, 70, 160)), ('g', (35, 135, 55)), ('n', (60, 62, 70))):
        me_box(f'rd_me_mode{tag}', 490, 505, 760, 560, colour, f'me_mode_{tag}')
    me_text('rd_me_mode', 625, 542, H + 'tm_badge', 'me_mode', size=20, align='centre', value='')

    # ---- the top row: the delta's cell tinted (green gaining, red losing, nothing before there's one)
    for tag, colour in (('dneg', (25, 60, 35)), ('dpos', (75, 28, 30))):
        me_box(f'rd_me_{tag}', 70, 99, 487, 251, colour, f'me_{tag}', L_BACK + 3)
    me_text('rd_me_delta', 105, 205, H + 'fp_delta', 'rq', size=60, chars=6)
    me_text('rd_me_pos', 622, 150, H + 'pos', 'rqp', size=36)  # 1.2.35: left-aligned with LAP under it
    me_text('rd_me_lap', 622, 228, H + 'lap', 'rp', size=36)
    me_text('rd_me_stint', 622, 228, H + 'fp_stint', 'q', size=36)
    me_text('rd_me_lt', 790, 202, H + 'cur', 'rq', size=40)
    # the pit page's top row: pit window, the tyres on their compound's colour
    me_text('rd_me_pw', 105, 205, H + 'pw', 'p', size=60)
    for tag, colour in (('s', (150, 30, 35)), ('m', (150, 115, 20)), ('h', (85, 85, 92)), ('i', (30, 110, 45)),
                        ('w', (30, 70, 150))):
        me_box(f'rd_me_ty{tag}', 763, 99, 1206, 251, colour, f'ty_{tag}_e', L_BACK + 3)
    me_text('rd_me_tyre', 793, 205, H + 'tyre', 'p', size=60)
    # under a SC: three yellow cells across the top
    for i, (x0, x1) in enumerate(((69, 488), (490, 760), (762, 1207))):
        me_box(f'rd_me_scy{i}', x0, 98, x1, 252, (205, 160, 0), 's', L_BACK + 3)
    me_text('rd_me_scd', 100, 210, H + 'fp_delta', 's', size=60)
    me_text('rd_me_scpos', 625, 205, H + 'pos', 's', size=58, align='centre')
    me_text('rd_me_scpit', 790, 210, H + 'pitnow', 's', size=60)

    # ---- the middle: tyres (temperatures race / SC, wear pit page), sectors and a 2 x 2 grid (practice), the gear
    for corner, (x, y) in {'fl': (455, 362), 'rl': (455, 462), 'fr': (1175, 362), 'rr': (1175, 462)}.items():
        me_text(f'rd_me_t{corner}', x, y, H + f't_{corner}', 'rs', size=56, align='right', chars=3, value='--')
        me_text(f'rd_me_w{corner}', x, y, H + f'w_{corner}', 'p', size=56, align='right', chars=3, value='--')
    for corner, (x, y) in {'fl': (962, 365), 'fr': (1172, 365), 'rl': (962, 459), 'rr': (1172, 459)}.items():
        me_text(f'rd_me_qt{corner}', x, y, H + f't_{corner}', 'q', size=38, align='right', chars=3, value='--')
    for k, y in ((1, 351), (2, 413), (3, 475)):
        me_text(f'rd_me_s{k}', 460, y, H + f's{k}', 'q', size=38, align='right', chars=6)
    me_text('rd_me_gear', 625, 418, H + 'gear', 'rqsp', size=150, align='centre', style='OSD_80_bold', value='N')

    # ---- the bottom row
    me_text('rd_me_spd', 625, 662, H + 'fp_spd', 'rqsp', size=50, align='centre')
    me_text('rd_me_ahead', 461, 578, H + 'fp_gap0', 'r', size=36, align='right', chars=6)
    me_text('rd_me_behind', 461, 662, H + 'fp_gap1', 'r', size=36, align='right', chars=6)
    me_text('rd_me_slap', 461, 578, H + 'lap', 's', size=36, align='right', chars=5)
    me_text('rd_me_spw', 461, 662, H + 'pw', 's', size=36, align='right', chars=5)
    me_text('rd_me_fuel', 1180, 578, H + 'fuel', 'rqs', size=36, align='right', chars=5)
    me_text('rd_me_bb', 1180, 662, H + 'bbal', 'rqs', size=36, align='right', chars=3)
    me_box('rd_me_pbok', 70, 506, 487, 694, (25, 60, 35), 'q', L_BACK + 3)          # the best lap's cell green
    me_text('rd_me_pb', 97, 632, H + 'fp_pb', 'q', size=52)
    me_text('rd_me_dmix', 103, 632, H + 'mix', 'd', size=60)                        # the car page
    me_text('rd_me_ddiff', 625, 662, H + 'diff', 'd', size=46, align='centre')
    me_text('rd_me_dbb', 797, 632, H + 'bbal', 'd', size=60)
    me_text('rd_me_rejoin', 461, 578, H + 'rejoin', 'p', size=36, align='right', chars=3)  # the pit page
    me_box('rd_me_wingfix', 70, 603, 487, 694, (150, 80, 20), 'me_wing_yes', L_BACK + 3)
    me_text('rd_me_wing', 461, 662, H + 'fp_wing', 'p', size=36, align='right', chars=3)
    me_text('rd_me_pst', 793, 632, H + 'pst', 'p', size=60)

    hdr = ['// Written by tools/build_ui.py: the Mercedes pages\' layout sets (me_<states>), as kFpCombos.', '#pragma once',
           '', 'inline constexpr const char *kMeCombos[] = {']
    hdr += [f'    "{c}",' for c in sorted(me_combos)] + ['};', '']
    open(os.path.join(ROOT, 'native', 'src', 'me_combos.hpp'), 'w', encoding='utf-8').write('\n'.join(hdr))

    # ---- the 2026 Red Bull's own pages (1.2.37, the user's mock-ups). Open layout: a big speed top left with a light
    # beside it, a huge gear in the centre, the deltas and lap time on the right, a list of values down the left, the
    # battery as a blue column on the left edge, a yellow box at the bottom, the tyres as four coloured cells bottom
    # right. White text only (Red Bull has boxes to carry colour); the boxes are a shade deeper than the mock-ups' so
    # white reads on them. The light top left: green clear, yellow under a yellow flag / SC, blue for a blue flag, red
    # under a red flag; on the pit page the tyres' compound colour. Layouts r q s d p, sets on rb_<states>.
    rb_combos = set()

    def rb_leaf(states):
        if '_' in states:
            return states
        rb_combos.add(states)
        return 'rb_' + states

    RB_LABEL = (175, 178, 186)

    def rb_text(name, x, y, dbentry, states, size=40, align='left', value='-', style='OSD_24_bold', chars=5):
        scale = M(size) / 250.0 if style == 'OSD_80_bold' else size / 34.0
        lw = W(900)
        nm = name + '_b'
        if align == 'left':
            left = X(M(x))
        elif align == 'right':
            left = X(M(x) - chars * 45 * scale)
        else:
            left = centred(X(M(x)), lw, scale)
        lcd.add_text(nm, 'speedo_value', left, Y(M(y)), width=lw, layer=L_TEXT, style=style,
                     halign=None if align == 'centre' else 'left')
        if scale != 1.0:
            lcd.move(nm, sx=scale, sy=scale)
        tpl.add_label(f'{nm}_w', nm, dbentry, value=value, colour=None, visible=H + rb_leaf(states))

    def rb_box(name, x0, y0, x1, y1, colour, states, layer=None):
        car_quad(name + '_b', white_texture(), (M(x0), M(y0), M(x1), M(y1)), colour, H + rb_leaf(states),
                 L_PART if layer is None else layer)

    RB_SOC = [(0, 223 + i * (477 / 16), 61, 223 + (i + 1) * (477 / 16) - 3) for i in range(16)]  # top .. bottom
    RB_CELLS = {'fl': (935, 550, 1092, 623), 'fr': (1096, 550, 1256, 623), 'rl': (935, 627, 1092, 700),
                'rr': (1096, 627, 1256, 700)}

    def rb_picture(tag, labels, cells=True, ybox=True, dark_band=False):
        big = Image.new('L', (1672, 944), 0)
        d = ImageDraw.Draw(big)
        for x0, y0, x1, y1 in RB_SOC:
            d.rectangle([M(x0), M(y0), M(x1), M(y1)], outline=255, width=2)
        d.text((M(163), M(630)), 'ERS SOC', font=fp_font(int(M(25))), fill=255, anchor='mm')
        if cells:
            for c, (x0, y0, x1, y1) in RB_CELLS.items():
                d.rectangle([M(x0), M(y0), M(x1), M(y1)], outline=255, width=2)
                d.text((M(x0 + 13), M(y0 + 14)), c.upper(), font=fp_font(int(M(19))), fill=255, anchor='lm')
        for word, x, y, anchor, size in labels:  # 1.2.39: a third bigger (19 px read as almost unreadable)
            d.text((M(x), M(y)), word, font=fp_font(int(M(size) * 1.35)), fill=255, anchor=anchor)
        car_quad(f'rd_rb_pic_{tag}_b', overlay_texture(f'rd_ovl_rb{tag}0', big.resize((836, 472), Image.LANCZOS)),
                 (0, 0, 1670, 942), RB_LABEL, H + rb_leaf(tag), L_SHAPE + 3)

    L = 19
    rb_picture('r', [('SPEED', 185, 27, 'lm', L), ('PREV LAP Δ', 1250, 27, 'rm', L), ('LAP TIME', 1250, 156, 'rm', L),
                     ('LAP', 92, 243, 'lm', 24), ('POS', 92, 325, 'lm', 24), ('TYRE AGE', 92, 406, 'lm', 22),
                     ('FUEL Δ', 92, 489, 'lm', 24), ('BRAKE BALANCE', 1250, 378, 'rm', L), ('AHEAD', 530, 525, 'lm', L),
                     ('BEHIND', 712, 525, 'lm', L), ('TYRE TEMP', 940, 527, 'lm', L), ('LAST LAP', 280, 620, 'lm', 15)])
    rb_picture('q', [('SPEED', 185, 27, 'lm', L), ('DELTA TO BEST', 1250, 27, 'rm', L), ('LAST LAP', 1250, 156, 'rm', L),
                     ('S1', 92, 243, 'lm', 24), ('S2', 92, 325, 'lm', 24), ('S3', 92, 406, 'lm', 24),
                     ('PERSONAL BEST', 92, 488, 'lm', L), ('STINT LAP', 1250, 378, 'rm', L), ('FUEL Δ', 530, 525, 'lm', L),
                     ('BB', 712, 525, 'lm', L), ('TYRE TEMP', 940, 527, 'lm', L), ('TIME LEFT', 280, 620, 'lm', 15)])
    rb_picture('s', [('VSC DELTA', 85, 37, 'lm', 22), ('GEAR', 633, 30, 'mm', 22), ('EST. PIT EXIT', 1250, 37, 'rm', 22),
                     ('LAP', 92, 283, 'lm', 24), ('POS', 92, 368, 'lm', 24), ('TYRE AGE', 92, 452, 'lm', 22),
                     ('SPEED', 635, 243, 'mm', L), ('BRAKE BALANCE', 1250, 378, 'rm', L), ('PIT WINDOW', 558, 525, 'lm', L),
                     ('TYRE TEMP', 940, 527, 'lm', L), ('FUEL Δ', 280, 620, 'lm', 15)])
    rb_picture('d', [('FUEL MODE', 128, 480, 'lm', L), ('DIFFERENTIAL', 637, 516, 'mm', L),
                     ('BRAKE BALANCE', 1225, 480, 'rm', L)], cells=False, ybox=False)
    rb_picture('p', [('PIT WINDOW', 178, 35, 'lm', L), ('TYRES', 1250, 27, 'rm', L),
                     ('REPAIR WING', 1250, 175, 'rm', L), ('REJOIN', 1250, 318, 'rm', L), ('LAP', 92, 276, 'lm', 24), ('POS', 92, 365, 'lm', 24),
                     ('TYRE WEAR', 940, 527, 'lm', L), ('PIT STOP TIME', 280, 620, 'lm', 15)])

    # ---- every page: the battery column (green; blue in the overtake zone, 1.2.42), ERS SOC, the mode badge
    for i, (x0, y0, x1, y1) in enumerate(RB_SOC):  # rb_seg00 at the bottom
        rb_box(f'rd_rb_seg{15 - i:02d}', x0 + 1, y0 + 1, x1 - 1, y1 - 1, (110, 190, 90), f'rb_seg{15 - i:02d}')  # green
        rb_box(f'rd_rb_sob{15 - i:02d}', x0 + 1, y0 + 1, x1 - 1, y1 - 1, (40, 110, 255), f'rb_sob{15 - i:02d}')  # overtake
    rb_text('rd_rb_soc', 163, 690, H + 'soc_n', 'rqsdp', size=44, align='centre')
    for tag, colour in (('p', (95, 45, 205)), ('g', (35, 135, 55)), ('n', (70, 70, 76))):
        rb_box(f'rd_rb_mode{tag}', 500, 365, 770, 428, colour, f'rb_mode_{tag}')
    rb_text('rd_rb_mode', 635, 406, H + 'tm_badge', 'rb_mode', size=19, align='centre', value='')
    # the light top left (race, practice, pit page; 1.2.43, the user's idea): the last sector's pace, grey before
    # there is one, yellow slower than your best, green your best, purple the session's best
    for tag, colour in (('n', (90, 92, 100)), ('y', (230, 190, 0)), ('g', (40, 200, 70)), ('p', (130, 60, 220))):
        rb_box(f'rd_rb_sq{tag}', 80, 25, 149, 86, colour, f'rb_sq_{tag}')
    # the yellow box at the bottom (a deeper yellow, white writing)
    rb_box('rd_rb_ybox', 263, 603, 536, 700, (200, 155, 0), 'rqsp')
    # ---- the tyre cells: temperature bands (race, practice, SC), wear bands (pit page)
    for c, (x0, y0, x1, y1) in RB_CELLS.items():
        for tag, colour in (('k', (40, 100, 200)), ('g', (35, 145, 60)), ('o', (210, 120, 30))):
            rb_box(f'rd_rb_t{c}{tag}', x0 + 2, y0 + 2, x1 - 2, y1 - 2, colour, f'rb_t{c}_{tag}', L_BACK + 3)
        for tag, colour in (('g', (35, 145, 60)), ('a', (210, 140, 20)), ('r', (190, 35, 35))):
            rb_box(f'rd_rb_w{c}{tag}', x0 + 2, y0 + 2, x1 - 2, y1 - 2, colour, f'w{tag}_{c}_b', L_BACK + 3)
        rb_text(f'rd_rb_t{c}', (x0 + x1) / 2, y1 - 12, H + f't_{c}', 'rqs', size=42, align='centre', value='--')
        rb_text(f'rd_rb_w{c}', (x0 + x1) / 2, y1 - 12, H + f'w_{c}', 'p', size=42, align='centre', value='--')

    # ---- race / practice: speed, the gear, RPM, the right-hand column
    rb_text('rd_rb_spd', 190, 135, H + 'fp_spd', 'rq', size=86, chars=3)
    rb_text('rd_rb_gear', 637, 200, H + 'gear', 'rq', size=165, align='centre', style='OSD_80_bold', value='N')
    rb_text('rd_rb_rpm', 637, 316, H + 'fp_rev', 'rq', size=30, align='centre')
    rb_text('rd_rb_delta', 1243, 113, H + 'fp_delta', 'rq', size=68, align='right', chars=6)
    rb_text('rd_rb_lt', 1245, 240, H + 'cur', 'r', size=56, align='right', chars=8)
    rb_text('rd_rb_last', 1245, 240, H + 'fp_last', 'q', size=56, align='right', chars=8)
    rb_text('rd_rb_bb', 1215, 468, H + 'bbal', 'rs', size=56, align='right', chars=3)
    rb_text('rd_rb_stint', 1215, 468, H + 'fp_stint', 'q', size=56, align='right', chars=2)
    # the left list
    rb_text('rd_rb_lap', 225, 262, H + 'lap', 'r', size=50)
    rb_text('rd_rb_pos', 225, 343, H + 'pos', 'r', size=50)
    rb_text('rd_rb_age', 225, 425, H + 'fp_stint', 'r', size=50)
    rb_text('rd_rb_fuel', 225, 507, H + 'fuel', 'r', size=50)
    for k, y in ((1, 262), (2, 343), (3, 425)):
        rb_text(f'rd_rb_s{k}', 220, y, H + f's{k}', 'q', size=50)
    rb_text('rd_rb_pb', 97, 557, H + 'fp_pb', 'q', size=52)
    # the middle row
    rb_text('rd_rb_ahead', 530, 585, H + 'fp_gap0', 'r', size=36)
    rb_text('rd_rb_behind', 712, 585, H + 'fp_gap1', 'r', size=36)
    rb_text('rd_rb_qfuel', 530, 585, H + 'fuel', 'q', size=36)
    rb_text('rd_rb_qbb', 712, 585, H + 'bbal', 'q', size=36)
    # the yellow box's value
    rb_text('rd_rb_ylast', 400, 680, H + 'fp_last', 'r', size=34, align='centre')
    rb_text('rd_rb_yleft', 400, 680, H + 'fp_togo', 'q', size=34, align='centre')
    rb_text('rd_rb_yfuel', 400, 680, H + 'fuel', 's', size=34, align='centre')
    rb_text('rd_rb_ypst', 400, 680, H + 'pst', 'p', size=34, align='centre')

    # ---- under a SC / VSC: the top band yellow
    rb_box('rd_rb_scband', 0, 0, 1280, 220, (200, 155, 0), 's', L_BACK + 3)
    rb_text('rd_rb_scd', 92, 140, H + 'fp_delta', 's', size=70)
    rb_text('rd_rb_sgear', 633, 178, H + 'gear', 's', size=130, align='centre', style='OSD_80_bold', value='N')
    rb_text('rd_rb_spit', 1240, 145, H + 'pitnow', 's', size=70, align='right', chars=3)
    rb_text('rd_rb_slap', 225, 302, H + 'lap', 's', size=50)
    rb_text('rd_rb_spos', 225, 387, H + 'pos', 's', size=50)
    rb_text('rd_rb_sage', 225, 472, H + 'fp_stint', 's', size=50)
    rb_text('rd_rb_sspd', 635, 330, H + 'fp_spd', 's', size=70, align='centre')
    rb_text('rd_rb_spw', 558, 587, H + 'pw', 's', size=36)

    # ---- the car page
    rb_text('rd_rb_mix', 130, 570, H + 'mix', 'd', size=72)
    rb_text('rd_rb_diff', 633, 622, H + 'diff', 'd', size=72, align='centre')
    rb_text('rd_rb_dbb', 1213, 585, H + 'bbal', 'd', size=72, align='right', chars=3)

    # ---- the pit page
    rb_text('rd_rb_pw', 178, 135, H + 'pw', 'p', size=72)
    rb_text('rd_rb_rejoin', 1243, 405, H + 'rejoin', 'p', size=60, align='right', chars=3)  # 1.2.39: under the wing
    for tag, n in (('s', 5.2), ('m', 7.4), ('h', 5.2), ('i', 6.2), ('w', 4.2)):  # 1.2.39: SOFT sat too far left
        rb_text(f'rd_rb_tyre{tag}', 1243, 125, H + 'tyre', f'ty_{tag}_b', size=64, align='right', chars=n)
    rb_text('rd_rb_wing', 1243, 285, H + 'fp_wing', 'p', size=60, align='right', chars=3)
    rb_text('rd_rb_plap', 225, 295, H + 'lap', 'p', size=50)
    rb_text('rd_rb_ppos', 225, 382, H + 'pos', 'p', size=50)

    hdr = ['// Written by tools/build_ui.py: the Red Bull pages\' layout sets (rb_<states>), as kFpCombos.', '#pragma once',
           '', 'inline constexpr const char *kRbCombos[] = {']
    hdr += [f'    "{c}",' for c in sorted(rb_combos)] + ['};', '']
    open(os.path.join(ROOT, 'native', 'src', 'rb_combos.hpp'), 'w', encoding='utf-8').write('\n'.join(hdr))

    # ---- the 2026 Aston Martin's own pages (1.3.1, the user's mock-ups). A slate centre panel with the gear in a box
    # and the tyres round an axle drawing, columns of values either side, a fuel bar on the left edge and the
    # battery on the right (green, blue in the overtake zone), a 30-light rev strip across the top, the page's name
    # and the mode badge along the bottom. White text only: the mock-ups' cream boxes are Aston green here and the
    # tyre cells a deeper blue, so white reads on them; the cyan brackets and their labels are one picture each.
    # Layouts r q s d p, sets on am_<states>; the game's badge for the car may still be Force India's.
    am_combos = set()

    def am_leaf(states):
        if '_' in states:
            return states
        am_combos.add(states)
        return 'am_' + states

    AM_LABEL = (190, 194, 205)
    AM_CYAN = (130, 210, 230)
    AM_GREEN = (25, 100, 80)

    def am_text(name, x, y, dbentry, states, size=40, align='centre', value='-', style='OSD_24_bold', chars=5):
        scale = M(size) / 250.0 if style == 'OSD_80_bold' else size / 34.0
        lw = W(900)
        nm = name + '_am'
        if align == 'left':
            left = X(M(x))
        elif align == 'right':
            left = X(M(x) - chars * 45 * scale)
        else:
            left = centred(X(M(x)), lw, scale)
        lcd.add_text(nm, 'speedo_value', left, Y(M(y)), width=lw, layer=L_TEXT, style=style,
                     halign=None if align == 'centre' else 'left')
        if scale != 1.0:
            lcd.move(nm, sx=scale, sy=scale)
        tpl.add_label(f'{nm}_w', nm, dbentry, value=value, colour=None, visible=H + am_leaf(states))

    def am_box(name, x0, y0, x1, y1, colour, states, layer=None):
        car_quad(name + '_am', white_texture(), (M(x0), M(y0), M(x1), M(y1)), colour, H + am_leaf(states),
                 L_PART if layer is None else layer)

    AM_RPM = [(75 + i * 38, 21, 75 + i * 38 + 35, 40) for i in range(30)]
    AM_FUEL = [(30, 65 + i * (542 / 32), 53, 65 + (i + 1) * (542 / 32) - 2) for i in range(32)]  # top .. bottom
    AM_SOC = [(1226, 65 + i * (542 / 32), 1249, 65 + (i + 1) * (542 / 32) - 2) for i in range(32)]
    AM_CELLS = {'fl': (467, 325, 577, 392), 'fr': (704, 325, 814, 392), 'rl': (467, 447, 577, 514),
                'rr': (704, 447, 814, 514)}
    AM_BARS = {'fl': (447, 335, 458, 382), 'fr': (820, 335, 831, 382), 'rl': (447, 457, 458, 504),
               'rr': (820, 457, 831, 504)}
    PANEL = (418, 62, 862, 595)

    def am_picture(tag, labels, brackets=(), lines=(), boxes=(), panel=True, cells=True, axle_label='TYRE TEMPS'):
        """Grey labels and lines in one picture, the cyan brackets and their labels in another."""
        big = Image.new('L', (1672, 944), 0)
        d = ImageDraw.Draw(big)
        for x0, y0, x1, y1 in AM_RPM + AM_FUEL + AM_SOC:
            d.rectangle([M(x0), M(y0), M(x1), M(y1)], outline=255, width=2)
        for x in (75, 645, 1215):
            d.line([M(x), M(16), M(x), M(44)], fill=255, width=3)
        d.text((M(48), M(631)), 'FUEL', font=fp_font(int(M(19))), fill=255, anchor='mm')
        d.text((M(1232), M(631)), 'ERS SOC', font=fp_font(int(M(19))), fill=255, anchor='mm')
        if panel:
            d.rectangle([M(PANEL[0]), M(PANEL[1]), M(PANEL[2]), M(PANEL[3])], outline=255, width=3)
        if cells:
            for c, (x0, y0, x1, y1) in AM_CELLS.items():
                d.text((M((x0 + x1) / 2), M(y0 + 11)), c.upper(), font=fp_font(int(M(15))), fill=255, anchor='mm')
            for y in (358, 480):  # the axle drawing
                d.rectangle([M(592), M(y), M(688), M(y + 7)], fill=255)
            d.rectangle([M(636), M(365), M(644), M(395)], fill=255)
            d.rectangle([M(636), M(440), M(644), M(480)], fill=255)
            d.text((M(640), M(415)), axle_label, font=fp_font(int(M(21))), fill=255, anchor='mm')
        for x0, y0, x1, y1 in lines:
            d.line([M(x0), M(y0), M(x1), M(y1)], fill=255, width=2)
        for x0, y0, x1, y1 in boxes:
            d.rectangle([M(x0), M(y0), M(x1), M(y1)], outline=255, width=3)
        for word, x, y, anchor, size in labels:
            d.text((M(x), M(y)), word, font=fp_font(int(M(size))), fill=255, anchor=anchor)
        car_quad(f'rd_am_pic_{tag}_am', overlay_texture(f'rd_ovl_am{tag}0', big.resize((836, 472), Image.LANCZOS)),
                 (0, 0, 1670, 942), AM_LABEL, H + am_leaf(tag), L_SHAPE + 3)
        cy = Image.new('L', (1672, 944), 0)
        dc = ImageDraw.Draw(cy)
        for (x0, x1, y, word) in brackets:  # a bracket: a line with the ends turned down, its label under it
            dc.line([M(x0), M(y + 22), M(x0), M(y), M(x1), M(y), M(x1), M(y + 22)], fill=255, width=4)
            dc.text((M((x0 + x1) / 2), M(y + 19)), word, font=fp_font(int(M(19))), fill=255, anchor='mm')
        car_quad(f'rd_am_cyan_{tag}_am', overlay_texture(f'rd_ovl_am{tag}c', cy.resize((836, 472), Image.LANCZOS)),
                 (0, 0, 1670, 942), AM_CYAN, H + am_leaf(tag), L_SHAPE + 4)

    L = 19
    am_picture('r', [('SPEED', 242, 220, 'mm', L), ('RPM', 242, 356, 'mm', L), ('PREV LAP Δ', 1038, 82, 'mm', L),
                     ('LAP TIME', 1038, 219, 'mm', L), ('POSITION', 1038, 355, 'mm', L), ('AHEAD', 530, 253, 'mm', 17),
                     ('BEHIND', 750, 253, 'mm', 17), ('FUEL Δ', 242, 94, 'mm', 18)],
               brackets=[(101, 385, 479, 'BRAKE BIAS'), (896, 1180, 479, 'LAP')])
    am_picture('q', [('SPEED', 242, 220, 'mm', L), ('PERSONAL BEST', 242, 356, 'mm', L), ('LAST LAP', 1038, 88, 'mm', L),
                     ('DELTA TO BEST', 242, 94, 'mm', 18)],
               brackets=[(101, 385, 479, 'TIME LEFT')],
               lines=[(907, 300, 1178, 300), (907, 410, 1178, 410), (907, 520, 1178, 520)])
    am_picture('s', [('POSITION', 242, 297, 'mm', L), ('LAP', 1038, 297, 'mm', L), ('SPEED', 590, 268, 'mm', 26)],
               brackets=[(101, 385, 479, 'FUEL Δ'), (896, 1180, 479, 'PIT WINDOW')])
    am_picture('d', [('FUEL MODE', 235, 473, 'mm', 18)], brackets=[(491, 790, 457, 'DIFFERENTIAL'),
                                                                   (887, 1181, 457, 'BRAKE BIAS')],
               boxes=[(77, 62, 1204, 415)], panel=False, cells=False)
    am_picture('p', [('REPAIR WING', 1038, 262, 'mm', L), ('TYRES', 1038, 97, 'mm', L), ('TYRE AGE', 242, 262, 'mm', L),
                     ('PIT WINDOW', 242, 94, 'mm', 18), ('PIT STOP TIME', 640, 115, 'mm', 18)],
               brackets=[(101, 385, 479, 'LAP'), (896, 1180, 479, 'PIT EXIT POS')], axle_label='TYRE WEAR')
    # the sector labels on the practice page in cyan
    sq = Image.new('L', (1672, 944), 0)
    ds = ImageDraw.Draw(sq)
    for k, y in ((1, 255), (2, 365), (3, 474)):
        ds.text((M(912), M(y)), f'S{k}', font=fp_font(int(M(26))), fill=255, anchor='lm')
    car_quad('rd_am_secl_am', overlay_texture('rd_ovl_amsq', sq.resize((836, 472), Image.LANCZOS)), (0, 0, 1670, 942),
             AM_CYAN, H + am_leaf('q'), L_SHAPE + 4)

    # ---- every page: the slate panel, the rev strip, fuel and battery bars, the page's name, the mode badge
    am_box('rd_am_panel', PANEL[0] + 2, PANEL[1] + 2, PANEL[2] - 2, PANEL[3] - 2, (60, 64, 86), 'rqsp', L_BACK + 2)
    for i, (x0, y0, x1, y1) in enumerate(AM_RPM):
        am_box(f'rd_am_rpm{i:02d}', x0 + 1, y0 + 1, x1 - 1, y1 - 1, (150, 55, 72), f'am_rpm{i:02d}')
    for i, (x0, y0, x1, y1) in enumerate(AM_FUEL):  # am_fuel00 at the bottom
        am_box(f'rd_am_fuel{31 - i:02d}', x0 + 1, y0 + 1, x1 - 1, y1 - 1, (240, 215, 130), f'am_fuel{31 - i:02d}')
    am_text('rd_am_fuelpct', 48, 680, H + 'am_fuelpct', 'rqsdp', size=22, value='')
    for i, (x0, y0, x1, y1) in enumerate(AM_SOC):
        am_box(f'rd_am_seg{31 - i:02d}', x0 + 1, y0 + 1, x1 - 1, y1 - 1, (150, 220, 130), f'am_seg{31 - i:02d}')
        am_box(f'rd_am_sob{31 - i:02d}', x0 + 1, y0 + 1, x1 - 1, y1 - 1, (40, 110, 255), f'am_sob{31 - i:02d}')
    am_text('rd_am_soc', 1232, 680, H + 'soc_n', 'rqsdp', size=22)
    am_box('rd_am_pagebox', 342, 620, 604, 695, (30, 130, 110), 'rqsdp')
    for st, word in (('r', 'RACE'), ('q', 'PRACTICE'), ('d', 'CAR'), ('p', 'PIT INFO')):
        am_text(f'rd_am_page{st}', 473, 672, H + 'none', st, size=30, value=word)
    am_text('rd_am_pages', 473, 672, H + 'fp_scshort', 's', size=30, value='SC')
    for tag, colour in (('p', (100, 60, 180)), ('g', (35, 135, 55)), ('n', (70, 72, 82))):
        am_box(f'rd_am_mode{tag}', 608, 620, 938, 695, colour, f'am_mode_{tag}')
    am_text('rd_am_mode', 773, 670, H + 'tm_badge', 'am_mode', size=26, value='')

    # ---- the top-left box (Aston green): fuel delta, delta to best, pit window
    am_box('rd_am_tlbox', 93, 75, 390, 178, AM_GREEN, 'rqp', L_BACK + 3)
    am_text('rd_am_tlfuel', 242, 160, H + 'fuel', 'r', size=40)
    am_text('rd_am_tldelta', 242, 160, H + 'fp_delta', 'q', size=40)
    am_text('rd_am_tlpw', 242, 160, H + 'pw', 'p', size=40)
    # ---- the gear in its box (race, practice); the pit page's pit stop time box
    am_box('rd_am_gearbox', 565, 82, 715, 230, AM_GREEN, 'rq', L_BACK + 3)
    am_text('rd_am_gear', 640, 205, H + 'gear', 'rq', size=105, style='OSD_80_bold', value='N')
    am_box('rd_am_pstbox', 487, 96, 793, 200, AM_GREEN, 'p', L_BACK + 3)
    am_text('rd_am_pst', 640, 182, H + 'pst', 'p', size=40)
    # ---- the tyres: cells (temperatures race / practice / SC, wear pit page) and their side bars (temperature
    # bands: blue cold, green, orange hot; wear bands on the pit page)
    for c, (x0, y0, x1, y1) in AM_CELLS.items():
        am_box(f'rd_am_cell{c}', x0, y0, x1, y1, (45, 110, 150), 'rqsp', L_BACK + 3)
        am_text(f'rd_am_t{c}', (x0 + x1) / 2, y1 - 9, H + f't_{c}', 'rqs', size=30, value='--')
        am_text(f'rd_am_w{c}', (x0 + x1) / 2, y1 - 9, H + f'w_{c}', 'p', size=30, value='--')
        bx0, by0, bx1, by1 = AM_BARS[c]
        for tag, colour in (('k', (60, 140, 230)), ('g', (150, 220, 130)), ('o', (230, 140, 40))):
            am_box(f'rd_am_b{c}{tag}', bx0, by0, bx1, by1, colour, f'am_t{c}_{tag}')
        for tag, colour in (('g', (150, 220, 130)), ('a', (230, 170, 40)), ('r', (210, 50, 50))):
            am_box(f'rd_am_bw{c}{tag}', bx0, by0, bx1, by1, colour, f'w{tag}_{c}_am')

    # ---- race
    am_text('rd_am_ahead', 530, 297, H + 'fp_gap0', 'r', size=28)
    am_text('rd_am_behind', 750, 297, H + 'fp_gap1', 'r', size=28)
    am_text('rd_am_spd', 242, 302, H + 'fp_spd', 'rq', size=56)  # 1.3.3: lower
    am_text('rd_am_rpmv', 242, 428, H + 'fp_rev', 'r', size=48)
    am_text('rd_am_bb', 242, 570, H + 'bbal', 'r', size=48)
    am_text('rd_am_delta', 1038, 155, H + 'fp_delta', 'r', size=52)
    am_text('rd_am_lt', 1038, 286, H + 'cur', 'r', size=35)  # 1.3.6: smaller still (1.3.3's 40 a little too wide)
    am_text('rd_am_pos', 1038, 428, H + 'pos', 'r', size=52)
    am_text('rd_am_lap', 1038, 570, H + 'lap', 'r', size=48)
    # ---- practice
    am_text('rd_am_pb', 242, 428, H + 'fp_pb', 'q', size=46)
    am_text('rd_am_left', 242, 570, H + 'fp_togo', 'q', size=48)
    am_text('rd_am_last', 1038, 160, H + 'fp_last', 'q', size=46)
    for k, y in ((1, 272), (2, 382), (3, 492)):
        am_text(f'rd_am_s{k}', 1180, y, H + f's{k}', 'q', size=40, align='right', chars=6)
    # ---- under a SC / VSC: the top band, then position, lap, speed, fuel delta, pit window
    am_box('rd_am_scband', 75, 62, 1205, 235, (200, 155, 0), 's', L_BACK + 3)
    sc = Image.new('L', (1672, 944), 0)
    dsc = ImageDraw.Draw(sc)
    for word, x in (('VSC DELTA', 241), ('GEAR', 640), ('EST. PIT EXIT', 1040)):
        dsc.text((M(x), M(92)), word, font=fp_font(int(M(19))), fill=255, anchor='mm')
    car_quad('rd_am_sclab_am', overlay_texture('rd_ovl_amsl', sc.resize((836, 472), Image.LANCZOS)), (0, 0, 1670, 942),
             (245, 245, 245), H + am_leaf('s'), L_SHAPE + 4)
    am_text('rd_am_scd', 241, 182, H + 'fp_delta', 's', size=50)
    am_text('rd_am_sgear', 640, 205, H + 'gear', 's', size=95, style='OSD_80_bold', value='N')
    am_text('rd_am_spit', 1040, 182, H + 'pitnow', 's', size=50)
    am_text('rd_am_sspd', 690, 280, H + 'fp_spd', 's', size=34)
    am_text('rd_am_spos', 242, 370, H + 'pos', 's', size=52)
    am_text('rd_am_slap', 1038, 370, H + 'lap', 's', size=52)
    am_text('rd_am_sfuel', 242, 570, H + 'fuel', 's', size=48)
    am_text('rd_am_spw', 1038, 570, H + 'pw', 's', size=48)
    # ---- the car page
    am_box('rd_am_mixbox', 94, 455, 376, 558, AM_GREEN, 'd', L_BACK + 3)
    am_text('rd_am_mix', 235, 540, H + 'mix', 'd', size=46)
    am_text('rd_am_diff', 640, 548, H + 'diff', 'd', size=52)
    am_text('rd_am_dbb', 1038, 548, H + 'bbal', 'd', size=52)
    # ---- the pit page
    am_text('rd_am_age', 242, 355, H + 'fp_stint', 'p', size=60)  # 1.3.3: lower
    am_text('rd_am_tyre', 1038, 170, H + 'tyre', 'p', size=48)
    am_text('rd_am_wing', 1038, 355, H + 'fp_wing', 'p', size=60)
    am_text('rd_am_plap', 242, 570, H + 'lap', 'p', size=48)
    am_text('rd_am_rejoin', 1038, 570, H + 'rejoin', 'p', size=48)

    hdr = ['// Written by tools/build_ui.py: the Aston Martin pages\' layout sets (am_<states>), as kFpCombos.',
           '#pragma once', '', 'inline constexpr const char *kAmCombos[] = {']
    hdr += [f'    "{c}",' for c in sorted(am_combos)] + ['};', '']
    open(os.path.join(ROOT, 'native', 'src', 'am_combos.hpp'), 'w', encoding='utf-8').write('\n'.join(hdr))

    # ---- the 2026 Williams's own pages (1.3.4, the user's mock-ups). No boxes but a slate strip across the middle,
    # the battery's box and the mode badge: the user's coloured text (salmon values, purple gear and headings, a mint
    # or salmon delta, dark writing on the safety car's yellow band). Colour is scarce (every team's coloured
    # widgets share the game's first 98), so a row of values is one widget whose text the plugin picks for the page
    # (wi_l*, wi_m*, wi_t*): those rows start at a fixed column (left-aligned: one widget's text changes length
    # with the page, and right alignment breaks when scaled). Layouts r q s d p, sets on wi_<states>.
    wi_combos = set()

    def wi_leaf(states):
        if '_' in states:
            return states
        wi_combos.add(states)
        return 'wi_' + states

    WI_LABEL = (165, 170, 188)
    WI_SALMON, WI_PURPLE, WI_MINT = (250, 130, 110), (125, 110, 240), (160, 230, 200)
    WI_DARK = (20, 20, 26)

    def wi_text(name, x, y, dbentry, states, size=32, align='left', colour=None, value='-', style='OSD_24_bold'):
        scale = M(size) / 250.0 if style == 'OSD_80_bold' else size / 34.0
        lw = W(900)
        nm = name + '_wi'
        left = X(M(x)) if align == 'left' else centred(X(M(x)), lw, scale)
        lcd.add_text(nm, 'speedo_value', left, Y(M(y)), width=lw, layer=L_TEXT, style=style,
                     halign=None if align == 'centre' else 'left')
        if scale != 1.0:
            lcd.move(nm, sx=scale, sy=scale)
        tpl.add_label(f'{nm}_w', nm, dbentry, value=value, colour=colour, visible=H + wi_leaf(states))

    def wi_box(name, x0, y0, x1, y1, colour, states, layer=None):
        car_quad(name + '_wi', white_texture(), (M(x0), M(y0), M(x1), M(y1)), colour, H + wi_leaf(states),
                 L_PART if layer is None else layer)

    ROW4 = (431, 497, 563, 629)  # race / practice rows (label centres)
    ROW3 = (438, 523, 609)       # SC, car and pit page rows
    WI_SEGS = [(580, 455 + i * (125 / 12), 621, 455 + (i + 1) * (125 / 12) - 2) for i in range(12)]  # top .. bottom

    def wi_pictures(tag, grey, white, lines=True, strip=True):
        """One picture of grey headings and lines and one of white row names for a layout."""
        big = Image.new('L', (1672, 944), 0)
        d = ImageDraw.Draw(big)
        d.rectangle([M(10), M(10), M(1270), M(710)], outline=255, width=3)
        d.rectangle([M(573), M(449), M(628), M(586)], outline=255, width=3)
        if lines:
            for x0, x1 in ((48, 507), (771, 1232)):
                d.line([M(x0), M(397), M(x1), M(397)], fill=255, width=2)
        for word, x, y, anchor, size in grey:
            d.text((M(x), M(y)), word, font=fp_font(int(M(size))), fill=255, anchor=anchor)
        car_quad(f'rd_wi_pic_{tag}_wi', overlay_texture(f'rd_ovl_wi{tag}g', big.resize((836, 472), Image.LANCZOS)),
                 (0, 0, 1670, 942), WI_LABEL, H + wi_leaf(tag), L_SHAPE + 3)
        wh = Image.new('L', (1672, 944), 0)
        dw = ImageDraw.Draw(wh)
        dw.text((M(640), M(412)), 'ERS SOC', font=fp_font(int(M(20))), fill=255, anchor='mm')
        for word, x, y in white:
            dw.text((M(x), M(y)), word, font=fp_font(int(M(21))), fill=255, anchor='lm')
        car_quad(f'rd_wi_wpic_{tag}_wi', overlay_texture(f'rd_ovl_wi{tag}w', wh.resize((836, 472), Image.LANCZOS)),
                 (0, 0, 1670, 942), (240, 240, 244), H + wi_leaf(tag), L_SHAPE + 4)

    def strip_labels(a, b, c):
        return [(a, 58, 250, 'lm', 19), (b, 498, 250, 'lm', 19), (c, 892, 250, 'lm', 19)]

    TOP = [('GEAR', 640, 40, 'mm', 19), ('SPEED', 1022, 40, 'mm', 19)]
    TYRES = [(c, 776, y) for c, y in zip(('FL', 'FR', 'RL', 'RR'), ROW4)]
    wi_pictures('r', TOP + strip_labels('LAP TIME', 'PREV LAP Δ', 'LAP') +
                [('RACE DATA', 52, 371, 'lm', 19), ('TYRE TEMPS', 776, 371, 'lm', 19)],
                [(w, 52, y) for w, y in zip(('AHEAD', 'BEHIND', 'FUEL Δ', 'RPM'), ROW4)] + TYRES)
    wi_pictures('q', TOP + strip_labels('LAST LAP', 'DELTA TO BEST', 'SESSION LEFT') +
                [('LAP TIMING', 52, 371, 'lm', 19), ('TYRE TEMPS', 776, 371, 'lm', 19)],
                [(w, 52, y) for w, y in zip(('S1', 'S2', 'S3', 'BEST LAP'), ROW4)] + TYRES)
    wi_pictures('s', strip_labels('TRACK STATUS', 'POSITION', 'LAP') +
                [('RACE DATA', 52, 371, 'lm', 19), ('TYRE TEMPS', 776, 371, 'lm', 19)],
                [(w, 52, y) for w, y in zip(('FUEL Δ', 'PIT WINDOW', 'BRAKE BIAS'), ROW3)] + TYRES)
    wi_pictures('d', [('CAR SETTINGS', 52, 371, 'lm', 19), ('BRAKES / FUEL', 776, 371, 'lm', 19)],
                [('FUEL MODE', 52, ROW3[0]), ('DIFFERENTIAL', 52, ROW3[1]), ('BRAKE BIAS', 776, ROW3[0]),
                 ('FUEL Δ', 776, ROW3[1])])
    wi_pictures('p', [('PIT WINDOW', 625, 45, 'mm', 19), ('NEXT TYRES', 1033, 45, 'mm', 19)] +
                strip_labels('PIT STOP TIME', 'CURRENT LAP', 'PIT EXIT POS') +
                [('PIT SETTINGS', 52, 371, 'lm', 19), ('TYRE WEAR', 776, 371, 'lm', 19)],
                [(w, 52, y) for w, y in zip(('TYRE AGE', 'REPAIR WING', 'BRAKE BIAS'), ROW3)] + TYRES)
    # the safety car's band: dark headings on yellow
    sc = Image.new('L', (1672, 944), 0)
    dsc = ImageDraw.Draw(sc)
    for word, x, y in (('VSC DELTA', 240, 46), ('GEAR', 640, 34), ('EST. PIT EXIT', 1027, 46), ('SPEED', 604, 183)):
        dsc.text((M(x), M(y)), word, font=fp_font(int(M(20))), fill=255, anchor='mm')
    car_quad('rd_wi_sclab_wi', overlay_texture('rd_ovl_wisk', sc.resize((836, 472), Image.LANCZOS)), (0, 0, 1670, 942),
             (245, 245, 245), H + wi_leaf('s'), L_SHAPE + 4)  # 1.3.11: white on amber
    wi_box('rd_wi_scband', 12, 12, 1268, 220, (180, 132, 10), 's', L_BACK + 3)  # 1.3.11: amber, white text

    # ---- every page: the battery's box and its 12 segments (mint; blue in the overtake zone), the mode badge
    wi_box('rd_wi_ersbox', 544, 388, 736, 619, (20, 22, 34), 'rqsdp', L_BACK + 2)
    for i, (x0, y0, x1, y1) in enumerate(WI_SEGS):  # wi_seg00 at the bottom
        wi_box(f'rd_wi_seg{11 - i:02d}', x0, y0, x1, y1, WI_MINT, f'wi_seg{11 - i:02d}')
        wi_box(f'rd_wi_sob{11 - i:02d}', x0, y0, x1, y1, (40, 110, 255), f'wi_sob{11 - i:02d}')
    wi_text('rd_wi_soc', 682, 525, H + 'soc_n', 'rqsdp', size=28, align='centre', colour=WI_MINT)
    for tag, colour in (('p', (100, 75, 205)), ('g', (35, 135, 55)), ('n', (70, 72, 82))):
        wi_box(f'rd_wi_mode{tag}', 516, 640, 764, 695, colour, f'wi_mode_{tag}')
    wi_text('rd_wi_mode', 640, 678, H + 'tm_badge', 'wi_mode', size=22, align='centre', value='')
    # the middle strip (race, practice, SC, pit page)
    wi_box('rd_wi_strip', 26, 225, 1255, 345, (22, 24, 36), 'rqsp', L_BACK + 2)
    wi_box('rd_wi_accent', 26, 225, 30, 345, WI_PURPLE, 'rqsp', L_BACK + 3)

    # ---- the top: the position / the session / PIT, its subtitle, the gear, the speed (race, practice, pit)
    wi_text('rd_wi_top', 243, 108, H + 'wi_top', 'rqp', size=58, align='centre')
    wi_text('rd_wi_sub', 243, 182, H + 'wi_sub', 'rqp', size=34, align='centre', colour=WI_PURPLE, value='')
    wi_text('rd_wi_gear', 640, 178, H + 'gear', 'rq', size=112, align='centre', style='OSD_80_bold', value='N',
            colour=WI_PURPLE)
    wi_text('rd_wi_spd', 1022, 165, H + 'fp_spd', 'rq', size=86, align='centre')
    wi_text('rd_wi_pw', 625, 158, H + 'pw', 'p', size=68, align='centre', colour=WI_PURPLE)
    for tag, colour in (('s', (235, 70, 65)), ('m', (255, 226, 90)), ('h', None), ('i', (90, 200, 100)),
                        ('w', (80, 150, 255))):
        wi_text(f'rd_wi_ty{tag}', 1033, 152, H + 'tyre', f'ty_{tag}_wi', size=50, align='centre', colour=colour)
    # ---- the strip's values
    wi_text('rd_wi_sl', 63, 322, H + 'wi_sl', 'rqp', size=38)
    wi_text('rd_wi_scs', 63, 322, H + 'fp_scshort', 's', size=38, colour=WI_PURPLE)
    wi_text('rd_wi_sm', 503, 322, H + 'wi_sm', 'sp', size=38)
    for tag, colour in (('dneg', WI_MINT), ('dpos', WI_SALMON), ('dnone', None)):
        wi_text(f'rd_wi_{tag}', 503, 322, H + 'fp_delta', f'wi_{tag}', size=38, colour=colour)
    wi_text('rd_wi_sr', 897, 322, H + 'wi_sr', 'rqsp', size=38)
    # ---- the rows (salmon; practice's best lap white)
    for i in range(3):
        wi_text(f'rd_wi_l{i}', 280, ROW4[i] + 16, H + f'wi_l{i}', 'rq', size=32, colour=WI_SALMON)
    wi_text('rd_wi_rpm', 280, ROW4[3] + 16, H + 'fp_rev', 'r', size=32, colour=WI_SALMON)
    wi_text('rd_wi_pb', 280, ROW4[3] + 16, H + 'fp_pb', 'q', size=32)
    for i, states in enumerate(('sdp', 'sdp', 'sp')):
        wi_text(f'rd_wi_m{i}', 280, ROW3[i] + 16, H + f'wi_m{i}', states, size=32, colour=WI_SALMON)
    wi_text('rd_wi_dbb', 1080, ROW3[0] + 16, H + 'bbal', 'd', size=32, colour=WI_SALMON)
    wi_text('rd_wi_dfuel', 1080, ROW3[1] + 16, H + 'fuel', 'd', size=32, colour=WI_SALMON)
    for i in range(4):
        wi_text(f'rd_wi_t{i}', 1120, ROW4[i] + 16, H + f'wi_t{i}', 'rqsp', size=32, colour=WI_SALMON)
    # ---- the safety car's band
    wi_text('rd_wi_scd', 240, 152, H + 'fp_delta', 's', size=54, align='centre')
    wi_text('rd_wi_sgear', 640, 128, H + 'gear', 's', size=78, align='centre', style='OSD_80_bold', value='N')
    wi_text('rd_wi_sspd', 668, 193, H + 'fp_spd', 's', size=24)
    wi_text('rd_wi_spit', 1027, 152, H + 'pitnow', 's', size=62, align='centre')

    hdr = ['// Written by tools/build_ui.py: the Williams pages\' layout sets (wi_<states>), as kFpCombos.',
           '#pragma once', '', 'inline constexpr const char *kWiCombos[] = {']
    hdr += [f'    "{c}",' for c in sorted(wi_combos)] + ['};', '']
    open(os.path.join(ROOT, 'native', 'src', 'wi_combos.hpp'), 'w', encoding='utf-8').write('\n'.join(hdr))

    # ---- the 2026 Audi's own pages (1.3.7, the user's mock-ups; the 2025 Sauber's too, both badged alfa_romeo).
    # A navy screen: the four tyres in the corners (temperature and wear), the page's name on a green box, rows of
    # values on the left, the gear in an outlined box with the speed and revs under it, an outlined box of two values
    # on the right, the mode badge and the battery's 24-segment bar at the bottom. White text only (the user: no more
    # colour slots): the outlines and lines are one blue-grey picture a layout, the names one white picture.
    # Layouts r q s d p, sets on au_<states>.
    au_combos = set()

    def au_leaf(states):
        if '_' in states:
            return states
        au_combos.add(states)
        return 'au_' + states

    AU_LINE = (110, 135, 180)
    AU_NAVY, AU_PANEL = (10, 22, 46), (14, 30, 60)

    def au_text(name, x, y, dbentry, states, size=34, align='left', value='-', style='OSD_24_bold', chars=5):
        scale = M(size) / 250.0 if style == 'OSD_80_bold' else size / 34.0
        lw = W(900)
        nm = name + '_au'
        if align == 'left':
            left = X(M(x))
        elif align == 'right':
            left = X(M(x) - chars * 45 * scale)
        else:
            left = centred(X(M(x)), lw, scale)
        lcd.add_text(nm, 'speedo_value', left, Y(M(y)), width=lw, layer=L_TEXT, style=style,
                     halign=None if align == 'centre' else 'left')
        if scale != 1.0:
            lcd.move(nm, sx=scale, sy=scale)
        tpl.add_label(f'{nm}_w', nm, dbentry, value=value, colour=None, visible=H + au_leaf(states))

    def au_box(name, x0, y0, x1, y1, colour, states, layer=None):
        car_quad(name + '_au', white_texture(), (M(x0), M(y0), M(x1), M(y1)), colour, H + au_leaf(states),
                 L_PART if layer is None else layer)

    CORNERS = {'fl': (0, 0), 'fr': (817, 0), 'rl': (0, 509), 'rr': (817, 509)}
    AU_SEGS = [(491 + i * (286 / 24), 649, 491 + (i + 1) * (286 / 24) - 2, 671) for i in range(24)]
    RBOX = (852, 205, 1235, 459)

    def au_pictures(tag, names, boxes=(), lines=(), corners=True, rbox=True, gearbox=True):
        out = Image.new('L', (1672, 944), 0)
        d = ImageDraw.Draw(out)
        wh = Image.new('L', (1672, 944), 0)
        dw = ImageDraw.Draw(wh)
        allboxes = list(boxes) + [(473, 590, 807, 690)]
        if rbox:
            allboxes.append(RBOX)
            lines = list(lines) + [(869, 331, 1219, 331)]
        if gearbox:
            allboxes.append((486, 40, 794, 265))
            names = list(names) + [('GEAR', 640, 64, 'mm', 19)]
        for x0, y0, x1, y1 in allboxes:
            d.rectangle([M(x0), M(y0), M(x1), M(y1)], outline=255, width=3)
        if corners:
            for c, (dx, dy) in CORNERS.items():
                d.line([M(115 + dx), M(48 + dy), M(413 + dx), M(48 + dy)], fill=255, width=2)
                names = list(names) + [(c.upper(), 54 + dx, 48 + dy, 'lm', 21), ('TEMP', 53 + dx, 150 + dy, 'lm', 16),
                                       ('WEAR', 406 + dx, 150 + dy, 'rm', 16)]
        for x0, y0, x1, y1 in lines:
            d.line([M(x0), M(y0), M(x1), M(y1)], fill=255, width=2)
        names = list(names) + [('ERS SOC', 491, 612, 'lm', 19)]
        for word, x, y, anchor, size in names:
            dw.text((M(x), M(y)), word, font=fp_font(int(M(size))), fill=255, anchor=anchor)
        car_quad(f'rd_au_lpic_{tag}_au', overlay_texture(f'rd_ovl_au{tag}l', out.resize((836, 472), Image.LANCZOS)),
                 (0, 0, 1670, 942), AU_LINE, H + au_leaf(tag), L_SHAPE + 3)
        car_quad(f'rd_au_wpic_{tag}_au', overlay_texture(f'rd_ovl_au{tag}w', wh.resize((836, 472), Image.LANCZOS)),
                 (0, 0, 1670, 942), (240, 240, 244), H + au_leaf(tag), L_SHAPE + 4)

    ROWS = (320, 387, 454)

    def rows(words):
        return [(w, 54, y, 'lm', 20) for w, y in zip(words, ROWS)]

    def rbox_names(a, b):
        return [(a, 874, 230, 'lm', 19), (b, 874, 357, 'lm', 19)]

    au_pictures('r', [('SPEED', 640, 294, 'mm', 19), ('RPM', 640, 426, 'mm', 19), ('AHEAD', 856, 488, 'lm', 18),
                      ('BEHIND', 1079, 488, 'lm', 18)] + rows(('POSITION', 'LAP', 'FUEL Δ')) +
                rbox_names('LAP TIME', 'PREV LAP Δ'))
    au_pictures('q', [('SPEED', 640, 294, 'mm', 19)] + rows(('S1', 'S2', 'S3')) + rbox_names('LAST LAP', 'DELTA TO BEST'))
    au_pictures('p', [('SPEED', 640, 294, 'mm', 19), ('PIT STOP TIME', 640, 426, 'mm', 19)] +
                rows(('PIT WINDOW', 'LAP', 'TYRE AGE')) + rbox_names('NEXT TYRES', 'REPAIR WING'))
    au_pictures('s', [('VSC DELTA', 53, 63, 'lm', 21), ('GEAR', 640, 44, 'mm', 21), ('EST. PIT EXIT', 1216, 63, 'rm', 21),
                      ('SPEED', 604, 208, 'mm', 21), ('PIT WINDOW', 640, 427, 'mm', 19), ('TYRE TEMPS', 873, 305, 'lm', 19),
                      ('FL', 873, 360, 'lm', 19), ('FR', 1055, 360, 'lm', 19), ('RL', 873, 437, 'lm', 19),
                      ('RR', 1055, 437, 'lm', 19)] + [(w, 54, y, 'lm', 20) for w, y in zip(('POSITION', 'LAP', 'FUEL Δ'),
                                                                                         (307, 376, 445))],
                boxes=[(852, 280, 1235, 498)], corners=False, rbox=False, gearbox=False)
    au_pictures('d', [('FUEL MODE', 65, 538, 'lm', 19), ('DIFFERENTIAL', 883, 553, 'lm', 19), ('BRAKE BIAS', 883, 632, 'lm', 19)],
                boxes=[(46, 510, 417, 690), (862, 510, 1233, 690)], lines=[(48, 465, 1232, 465)], corners=False,
                rbox=False, gearbox=False)

    # ---- every page: the navy screen, the panels, the mode badge, the battery
    au_box('rd_au_navy', 0, 0, 1280, 720, AU_NAVY, 'rqsdp', L_BACK + 1)
    au_box('rd_au_gearp', 488, 42, 792, 263, AU_PANEL, 'rqp', L_BACK + 2)
    au_box('rd_au_rboxp', RBOX[0] + 2, RBOX[1] + 2, RBOX[2] - 2, RBOX[3] - 2, AU_PANEL, 'rqp', L_BACK + 2)
    au_box('rd_au_ersp', 475, 592, 805, 688, AU_PANEL, 'rqsdp', L_BACK + 2)
    au_box('rd_au_ttp', 854, 282, 1233, 496, AU_PANEL, 's', L_BACK + 2)
    au_box('rd_au_fmp', 48, 512, 415, 688, AU_PANEL, 'd', L_BACK + 2)
    au_box('rd_au_dbp', 864, 512, 1231, 688, AU_PANEL, 'd', L_BACK + 2)
    au_box('rd_au_segbase', 491, 649, 777, 671, (30, 42, 66), 'rqsdp', L_BACK + 3)
    for i, (x0, y0, x1, y1) in enumerate(AU_SEGS):  # au_seg00 on the left
        au_box(f'rd_au_seg{i:02d}', x0, y0, x1, y1, (140, 230, 140), f'au_seg{i:02d}')
        au_box(f'rd_au_sob{i:02d}', x0, y0, x1, y1, (40, 110, 255), f'au_sob{i:02d}')
    au_text('rd_au_soc', 786, 625, H + 'soc_n', 'rqsdp', size=28, align='right', chars=2)
    for tag, colour in (('p', (110, 80, 200)), ('g', (35, 135, 55)), ('n', (70, 72, 82))):
        au_box(f'rd_au_mode{tag}', 473, 514, 807, 570, colour, f'au_mode_{tag}')
    au_text('rd_au_mode', 640, 555, H + 'tm_badge', 'au_mode', size=24, align='centre', value='')
    # ---- the page's name on its green box (race, practice, pit page)
    au_box('rd_au_namebox', 46, 204, 420, 272, (35, 130, 80), 'rqp', L_BACK + 3)
    for st, word in (('r', 'RACE'), ('q', 'PRACTICE'), ('p', 'PIT INFO')):
        au_text(f'rd_au_name{st}', 233, 252, H + 'none', st, size=36, align='centre', value=word)
    # ---- the tyres in the corners: temperature (left) and wear (right)
    for c, (dx, dy) in CORNERS.items():
        au_text(f'rd_au_t{c}', 58 + dx, 120 + dy, H + f't_{c}', 'rqp', size=44, value='--')
        au_text(f'rd_au_w{c}', 400 + dx, 120 + dy, H + f'w_{c}', 'rqp', size=40, align='right', chars=3, value='--')
    # ---- the gear, the speed, the revs / time left / pit stop time
    au_text('rd_au_gear', 640, 210, H + 'gear', 'rqp', size=118, align='centre', style='OSD_80_bold', value='N')
    au_text('rd_au_spd', 640, 385, H + 'fp_spd', 'rqp', size=62, align='centre')
    au_text('rd_au_rpm', 640, 482, H + 'fp_rev', 'r', size=36, align='centre')
    au_text('rd_au_sessl', 640, 432, H + 'sess_label', 'q', size=18, align='centre', value='TIME LEFT')
    au_text('rd_au_left', 640, 482, H + 'fp_togo', 'q', size=36, align='centre')
    au_text('rd_au_pst', 640, 482, H + 'pst', 'p', size=36, align='centre')
    # ---- the left rows (right edge 405)
    for st, vals in (('r', (('pos', 3), ('lap', 5), ('fuel', 5))), ('q', (('s1', 6), ('s2', 6), ('s3', 6))),
                     ('p', (('pw', 6), ('lap', 5), ('fp_stint', 2)))):
        for i, (key, n) in enumerate(vals):
            au_text(f'rd_au_{st}row{i}', 405, ROWS[i] + 16, H + key, st, size=34, align='right', chars=n)
    # ---- the right box's two values (right edge 1210)
    for st, (a, na), (b, nb) in (('r', ('cur', 8), ('fp_delta', 6)), ('q', ('fp_last', 8), ('fp_delta', 6)),
                                 ('p', ('tyre', 6.5), ('fp_wing', 3.2))):
        au_text(f'rd_au_{st}box0', 1210, 296, H + a, st, size=44 if st == 'p' else 38, align='right', chars=na)  # lap times: 38 fits the box
        au_text(f'rd_au_{st}box1', 1210, 425, H + b, st, size=44, align='right', chars=nb)
    au_text('rd_au_ahead', 941, 496, H + 'fp_gap0', 'r', size=20)
    au_text('rd_au_behind', 1171, 496, H + 'fp_gap1', 'r', size=20)
    # ---- the safety car: a band across the top (amber, white writing), the rows, the SC name, the tyre box
    au_box('rd_au_scband', 14, 14, 1266, 258, (190, 140, 10), 's', L_BACK + 3)
    au_text('rd_au_scd', 58, 178, H + 'fp_delta', 's', size=62)
    au_text('rd_au_sgear', 640, 150, H + 'gear', 's', size=88, align='centre', style='OSD_80_bold', value='N')
    au_text('rd_au_sspd', 676, 220, H + 'fp_spd', 's', size=28)
    au_text('rd_au_spit', 1210, 178, H + 'pitnow', 's', size=66, align='right', chars=3)
    for i, (key, n) in enumerate((('pos', 3), ('lap', 5), ('fuel', 5))):
        au_text(f'rd_au_srow{i}', 405, (307, 376, 445)[i] + 16, H + key, 's', size=34, align='right', chars=n)
    au_text('rd_au_scname', 640, 372, H + 'fp_scshort', 's', size=66, align='centre', value='SC')
    au_text('rd_au_spw', 640, 483, H + 'pw', 's', size=38, align='centre')
    for c, (x, y) in {'fl': (1022, 375), 'fr': (1203, 375), 'rl': (1022, 452), 'rr': (1203, 452)}.items():
        au_text(f'rd_au_st{c}', x, y, H + f't_{c}', 's', size=36, align='right', chars=2.4, value='--')
    # ---- the car page
    au_text('rd_au_mix', 231, 632, H + 'mix', 'd', size=56, align='centre')
    au_text('rd_au_diff', 1208, 568, H + 'diff', 'd', size=34, align='right', chars=3)
    au_text('rd_au_bb', 1208, 647, H + 'bbal', 'd', size=34, align='right', chars=3)

    hdr = ['// Written by tools/build_ui.py: the Audi pages\' layout sets (au_<states>), as kFpCombos.',
           '#pragma once', '', 'inline constexpr const char *kAuCombos[] = {']
    hdr += [f'    "{c}",' for c in sorted(au_combos)] + ['};', '']
    open(os.path.join(ROOT, 'native', 'src', 'au_combos.hpp'), 'w', encoding='utf-8').write('\n'.join(hdr))

    # ---- the 2026 Haas's own pages (1.3.9, the user's mock-ups, made dark: the real screen is light, but black
    # text would need a colour slot for nearly every element). Page name top left, the lap top right, the battery
    # as a bar across the top, the gear between the tyre cells with the speed under it, three rows of values each
    # side, a line, then the mode badge and two small values along the bottom. White text only; the cells, the
    # bar, the badge and the safety car's band carry the colour. Layouts r q s d p, sets on hs_<states>.
    hs_combos = set()

    def hs_leaf(states):
        if '_' in states:
            return states
        hs_combos.add(states)
        return 'hs_' + states

    HS_LABEL = (185, 192, 198)

    def hs_text(name, x, y, dbentry, states, size=34, align='left', value='-', style='OSD_24_bold', chars=5):
        scale = M(size) / 250.0 if style == 'OSD_80_bold' else size / 34.0
        lw = W(900)
        nm = name + '_hs'
        if align == 'left':
            left = X(M(x))
        elif align == 'right':
            left = X(M(x) - chars * 45 * scale)
        else:
            left = centred(X(M(x)), lw, scale)
        lcd.add_text(nm, 'speedo_value', left, Y(M(y)), width=lw, layer=L_TEXT, style=style,
                     halign=None if align == 'centre' else 'left')
        if scale != 1.0:
            lcd.move(nm, sx=scale, sy=scale)
        tpl.add_label(f'{nm}_w', nm, dbentry, value=value, colour=None, visible=H + hs_leaf(states))

    def hs_box(name, x0, y0, x1, y1, colour, states, layer=None):
        car_quad(name + '_hs', white_texture(), (M(x0), M(y0), M(x1), M(y1)), colour, H + hs_leaf(states),
                 L_PART if layer is None else layer)

    HS_SEGS = [(361 + i * (557 / 25), 25, 361 + (i + 1) * (557 / 25), 75) for i in range(25)]  # one bar, left .. right
    ROWY = (132, 290, 447)       # row labels
    VALY = (200, 358, 515)       # row values (baselines)

    def hs_picture(tag, names, tyres='TYRE TEMPS', line_y=600, gear=True):
        big = Image.new('L', (1672, 944), 0)
        d = ImageDraw.Draw(big)
        d.line([M(38), M(line_y), M(1243), M(line_y)], fill=255, width=2)
        names = list(names)
        if tyres:
            ty = 457 if tag == 's' else 365
            names.append((tyres, 640, ty, 'mm', 18))
        if gear and tag != 's':
            names.append(('GEAR', 640, 141, 'mm', 18))
        if tag != 'q':
            names.append(('LAP', 1100, 48, 'mm', 22))
        for word, x, y, anchor, size in names:
            d.text((M(x), M(y)), word, font=fp_font(int(M(size))), fill=255, anchor=anchor)
        car_quad(f'rd_hs_pic_{tag}_hs', overlay_texture(f'rd_ovl_hs{tag}0', big.resize((836, 472), Image.LANCZOS)),
                 (0, 0, 1670, 942), HS_LABEL, H + hs_leaf(tag), L_SHAPE + 3)

    def left_rows(words):
        return [(w, 41, y, 'lm', 18) for w, y in zip(words, ROWY)]

    def right_rows(words):
        return [(w, 1240, y, 'rm', 18) for w, y in zip(words, ROWY)]

    def bottom(a, b):
        return [(a, 380, 661, 'lm', 17), (b, 781, 661, 'lm', 17), ('SPEED', 640, 438, 'mm', 18)]

    hs_picture('r', left_rows(('LAP TIME', 'PREV LAP Δ', 'POSITION')) + right_rows(('AHEAD', 'BEHIND', 'FUEL Δ')) +
               bottom('RPM', 'BRAKE BIAS'))
    hs_picture('q', left_rows(('LAST LAP', 'PERSONAL BEST', 'DELTA TO BEST')) +
               right_rows(('SECTOR 1', 'SECTOR 2', 'SECTOR 3')) + bottom('STINT LAP', 'SESSION LEFT'))
    hs_picture('p', left_rows(('PIT WINDOW', 'TYRE AGE', 'REPAIR WING')) +
               right_rows(('NEXT TYRES', 'PIT STOP TIME', 'PIT EXIT POS')) + bottom('FUEL Δ', 'BRAKE BIAS'),
               tyres='TYRE WEAR')
    hs_picture('s', [('VSC DELTA', 41, 117, 'lm', 18), ('GEAR', 640, 104, 'mm', 18), ('EST. PIT EXIT', 1240, 117, 'rm', 18),
                     ('POSITION', 41, 314, 'lm', 18), ('FUEL Δ', 41, 470, 'lm', 18), ('PIT WINDOW', 1240, 314, 'rm', 18),
                     ('BRAKE BIAS', 1240, 470, 'rm', 18), ('SPEED', 640, 281, 'mm', 18), ('RPM', 380, 661, 'lm', 17),
                     ('PIT STOP TIME', 781, 661, 'lm', 17)])
    hs_picture('d', [('FUEL MODE', 44, 548, 'lm', 18), ('DIFFERENTIAL', 535, 548, 'lm', 18),
                     ('BRAKE BIAS', 1242, 548, 'rm', 18)], tyres=None, line_y=515, gear=False)

    # ---- every page: the dark screen, the battery bar, the mode badge, the lap / session top right
    hs_box('rd_hs_bg', 0, 0, 1280, 720, (20, 23, 27), 'rqsdp', L_BACK + 1)
    hs_box('rd_hs_scband', 0, 0, 1280, 252, (175, 128, 10), 's', L_BACK + 2)
    hs_box('rd_hs_socbase', 361, 25, 918, 75, (62, 72, 66), 'rqsdp', L_BACK + 3)
    for i, (x0, y0, x1, y1) in enumerate(HS_SEGS):  # hs_seg00 on the left
        hs_box(f'rd_hs_seg{i:02d}', x0, y0, x1 + 0.6, y1, (95, 175, 115), f'hs_seg{i:02d}')
        hs_box(f'rd_hs_sob{i:02d}', x0, y0, x1 + 0.6, y1, (40, 110, 255), f'hs_sob{i:02d}')
    socl = Image.new('L', (1672, 944), 0)
    ImageDraw.Draw(socl).text((M(380), M(50)), 'ERS SOC', font=fp_font(int(M(19))), fill=255, anchor='lm')
    car_quad('rd_hs_socl_hs', overlay_texture('rd_ovl_hssl', socl.resize((836, 472), Image.LANCZOS)), (0, 0, 1670, 942),
             (245, 245, 245), H + hs_leaf('rqsdp'), L_SHAPE + 4)
    hs_text('rd_hs_soc', 895, 62, H + 'soc_n', 'rqsdp', size=24, align='right', chars=2)
    for tag, colour in (('p', (100, 64, 160)), ('g', (35, 135, 55)), ('n', (70, 72, 82))):
        hs_box(f'rd_hs_mode{tag}', 38, 631, 335, 692, colour, f'hs_mode_{tag}')
    hs_text('rd_hs_mode', 186, 672, H + 'tm_badge', 'hs_mode', size=22, align='centre', value='')
    for st, word in (('r', 'RACE'), ('q', 'PRACTICE'), ('d', 'CAR DAMAGE'), ('p', 'PIT INFO')):
        hs_text(f'rd_hs_name{st}', 40, 61, H + 'none', st, size=24, value=word)
    hs_text('rd_hs_names', 40, 61, H + 'fp_scshort', 's', size=24, value='SC')
    hs_text('rd_hs_lap', 1125, 61, H + 'lap', 'rsdp', size=24)
    hs_text('rd_hs_sess', 1238, 61, H + 'fp_sess', 'q', size=24, align='right', chars=3)

    # ---- the tyre cells (temperatures; wear on the pit page) and the gear between them
    for c, (x0, x1), top in (('fl', (376, 521), True), ('fr', (759, 904), True), ('rl', (376, 521), False),
                             ('rr', (759, 904), False)):
        for st, base in (('rqp', 201), ('s', 286)):
            y0 = base if top else base + 97
            y1 = y0 + 97
            hs_box(f'rd_hs_cell{c}{st[0]}', x0, y0, x1, y1, (32, 128, 148) if top else (24, 110, 130), st, L_BACK + 3)
            hs_text(f'rd_hs_t{c}{st[0]}', (x0 + x1) / 2, y1 - 22, H + f't_{c}', 'rq' if st == 'rqp' else 's', size=34,
                    align='centre', value='--')
        hs_text(f'rd_hs_w{c}', (x0 + x1) / 2, (201 if top else 298) + 97 - 22, H + f'w_{c}', 'p', size=34,
                align='centre', value='--')
    cells = {}
    for tag, base in (('a', 201), ('b', 286)):
        img = Image.new('L', (1672, 944), 0)
        dc = ImageDraw.Draw(img)
        for c, x0, top in (('FL', 376, True), ('FR', 759, True), ('RL', 376, False), ('RR', 759, False)):
            dc.text((M(x0 + 12), M((base if top else base + 97) + 18)), c, font=fp_font(int(M(17))), fill=255, anchor='lm')
        cells[tag] = img
    car_quad('rd_hs_cella_hs', overlay_texture('rd_ovl_hsca', cells['a'].resize((836, 472), Image.LANCZOS)),
             (0, 0, 1670, 942), (245, 245, 245), H + hs_leaf('rqp'), L_SHAPE + 4)
    car_quad('rd_hs_cellb_hs', overlay_texture('rd_ovl_hscb', cells['b'].resize((836, 472), Image.LANCZOS)),
             (0, 0, 1670, 942), (245, 245, 245), H + hs_leaf('s'), L_SHAPE + 4)
    hs_text('rd_hs_gear', 640, 290, H + 'gear', 'rqp', size=112, align='centre', style='OSD_80_bold', value='N')
    hs_text('rd_hs_spd', 640, 533, H + 'fp_spd', 'rqp', size=62, align='centre')

    # ---- the rows: left (left-aligned), right (by width from the right edge)
    for st, left, right in (('r', ('cur', 'fp_delta', 'pos'), (('fp_gap0', 6), ('fp_gap1', 6), ('fuel', 5))),
                            ('q', ('fp_last', 'fp_pb', 'fp_delta'), (('s1', 6), ('s2', 6), ('s3', 6))),
                            ('p', ('pw', 'fp_stint', 'fp_wing'), (('tyre', 7), ('pst', 5), ('rejoin', 3)))):
        for i in range(3):
            hs_text(f'rd_hs_{st}l{i}', 46, VALY[i], H + left[i], st, size=42)
            key, n = right[i]
            hs_text(f'rd_hs_{st}r{i}', 1236, VALY[i], H + key, st, size=42, align='right', chars=n)
    # ---- the bottom row
    for st, a, b, nb in (('r', 'fp_rev', 'bbal', 3), ('q', 'fp_stint', 'fp_togo', 5), ('p', 'fuel', 'bbal', 3),
                         ('s', 'fp_rev', 'pst', 5)):
        hs_text(f'rd_hs_{st}b0', 511, 673, H + a, st, size=30)
        hs_text(f'rd_hs_{st}b1', 1238, 673, H + b, st, size=30, align='right', chars=nb)
    # ---- the safety car: the band's values, the gear, the speed, four values
    hs_text('rd_hs_scd', 46, 190, H + 'fp_delta', 's', size=54)
    hs_text('rd_hs_sgear', 640, 212, H + 'gear', 's', size=90, align='centre', style='OSD_80_bold', value='N')
    hs_text('rd_hs_spit', 1232, 190, H + 'pitnow', 's', size=54, align='right', chars=3)
    hs_text('rd_hs_sspd', 640, 380, H + 'fp_spd', 's', size=58, align='centre')
    hs_text('rd_hs_spos', 46, 385, H + 'pos', 's', size=46)
    hs_text('rd_hs_sfuel', 46, 540, H + 'fuel', 's', size=46)
    hs_text('rd_hs_spw', 1236, 385, H + 'pw', 's', size=46, align='right', chars=6)
    hs_text('rd_hs_sbb', 1236, 540, H + 'bbal', 's', size=46, align='right', chars=3)
    # ---- the car page
    hs_text('rd_hs_mix', 47, 608, H + 'mix', 'd', size=42)
    hs_text('rd_hs_diff', 538, 632, H + 'diff', 'd', size=50)
    hs_text('rd_hs_dbb', 1232, 632, H + 'bbal', 'd', size=50, align='right', chars=3)

    hdr = ['// Written by tools/build_ui.py: the Haas pages\' layout sets (hs_<states>), as kFpCombos.',
           '#pragma once', '', 'inline constexpr const char *kHsCombos[] = {']
    hdr += [f'    "{c}",' for c in sorted(hs_combos)] + ['};', '']
    open(os.path.join(ROOT, 'native', 'src', 'hs_combos.hpp'), 'w', encoding='utf-8').write('\n'.join(hdr))

    # ---- the 2026 Alpine's own pages (1.3.10, the user's mock-ups): a grid of outlined boxes on black, the battery
    # as a column on the left (32 segments), a little French flag stripe under the screen. White values (the user:
    # everything is in boxes; coloured sector times maybe later), grey names and lines. Layouts r q s d p, sets on
    # al_<states>.
    al_combos = set()

    def al_leaf(states):
        if '_' in states:
            return states
        al_combos.add(states)
        return 'al_' + states

    def al_text(name, x, y, dbentry, states, size=40, align='left', value='-', style='OSD_24_bold', chars=5):
        scale = M(size) / 250.0 if style == 'OSD_80_bold' else size / 34.0
        lw = W(900)
        nm = name + '_al'
        if align == 'left':
            left = X(M(x))
        elif align == 'right':
            left = X(M(x) - chars * 45 * scale)
        else:
            left = centred(X(M(x)), lw, scale)
        lcd.add_text(nm, 'speedo_value', left, Y(M(y)), width=lw, layer=L_TEXT, style=style,
                     halign=None if align == 'centre' else 'left')
        if scale != 1.0:
            lcd.move(nm, sx=scale, sy=scale)
        tpl.add_label(f'{nm}_w', nm, dbentry, value=value, colour=None, visible=H + al_leaf(states))

    def al_box(name, x0, y0, x1, y1, colour, states, layer=None):
        car_quad(name + '_al', white_texture(), (M(x0), M(y0), M(x1), M(y1)), colour, H + al_leaf(states),
                 L_PART if layer is None else layer)

    AL_SEGS = [(38, 78 + i * (542 / 32), 83, 78 + (i + 1) * (542 / 32) - 3) for i in range(32)]  # top .. bottom
    TOP3 = [(106, 14, 476, 112), (476, 14, 862, 112), (862, 14, 1263, 112)]
    GRID = [(106, 112, 535, 303), (805, 112, 1263, 303), (106, 303, 417, 466), (106, 466, 417, 600),
            (417, 303, 911, 500), (911, 303, 1263, 466), (417, 500, 911, 689), (911, 466, 1263, 689),
            (106, 600, 417, 689)]

    def al_pictures(tag, names, boxes):
        ln = Image.new('L', (1672, 944), 0)
        d = ImageDraw.Draw(ln)
        d.rectangle([M(17), M(14), M(1263), M(689)], outline=255, width=3)
        d.line([M(106), M(14), M(106), M(689)], fill=255, width=3)
        for x0, y0, x1, y1 in boxes:
            d.rectangle([M(x0), M(y0), M(x1), M(y1)], outline=255, width=3)
        car_quad(f'rd_al_lpic_{tag}_al', overlay_texture(f'rd_ovl_al{tag}l', ln.resize((836, 472), Image.LANCZOS)),
                 (0, 0, 1670, 942), (125, 125, 132), H + al_leaf(tag), L_SHAPE + 3)
        nm = Image.new('L', (1672, 944), 0)
        dn = ImageDraw.Draw(nm)
        for word, x, y, anchor, size in list(names) + [('ERS SOC', 60, 43, 'mm', 17)]:
            dn.text((M(x), M(y)), word, font=fp_font(int(M(size))), fill=255, anchor=anchor)
        car_quad(f'rd_al_npic_{tag}_al', overlay_texture(f'rd_ovl_al{tag}n', nm.resize((836, 472), Image.LANCZOS)),
                 (0, 0, 1670, 942), (200, 200, 206), H + al_leaf(tag), L_SHAPE + 4)

    def top(a, b, c):
        return [(a, 124, 33, 'lm', 18), (b, 668, 33, 'mm', 18), (c, 1242, 33, 'rm', 18)]

    def lower(a, b, c, d, tyres='TYRE TEMPS'):
        return [(a, 125, 341, 'lm', 19), (b, 125, 491, 'lm', 19), ('SPEED', 663, 334, 'mm', 19), (c, 929, 341, 'lm', 19),
                (d, 663, 546, 'mm', 19), (tyres, 928, 495, 'lm', 19), ('FL', 928, 550, 'lm', 18), ('FR', 1100, 550, 'lm', 18),
                ('RL', 928, 613, 'lm', 18), ('RR', 1100, 613, 'lm', 18)]

    GEARN = [('GEAR', 671, 132, 'mm', 19)]
    al_pictures('r', top('PREV LAP Δ', 'POSITION', 'RACE') + GEARN + [('LAP TIME', 125, 147, 'lm', 19),
                ('LAST LAP', 127, 269, 'lm', 18), ('AHEAD', 826, 171, 'lm', 19), ('BEHIND', 826, 250, 'lm', 19)] +
                lower('FUEL Δ', 'BRAKE BIAS', 'RPM', 'CLOCK'), TOP3 + GRID)
    al_pictures('q', top('DELTA TO BEST', 'SESSION', 'TIME LEFT') + GEARN + [('LAST LAP', 125, 147, 'lm', 19),
                ('PERSONAL BEST', 127, 269, 'lm', 18), ('S1', 826, 151, 'lm', 19), ('S2', 826, 211, 'lm', 19),
                ('S3', 826, 271, 'lm', 19)] + lower('STINT LAP', 'FUEL Δ', 'RPM', 'CLOCK'), TOP3 + GRID)
    al_pictures('p', top('PIT WINDOW', 'LAP', 'NEXT TYRES') + GEARN + [('REPAIR WING', 125, 156, 'lm', 19),
                ('TYRE AGE', 825, 156, 'lm', 19)] + lower('POSITION', 'FUEL Δ', 'PIT EXIT POS', 'PIT STOP TIME',
                                                            'TYRE WEAR'), TOP3 + GRID)
    al_pictures('s', lower('POSITION', 'FUEL Δ', 'LAP', 'PIT WINDOW'), GRID[2:])
    al_pictures('d', top('FUEL MODE', 'DIFFERENTIAL', 'BRAKE BIAS') + [('LAP', 445, 643, 'lm', 19),
                ('FUEL Δ', 855, 643, 'lm', 19)], TOP3 + [(106, 112, 1263, 600), (106, 600, 1263, 689)])
    # the safety car's band: white names on amber
    sc = Image.new('L', (1672, 944), 0)
    dsc = ImageDraw.Draw(sc)
    for word, x, y, anchor in (('VSC DELTA', 125, 53, 'lm'), ('GEAR', 669, 53, 'mm'), ('EST. PIT EXIT', 1240, 53, 'rm')):
        dsc.text((M(x), M(y)), word, font=fp_font(int(M(21))), fill=255, anchor=anchor)
    car_quad('rd_al_sclab_al', overlay_texture('rd_ovl_alsk', sc.resize((836, 472), Image.LANCZOS)), (0, 0, 1670, 942),
             (245, 245, 245), H + al_leaf('s'), L_SHAPE + 4)
    al_box('rd_al_scband', 106, 14, 1263, 303, (180, 132, 10), 's', L_BACK + 3)
    # the French flag stripes under the screen (one picture a colour, every page)
    for k, colour in enumerate(((45, 110, 200), (235, 235, 240), (200, 50, 60))):
        img = Image.new('L', (1672, 944), 0)
        di = ImageDraw.Draw(img)
        for x in (348, 553, 758):
            x0 = x + k * 58
            di.polygon([(M(x0 + 8), M(695)), (M(x0 + 66), M(695)), (M(x0 + 58), M(701)), (M(x0), M(701))], fill=255)
        car_quad(f'rd_al_flag{k}_al', overlay_texture(f'rd_ovl_alf{k}', img.resize((836, 472), Image.LANCZOS)),
                 (0, 0, 1670, 942), colour, H + al_leaf('rqsdp'), L_SHAPE + 4)

    # ---- every page: the battery column, the mode badge
    al_box('rd_al_socbase', 38, 78, 83, 620, (30, 40, 26), 'rqsdp', L_BACK + 3)
    for i, (x0, y0, x1, y1) in enumerate(AL_SEGS):  # al_seg00 at the bottom
        al_box(f'rd_al_seg{31 - i:02d}', x0, y0, x1, y1, (160, 230, 60), f'al_seg{31 - i:02d}')
        al_box(f'rd_al_sob{31 - i:02d}', x0, y0, x1, y1, (40, 110, 255), f'al_sob{31 - i:02d}')
    al_text('rd_al_soc', 62, 666, H + 'soc_n', 'rqsdp', size=22, align='centre')
    for tag, colour in (('p', (100, 64, 170)), ('g', (35, 135, 55)), ('n', (70, 72, 82))):
        al_box(f'rd_al_mode{tag}', 114, 613, 407, 676, colour, f'al_mode_{tag}')
    al_text('rd_al_mode', 260, 656, H + 'tm_badge', 'al_mode', size=22, align='centre', value='')

    # ---- the top row
    for st, (a, na), (b, nb), (c, nc) in (('r', ('fp_delta', 6), ('pos', 3), ('lap', 5)),
                                          ('q', ('fp_delta', 6), ('fp_sess', 3), ('fp_togo', 5)),
                                          ('p', ('pw', 6), ('lap', 5), ('tyre', 7)),
                                          ('d', ('mix', 3), ('diff', 3), ('bbal', 3))):
        al_text(f'rd_al_{st}t0', 128, 92, H + a, st, size=40)
        al_text(f'rd_al_{st}t1', 668, 92, H + b, st, size=40, align='centre')
        al_text(f'rd_al_{st}t2', 1240, 92, H + c, st, size=40, align='right', chars=nc)
    # ---- the gear and the speed
    al_text('rd_al_gear', 671, 262, H + 'gear', 'rqp', size=110, align='centre', style='OSD_80_bold', value='N')
    al_text('rd_al_spd', 663, 445, H + 'fp_spd', 'rqsp', size=80, align='centre')
    # ---- the second row: the lap time box (with its smaller line) and the right box
    al_text('rd_al_lt', 132, 220, H + 'cur', 'r', size=44)  # 50 ran past the box
    al_text('rd_al_ltsub', 258, 277, H + 'fp_last', 'r', size=20)
    al_text('rd_al_ql', 132, 220, H + 'fp_last', 'q', size=44)
    al_text('rd_al_qpb', 300, 277, H + 'fp_pb', 'q', size=20)
    al_text('rd_al_wing', 132, 234, H + 'fp_wing', 'p', size=56)
    al_text('rd_al_age', 832, 234, H + 'fp_stint', 'p', size=56)
    al_text('rd_al_ahead', 1233, 185, H + 'fp_gap0', 'r', size=38, align='right', chars=6)
    al_text('rd_al_behind', 1233, 264, H + 'fp_gap1', 'r', size=38, align='right', chars=6)
    for k, y in ((1, 165), (2, 225), (3, 285)):
        al_text(f'rd_al_s{k}', 1233, y, H + f's{k}', 'q', size=36, align='right', chars=6)
    # ---- the lower boxes
    for st, a, b, c, d in (('r', 'fuel', 'bbal', 'fp_rev', 'al_clock'), ('q', 'fp_stint', 'fuel', 'fp_rev', 'al_clock'),
                           ('p', 'pos', 'fuel', 'rejoin', 'pst'), ('s', 'pos', 'fuel', 'lap', 'pw')):
        al_text(f'rd_al_{st}l0', 130, 415, H + a, st, size=48)
        al_text(f'rd_al_{st}l1', 130, 565, H + b, st, size=44)
        al_text(f'rd_al_{st}r0', 934, 412, H + c, st, size=48)
        al_text(f'rd_al_{st}c', 663, 622, H + d, st, size=56, align='centre')
    for c, (x, y) in {'fl': (1070, 562), 'fr': (1244, 562), 'rl': (1070, 625), 'rr': (1244, 625)}.items():
        al_text(f'rd_al_t{c}', x, y, H + f't_{c}', 'rqs', size=30, align='right', chars=2.4, value='--')
        al_text(f'rd_al_w{c}', x, y, H + f'w_{c}', 'p', size=30, align='right', chars=3, value='--')
    # ---- the safety car's band
    al_text('rd_al_scd', 135, 180, H + 'fp_delta', 's', size=62)
    al_text('rd_al_sgear', 667, 220, H + 'gear', 's', size=120, align='centre', style='OSD_80_bold', value='N')
    al_text('rd_al_spit', 1228, 180, H + 'pitnow', 's', size=68, align='right', chars=3)
    # ---- the car page's bottom row
    al_text('rd_al_dlap', 530, 657, H + 'lap', 'd', size=36)
    al_text('rd_al_dfuel', 1232, 657, H + 'fuel', 'd', size=36, align='right', chars=5)

    hdr = ['// Written by tools/build_ui.py: the Alpine pages\' layout sets (al_<states>), as kFpCombos.',
           '#pragma once', '', 'inline constexpr const char *kAlCombos[] = {']
    hdr += [f'    "{c}",' for c in sorted(al_combos)] + ['};', '']
    open(os.path.join(ROOT, 'native', 'src', 'al_combos.hpp'), 'w', encoding='utf-8').write('\n'.join(hdr))

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
        # page 0's give way to the Ferrari's own page 0 (their _n copy); the rest show on every car (1.2.20)
        vis_n = split_leaf(vis)[0] if group in ('pg0', 'pgr', 'pgq', 'pg1', 'pg2') else (vis or H + 'all')
        for v, bend, vv in (('n', straight, vis_n),):
            tag = f'rd_ln_{group}_{v}'
            car_quad(tag, overlay_texture(tag, draw_lines(segs, bend)), (0, 0, 1670, 942), colour, vv, L_SHAPE)

    # (two textures since 0.4.23: 0.4.22's 35 rectangles took the display past 512 nodes and it drew nothing; a
    # Ferrari copy of each since 0.4.24, bent the other way: on that wheel it should show straight)
    # the switches the plugin splits (names after ui_menu_data.hud_rdash_)
    hdr = ['// Written by tools/build_ui.py: the switches drawn as two copies, <name>_n on any other car and',
           '// <name>_f on the 2026 Ferrari (its wheel bends the display; the _f copies are placed against the bend).',
           '#pragma once', '', 'inline constexpr const char *kSplitLeaves[] = {']
    hdr += [f'    "{leaf}",' for leaf in sorted(split)] + ['};', '']
    open(os.path.join(ROOT, 'native', 'src', 'split_leaves.hpp'), 'w', encoding='utf-8').write('\n'.join(hdr))

    os.makedirs(os.path.join(ROOT, 'ui', 'clips'), exist_ok=True)
    os.makedirs(os.path.join(ROOT, 'ui', 'tex'), exist_ok=True)
    strip_stock(lcd, tpl, shared_stock_elements(lcd))
    order_widgets(tpl)
    order_nodes(lcd, tpl)
    # colour limit tests only (2026-11-24). DD_CTEST=1 put red '#N' markers at file places among uncoloured elements:
    # only #60 (inside the coloured block) came out red, though 1.2.18's Ferrari colours at places up to ~104 worked
    # (all inside one unbroken coloured block). DD_CTEST=2 extends the unbroken coloured block (widgets and nodes)
    # with red fillers up to place 300; the fillers at the sample places show their place on top, the rest sit
    # tiny behind the backdrop. A screenshot shows where colour stops inside an unbroken block.
    # DD_CTEST=3 (2026-11-25): colour from a text's animation clip instead of colourOverride (the stock info_lap_time
    # is grey that way). Twelve labelled samples over every page, after every widget and node (past the override
    # limit): eleven coloured by clip alone (black and navy on white boxes), one control by colourOverride (OVR,
    # expected white out there). A sample showing its colour means clip colour has no such limit.
    if os.environ.get('DD_CTEST') == '3':
        samples = [('RED', (1.0, 0.2, 0.2)), ('GREEN', (0.2, 0.9, 0.3)), ('BLUE', (0.25, 0.45, 1.0)),
                   ('YELLOW', (1.0, 0.88, 0.2)), ('PURPLE', (0.5, 0.43, 0.94)), ('CYAN', (0.3, 0.85, 0.9)),
                   ('ORANGE', (1.0, 0.55, 0.1)), ('SALMON', (0.98, 0.51, 0.43)), ('GREY', (0.55, 0.55, 0.58)),
                   ('BLACK', (0.04, 0.04, 0.06)), ('NAVY', (0.15, 0.2, 0.55)), ('OVR', None)]
        for i, (word, rgb) in enumerate(samples):
            x, y = 230 + (i % 4) * 400, 230 + (i // 4) * 230
            nm = f'rd_cclip_{word.lower()}'
            if word in ('BLACK', 'NAVY'):
                car_quad(f'rd_cclip_box{i}', white_texture(), (x - 190, y - 130, x + 190, y + 40), (245, 245, 245),
                         None, L_FULL + 18)
            lcd.add_text(nm, 'rd_lap', X(x) - W(380) / 2, Y(y), width=W(380), layer=L_FULL + 20, style='OSD_24_bold')
            lcd.move(nm, sx=1.6, sy=1.6)
            if rgb is not None:
                colour_clip(nm, rgb + (1.0,))
            w = tpl.add_label(nm + '_w', nm, H + 'none', like='rd_lap_w', value=word,
                              colour=(245, 45, 45) if rgb is None else None)
            w[0][:] = [(k, v) for k, v in w[0] if k != 'visible']
        kids = [k.get('name').v for k in lcd.scene.get('children')]
        wl = [w[0].get('name').v if w[0].get('name') is not None else '' for w in tpl.lcd[1]]
        print('colour test 3: clip samples at nodes', [kids.index(f'rd_cclip_{w.lower()}') for w, _ in samples],
              'widgets', [wl.index(f'rd_cclip_{w.lower()}_w') for w, _ in samples])
    if os.environ.get('DD_CTEST') == '2':
        SAMPLES = [int(x) for x in os.environ.get('DD_CSAMPLES', '100,120,128,135,150,175,200,250,300').split(',')]
        kids = list(lcd.scene.get('children'))
        nstock = sum(1 for k in kids if not k.get('name').v.startswith('rd_'))
        ncol_n = len([k for k in kids if k.get('name').v.startswith('rd_')])  # placeholder, replaced below
        coloured = {d.get('debugName').v for w in tpl.lcd[1] if w[0].get('colourOverride') is not None
                    and w[0].get('name') is not None for path in w[4] for d in path}
        node_at = nstock
        while node_at < len(kids) and kids[node_at].get('name').v in coloured:
            node_at += 1
        wl = tpl.lcd[1]
        w_at = 0
        while w_at < len(wl) and wl[w_at][0].get('colourOverride') is not None:
            w_at += 1
        print(f'colour test 2: the coloured block ends at node {node_at - 1} / widget {w_at - 1}')
        made = 0
        place = node_at
        si = 0
        while place <= max(SAMPLES):
            nm = f'rd_ctest_{place}'
            show = place in SAMPLES
            if show:
                i = SAMPLES.index(place)
                cols = 3 if len(SAMPLES) <= 9 else 4
                x, y = (330 + (i % 3) * 500, 250 + (i // 3) * 220) if cols == 3 else (230 + (i % 4) * 400,
                                                                                     200 + (i // 4) * 190)
                lcd.add_text(nm, 'rd_lap', X(x) - W(300) / 2, Y(y), width=W(300), layer=L_FULL + 20,
                             style='OSD_24_bold')
                lcd.move(nm, sx=1.6, sy=1.6)
            else:
                lcd.add_text(nm, 'rd_lap', X(30), Y(30), width=W(40), layer=L_BACK - 10, style='OSD_24_bold')
                lcd.move(nm, sx=0.2, sy=0.2)
            node = lcd.scene.get('children')[-1]
            del lcd.scene.get('children')[-1]
            kids.insert(place, node)
            w = tpl.add_label(nm + '_w', nm, H + 'none', like='rd_lap_w', value=f'#{place}' if show else '8',
                              colour=(245, 45, 45))
            w[0][:] = [(k, v) for k, v in w[0] if k != 'visible']
            wl.remove(w)
            wl.insert(w_at + made, w)
            made += 1
            place += 1
        lcd.scene.set('children', L(kids))
        ks = [k.get('name').v for k in lcd.scene.get('children')]
        print('colour test 2:', made, 'fillers; sample node places', [ks.index(f'rd_ctest_{t}') for t in SAMPLES],
              'widget places', [[w[0].get('name').v if w[0].get('name') is not None else '' for w in wl]
                                .index(f'rd_ctest_{t}_w') for t in SAMPLES])
    # limit test builds only (2026-11-24): DD_PAD_TO=N pads the layout with tiny rectangles under the backdrop (no
    # widget, so no colour slot and nothing seen) until it holds N nodes, to find the display's real node limit
    pad_to = int(os.environ.get('DD_PAD_TO', '0') or 0)
    if pad_to:
        have = len(lcd.scene0.get('nodeidlookup'))
        for i in range(max(0, pad_to - have)):
            lcd.add_rect(f'rd_pad_{i:05d}', -2.9, -1.7, 0.01, 0.01, layer=L_BACK - 10, like='rd_backdrop')
        print(f'limit test: padded from {have} to {len(lcd.scene0.get("nodeidlookup"))} nodes')
    # DD_WPAD=N: N more pads, each with its own widget like a real element (half text labels with a fixed '8',
    # half rectangles with a group widget; shown by the always-on leaf, no colour override), under the backdrop
    wpad = int(os.environ.get('DD_WPAD', '0') or 0)
    if wpad:
        w0 = len(tpl.lcd[1])
        # DD_WPAD_HIDDEN=1 (2026-11-25): every pad on a switch nothing ever turns on (hidden, as ten teams' pages
        # would be while another team's show), one in five a text and the rest boxes (a team's pages' mix)
        hidden = bool(os.environ.get('DD_WPAD_HIDDEN'))
        for i in range(wpad):
            nm = f'rd_wpad_{i:05d}'
            vis = H + ('ctest_hidden' if hidden else 'all_n')
            if (i % 5 == 0) if hidden else (i % 2 == 0):
                lcd.add_text(nm, 'rd_lap', 0.0, 0.0, layer=L_BACK - 10)
                tpl.add_label(nm + '_w', nm, H + 'none', like='rd_lap_w', value='8', visible=vis)
            else:
                lcd.add_rect(nm, -0.1, -0.1, 0.2, 0.2, layer=L_BACK - 10, like='rd_backdrop')
                tpl.add_group(nm + '_group', [nm], vis)
        pad_to = pad_to or 1  # skip the node budget refusal
        print(f'limit test: {wpad} widget pads: {len(lcd.scene0.get("nodeidlookup"))} nodes, widgets {w0} -> '
              f'{len(tpl.lcd[1])}')
    archive_check(lcd.encode(), tpl.encode())  # before anything is written: a refused layout never reaches ui/
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
    # 1.3.8: ui/clips keeps only this build's clips (a test build's would otherwise be packed with the next one)
    keep = {res.split('/')[-1].split('?')[0] for res in clips}
    for f in os.listdir(os.path.join(ROOT, 'ui', 'clips')):
        if f.rsplit('.', 1)[0] not in keep:
            os.remove(os.path.join(ROOT, 'ui', 'clips', f))
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
    # 1.2.20: the 2026 Ferrari's wheel screen mapped straight (tools/ferrari_screen.py): its texture coordinates
    import ferrari_screen
    os.makedirs(os.path.join(ROOT, 'erp'), exist_ok=True)
    open(os.path.join(ROOT, 'erp', 'ferrari_screen_vb9.bin'), 'wb').write(ferrari_screen.screen_buffer(GAME))
    ops.append({'op': 'replace-resource', 'erp': ferrari_screen.REL, 'resource': ferrari_screen.VB_UV, 'fragment': 1,
                'source': 'erp/ferrari_screen_vb9.bin'})
    # 1.3.2: and the 2025 Ferrari's (taller, its own band), so the team pages show on it as on the 2026 car
    open(os.path.join(ROOT, 'erp', 'ferrari25_screen_vb3.bin'), 'wb').write(ferrari_screen.screen_buffer(GAME, '2025'))
    c25 = ferrari_screen.CARS['2025']
    ops.append({'op': 'replace-resource', 'erp': c25['rel'], 'resource': c25['idf'] + 'lod=all&vb=3', 'fragment': 1,
                'source': 'erp/ferrari25_screen_vb3.bin'})
    json.dump({'schema_version': 1, 'ops': ops}, open(os.path.join(ROOT, 'erp', 'driver_display.json'), 'w'), indent=2)
    # 1.2.15: ui/tex keeps only what an op uses (earlier builds left renumbered car part pictures behind, which were
    # packed with every release: 85 files the upload check flagged as unused)
    used = {src for op in ops for src in op.get('sources', [op.get('source')]) if src}
    tex = os.path.join(ROOT, 'ui', 'tex')
    stale = [n for n in os.listdir(tex) if 'ui/tex/' + n not in used]
    for n in stale:
        os.remove(os.path.join(tex, n))
    print(f'ui/tex: {len(stale)} unused file(s) removed')
    # sanity: both files read back
    pkgfmt.read(lcd.encode())
    # the LCD's node budget (2026-11-24 limit tests): 512 was never the limit (0.4.22's blank at 520 had another
    # cause). 4,096 nodes without widgets drew fine; 770 nodes / 701 widgets drew fine; 1,470 / 1,401 drew but
    # flashed with the game's own screen (a soft per-frame draw limit). 770 is the tested safe budget. Colour
    # overrides still reach only the first ~128 nodes (see order_nodes).
    # 2026-11-25: 2,405 nodes with 1,880 hidden (an 11-team test) drew fine: hidden ones are nearly free, what is
    # visible at once is what costs (1,470 visible flashed)
    # 1.3.9 (the user's choice): the cap raised to the 4,096-node pad test (hidden shapes drew fine); real
    # layouts with widgets past 2,405 are tested by each build in game
    LCD_MAX_NODES = 4096
    n_nodes = len(lcd.scene0.get('nodeidlookup'))
    print(f'lcd nodes: {n_nodes} of {LCD_MAX_NODES}')
    if n_nodes > LCD_MAX_NODES and not pad_to:
        raise SystemExit(f'the LCD layout has {n_nodes} nodes: past the tested {LCD_MAX_NODES} the display may '
                         "flash with the game's own screen")
    pkgfmt.read(tpl.encode())
    print('wrote ui/lcd_display.idf, ui/race_assets_f1.ui, erp/driver_display.json')


if __name__ == '__main__':
    main()
