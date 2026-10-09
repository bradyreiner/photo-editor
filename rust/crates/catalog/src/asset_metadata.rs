use std::collections::{BTreeMap, HashSet};
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use sqlx::Row;
use crate::{CatalogRuntime, CatalogService, ReviewStateBatchResult};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct AssetMetadataRecord {
    pub asset_id: String,
    pub metadata: Map<String, Value>,
}

pub fn validate_asset_metadata(records: &[AssetMetadataRecord]) -> Result<(), &'static str> {
    if records.is_empty() || records.len() > 256 { return Err("metadata batch must contain 1 to 256 records"); }
    let mut ids = HashSet::new();
    for record in records {
        if record.asset_id.trim().is_empty() || record.asset_id != record.asset_id.trim() || !ids.insert(&record.asset_id) {
            return Err("metadata asset IDs must be nonempty and unique");
        }
        for (key, value) in &record.metadata {
            if key == "keywords" {
                let keywords = value.as_array().ok_or("keywords must be an array")?;
                if keywords.len() > 256 || keywords.iter().any(|v| v.as_str().is_none_or(|s| s.len() > 512)) {
                    return Err("keywords exceed supported limits");
                }
            } else if !["title", "caption", "creator", "copyright", "location", "captureTime"].contains(&key.as_str())
                || value.as_str().is_none_or(|s| s.len() > 16384) {
                return Err("unsupported metadata field or value");
            }
            if key == "captureTime" {
                let value = value.as_str().ok_or("capture time must be a string")?;
                let format = time::format_description::well_known::Iso8601::DEFAULT;
                if !value.is_empty()
                    && time::OffsetDateTime::parse(value, &format).is_err()
                    && time::PrimitiveDateTime::parse(value, &format).is_err()
                    && time::Date::parse(value, &format).is_err() {
                    return Err("capture time must be a valid ISO date or timestamp");
                }
            }
        }
    }
    Ok(())
}

impl CatalogService {
    pub async fn list_asset_metadata(&self) -> Result<BTreeMap<String, Value>, sqlx::Error> {
        let rows = sqlx::query("SELECT asset_id, metadata_json FROM asset_metadata ORDER BY asset_id").fetch_all(&self.pool).await?;
        rows.into_iter().map(|row| Ok((row.try_get("asset_id")?, row.try_get("metadata_json")?))).collect()
    }
    pub async fn upsert_asset_metadata(&self, records: &[AssetMetadataRecord]) -> Result<ReviewStateBatchResult, sqlx::Error> {
        validate_asset_metadata(records).map_err(|e| sqlx::Error::Protocol(e.into()))?;
        let mut transaction = self.pool.begin().await?;
        let mut result = ReviewStateBatchResult { applied: Vec::new(), missing: Vec::new() };
        // Stable ordering avoids conflicting batch lock order.
        let mut records: Vec<_> = records.iter().collect();
        records.sort_by(|a,b| a.asset_id.cmp(&b.asset_id));
        for record in records {
            let found = sqlx::query("SELECT asset_id FROM assets WHERE asset_id = $1 FOR KEY SHARE")
                .bind(&record.asset_id).fetch_optional(&mut *transaction).await?;
            if found.is_none() { result.missing.push(record.asset_id.clone()); continue; }
            sqlx::query("INSERT INTO asset_metadata(asset_id, metadata_json) VALUES ($1,$2) ON CONFLICT(asset_id) DO UPDATE SET metadata_json = EXCLUDED.metadata_json, updated_at = NOW()")
                .bind(&record.asset_id).bind(Value::Object(record.metadata.clone())).execute(&mut *transaction).await?;
            result.applied.push(record.asset_id.clone());
        }
        transaction.commit().await?;
        Ok(result)
    }
}
impl CatalogRuntime {
    pub async fn list_asset_metadata(&self) -> Result<BTreeMap<String, Value>, sqlx::Error> {
        self.repository.list_asset_metadata().await
    }
    pub async fn upsert_asset_metadata(&self, records: &[AssetMetadataRecord]) -> Result<ReviewStateBatchResult, sqlx::Error> {
        self.repository.upsert_asset_metadata(records).await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn metadata_contract_rejects_invalid_and_duplicate_writes() {
        let good = AssetMetadataRecord { asset_id: "photo-1".into(), metadata: serde_json::from_value(serde_json::json!({"keywords":["Nepal"],"caption":"Trip"})).unwrap() };
        assert!(validate_asset_metadata(std::slice::from_ref(&good)).is_ok());
        assert!(validate_asset_metadata(&[good.clone(), good]).is_err());
        for payload in [serde_json::json!({"keywords":"Nepal"}), serde_json::json!({"unknown":true}), serde_json::json!({"caption":12})] {
            assert!(validate_asset_metadata(&[AssetMetadataRecord { asset_id:"one".into(), metadata: serde_json::from_value(payload).unwrap() }]).is_err());
        }
    }
}
