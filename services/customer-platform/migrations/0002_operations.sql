ALTER TABLE webhook_receipts ADD COLUMN attempts INTEGER NOT NULL DEFAULT 0;
ALTER TABLE webhook_receipts ADD COLUMN next_retry_at INTEGER NOT NULL DEFAULT 0;
ALTER TABLE webhook_receipts ADD COLUMN last_error_code TEXT;

CREATE INDEX webhook_retry_due ON webhook_receipts(processed_at, next_retry_at, received_at);

CREATE TABLE privacy_tombstones (
  customer_id TEXT PRIMARY KEY REFERENCES customers(id),
  provider_customer_hash TEXT,
  deleted_at INTEGER NOT NULL
);

-- Enforce the paid device limit on both first activation and recovery inserts.
CREATE TRIGGER enforce_device_limit_insert BEFORE INSERT ON installations
WHEN NEW.license_id IS NOT NULL AND NEW.revoked = 0
  AND (SELECT count(*) FROM installations WHERE license_id = NEW.license_id AND revoked = 0)
    >= (SELECT device_limit FROM licenses WHERE id = NEW.license_id)
BEGIN SELECT RAISE(ABORT, 'device_limit'); END;

CREATE TRIGGER require_active_license_insert BEFORE INSERT ON installations
WHEN NEW.license_id IS NOT NULL AND NEW.revoked = 0
  AND NOT EXISTS (SELECT 1 FROM licenses WHERE id = NEW.license_id AND customer_id = NEW.customer_id
    AND blocked = 0 AND status IN ('active','inactive'))
BEGIN SELECT RAISE(ABORT, 'license_inactive'); END;
