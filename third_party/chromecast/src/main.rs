use cast_sender::{AppId, MediaController, Receiver};
use cast_sender::namespace::media::{MediaInformationBuilder, StreamType};

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let Some(command) = args.get(1).map(String::as_str) else { eprintln!("usage: beamer-chromecast cast|pause|stop <host> [url]"); std::process::exit(2); };
    let Some(host) = args.get(2) else { eprintln!("missing Chromecast host"); std::process::exit(2); };
    let result = smol::block_on(async {
        let receiver = Receiver::new(); receiver.connect(host).await.map_err(|e| e.to_string())?;
        let app = receiver.launch_app(AppId::DefaultMediaReceiver).await.map_err(|e| e.to_string())?;
        let media = MediaController::new(app, receiver.clone()).map_err(|e| e.to_string())?;
        match command {
            "cast" => { let url = args.get(3).ok_or("missing media URL")?; let info = MediaInformationBuilder::default().content_id(url).stream_type(StreamType::Buffered).content_type("video/mp4").build().map_err(|e| e.to_string())?; media.load(info).await.map_err(|e| e.to_string())?; }
            "pause" => media.pause().await.map_err(|e| e.to_string())?,
            "stop" => media.stop().await.map_err(|e| e.to_string())?,
            _ => return Err("unknown command".to_string()),
        }
        Ok::<(), String>(())
    });
    if let Err(error) = result { eprintln!("{error}"); std::process::exit(1); }
}
