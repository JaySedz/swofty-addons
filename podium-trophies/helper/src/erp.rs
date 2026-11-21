//! Reading resources from an EGO ERPK archive (v3/v4): the index, and a resource's fragments, decompressed.
use std::io::Read;

pub struct Fragment {
    offset: u64,
    size: u64,
    comp: u8,
    packed: u64,
}

pub struct Entry {
    pub name: String,
    #[allow(dead_code)]
    pub rtype: String,
    frags: Vec<Fragment>,
}

pub struct Archive {
    data: Vec<u8>,
    pub entries: Vec<Entry>,
}

fn u16_at(b: &[u8], o: usize) -> Result<u16, String> {
    b.get(o..o + 2).map(|s| u16::from_le_bytes([s[0], s[1]])).ok_or_else(|| "truncated archive".into())
}

fn u32_at(b: &[u8], o: usize) -> Result<u32, String> {
    b.get(o..o + 4).map(|s| u32::from_le_bytes(s.try_into().unwrap())).ok_or_else(|| "truncated archive".into())
}

fn u64_at(b: &[u8], o: usize) -> Result<u64, String> {
    b.get(o..o + 8).map(|s| u64::from_le_bytes(s.try_into().unwrap())).ok_or_else(|| "truncated archive".into())
}

fn cstr(b: &[u8]) -> String {
    let end = b.iter().position(|&c| c == 0).unwrap_or(b.len());
    b[..end].iter().map(|&c| c as char).collect()
}

impl Archive {
    pub fn open(path: &std::path::Path) -> Result<Archive, String> {
        let data = std::fs::read(path).map_err(|e| format!("{}: {e}", path.display()))?;
        if data.get(..4) != Some(b"ERPK") {
            return Err(format!("{} is not an ERPK archive", path.display()));
        }
        let ver = u32_at(&data, 4)?;
        let data_off = u64_at(&data, 0x20)?;
        let n = u32_at(&data, 0x30)? as usize;
        let mut p = 0x38usize;
        let mut entries = Vec::with_capacity(n);
        for _ in 0..n {
            let elen = u32_at(&data, p)? as usize;
            let start = p + 4;
            let idlen = u16_at(&data, start)? as usize;
            let name = cstr(data.get(start + 2..start + 2 + idlen).ok_or("truncated archive")?);
            let mut q = start + 2 + idlen;
            let rtype = cstr(data.get(q..q + 16).ok_or("truncated archive")?);
            q += 20;
            let nfrag = *data.get(q).ok_or("truncated archive")? as usize;
            q += 1;
            let mut frags = Vec::with_capacity(nfrag);
            for _ in 0..nfrag {
                q += 4;
                let offset = u64_at(&data, q)? + data_off;
                let size = u64_at(&data, q + 8)?;
                q += 20;
                let (mut comp, mut packed) = (0u8, size);
                if ver > 2 {
                    comp = *data.get(q).ok_or("truncated archive")?;
                    packed = u64_at(&data, q + 1)?;
                    q += 9;
                }
                frags.push(Fragment { offset, size, comp, packed });
            }
            p = start + elen;
            entries.push(Entry { name, rtype, frags });
        }
        Ok(Archive { data, entries })
    }

    pub fn fragments(&self, name: &str) -> Result<Vec<Vec<u8>>, String> {
        let e = self.entries.iter().find(|e| e.name == name).ok_or_else(|| format!("no resource {name}"))?;
        e.frags.iter().map(|f| self.fragment(f)).collect()
    }

    fn fragment(&self, f: &Fragment) -> Result<Vec<u8>, String> {
        let raw = self
            .data
            .get(f.offset as usize..(f.offset + f.packed) as usize)
            .ok_or("a fragment runs past the end of the archive")?;
        let mut out = Vec::with_capacity(f.size as usize);
        match f.comp {
            1 => {
                flate2::read::ZlibDecoder::new(raw).read_to_end(&mut out).map_err(|e| format!("zlib: {e}"))?;
            }
            17 => {
                let mut d = ruzstd::decoding::StreamingDecoder::new(raw).map_err(|e| format!("zstd: {e}"))?;
                d.read_to_end(&mut out).map_err(|e| format!("zstd: {e}"))?;
            }
            _ => out.extend_from_slice(raw),
        }
        if out.len() as u64 != f.size {
            return Err(format!("a fragment unpacked to {} bytes, not {}", out.len(), f.size));
        }
        Ok(out)
    }
}
