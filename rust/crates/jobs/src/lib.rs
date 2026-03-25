use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum ExportStatus {
    Queued,
    Running,
    Paused,
    Failed,
    Completed,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct ExportJob {
    pub job_id: String,
    pub asset_ids: Vec<String>,
    pub preset_name: String,
    pub destination: String,
    pub status: ExportStatus,
}
