//! Podium Trophies launcher helper.
//!
//! A trophy pack is a folder with pack.json: ready game resources for one or more trophies, each a new podium
//! trophy type (see tools/build_pack.py). The engine reads its own pack (pack/) and the podium-trophies/ folder of
//! every enabled addon, lets the player pair a trophy with each track on its Trophies page, and at build adds the
//! paired trophies: their resources, a scene and five winner's clips for each type (copied from the stock trophy it
//! stands in for), and the database values that point each track at its trophy. A track nobody pairs keeps the
//! game's trophy, and with no pairing at all the build changes nothing.
mod bxml;
mod erp;

use std::collections::{BTreeMap, HashMap, HashSet};
use std::io::{BufRead, Lines, StdinLock, StdoutLock, Write};
use std::path::{Path, PathBuf};

use serde_json::{Map, Value, json};

const API: u64 = 1;
const SECTION: &str = "trophies";
const PACK_DIR: &str = "podium-trophies"; // in another addon
const OWN_PACK: &str = "pack"; // in this addon
const PODIUM: &str = "2025_asset_groups/animation_package/scenes/cinematics/cinematicspodium.erp";
const CINEMATICS: &str = "2025_asset_groups/animation_package/scenes/cinematics/";
const SCENE: &str = "eaid://animation_package/scenes/cinematics/podiums2019.scene";
// Every stock archive that carries the scene; the sequences are in the first two. Each gets the edited copy, or the
// game may load the stock one.
const SCENE_ERPS: [&str; 7] = [
    "cinematicspodium.erp",
    "cinematicspodiummonaco.erp",
    "podium_car_filler_01.erp",
    "podium/podium2022_celebrate_parcferme_01.erp",
    "podium/podium2022_celebrate_parcferme_02.erp",
    "podium/podium2022_celebrate_pitlane.erp",
    "podium/podium2022_celebrate_pitlane_02.erp",
];
const SEQUENCE_ERPS: [&str; 2] = ["cinematicspodium.erp", "cinematicspodiummonaco.erp"];
// Monaco's podium is its own scene with its own trophy.
const NOT_PAIRED: [&str; 1] = ["monaco"];
const FACTS: &str = include_str!("../../tools/track_trophies.json");

fn sequence(n: u32) -> String {
    format!("eaid://animation_package/sequences/cinematics/podium/podium2019_trophy_seq0{n}.sequence")
}

// ---- the protocol

struct Host {
    lines: Lines<StdinLock<'static>>,
    out: StdoutLock<'static>,
    next: u64,
}

impl Host {
    fn send(&mut self, m: &Value) {
        writeln!(self.out, "{m}").and_then(|()| self.out.flush()).ok();
    }

    fn call(&mut self, method: &str, params: Value) -> Result<Value, String> {
        let id = self.next;
        self.next += 1;
        self.send(&json!({ "id": id, "method": method, "params": params }));
        let line = self.lines.next().ok_or("the launcher closed the connection")?.map_err(|e| e.to_string())?;
        let reply: Value = serde_json::from_str(&line).map_err(|e| format!("bad reply to {method}: {e}"))?;
        if reply["id"] != json!(id) {
            return Err(format!("the launcher answered {method} out of order"));
        }
        if let Some(e) = reply.get("error") {
            return Err(e.as_str().unwrap_or("error").to_owned());
        }
        Ok(reply["result"].clone())
    }

    fn progress(&mut self, done: f32, text: &str) {
        self.send(&json!({ "method": "progress", "params": { "done": done, "text": text } }));
    }
}

// ---- packs and tracks

struct Trophy {
    id: String,
    name: String,
    donor: String,
    grip: String,
    tracks: Vec<String>,
    ops: Vec<Value>,
    dir: PathBuf,
    addon: String,
    addon_name: String,
    own: bool,
    preview: Option<PathBuf>,
}

struct Track {
    code: String,
    name: String,
    addon: Option<String>,
    addon_name: Option<String>,
}

struct App {
    addon: String,
    own_dir: PathBuf,
    cache_dir: PathBuf,
    host_calls: Vec<String>,
    vanilla: bool,
    facts: Value,
}

struct World {
    trophies: Vec<Trophy>,
    tracks: Vec<Track>,
    tracks_known: bool,
    addons: Vec<Value>,
    picks: Map<String, Value>,
    warnings: Vec<String>,
}

fn title(code: &str) -> String {
    code.split('_')
        .map(|w| {
            let mut c = w.chars();
            c.next().map(|f| f.to_uppercase().collect::<String>() + c.as_str()).unwrap_or_default()
        })
        .collect::<Vec<_>>()
        .join(" ")
}

fn safe_rel(rel: &str) -> bool {
    !rel.is_empty()
        && !rel.starts_with('/')
        && !rel.contains(':')
        && !rel.contains('\\')
        && rel.split('/').all(|p| !p.is_empty() && p != "." && p != "..")
}

impl App {
    fn load_pack(&self, dir: &Path, addon: &str, addon_name: &str, own: bool, w: &mut World) {
        let path = dir.join("pack.json");
        let Ok(text) = std::fs::read_to_string(&path) else { return };
        let pack: Value = match serde_json::from_str(&text) {
            Ok(v) => v,
            Err(e) => return w.warnings.push(format!("{addon_name}: {} is not valid JSON: {e}", path.display())),
        };
        if pack["schema_version"] != json!(1) {
            return w.warnings.push(format!("{addon_name}: its trophy pack has schema_version {}; this engine reads 1", pack["schema_version"]));
        }
        for t in pack["trophies"].as_array().into_iter().flatten() {
            let id = t["id"].as_str().unwrap_or_default().to_owned();
            let ok_id = id.starts_with("trophy_")
                && id.len() <= 47
                && id.bytes().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == b'_');
            if !ok_id {
                w.warnings.push(format!("{addon_name}: trophy id {id:?} is not \"trophy_\" and a-z, 0-9 or _"));
                continue;
            }
            if let Some(other) = w.trophies.iter().find(|o| o.id == id) {
                w.warnings.push(format!("{addon_name}: {id} is also in {}, which loads first and is kept", other.addon_name));
                continue;
            }
            let ops: Vec<Value> = t["ops"].as_array().cloned().unwrap_or_default();
            let bad = ops.iter().find(|o| {
                !matches!(o["op"].as_str(), Some("add-resource" | "copy-resource"))
                    || o["sources"].as_array().into_iter().flatten().any(|s| !s.as_str().is_some_and(safe_rel))
            });
            if let Some(o) = bad {
                w.warnings.push(format!("{addon_name}: {id} has an op this engine doesn't take: {}", o["op"]));
                continue;
            }
            let model = format!("{}{id}_01.idf?model", bxml::TROPHY_DIR);
            if !ops.iter().any(|o| o["op"] == "add-resource" && o["resource"] == json!(model)) {
                w.warnings.push(format!("{addon_name}: {id} has no model named for it ({model}); build the pack with tools/build_pack.py"));
                continue;
            }
            let preview = t["preview"].as_str().filter(|p| safe_rel(p)).map(|p| dir.join(p)).filter(|p| p.is_file());
            w.trophies.push(Trophy {
                id,
                name: t["name"].as_str().unwrap_or_default().to_owned(),
                donor: t["donor"].as_str().unwrap_or_default().to_owned(),
                // the stock trophy whose winner's clip (hand pose) it copies; "none" is Monaco's, which has none
                grip: match t["grip"].as_str().unwrap_or(t["donor"].as_str().unwrap_or_default()) {
                    "none" => "trophy_monaco".to_owned(),
                    g => g.to_owned(),
                },
                tracks: serde_json::from_value(t["tracks"].clone()).unwrap_or_default(),
                ops,
                dir: dir.to_path_buf(),
                addon: addon.to_owned(),
                addon_name: addon_name.to_owned(),
                own,
                preview,
            });
        }
    }

    fn world(&self, host: &mut Host) -> Result<World, String> {
        let mut w = World {
            trophies: Vec::new(),
            tracks: Vec::new(),
            tracks_known: false,
            addons: Vec::new(),
            picks: Map::new(),
            warnings: Vec::new(),
        };
        self.load_pack(&self.own_dir.join(OWN_PACK), &self.addon, "Podium Trophies", true, &mut w);
        if self.host_calls.iter().any(|c| c == "addons") {
            w.addons = host.call("addons", Value::Null)?.as_array().cloned().unwrap_or_default();
            for a in w.addons.clone() {
                let id = a["id"].as_str().unwrap_or_default();
                if a["enabled"] != json!(true) || id == self.addon {
                    continue;
                }
                let dir = PathBuf::from(a["dir"].as_str().unwrap_or_default()).join(PACK_DIR);
                self.load_pack(&dir, id, a["name"].as_str().unwrap_or(id), false, &mut w);
            }
        }
        let name_of = |id: &str, addons: &[Value]| {
            addons.iter().find(|a| a["id"] == json!(id)).and_then(|a| a["name"].as_str()).unwrap_or(id).to_owned()
        };
        if let Ok(v) = host.call("profile_tracks", Value::Null)
            && let Some(list) = v["tracks"].as_array()
        {
            w.tracks_known = true;
            for t in list {
                let code = t["code"].as_str().unwrap_or_default().to_owned();
                if code.is_empty() || NOT_PAIRED.contains(&code.as_str()) || w.tracks.iter().any(|x| x.code == code) {
                    continue;
                }
                let addon = if t["modded"] == json!(true) { t["addon"].as_str().map(str::to_owned) } else { None };
                w.tracks.push(Track {
                    name: t["name"].as_str().map(str::to_owned).unwrap_or_else(|| title(&code)),
                    addon_name: addon.as_deref().map(|a| name_of(a, &w.addons)),
                    addon,
                    code,
                });
            }
        }
        if !w.tracks_known {
            for code in self.facts["tracks"].as_object().into_iter().flatten().map(|(k, _)| k) {
                if !NOT_PAIRED.contains(&code.as_str()) {
                    w.tracks.push(Track { code: code.clone(), name: title(code), addon: None, addon_name: None });
                }
            }
        }
        let state = host.call("get_state", Value::Null)?;
        w.picks = state["picks"].as_object().cloned().unwrap_or_default();
        Ok(w)
    }
}

impl World {
    /// The trophy a track gets without a pick: one its own addon's pack names it for, else the engine's, else the
    /// first pack that names it.
    fn default_for(&self, t: &Track) -> Option<&Trophy> {
        let named: Vec<&Trophy> = self.trophies.iter().filter(|x| x.tracks.contains(&t.code)).collect();
        named
            .iter()
            .find(|x| t.addon.as_deref() == Some(x.addon.as_str()))
            .or_else(|| named.iter().find(|x| x.own))
            .or_else(|| named.first())
            .copied()
    }

    /// (the trophy, whether the player picked it) for a track; None is the game's own trophy.
    fn paired(&self, t: &Track) -> (Option<&Trophy>, bool) {
        match self.picks.get(&t.code).and_then(Value::as_str) {
            Some("stock") => (None, true),
            Some(id) => match self.trophies.iter().find(|x| x.id == id) {
                Some(x) => (Some(x), true),
                None => (self.default_for(t), false),
            },
            None => (self.default_for(t), false),
        }
    }
}

// ---- the build

fn find_all<'a>(text: &'a str, pre: &str, post: &str) -> Vec<&'a str> {
    let mut out = Vec::new();
    let mut rest = text;
    while let Some(i) = rest.find(pre) {
        rest = &rest[i + pre.len()..];
        if let Some(j) = rest.find(post) {
            out.push(&rest[..j]);
        }
    }
    out
}

fn json_files(dir: &Path, depth: u32, out: &mut Vec<PathBuf>) {
    let Ok(rd) = std::fs::read_dir(dir) else { return };
    for e in rd.flatten() {
        let p = e.path();
        if p.is_dir() && depth > 0 {
            json_files(&p, depth - 1, out);
        } else if p.extension().is_some_and(|x| x == "json") && e.metadata().is_ok_and(|m| m.len() < 8 << 20) {
            out.push(p);
        }
    }
}

impl App {
    /// The data sets whose TrackData row for code sets the trophy: from the stock facts, or for a modded track from
    /// its addon's files (the base track a track package copies, and the data sets its own ops name).
    fn datasets(&self, t: &Track, addons: &[Value]) -> Vec<String> {
        if t.addon.is_none() {
            return serde_json::from_value(self.facts["tracks"][&t.code]["datasets"].clone()).unwrap_or_default();
        }
        let mut sets: Vec<String> = Vec::new();
        let dir = addons
            .iter()
            .find(|a| a["id"].as_str() == t.addon.as_deref())
            .and_then(|a| a["dir"].as_str())
            .map(PathBuf::from);
        let mut files = Vec::new();
        if let Some(d) = dir {
            json_files(&d, 4, &mut files);
        }
        for f in files {
            let Ok(text) = std::fs::read_to_string(&f) else { continue };
            if text.contains(&format!("\"track_code\": \"{}\"", t.code)) || text.contains(&format!("\"track_code\":\"{}\"", t.code)) {
                if let Ok(v) = serde_json::from_str::<Value>(&text)
                    && let Some(base) = v["activation"]["copy_from_track"].as_str()
                {
                    let b: Vec<String> =
                        serde_json::from_value(self.facts["tracks"][base]["datasets"].clone()).unwrap_or_default();
                    sets.extend(b);
                }
            }
            for ds in find_all(&text, "TrackDataSet[_metadata_=", "]") {
                let tail = format!("{ds}].TrackData[_metadata_={}]", t.code);
                if text.contains(&format!("TrackDataSet[_metadata_={tail}")) {
                    sets.push(ds.to_owned());
                }
            }
        }
        sets.sort();
        sets.dedup();
        sets
    }

    fn source(&self, t: &Trophy, rel: &str) -> Result<Value, String> {
        if t.own {
            return Ok(json!(format!("{OWN_PACK}/{rel}")));
        }
        // another addon's file: the launcher reads sources from this addon, its data_dir or its cache_dir
        let cached = PathBuf::from("packs").join(&t.addon).join(&t.id).join(rel);
        let dst = self.cache_dir.join(&cached);
        let src = t.dir.join(rel);
        let fresh = match (std::fs::metadata(&src), std::fs::metadata(&dst)) {
            (Ok(a), Ok(b)) => a.len() == b.len() && a.modified().ok() <= b.modified().ok(),
            _ => false,
        };
        if !fresh {
            std::fs::create_dir_all(dst.parent().unwrap()).map_err(|e| e.to_string())?;
            std::fs::copy(&src, &dst).map_err(|e| format!("{}: {e}", src.display()))?;
        }
        Ok(json!({ "root": "cache", "path": cached.to_string_lossy().replace('\\', "/") }))
    }

    fn original(&self, host: &mut Host, rel: &str) -> Result<erp::Archive, String> {
        let p = host.call("game_original", json!({ "rel": rel }))?;
        let p = p.as_str().ok_or_else(|| format!("the launcher has no clean copy of {rel}"))?;
        erp::Archive::open(Path::new(p))
    }

    fn build(&self, host: &mut Host, params: &Value) -> Result<Value, String> {
        let stable: Vec<&str> = params["features"]["stable"].as_array().into_iter().flatten().filter_map(Value::as_str).collect();
        if !(stable.contains(&"erp") && stable.contains(&"db")) {
            return Ok(json!({ "warnings": ["this game loader can't apply archive and database changes, so no trophy is added"] }));
        }
        let w = self.world(host)?;
        let mut warnings = w.warnings.clone();
        let pairs: Vec<(&Track, &Trophy)> = w.tracks.iter().filter_map(|t| w.paired(t).0.map(|x| (t, x))).collect();
        // the build_all setting (a test): every installed trophy goes into the podium scene, paired or not
        let all = params["options"]["build_all"] == json!(true);
        if pairs.is_empty() && !(all && !w.trophies.is_empty()) {
            return Ok(json!({ "lines": ["no trophy paired with a track: the podiums keep the game's trophies"], "warnings": warnings }));
        }
        let mut used: Vec<&Trophy> = Vec::new();
        for x in pairs.iter().map(|(_, x)| *x).chain(w.trophies.iter().filter(|_| all)) {
            if !used.iter().any(|u| u.id == x.id) {
                used.push(x);
            }
        }
        host.progress(0.1, "Reading the podium scene");
        let podium = self.original(host, PODIUM)?;
        let stock: HashSet<String> = bxml::scene_types(&podium.fragments(SCENE)?[0])?.into_iter().collect();
        let have: HashSet<&str> = podium.entries.iter().map(|e| e.name.as_str()).collect();
        let mut types: Vec<(String, String)> = Vec::new();
        let mut grips: Vec<(String, String)> = Vec::new();
        let (mut copies, mut adds, mut replaces, mut db) = (Vec::new(), Vec::new(), Vec::new(), Vec::new());
        let mut seen: HashSet<String> = HashSet::new();
        let mut lines = Vec::new();
        for x in &used {
            if stock.contains(&x.id) {
                warnings.push(format!("{}: {} is a stock trophy type; left out", x.addon_name, x.id));
                continue;
            }
            if !stock.contains(&x.donor) || x.donor == "trophy_monaco" {
                warnings.push(format!("{}: {} stands in for {}, which isn't a podium trophy this engine copies; left out", x.addon_name, x.id, x.donor));
                continue;
            }
            if !stock.contains(&x.grip) || x.grip.starts_with("trophy_f2") {
                warnings.push(format!("{}: {} takes the grip of {}, which isn't a stock podium trophy; left out", x.addon_name, x.id, x.grip));
                continue;
            }
            for o in &x.ops {
                let res = o["resource"].as_str().unwrap_or_default().to_owned();
                if have.contains(res.as_str()) || !seen.insert(res.clone()) {
                    continue; // already in the archive, or added by another trophy (a shared game material)
                }
                match o["op"].as_str() {
                    Some("copy-resource") => copies.push(json!({ "op": "erp.copy-resource", "from_erp": o["from_erp"],
                        "erp": PODIUM, "resource": res })),
                    _ => {
                        let mut sources = Vec::new();
                        for s in o["sources"].as_array().into_iter().flatten() {
                            sources.push(self.source(x, s.as_str().unwrap_or_default())?);
                        }
                        adds.push(json!({ "op": "erp.add-resource", "erp": PODIUM, "resource": res, "type": o["type"],
                            "sources": sources }));
                    }
                }
            }
            types.push((x.id.clone(), x.donor.clone()));
            grips.push((x.id.clone(), x.grip.clone()));
        }
        // the scene and the sequences, from each archive's own clean copy
        let sdir = self.cache_dir.join("scene");
        std::fs::create_dir_all(&sdir).map_err(|e| e.to_string())?;
        let mut jobs: Vec<(String, String, bool)> = SCENE_ERPS.iter().map(|a| (a.to_string(), SCENE.to_string(), true)).collect();
        for a in SEQUENCE_ERPS {
            for n in 1..=5 {
                jobs.push((a.to_string(), sequence(n), false));
            }
        }
        let mut open: HashMap<String, erp::Archive> = HashMap::new();
        for (k, (arc, res, scene)) in jobs.iter().enumerate() {
            host.progress(0.2 + 0.7 * k as f32 / jobs.len() as f32, &format!("Adding the trophies to {arc}"));
            if !open.contains_key(arc) {
                let a = self.original(host, &format!("{CINEMATICS}{arc}"))?;
                open.insert(arc.clone(), a);
            }
            let a = open.get(arc).unwrap();
            let f = a.fragments(res)?;
            if f.len() != 2 {
                return Err(format!("{arc}: {res} has {} fragments, not 2", f.len()));
            }
            let doc = if *scene { bxml::edit_scene(&f[0], &types)? } else { bxml::edit_sequence(&f[0], &grips)? };
            let f0 = bxml::write(&doc);
            let f1 = bxml::tsmd(&doc.root, &f[0], &f[1])?;
            let stem = format!("{}.{}", arc.replace('/', "_"), res.rsplit('/').next().unwrap());
            for (i, b) in [(0, &f0), (1, &f1)] {
                let name = format!("{stem}.{i}.bin");
                std::fs::write(sdir.join(&name), b).map_err(|e| e.to_string())?;
                replaces.push(json!({ "op": "erp.replace-resource", "erp": format!("{CINEMATICS}{arc}"), "resource": res,
                    "fragment": i, "source": { "root": "cache", "path": format!("scene/{name}") } }));
            }
        }
        let kept: HashSet<&str> = types.iter().map(|(t, _)| t.as_str()).collect();
        for (t, x) in &pairs {
            if !kept.contains(x.id.as_str()) {
                continue;
            }
            let when = t.addon.as_ref().map(|a| json!({ "addon": a }));
            let mut set = |select: String, field: &str| {
                let mut o = json!({ "op": "db.set", "select": select, "field": field, "value": x.id });
                if let Some(wh) = &when {
                    o["when"] = wh.clone();
                    o["create"] = json!(true);
                    o["type"] = json!(2);
                }
                db.push(o);
            };
            set(format!("tracks.Track[_metadata_={}]", t.code), "trophyType");
            let sets = self.datasets(t, &w.addons);
            for ds in &sets {
                set(format!("track_data.game_setup.TrackDataSet[_metadata_={ds}].TrackData[_metadata_={}]", t.code), "TrackGeneralData_0.trophyType");
            }
            if t.addon.is_some() && sets.is_empty() {
                warnings.push(format!("{}: found no season data for it in {}; only its track row gets {}", t.code,
                    t.addon_name.as_deref().unwrap_or("its addon"), x.id));
            }
            lines.push(format!("{} -> {} ({})", t.code, x.id, x.addon_name));
        }
        let ops: Vec<Value> = copies.into_iter().chain(adds).chain(replaces).chain(db).collect();
        lines.insert(0, format!("{} trophy types in the podium scene, {} tracks paired", kept.len(), lines.len()));
        Ok(json!({ "ops": ops, "lines": lines, "warnings": warnings }))
    }

    // ---- the page

    fn preview(&self, x: &Trophy) -> Option<String> {
        let src = x.preview.as_ref()?;
        let m = std::fs::metadata(src).ok()?;
        let ext = src.extension()?.to_string_lossy().to_lowercase();
        let name = format!("{}-{}-{}.{ext}", x.id, m.len(), m.modified().ok()?.duration_since(std::time::UNIX_EPOCH).ok()?.as_secs());
        let dst = self.cache_dir.join("previews").join(name);
        if !dst.is_file() {
            std::fs::create_dir_all(dst.parent()?).ok()?;
            std::fs::copy(src, &dst).ok()?;
        }
        Some(dst.to_string_lossy().into_owned())
    }

    fn view(&self, host: &mut Host) -> Result<Value, String> {
        let w = self.world(host)?;
        let mut notices: Vec<Value> = w.warnings.iter().map(|m| json!({ "text": m, "tone": "warn" })).collect();
        if !w.tracks_known {
            notices.push(json!({ "text": "Press Play once to list this profile's modded tracks here.", "tone": "info" }));
        }
        if self.vanilla {
            notices.push(json!({ "text": "The vanilla profile takes no addons, so it keeps the game's trophies.", "tone": "info" }));
        }
        let mut options = vec![json!({ "value": "stock", "label": "Game's own trophy" }), json!({ "heading": "Trophies" })];
        for x in &w.trophies {
            let mut o = json!({ "value": x.id, "label": format!("{} ({})", x.name, x.addon_name) });
            if let Some(p) = self.preview(x) {
                o["image"] = json!(p);
            }
            options.push(o);
        }
        let mut paired = 0;
        let mut rows = Vec::new();
        for t in &w.tracks {
            let (x, picked) = w.paired(t);
            paired += x.is_some() as usize;
            let auto = w.default_for(t);
            let mut opts = vec![json!({ "value": "auto", "label": format!("Automatic: {}", auto.map_or("game's own trophy".to_owned(), |a| a.name.clone())) })];
            opts.extend(options.iter().cloned());
            let value = if picked { w.picks[&t.code].as_str().unwrap_or("auto").to_owned() } else { "auto".to_owned() };
            let tag = match x {
                Some(x) => json!({ "text": x.name, "tone": "accent" }),
                None => json!({ "text": "Game's trophy", "tone": "muted" }),
            };
            rows.push(json!({
                "id": t.code,
                "title": t.name,
                "subtitle": t.addon_name.clone().unwrap_or_else(|| "Base game".to_owned()),
                "art": { "monogram": t.name.chars().next().unwrap_or('?').to_uppercase().to_string() },
                "tags": [tag],
                "detail": [
                    { "form": { "id": format!("pick:{}", t.code), "live": true, "disabled": self.vanilla, "fields": [
                        { "select": { "id": "trophy", "label": "Podium trophy", "value": value, "search": true, "options": opts } } ] } },
                    { "facts": [ { "label": "Track code", "value": t.code } ] }
                ]
            }));
        }
        Ok(json!({
            "subtitle": format!("{} trophies, {paired} tracks paired", w.trophies.len()),
            "notices": notices,
            "body": { "list": { "rows": rows, "reorder": false, "search": { "placeholder": "Search tracks" } } },
            "footer": "A track addon can carry its own trophy in a podium-trophies folder; it is picked for that track automatically. Changes apply at the next Play."
        }))
    }

    fn event(&self, host: &mut Host, event: &Value) -> Result<Value, String> {
        let Some(sub) = event.get("submit") else { return Ok(json!("none")) };
        let Some(code) = sub["form"].as_str().and_then(|f| f.strip_prefix("pick:")) else { return Ok(json!("none")) };
        let pick = sub["values"]["trophy"].as_str().unwrap_or("auto");
        let state = host.call("get_state", Value::Null)?;
        let mut picks: BTreeMap<String, Value> = serde_json::from_value(state["picks"].clone()).unwrap_or_default();
        if pick == "auto" {
            picks.remove(code);
        } else {
            picks.insert(code.to_owned(), json!(pick));
        }
        host.call("set_state", json!({ "picks": picks }))?;
        Ok(json!("reload"))
    }
}

fn main() {
    let mut host = Host { lines: std::io::stdin().lock().lines(), out: std::io::stdout().lock(), next: 0 };
    let mut app = App {
        addon: "sedz.podium-trophies".into(),
        own_dir: std::env::current_dir().unwrap_or_default(),
        cache_dir: PathBuf::new(),
        host_calls: Vec::new(),
        vanilla: false,
        facts: serde_json::from_str(FACTS).expect("tools/track_trophies.json"),
    };
    while let Some(Ok(line)) = host.lines.next() {
        let Ok(msg) = serde_json::from_str::<Value>(&line) else { continue };
        let p = &msg["params"];
        if msg.get("id").is_none() {
            if msg["method"] == "profile" {
                app.vanilla = p["vanilla"] == json!(true);
            }
            continue;
        }
        let result = match msg["method"].as_str().unwrap_or_default() {
            "hello" => {
                app.addon = p["addon"].as_str().unwrap_or(&app.addon).to_owned();
                app.cache_dir = PathBuf::from(p["cache_dir"].as_str().unwrap_or_default());
                app.host_calls = serde_json::from_value(p["host_calls"].clone()).unwrap_or_default();
                app.vanilla = p["profile"]["vanilla"] == json!(true);
                std::fs::create_dir_all(&app.cache_dir).ok();
                Ok(json!({ "api": API }))
            }
            "view" if p["section"] == SECTION => app.view(&mut host),
            "event" => app.event(&mut host, &p["event"]),
            "build" => app.build(&mut host, p),
            "summary" => Ok(Value::Null),
            "tick" => Ok(Value::Null),
            m => Err(format!("Podium Trophies doesn't handle {m}")),
        };
        let reply = match result {
            Ok(r) => json!({ "id": msg["id"], "result": r }),
            Err(e) => json!({ "id": msg["id"], "error": e }),
        };
        host.send(&reply);
    }
}
