use cast_sender::{AppId, MediaController, Receiver, Payload};
use cast_sender::namespace::media::{Media, MediaInformationBuilder, StreamType, GetStatusRequestData, SeekRequestData};

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let Some(command) = args.get(1).map(String::as_str) else { eprintln!("usage: beamer-chromecast cast|pause|stop|probe <host> [url]"); std::process::exit(2); };
    let Some(host) = args.get(2) else { eprintln!("missing Chromecast host"); std::process::exit(2); };
    let result = smol::block_on(async {
        let receiver = Receiver::new(); receiver.connect(host).await.map_err(|e| e.to_string())?;
        if command == "probe" { let status = receiver.status().await.map_err(|e| e.to_string())?; println!("receiver_status={status:?}"); return Ok::<(), String>(()); }
        let app = receiver.launch_app(AppId::DefaultMediaReceiver).await.map_err(|e| e.to_string())?;
        let status_app = app.clone();
        let media = MediaController::new(app, receiver.clone()).map_err(|e| e.to_string())?;
        match command {
            "seek" => { let seconds: f64 = args.get(3).ok_or("missing seek position")?.parse().map_err(|e| format!("invalid seek position: {e}"))?; let response = receiver.send_request(&status_app, Media::GetStatus(GetStatusRequestData::default())).await.map_err(|e| e.to_string())?; if let Payload::Media(Media::MediaStatus(data)) = response.payload { if let Some(status) = data.status.first() { receiver.send(&status_app, Media::Seek(SeekRequestData { current_time: Some(seconds), media_session_id: Some(status.media_session_id), relative_time: None })).await.map_err(|e| e.to_string())?; } else { return Err("no active media session".into()); } } else { return Err("no media status".into()); } }
            "cast" => { let url = args.get(3).ok_or("missing media URL")?; let info = MediaInformationBuilder::default().content_id(url).stream_type(StreamType::Buffered).content_type("video/mp4").build().map_err(|e| e.to_string())?; media.load(info).await.map_err(|e| e.to_string())?;
                for _ in 0..10 { smol::Timer::after(std::time::Duration::from_millis(500)).await;
                    let response = receiver.send_request(&status_app, Media::GetStatus(GetStatusRequestData::default())).await.map_err(|e| e.to_string())?;
                    if let Payload::Media(Media::MediaStatus(data)) = response.payload { if let Some(status) = data.status.first() { eprintln!("media_state={:?} idle_reason={:?}", status.player_state, status.idle_reason); if format!("{:?}", status.player_state) == "Playing" || format!("{:?}", status.player_state) == "Buffering" { return Ok::<(), String>(()); } if status.idle_reason.is_some() { return Err(format!("receiver rejected media: {:?}", status.idle_reason)); } } }
                }
                return Err("receiver did not enter PLAYING/BUFFERING within 5 seconds".to_string());
            }
            "volume" => { let level: f64 = args.get(3).ok_or("missing volume")?.parse::<f64>().map_err(|e| e.to_string())?; receiver.set_volume(level.clamp(0.0, 1.0), false).await.map_err(|e| e.to_string())?; }
            "pause" => media.pause().await.map_err(|e| e.to_string())?,
            "stop" => media.stop().await.map_err(|e| e.to_string())?,
            _ => return Err("unknown command".to_string()),
        }
        Ok::<(), String>(())
    });
    if let Err(error) = result { eprintln!("{error}"); std::process::exit(1); }
}
