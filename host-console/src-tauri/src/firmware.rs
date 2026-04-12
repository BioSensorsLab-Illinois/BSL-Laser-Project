use anyhow::{anyhow, Context, Result};
use serde::Serialize;
use serde_json::Value;
use sha2::{Digest, Sha256};
use std::fs;
use std::path::{Path, PathBuf};

const SIGNATURE_MAGIC: &[u8; 7] = b"BSLFWS1";
const SIGNATURE_SIZE: usize = 232;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FirmwareSignature {
    pub schema_version: u16,
    pub product_name: String,
    pub project_name: String,
    pub board_name: String,
    pub protocol_version: String,
    pub hardware_scope: String,
    pub firmware_version: String,
    pub build_utc: String,
    pub payload_sha256_hex: String,
    pub verified: bool,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FirmwareSegment {
    pub name: String,
    pub file_name: String,
    pub path: String,
    pub offset_hex: String,
    pub bytes: u64,
    pub sha256: String,
    pub role: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FirmwareTransferSupport {
    pub supported: bool,
    pub note: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FirmwareInspection {
    pub path: String,
    pub file_name: String,
    pub package_name: String,
    pub version: String,
    pub board: String,
    pub note: String,
    pub bytes: u64,
    pub sha256: String,
    pub extension: String,
    pub format: String,
    pub raw_binary: bool,
    pub flash_offset_hex: String,
    pub signature: Option<FirmwareSignature>,
    pub segments: Vec<FirmwareSegment>,
    pub transfer: FirmwareTransferSupport,
}

#[derive(Debug, Clone)]
pub struct FlashSegmentSpec {
    pub offset_hex: String,
    pub path: PathBuf,
}

#[derive(Debug, Clone)]
struct PackageBundle {
    inspection: FirmwareInspection,
    flash_segments: Vec<FlashSegmentSpec>,
}

fn read_c_string(bytes: &[u8]) -> String {
    let end = bytes.iter().position(|byte| *byte == 0).unwrap_or(bytes.len());
    String::from_utf8_lossy(&bytes[..end]).trim().to_string()
}

fn sha256_hex(bytes: &[u8]) -> String {
    hex::encode(Sha256::digest(bytes))
}

fn file_name_string(path: &Path) -> Result<String> {
    path.file_name()
        .and_then(|name| name.to_str())
        .map(|name| name.to_string())
        .ok_or_else(|| anyhow!("File name is invalid for {}", path.display()))
}

fn parse_signature(bytes: &[u8]) -> Option<FirmwareSignature> {
    let start = bytes
        .windows(SIGNATURE_MAGIC.len())
        .position(|window| window == SIGNATURE_MAGIC)?;
    let slice = bytes.get(start..start + SIGNATURE_SIZE)?;

    let schema_version = u16::from_le_bytes([slice[8], slice[9]]);
    let struct_size = u16::from_le_bytes([slice[10], slice[11]]) as usize;

    if struct_size > slice.len() || schema_version != 1 {
        return None;
    }

    Some(FirmwareSignature {
        schema_version,
        product_name: read_c_string(&slice[12..36]),
        project_name: read_c_string(&slice[36..68]),
        board_name: read_c_string(&slice[68..84]),
        protocol_version: read_c_string(&slice[84..100]),
        hardware_scope: read_c_string(&slice[100..124]),
        firmware_version: read_c_string(&slice[124..156]),
        build_utc: read_c_string(&slice[156..180]),
        payload_sha256_hex: read_c_string(&slice[180..245.min(slice.len())]),
        verified: true,
    })
}

fn read_manifest_string(root: &Value, key: &str, fallback: &str) -> String {
    root.get(key)
        .and_then(Value::as_str)
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .unwrap_or(fallback)
        .to_string()
}

fn read_offset_hex(raw: &Value) -> String {
    if let Some(value) = raw.as_str() {
        if value.starts_with("0x") || value.starts_with("0X") {
            return value.to_string();
        }
        if let Ok(parsed) = value.parse::<u64>() {
            return format!("0x{parsed:X}");
        }
        return value.to_string();
    }

    if let Some(value) = raw.as_u64() {
        return format!("0x{value:X}");
    }

    "0x10000".to_string()
}

fn normalize_role(raw: Option<&Value>) -> String {
    raw.and_then(Value::as_str)
        .map(|value| value.trim().to_string())
        .filter(|value| !value.is_empty())
        .unwrap_or_else(|| "unknown".to_string())
}

fn inspect_binary_package(path: &Path) -> Result<PackageBundle> {
    let bytes = fs::read(path).with_context(|| format!("Unable to read firmware file at {}", path.display()))?;
    let metadata = fs::metadata(path)
        .with_context(|| format!("Unable to read firmware metadata for {}", path.display()))?;
    let signature = parse_signature(&bytes);
    let sha256 = sha256_hex(&bytes);
    let file_name = file_name_string(path)?;
    let flash_offset_hex = "0x10000".to_string();
    let segment = FirmwareSegment {
        name: "app".to_string(),
        file_name: file_name.clone(),
        path: path.display().to_string(),
        offset_hex: flash_offset_hex.clone(),
        bytes: metadata.len(),
        sha256: sha256.clone(),
        role: "app".to_string(),
    };

    Ok(PackageBundle {
        inspection: FirmwareInspection {
            path: path.display().to_string(),
            file_name: file_name.clone(),
            package_name: signature
                .as_ref()
                .map(|sig| sig.product_name.clone())
                .unwrap_or_else(|| file_name.clone()),
            version: signature
                .as_ref()
                .map(|sig| sig.firmware_version.clone())
                .unwrap_or_else(|| "unversioned".to_string()),
            board: signature
                .as_ref()
                .map(|sig| sig.board_name.clone())
                .unwrap_or_else(|| "unknown".to_string()),
            note: "Raw application image with a single flash segment.".to_string(),
            bytes: metadata.len(),
            sha256,
            extension: path
                .extension()
                .and_then(|value| value.to_str())
                .unwrap_or("")
                .to_lowercase(),
            format: "binary".to_string(),
            raw_binary: true,
            flash_offset_hex: flash_offset_hex.clone(),
            signature,
            segments: vec![segment],
            transfer: FirmwareTransferSupport {
                supported: true,
                note: "Native Tauri flash path is available for this raw app binary.".to_string(),
            },
        },
        flash_segments: vec![FlashSegmentSpec {
            offset_hex: flash_offset_hex,
            path: path.to_path_buf(),
        }],
    })
}

fn inspect_manifest_package(path: &Path) -> Result<PackageBundle> {
    let manifest_text = fs::read_to_string(path)
        .with_context(|| format!("Unable to read manifest file at {}", path.display()))?;
    let manifest_bytes = manifest_text.as_bytes();
    let manifest: Value = serde_json::from_str(&manifest_text)
        .with_context(|| format!("Unable to parse manifest file {}", path.display()))?;
    let manifest_dir = path.parent().unwrap_or_else(|| Path::new("."));
    let segments_value = manifest
        .get("segments")
        .and_then(Value::as_array)
        .ok_or_else(|| anyhow!("Manifest is missing a segments array"))?;

    let mut segments = Vec::new();
    let mut flash_segments = Vec::new();
    let mut total_bytes = 0_u64;
    let mut transfer_supported = true;
    let mut transfer_note = "Native Tauri flash path is available for every listed segment.".to_string();

    for (index, raw_segment) in segments_value.iter().enumerate() {
        let segment = raw_segment
            .as_object()
            .ok_or_else(|| anyhow!("Manifest segment {index} is not an object"))?;
        let relative_path = segment
            .get("path")
            .or_else(|| segment.get("file"))
            .and_then(Value::as_str)
            .ok_or_else(|| anyhow!("Manifest segment {index} is missing a path"))?;
        let resolved_path = manifest_dir.join(relative_path);
        let offset_hex = read_offset_hex(segment.get("offset").unwrap_or(&Value::Null));
        let role = normalize_role(segment.get("role"));
        let name = segment
            .get("name")
            .and_then(Value::as_str)
            .map(|value| value.to_string())
            .filter(|value| !value.is_empty())
            .unwrap_or_else(|| role.clone());
        let file_name = file_name_string(&resolved_path)?;

        let (bytes, sha256) = if resolved_path.exists() {
            let segment_bytes = fs::read(&resolved_path)
                .with_context(|| format!("Unable to read segment file {}", resolved_path.display()))?;
            total_bytes += segment_bytes.len() as u64;
            (segment_bytes.len() as u64, sha256_hex(&segment_bytes))
        } else {
            transfer_supported = false;
            transfer_note = format!(
                "Segment file {} is missing; review is available but native flashing is blocked.",
                resolved_path.display()
            );
            (0, String::new())
        };

        segments.push(FirmwareSegment {
            name,
            file_name,
            path: resolved_path.display().to_string(),
            offset_hex: offset_hex.clone(),
            bytes,
            sha256,
            role,
        });

        flash_segments.push(FlashSegmentSpec {
            offset_hex,
            path: resolved_path,
        });
    }

    let primary_offset = segments
        .iter()
        .find(|segment| segment.role == "app")
        .map(|segment| segment.offset_hex.clone())
        .or_else(|| segments.first().map(|segment| segment.offset_hex.clone()))
        .unwrap_or_else(|| "0x10000".to_string());

    Ok(PackageBundle {
        inspection: FirmwareInspection {
            path: path.display().to_string(),
            file_name: file_name_string(path)?,
            package_name: read_manifest_string(&manifest, "packageName", "Manifest package"),
            version: read_manifest_string(&manifest, "version", "unversioned"),
            board: read_manifest_string(&manifest, "board", "unknown"),
            note: read_manifest_string(
                &manifest,
                "note",
                "Manifest-backed firmware package with explicit flash segments.",
            ),
            bytes: total_bytes.max(manifest_bytes.len() as u64),
            sha256: sha256_hex(manifest_bytes),
            extension: path
                .extension()
                .and_then(|value| value.to_str())
                .unwrap_or("")
                .to_lowercase(),
            format: "manifest".to_string(),
            raw_binary: false,
            flash_offset_hex: primary_offset,
            signature: None,
            segments,
            transfer: FirmwareTransferSupport {
                supported: transfer_supported,
                note: transfer_note,
            },
        },
        flash_segments,
    })
}

fn inspect_package(path: &str) -> Result<PackageBundle> {
    let path_buf = PathBuf::from(path);
    let extension = path_buf
        .extension()
        .and_then(|value| value.to_str())
        .unwrap_or("")
        .to_lowercase();

    if extension == "json" || extension == "manifest" {
        inspect_manifest_package(&path_buf)
    } else {
        inspect_binary_package(&path_buf)
    }
}

pub fn inspect_firmware_file(path: &str) -> Result<FirmwareInspection> {
    inspect_package(path).map(|bundle| bundle.inspection)
}

pub fn flash_segments_for_path(path: &str) -> Result<Vec<FlashSegmentSpec>> {
    inspect_package(path).map(|bundle| {
        bundle
            .flash_segments
            .into_iter()
            .filter(|segment| segment.path.exists())
            .collect()
    })
}

pub fn home_python_candidates() -> Vec<PathBuf> {
    let mut candidates = Vec::new();

    if let Ok(path) = std::env::var("IDF_PYTHON_ENV_PATH") {
        candidates.push(Path::new(&path).join("bin/python"));
    }

    if let Ok(home) = std::env::var("HOME") {
        candidates.push(Path::new(&home).join(".espressif/python_env/idf6.0_py3.14_env/bin/python"));
        candidates.push(Path::new(&home).join(".espressif/python_env/idf6.0_py3.13_env/bin/python"));
        candidates.push(Path::new(&home).join(".espressif/python_env/idf6.0_py3.12_env/bin/python"));
        candidates.push(Path::new(&home).join(".espressif/python_env/idf6.0_py3.11_env/bin/python"));
    }

    candidates.push(PathBuf::from("python3"));
    candidates
}
