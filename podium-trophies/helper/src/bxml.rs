//! Binary XML ("\x01BXML") as the podium scene and the trophy sequences store it, its TSMD size header, and the two
//! edits a new trophy type needs. The same format as tools/bxml.py, which round-trips every stock podium file.
use std::collections::HashSet;

#[derive(Clone)]
pub enum Node {
    El(Element),
    Text(Vec<u8>, u8),
}

#[derive(Clone)]
pub struct Element {
    pub name: Vec<u8>,
    pub attrs: Vec<(Vec<u8>, Vec<u8>)>,
    pub kids: Vec<Node>,
    flag: u8,
    end: u8,
}

impl Element {
    pub fn attr(&self, k: &[u8]) -> Option<&[u8]> {
        self.attrs.iter().find(|(a, _)| a == k).map(|(_, v)| v.as_slice())
    }

    pub fn set_attr(&mut self, k: &[u8], v: &[u8]) {
        for (a, val) in &mut self.attrs {
            if a == k {
                *val = v.to_vec();
            }
        }
    }

    fn walk<'a>(&'a self, out: &mut Vec<&'a Element>) {
        out.push(self);
        for k in &self.kids {
            if let Node::El(e) = k {
                e.walk(out);
            }
        }
    }

    fn walk_mut(&mut self, f: &mut dyn FnMut(&mut Element)) {
        f(self);
        for k in &mut self.kids {
            if let Node::El(e) = k {
                e.walk_mut(f);
            }
        }
    }
}

pub struct Doc {
    pub root: Element,
    tail: Vec<u8>,
}

struct Reader<'a> {
    d: &'a [u8],
    pos: usize,
}

impl Reader<'_> {
    fn rec(&mut self) -> Result<(u8, u8, u16, &[u8]), String> {
        let d = self.d;
        let h = d.get(self.pos..self.pos + 6).ok_or("binary XML ends early")?;
        let len = u16::from_le_bytes([h[0], h[1]]) as usize;
        let (kind, flag, n) = (h[2], h[3], u16::from_le_bytes([h[4], h[5]]));
        let body = d.get(self.pos + 6..self.pos + 2 + len).ok_or("binary XML ends early")?;
        self.pos += 2 + len;
        Ok((kind, flag, n, body))
    }

    fn element(&mut self, flag: u8, n: u16, body: &[u8]) -> Result<Element, String> {
        let parts: Vec<&[u8]> = body.split(|&c| c == 0).collect();
        let n = n as usize;
        if parts.len() < 1 + 2 * n {
            return Err("an element has fewer attributes than it says".into());
        }
        let mut el = Element {
            name: parts[0].to_vec(),
            attrs: (0..n).map(|i| (parts[1 + 2 * i].to_vec(), parts[2 + 2 * i].to_vec())).collect(),
            kids: Vec::new(),
            flag,
            end: 0,
        };
        loop {
            let (kind, f, m, b) = self.rec()?;
            match kind {
                5 => {
                    el.end = f;
                    return Ok(el);
                }
                0 => {
                    let b = b.to_vec();
                    el.kids.push(Node::El(self.element(f, m, &b)?));
                }
                1 => el.kids.push(Node::Text(b[..b.len().saturating_sub(1)].to_vec(), f)),
                k => return Err(format!("record kind {k} inside an element")),
            }
        }
    }
}

pub fn parse(d: &[u8]) -> Result<Doc, String> {
    if d.get(..5) != Some(b"\x01BXML") {
        return Err("not binary XML".into());
    }
    let mut r = Reader { d, pos: 5 };
    let (kind, flag, n, body) = r.rec()?;
    if kind != 0 {
        return Err("the root is not an element".into());
    }
    let body = body.to_vec();
    let root = r.element(flag, n, &body)?;
    let mut tail = Vec::new();
    while r.pos < d.len() {
        let (kind, flag, _, _) = r.rec()?;
        if kind != 6 {
            return Err(format!("record kind {kind} after the root"));
        }
        tail.push(flag);
    }
    Ok(Doc { root, tail })
}

fn rec(out: &mut Vec<u8>, kind: u8, flag: u8, n: usize, body: &[u8]) {
    out.extend_from_slice(&((body.len() + 4) as u16).to_le_bytes());
    out.extend_from_slice(&[kind, flag]);
    out.extend_from_slice(&(n as u16).to_le_bytes());
    out.extend_from_slice(body);
}

fn write_el(out: &mut Vec<u8>, el: &Element) {
    let mut body = el.name.clone();
    body.push(0);
    for (k, v) in &el.attrs {
        body.extend_from_slice(k);
        body.push(0);
        body.extend_from_slice(v);
        body.push(0);
    }
    rec(out, 0, el.flag, el.attrs.len(), &body);
    for k in &el.kids {
        match k {
            Node::El(e) => write_el(out, e),
            Node::Text(t, f) => {
                let mut b = t.clone();
                b.push(0);
                rec(out, 1, *f, 0, &b);
            }
        }
    }
    rec(out, 5, el.end, 0, &[]);
}

pub fn write(doc: &Doc) -> Vec<u8> {
    let mut out = b"\x01BXML".to_vec();
    write_el(&mut out, &doc.root);
    for &f in &doc.tail {
        rec(&mut out, 6, f, 0, &[]);
    }
    out
}

/// The 28 TSMD counts: count and total length of each string group, then of its distinct strings.
fn stats(root: &Element) -> Vec<u32> {
    let mut els = Vec::new();
    root.walk(&mut els);
    let texts: Vec<&[u8]> = els
        .iter()
        .flat_map(|e| e.kids.iter())
        .filter_map(|k| if let Node::Text(t, _) = k { Some(t.as_slice()) } else { None })
        .collect();
    let keys: Vec<&[u8]> = els.iter().flat_map(|e| e.attrs.iter().map(|(k, _)| k.as_slice())).collect();
    let vals: Vec<&[u8]> = els.iter().flat_map(|e| e.attrs.iter().map(|(_, v)| v.as_slice())).collect();
    let names: Vec<&[u8]> = els.iter().map(|e| e.name.as_slice()).collect();
    fn cat<'a>(a: &[&Vec<&'a [u8]>]) -> Vec<&'a [u8]> {
        a.iter().flat_map(|g| g.iter().copied()).collect()
    }
    let groups = [
        keys.clone(),
        vals.clone(),
        names.clone(),
        texts.clone(),
        cat(&[&names, &keys]),
        cat(&[&vals, &texts]),
        cat(&[&names, &keys, &vals, &texts]),
    ];
    let mut out = Vec::with_capacity(28);
    for g in &groups {
        out.push(g.len() as u32);
        out.push(g.iter().map(|s| s.len() as u32).sum());
    }
    for g in &groups {
        let u: HashSet<&[u8]> = g.iter().copied().collect();
        out.push(u.len() as u32);
        out.push(u.iter().map(|s| s.len() as u32).sum());
    }
    out
}

/// The TSMD header for an edited file: its own stats plus any margin the stock header had over the stock file, so
/// it never asks for less memory than the game's own did.
pub fn tsmd(edited: &Element, stock0: &[u8], stock1: &[u8]) -> Result<Vec<u8>, String> {
    if stock1.len() != 120 || &stock1[..4] != b"TSMD" {
        return Err("the stock size header is not TSMD".into());
    }
    let word = |i: usize| u32::from_le_bytes(stock1[4 + 4 * i..8 + 4 * i].try_into().unwrap());
    let version = word(0);
    let mine = stats(&parse(stock0)?.root);
    let new = stats(edited);
    let mut out = b"TSMD".to_vec();
    out.extend_from_slice(&version.to_le_bytes());
    for i in 0..28 {
        let margin = word(i + 1).saturating_sub(mine[i]);
        out.extend_from_slice(&(new[i] + margin).to_le_bytes());
    }
    Ok(out)
}

pub const TROPHY_DIR: &str = "eaid://environment_package/core_assets/objects/idf/trophy/";

/// The podium scene with a copy of the donor's scene for each new type, after the last trophy scene.
pub fn edit_scene(frag0: &[u8], types: &[(String, String)]) -> Result<Doc, String> {
    let mut doc = parse(frag0)?;
    let kids = &mut doc.root.kids;
    let is_scene = |n: &Node, name: Option<&[u8]>| match n {
        Node::El(e) if e.name == b"scene" => match name {
            Some(want) => e.attr(b"name") == Some(want),
            None => e.attr(b"name").is_some_and(|v| v.starts_with(b"trophy_")),
        },
        _ => false,
    };
    let mut at = kids.iter().rposition(|n| is_scene(n, None)).ok_or("the podium scene has no trophy scenes")? + 1;
    for (t, donor) in types {
        let src = kids
            .iter()
            .find(|n| is_scene(n, Some(donor.as_bytes())))
            .ok_or_else(|| format!("{donor}: no such scene in podiums2019.scene"))?;
        let Node::El(mut s) = src.clone() else { unreachable!() };
        s.set_attr(b"name", t.as_bytes());
        let mesh = format!("{TROPHY_DIR}{t}_01");
        let mut bad = None;
        s.walk_mut(&mut |e| {
            if e.attr(b"id") == Some(donor.as_bytes()) {
                e.set_attr(b"id", t.as_bytes());
            }
            if let Some(m) = e.attr(b"mesh") {
                if !m.starts_with(TROPHY_DIR.as_bytes()) {
                    bad = Some(String::from_utf8_lossy(m).into_owned());
                }
                e.set_attr(b"mesh", mesh.as_bytes());
            }
        });
        if let Some(m) = bad {
            return Err(format!("{donor}: its scene names the mesh {m}"));
        }
        kids.insert(at, Node::El(s));
        at += 1;
    }
    Ok(doc)
}

/// A trophy sequence with a copy of a stock trophy's winner's clip (its hand pose) for each new type, after it:
/// (new type, the stock trophy whose clip it copies).
pub fn edit_sequence(frag0: &[u8], types: &[(String, String)]) -> Result<Doc, String> {
    let mut doc = parse(frag0)?;
    for (t, donor) in types {
        let want = format!("~driver_placed_first_{donor}");
        let kids = &mut doc.root.kids;
        let i = kids
            .iter()
            .position(|n| matches!(n, Node::El(e) if e.name == b"NeSequenceClip" && e.attr(b"name") == Some(want.as_bytes())))
            .ok_or_else(|| format!("a trophy sequence has no {want} clip"))?;
        let Node::El(mut c) = kids[i].clone() else { unreachable!() };
        c.set_attr(b"name", format!("~driver_placed_first_{t}").as_bytes());
        kids.insert(i + 1, Node::El(c));
    }
    Ok(doc)
}

/// The trophy types the scene has (the stock ones), so a pack can't reuse one.
pub fn scene_types(frag0: &[u8]) -> Result<Vec<String>, String> {
    let doc = parse(frag0)?;
    Ok(doc
        .root
        .kids
        .iter()
        .filter_map(|n| match n {
            Node::El(e) if e.name == b"scene" => e.attr(b"name").filter(|v| v.starts_with(b"trophy_")),
            _ => None,
        })
        .map(|v| String::from_utf8_lossy(v).into_owned())
        .collect())
}
