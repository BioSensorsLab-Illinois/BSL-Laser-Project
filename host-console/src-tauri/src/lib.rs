mod backend;
mod firmware;

use backend::{
    export_session, flash_firmware, inspect_firmware, read_session_file, serial_connect,
    serial_disconnect, serial_list_ports, transport_send_command, wireless_connect,
    wireless_disconnect, write_session_autosave, AppState,
};

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(AppState::default())
        .invoke_handler(tauri::generate_handler![
            serial_list_ports,
            serial_connect,
            serial_disconnect,
            wireless_connect,
            wireless_disconnect,
            transport_send_command,
            inspect_firmware,
            flash_firmware,
            export_session,
            write_session_autosave,
            read_session_file
        ])
        .run(tauri::generate_context!())
        .expect("error while running bsl console v2");
}
