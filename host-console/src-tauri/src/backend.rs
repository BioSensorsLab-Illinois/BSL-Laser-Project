use crate::firmware::{
    flash_segments_for_path, home_python_candidates, inspect_firmware_file, FirmwareInspection,
};
use anyhow::{anyhow, Context, Result};
use futures_util::{SinkExt, StreamExt};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::fs;
use std::io::{Read, Write};
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::sync::{
    atomic::{AtomicBool, Ordering},
    mpsc, Arc, Mutex,
};
use std::thread;
use std::time::Duration;
use tauri::{AppHandle, Emitter, Runtime, State};
use tokio::sync::mpsc as tokio_mpsc;
use tokio_tungstenite::connect_async;
use tokio_tungstenite::tungstenite::Message;

const BRIDGE_EVENT: &str = "bsl://bridge";

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PortInfo {
    pub name: String,
    pub label: String,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CommandEnvelope {
    pub id: u32,
    pub cmd: String,
    #[serde(default)]
    pub args: Value,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FlashRequest {
    pub port: String,
    pub path: String,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FilePayloadRequest {
    pub path: String,
    pub payload: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(tag = "kind", rename_all = "camelCase")]
pub enum BridgeMessage {
    Transport {
        channel: String,
        status: String,
        detail: String,
    },
    ProtocolLine {
        channel: String,
        line: String,
    },
    FlashProgress {
        phase: String,
        percent: f32,
        detail: String,
    },
}

struct SerialSession {
    tx: mpsc::Sender<SerialControl>,
    stop: Arc<AtomicBool>,
    join: Option<thread::JoinHandle<()>>,
}

enum SerialControl {
    Write(String),
    Stop,
}

struct WirelessSession {
    tx: tokio_mpsc::UnboundedSender<Message>,
    stop: tokio_mpsc::UnboundedSender<()>,
}

#[derive(Default)]
pub struct AppState {
    serial: Mutex<Option<SerialSession>>,
    wireless: Mutex<Option<WirelessSession>>,
}

fn emit<R: Runtime>(app: &AppHandle<R>, message: BridgeMessage) {
    let _ = app.emit(BRIDGE_EVENT, message);
}

fn pump_flash_stream<R: Runtime>(
    app: AppHandle<R>,
    mut stream: Box<dyn Read + Send>,
    is_stderr: bool,
) {
    let mut buffer = String::new();
    let mut bytes = [0_u8; 1024];

    loop {
        match stream.read(&mut bytes) {
            Ok(0) => break,
            Ok(count) => {
                buffer.push_str(&String::from_utf8_lossy(&bytes[..count]));
                while let Some(index) = buffer.find('\n') {
                    let line = buffer[..index].trim().to_string();
                    buffer = buffer[index + 1..].to_string();
                    if line.is_empty() {
                        continue;
                    }

                    let percent = if let Some(percent_index) = line.find('%') {
                        line[..percent_index]
                            .split_whitespace()
                            .last()
                            .and_then(|value| value.parse::<f32>().ok())
                            .unwrap_or(0.0)
                    } else {
                        0.0
                    };

                    emit(
                        &app,
                        BridgeMessage::FlashProgress {
                            phase: if is_stderr { "stderr".to_string() } else { "flash".to_string() },
                            percent,
                            detail: line,
                        },
                    );
                }
            }
            Err(_) => break,
        }
    }
}

fn serial_disconnect_internal<R: Runtime>(app: &AppHandle<R>, state: &AppState) {
    let session = state.serial.lock().ok().and_then(|mut guard| guard.take());

    if let Some(mut session) = session {
        session.stop.store(true, Ordering::Relaxed);
        let _ = session.tx.send(SerialControl::Stop);
        if let Some(join) = session.join.take() {
            let _ = join.join();
        }
        emit(
            app,
            BridgeMessage::Transport {
                channel: "serial".to_string(),
                status: "disconnected".to_string(),
                detail: "Serial link closed.".to_string(),
            },
        );
    }
}

fn wireless_disconnect_internal<R: Runtime>(app: &AppHandle<R>, state: &AppState) {
    let session = state.wireless.lock().ok().and_then(|mut guard| guard.take());

    if let Some(session) = session {
        let _ = session.stop.send(());
        let _ = session.tx.send(Message::Close(None));
        emit(
            app,
            BridgeMessage::Transport {
                channel: "wireless".to_string(),
                status: "disconnected".to_string(),
                detail: "Wireless link closed.".to_string(),
            },
        );
    }
}

#[tauri::command]
pub fn serial_list_ports() -> Result<Vec<PortInfo>, String> {
    let ports = serialport::available_ports().map_err(|err| err.to_string())?;
    Ok(ports
        .into_iter()
        .map(|port| PortInfo {
            label: port.port_name.clone(),
            name: port.port_name,
        })
        .collect())
}

#[tauri::command]
pub fn serial_connect<R: Runtime>(
    app: AppHandle<R>,
    state: State<'_, AppState>,
    port: String,
    baud_rate: Option<u32>,
) -> Result<(), String> {
    serial_disconnect_internal(&app, &state);

    emit(
        &app,
        BridgeMessage::Transport {
            channel: "serial".to_string(),
            status: "connecting".to_string(),
            detail: format!("Opening {port}…"),
        },
    );

    let mut device = serialport::new(&port, baud_rate.unwrap_or(115200))
        .timeout(Duration::from_millis(100))
        .open()
        .map_err(|err| err.to_string())?;
    let app_handle = app.clone();
    let stop = Arc::new(AtomicBool::new(false));
    let stop_flag = stop.clone();
    let (tx, rx) = mpsc::channel::<SerialControl>();

    let join = thread::spawn(move || {
        emit(
            &app_handle,
            BridgeMessage::Transport {
                channel: "serial".to_string(),
                status: "connected".to_string(),
                detail: format!("Serial link active on {port}."),
            },
        );

        let mut staged = String::new();
        let mut scratch = [0_u8; 1024];

        while !stop_flag.load(Ordering::Relaxed) {
            while let Ok(control) = rx.try_recv() {
                match control {
                    SerialControl::Write(line) => {
                        let _ = device.write_all(line.as_bytes());
                    }
                    SerialControl::Stop => {
                        stop_flag.store(true, Ordering::Relaxed);
                    }
                }
            }

            match device.read(&mut scratch) {
                Ok(count) if count > 0 => {
                    staged.push_str(&String::from_utf8_lossy(&scratch[..count]));
                    while let Some(index) = staged.find('\n') {
                        let line = staged[..index].trim().to_string();
                        staged = staged[index + 1..].to_string();
                        if !line.is_empty() {
                            emit(
                                &app_handle,
                                BridgeMessage::ProtocolLine {
                                    channel: "serial".to_string(),
                                    line,
                                },
                            );
                        }
                    }
                }
                Ok(_) => {}
                Err(err) if err.kind() == std::io::ErrorKind::TimedOut => {}
                Err(err) => {
                    emit(
                        &app_handle,
                        BridgeMessage::Transport {
                            channel: "serial".to_string(),
                            status: "error".to_string(),
                            detail: format!("Serial read failed: {err}"),
                        },
                    );
                    break;
                }
            }
        }
    });

    *state.serial.lock().map_err(|_| "Serial state lock failed".to_string())? = Some(SerialSession {
        tx,
        stop,
        join: Some(join),
    });

    Ok(())
}

#[tauri::command]
pub fn serial_disconnect<R: Runtime>(
    app: AppHandle<R>,
    state: State<'_, AppState>,
) -> Result<(), String> {
    serial_disconnect_internal(&app, &state);
    Ok(())
}

#[tauri::command]
pub async fn wireless_connect<R: Runtime>(
    app: AppHandle<R>,
    state: State<'_, AppState>,
    url: String,
) -> Result<(), String> {
    wireless_disconnect_internal(&app, &state);

    emit(
        &app,
        BridgeMessage::Transport {
            channel: "wireless".to_string(),
            status: "connecting".to_string(),
            detail: format!("Connecting to {url}…"),
        },
    );

    let app_handle = app.clone();
    let (write_tx, mut write_rx) = tokio_mpsc::unbounded_channel::<Message>();
    let (stop_tx, mut stop_rx) = tokio_mpsc::unbounded_channel::<()>();

    tauri::async_runtime::spawn(async move {
        let result = async {
            let (stream, _) = connect_async(url.as_str()).await.context("Wireless WebSocket connect failed")?;
            let (mut writer, mut reader) = stream.split();

            emit(
                &app_handle,
                BridgeMessage::Transport {
                    channel: "wireless".to_string(),
                    status: "connected".to_string(),
                    detail: "Wireless link active.".to_string(),
                },
            );

            loop {
                tokio::select! {
                    _ = stop_rx.recv() => {
                        let _ = writer.send(Message::Close(None)).await;
                        break;
                    }
                    outbound = write_rx.recv() => {
                        if let Some(message) = outbound {
                            writer.send(message).await?;
                        } else {
                            break;
                        }
                    }
                    inbound = reader.next() => {
                        match inbound {
                            Some(Ok(Message::Text(text))) => {
                                for line in text.lines() {
                                    let trimmed = line.trim();
                                    if !trimmed.is_empty() {
                                        emit(
                                            &app_handle,
                                            BridgeMessage::ProtocolLine {
                                                channel: "wireless".to_string(),
                                                line: trimmed.to_string(),
                                            },
                                        );
                                    }
                                }
                            }
                            Some(Ok(Message::Binary(_))) => {}
                            Some(Ok(Message::Close(_))) => break,
                            Some(Ok(_)) => {}
                            Some(Err(err)) => return Err(anyhow!(err)),
                            None => break,
                        }
                    }
                }
            }

            Ok::<(), anyhow::Error>(())
        }.await;

        if let Err(err) = result {
            emit(
                &app_handle,
                BridgeMessage::Transport {
                    channel: "wireless".to_string(),
                    status: "error".to_string(),
                    detail: err.to_string(),
                },
            );
        }
    });

    *state
        .wireless
        .lock()
        .map_err(|_| "Wireless state lock failed".to_string())? = Some(WirelessSession {
        tx: write_tx,
        stop: stop_tx,
    });

    Ok(())
}

#[tauri::command]
pub fn wireless_disconnect<R: Runtime>(
    app: AppHandle<R>,
    state: State<'_, AppState>,
) -> Result<(), String> {
    wireless_disconnect_internal(&app, &state);
    Ok(())
}

#[tauri::command]
pub fn transport_send_command(
    state: State<'_, AppState>,
    channel: String,
    envelope: CommandEnvelope,
) -> Result<(), String> {
    let line = format!(
        "{}\n",
        serde_json::json!({
            "type": "cmd",
            "id": envelope.id,
            "cmd": envelope.cmd,
            "args": envelope.args,
        })
    );

    match channel.as_str() {
        "serial" => {
            let guard = state.serial.lock().map_err(|_| "Serial state lock failed".to_string())?;
            let session = guard.as_ref().ok_or_else(|| "Serial link is not connected.".to_string())?;
            session
                .tx
                .send(SerialControl::Write(line))
                .map_err(|_| "Serial writer is unavailable.".to_string())
        }
        "wireless" => {
            let guard = state.wireless.lock().map_err(|_| "Wireless state lock failed".to_string())?;
            let session = guard.as_ref().ok_or_else(|| "Wireless link is not connected.".to_string())?;
            session
                .tx
                .send(Message::Text(line.into()))
                .map_err(|_| "Wireless writer is unavailable.".to_string())
        }
        _ => Err("Unknown transport channel.".to_string()),
    }
}

#[tauri::command]
pub fn inspect_firmware(path: String) -> Result<FirmwareInspection, String> {
    inspect_firmware_file(&path).map_err(|err| err.to_string())
}

fn choose_python() -> Option<PathBuf> {
    for candidate in home_python_candidates() {
        if candidate.as_os_str() == "python3" || candidate.exists() {
            return Some(candidate);
        }
    }
    None
}

#[tauri::command]
pub fn flash_firmware<R: Runtime>(
    app: AppHandle<R>,
    request: FlashRequest,
) -> Result<(), String> {
    let python = choose_python().ok_or_else(|| "No Python interpreter with esptool support was found.".to_string())?;
    let firmware_path = request.path.clone();
    let serial_port = request.port.clone();
    let app_handle = app.clone();

    thread::spawn(move || {
        let run = || -> Result<()> {
            let flash_segments = flash_segments_for_path(&firmware_path)?;
            if flash_segments.is_empty() {
                return Err(anyhow!("No flashable segment files were found in {}", firmware_path));
            }

            emit(
                &app_handle,
                BridgeMessage::FlashProgress {
                    phase: "prepare".to_string(),
                    percent: 0.0,
                    detail: format!("Flashing {} to {}.", firmware_path, serial_port),
                },
            );

            let mut command = Command::new(&python);
            command
                .arg("-m")
                .arg("esptool")
                .arg("--chip")
                .arg("esp32s3")
                .arg("-p")
                .arg(&serial_port)
                .arg("-b")
                .arg("460800")
                .arg("--before")
                .arg("default-reset")
                .arg("--after")
                .arg("hard-reset")
                .arg("write-flash")
                .arg("--flash-mode")
                .arg("dio")
                .arg("--flash-size")
                .arg("2MB")
                .arg("--flash-freq")
                .arg("80m")
                .stdout(Stdio::piped())
                .stderr(Stdio::piped());

            for segment in flash_segments {
                command.arg(segment.offset_hex).arg(segment.path);
            }

            let mut child = command
                .spawn()
                .with_context(|| "Unable to start esptool flash process")?;

            let stdout = child.stdout.take().ok_or_else(|| anyhow!("Missing esptool stdout"))?;
            let stderr = child.stderr.take().ok_or_else(|| anyhow!("Missing esptool stderr"))?;
            let stdout_handle = {
                let app = app_handle.clone();
                thread::spawn(move || pump_flash_stream(app, Box::new(stdout), false))
            };
            let stderr_handle = {
                let app = app_handle.clone();
                thread::spawn(move || pump_flash_stream(app, Box::new(stderr), true))
            };
            let status = child.wait()?;
            let _ = stdout_handle.join();
            let _ = stderr_handle.join();

            if !status.success() {
                return Err(anyhow!("esptool exited with {status}"));
            }

            emit(
                &app_handle,
                BridgeMessage::FlashProgress {
                    phase: "done".to_string(),
                    percent: 100.0,
                    detail: "Native firmware flash completed.".to_string(),
                },
            );

            Ok(())
        };

        if let Err(err) = run() {
            emit(
                &app_handle,
                BridgeMessage::FlashProgress {
                    phase: "error".to_string(),
                    percent: 0.0,
                    detail: err.to_string(),
                },
            );
        }
    });

    Ok(())
}

#[tauri::command]
pub fn export_session(path: String, payload: String) -> Result<(), String> {
    fs::write(&path, payload).map_err(|err| err.to_string())
}

#[tauri::command]
pub fn write_session_autosave(request: FilePayloadRequest) -> Result<(), String> {
    fs::write(&request.path, request.payload).map_err(|err| err.to_string())
}

#[tauri::command]
pub fn read_session_file(path: String) -> Result<String, String> {
    fs::read_to_string(path).map_err(|err| err.to_string())
}
