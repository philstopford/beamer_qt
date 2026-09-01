//! AirPlay URL media playback.  The receiver fetches the URL and decodes it;
//! no local transcoder is involved.
use anyhow::{bail, Context, Result};
use std::io::BufRead;
use crate::rtsp::Session;

pub fn run(mut session: Session, url: &str, start: f64) -> Result<()> {
    let sid = session.session_id.clone();
    let headers = [("X-Apple-Session-ID", sid.as_str())];
    let body = format!("Content-Location: {url}\nStart-Position: {start:.6}\n");
    let response = session.transport.request("POST", "/play", "text/parameters", body.as_bytes(), &headers)?;
    if response.status / 100 != 2 { bail!("media /play rejected: HTTP {}", response.status); }
    eprintln!("media playback started");
    for line in std::io::stdin().lock().lines().flatten() {
        let mut p = line.split_whitespace();
        match p.next() {
            Some("pause") => rate(&mut session, &headers, 0.0)?,
            Some("resume") => rate(&mut session, &headers, 1.0)?,
            Some("seek") => {
                let seconds: f64 = p.next().context("missing seek position")?.parse().context("invalid seek position")?;
                let value = format!("{seconds:.6}");
                let r = session.transport.request("POST", "/scrub", "text/parameters", value.as_bytes(), &headers)?;
                if r.status / 100 != 2 { bail!("media seek rejected: HTTP {}", r.status); }
            }
            Some("stop") => { let _ = session.transport.request("POST", "/stop", "", &[], &headers); break; }
            Some("volume") => {},
            Some("status") => { let r = session.transport.request("GET", "/playback-info", "", &[], &headers)?; println!("{}", String::from_utf8_lossy(&r.body)); }
            _ => {}
        }
    }
    Ok(())
}

fn rate(session: &mut Session, headers: &[(&str, &str)], value: f64) -> Result<()> {
    let body = format!("value: {value:.6}\n");
    let r = session.transport.request("POST", "/rate", "text/parameters", body.as_bytes(), headers)?;
    if r.status / 100 != 2 { bail!("media rate change rejected: HTTP {}", r.status); }
    Ok(())
}
