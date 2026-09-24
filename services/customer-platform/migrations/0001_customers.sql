-- Hosted customer operations only; never apply this to a desktop catalog.
CREATE TABLE customers (
  id TEXT PRIMARY KEY,
  provider_customer_id TEXT UNIQUE,
  email TEXT,
  created_at INTEGER NOT NULL
);
CREATE TABLE licenses (
  id TEXT PRIMARY KEY,
  customer_id TEXT NOT NULL REFERENCES customers(id),
  order_id TEXT NOT NULL,
  order_item_id TEXT NOT NULL,
  variant_id TEXT NOT NULL,
  plan TEXT NOT NULL,
  mode TEXT NOT NULL CHECK(mode IN ('perpetual','subscription')),
  major INTEGER NOT NULL,
  features TEXT NOT NULL,
  device_limit INTEGER NOT NULL CHECK(device_limit BETWEEN 1 AND 100),
  status TEXT NOT NULL,
  expires_at INTEGER,
  checked_at INTEGER NOT NULL,
  refresh_revision INTEGER NOT NULL DEFAULT 0,
  blocked INTEGER NOT NULL DEFAULT 0 CHECK(blocked IN (0,1))
);
CREATE INDEX licenses_order ON licenses(order_id);
CREATE TABLE subscriptions (
  id TEXT PRIMARY KEY,
  order_item_id TEXT NOT NULL UNIQUE,
  variant_id TEXT NOT NULL,
  status TEXT NOT NULL,
  ends_at INTEGER,
  checked_at INTEGER NOT NULL,
  refresh_revision INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE refresh_sequences (id TEXT PRIMARY KEY, revision INTEGER NOT NULL);
CREATE TABLE installations (
  id TEXT PRIMARY KEY,
  credential_hash TEXT NOT NULL UNIQUE,
  customer_id TEXT NOT NULL REFERENCES customers(id),
  license_id TEXT REFERENCES licenses(id),
  trial_ends_at INTEGER NOT NULL,
  revoked INTEGER NOT NULL DEFAULT 0 CHECK(revoked IN (0,1)),
  created_at INTEGER NOT NULL,
  last_seen_at INTEGER NOT NULL,
  app_version TEXT NOT NULL DEFAULT '',
  platform TEXT NOT NULL DEFAULT ''
);
CREATE INDEX installations_license ON installations(license_id, revoked);
CREATE TRIGGER enforce_device_limit BEFORE UPDATE OF license_id, revoked ON installations
WHEN NEW.license_id IS NOT NULL AND NEW.revoked = 0
  AND (OLD.license_id IS NOT NEW.license_id OR OLD.revoked != 0)
  AND (SELECT count(*) FROM installations WHERE license_id = NEW.license_id AND revoked = 0 AND id != NEW.id)
    >= (SELECT device_limit FROM licenses WHERE id = NEW.license_id)
BEGIN SELECT RAISE(ABORT, 'device_limit'); END;
CREATE TABLE webhook_receipts (
  digest TEXT PRIMARY KEY,
  event TEXT NOT NULL,
  resource_id TEXT NOT NULL,
  received_at INTEGER NOT NULL,
  processed_at INTEGER
);
CREATE TABLE staff (
  subject TEXT PRIMARY KEY,
  role TEXT NOT NULL CHECK(role IN ('support','admin'))
);
CREATE TABLE audit (
  id TEXT PRIMARY KEY,
  actor TEXT NOT NULL,
  action TEXT NOT NULL,
  target TEXT NOT NULL,
  reason TEXT NOT NULL,
  created_at INTEGER NOT NULL
);
CREATE TABLE rate_buckets (
  id TEXT PRIMARY KEY,
  count INTEGER NOT NULL,
  expires_at INTEGER NOT NULL
);
