//! Experimental AirPlay 2 URL playback. The receiver fetches and decodes the file.
use std::io::{self, BufRead};
use std::net::{TcpStream, ToSocketAddrs, UdpSocket};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{mpsc, Arc};
use std::time::{Duration, Instant};

use anyhow::{bail, Context, Result};
use plist::Value;

use crate::{mirror, pairing, rtsp::Session};

fn plist_body(value: &Value) -> Result<Vec<u8>> {
    let mut body = Vec::new();
    plist::to_writer_binary(&mut body, value).context("serialize AirPlay plist")?;
    Ok(body)
}

fn play_body(url: &str, position: f64, uuid: &str) -> Result<Vec<u8>> {
    let mut fields = plist::Dictionary::new();
    fields.insert("Content-Location".into(), Value::String(url.into()));
    fields.insert("Start-Position-Seconds".into(), Value::Real(position));
    fields.insert("uuid".into(), Value::String(uuid.into()));
    fields.insert("streamType".into(), Value::Integer(1i64.into()));
    fields.insert("mediaType".into(), Value::String("file".into()));
    fields.insert("mightSupportStorePastisKeyRequests".into(), Value::Boolean(true));
    fields.insert("playbackRestrictions".into(), Value::Integer(0i64.into()));
    fields.insert("secureConnectionMs".into(), Value::Integer(22i64.into()));
    fields.insert("volume".into(), Value::Real(1.0));
    fields.insert("infoMs".into(), Value::Integer(122i64.into()));
    fields.insert("connectMs".into(), Value::Integer(18i64.into()));
    fields.insert("authMs".into(), Value::Integer(0i64.into()));
    fields.insert("bonjourMs".into(), Value::Integer(0i64.into()));
    fields.insert("referenceRestrictions".into(), Value::Integer(3i64.into()));
    fields.insert("SenderMACAddress".into(), Value::String("AA:BB:CC:DD:EE:FF".into()));
    fields.insert("model".into(), Value::String("iPhone14,3".into()));
    fields.insert("postAuthMs".into(), Value::Integer(0i64.into()));
    fields.insert("clientBundleID".into(), Value::String("dev.beamer.qt".into()));
    fields.insert("clientProcName".into(), Value::String("dev.beamer.qt".into()));
    fields.insert("osBuildVersion".into(), Value::String("20G1116".into()));
    fields.insert("rate".into(), Value::Real(1.0));
    plist_body(&Value::Dictionary(fields))
}

pub fn run(mut session: Session, url: &str, start: f64) -> Result<()> {
    if !url.starts_with("http://") && !url.starts_with("https://") {
        bail!("direct media requires an HTTP(S) URL reachable by the receiver");
    }
    let (host, _) = session.transport.peer_addr()?;
    let timing = UdpSocket::bind("0.0.0.0:0").context("bind media timing port")?;
    let timing_port = timing.local_addr()?.port();
    timing.set_read_timeout(Some(Duration::from_millis(200)))?;
    let stop_timing = Arc::new(AtomicBool::new(false));
    let timing_stop = stop_timing.clone();
    let timing_thread = std::thread::spawn(move || mirror::ntp_timing_responder(timing, timing_stop));

    let result = run_session(&mut session, url, start, &host, timing_port, stop_timing.clone());
    stop_timing.store(true, Ordering::Relaxed);
    let _ = timing_thread.join();
    result
}

fn run_session(session: &mut Session, url: &str, start: f64, host: &str, timing_port: u16, stop: Arc<AtomicBool>) -> Result<()> {
    // AirPlay 2 ties SETUP and RECORD to one session-specific RTSP URI.
    let session_uri = format!("rtsp://{}/{}", session.transport.local_ip()?, rand::random::<u32>());
    let mut setup = plist::Dictionary::new();
    let sender_id = "AA:BB:CC:DD:EE:FF";
    setup.insert("deviceID".into(), Value::String(sender_id.into()));
    setup.insert("macAddress".into(), Value::String(sender_id.into()));
    setup.insert("sessionUUID".into(), Value::String(pairing::generate_uuid()));
    setup.insert("timingPort".into(), Value::Integer((timing_port as i64).into()));
    setup.insert("timingProtocol".into(), Value::String("NTP".into()));
    setup.insert("isMultiSelectAirPlay".into(), Value::Boolean(true));
    setup.insert("groupContainsGroupLeader".into(), Value::Boolean(false));
    setup.insert("model".into(), Value::String("iPhone14,3".into()));
    setup.insert("name".into(), Value::String("Beamer Qt".into()));
    setup.insert("osBuildVersion".into(), Value::String("20F66".into()));
    setup.insert("osName".into(), Value::String("iPhone OS".into()));
    setup.insert("osVersion".into(), Value::String("16.5".into()));
    setup.insert("sourceVersion".into(), Value::String("690.7.1".into()));
    setup.insert("senderSupportsRelay".into(), Value::Boolean(false));
    setup.insert("statsCollectionEnabled".into(), Value::Boolean(false));
    let setup_response = session.transport.request(
        "SETUP", &session_uri, "application/x-apple-binary-plist", &plist_body(&Value::Dictionary(setup))?, &[],
    ).context("direct media base SETUP")?;
    let setup_info = Value::from_reader(std::io::Cursor::new(&setup_response.body))
        .context("parse direct media SETUP response")?;
    let event_port = setup_info.as_dictionary().and_then(|fields| fields.get("eventPort"))
        .and_then(Value::as_unsigned_integer).unwrap_or(0) as u16;
    if event_port != 0 {
        let address = format!("{host}:{event_port}");
        let socket = address.to_socket_addrs()?.next().context("resolve event channel")?;
        let channel = TcpStream::connect_timeout(&socket, Duration::from_secs(3))
            .context("connect direct media event channel")?;
        eprintln!("direct media event channel connected at {address}");
        let secret = session.pair_keys.shared_secret.clone();
        std::thread::spawn(move || mirror::decrypt_event_channel(channel, secret, stop));
    }
    session.transport.request("RECORD", &session_uri, "", &[], &[])
        .context("direct media RECORD")?;

    let uuid = pairing::generate_uuid();
    session.transport.request_http(
        "POST", "/play", "application/x-apple-binary-plist",
        &play_body(url, start, &uuid)?,
        &[("X-Apple-ProtocolVersion", "1"), ("X-Apple-Stream-ID", "1"),
          ("X-Apple-Session-ID", session.session_id.as_str())],
    ).context("direct media /play")?;
    set_property(session, "isInterestedInDateRange", Value::Boolean(true))?;
    set_property(session, "actionAtItemEnd", Value::Integer(0i64.into()))?;
    session.transport.request("POST", "/rate?value=1.000000", "", &[], &[])
        .context("start direct media playback")?;
    let mut end_time = plist::Dictionary::new();
    end_time.insert("flags".into(), Value::Integer(0i64.into()));
    end_time.insert("value".into(), Value::Integer(0i64.into()));
    end_time.insert("epoch".into(), Value::Integer(0i64.into()));
    end_time.insert("timescale".into(), Value::Integer(0i64.into()));
    set_property(session, "forwardEndTime", Value::Dictionary(end_time.clone()))?;
    set_property(session, "reverseEndTime", Value::Dictionary(end_time))?;
    eprintln!("direct media requested; waiting for receiver playback status");

    let (commands, incoming) = mpsc::channel::<String>();
    std::thread::spawn(move || {
        for line in io::stdin().lock().lines().flatten() {
            if commands.send(line).is_err() { break; }
        }
    });
    let started_at = Instant::now();
    let mut playing = false;
    let mut last_feedback = Instant::now() - Duration::from_secs(2);
    loop {
        while let Ok(command) = incoming.try_recv() {
            let mut parts = command.split_whitespace();
            match parts.next() {
                Some("stop") => {
                    let _ = session.transport.request("POST", "/stop", "", &[], &[]);
                    return Ok(());
                }
                Some("pause") => { session.transport.request("POST", "/rate?value=0.000000", "", &[], &[])?; }
                Some("resume") => { session.transport.request("POST", "/rate?value=1.000000", "", &[], &[])?; }
                Some("seek") => {
                    let seconds: f64 = parts.next().context("missing seek time")?.parse()?;
                    let path = format!("/scrub?position={seconds:.6}");
                    session.transport.request("POST", &path, "", &[], &[])?;
                }
                _ => {}
            }
        }
        if last_feedback.elapsed() >= Duration::from_secs(2) {
            if let Err(error) = session.transport.request("POST", "/feedback", "", &[], &[]) {
                crate::dlog!("[media] feedback failed: {error:#}");
            }
            last_feedback = Instant::now();
        }
        let response = match session.transport.request_http("GET", "/playback-info", "", &[], &[]) {
            Ok(response) => response,
            Err(error) if !playing && started_at.elapsed() < Duration::from_secs(10) => {
                crate::dlog!("[media] playback status pending: {error:#}");
                std::thread::sleep(Duration::from_secs(1));
                continue;
            }
            Err(error) => return Err(error).context("read direct media playback status"),
        };
        if !response.body.is_empty() {
            let status = Value::from_reader(std::io::Cursor::new(&response.body))
                .context("parse playback-info")?;
            if let Some(fields) = status.as_dictionary() {
                if let Some(error) = fields.get("error") { bail!("receiver playback error: {error:?}"); }
                if fields.contains_key("duration") {
                    if !playing { eprintln!("direct media playback started"); }
                    playing = true;
                } else if playing {
                    eprintln!("direct media playback ended");
                    return Ok(());
                }
            }
        }
        if !playing && started_at.elapsed() > Duration::from_secs(10) {
            bail!("receiver did not report playback within 10 seconds; check media URL access and codec support");
        }
        std::thread::sleep(Duration::from_secs(1));
    }
}

fn set_property(session: &mut Session, name: &str, value: Value) -> Result<()> {
    let mut body = plist::Dictionary::new();
    body.insert("value".into(), value);
    session.transport.request(
        "PUT", &format!("/setProperty?{name}"), "application/x-apple-binary-plist",
        &plist_body(&Value::Dictionary(body))?, &[],
    ).with_context(|| format!("set direct media property {name}"))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn play_request_carries_url_and_position() {
        let body = play_body("http://192.0.2.1/video.mp4", 12.5, "test-id").unwrap();
        let value = Value::from_reader(std::io::Cursor::new(body)).unwrap();
        let fields = value.as_dictionary().unwrap();
        assert_eq!(fields.get("Content-Location").and_then(Value::as_string), Some("http://192.0.2.1/video.mp4"));
        assert_eq!(fields.get("Start-Position-Seconds").and_then(Value::as_real), Some(12.5));
        assert_eq!(fields.get("mediaType").and_then(Value::as_string), Some("file"));
    }
}
