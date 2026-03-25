use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct CaptureMetadata {
    pub capture_year: u16,
    pub capture_month: u8,
    pub capture_day: u8,
    pub camera_make: String,
    pub camera_model: String,
    pub lens_model: String,
    pub iso: u32,
    pub shutter_speed: String,
    pub aperture: f32,
}
