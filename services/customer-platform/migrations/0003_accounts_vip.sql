-- Looksy identities are independent of merchant customer identities.
CREATE TABLE accounts (
  id TEXT PRIMARY KEY,
  email TEXT NOT NULL UNIQUE,
  trial_ends_at INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  disabled INTEGER NOT NULL DEFAULT 0 CHECK(disabled IN (0,1))
);
ALTER TABLE installations ADD COLUMN account_id TEXT REFERENCES accounts(id);
ALTER TABLE installations ADD COLUMN account_session_expires_at INTEGER;
CREATE INDEX installations_account ON installations(account_id,revoked);
CREATE TABLE account_licenses (
  license_id TEXT PRIMARY KEY REFERENCES licenses(id),
  account_id TEXT NOT NULL REFERENCES accounts(id),
  linked_at INTEGER NOT NULL
);
CREATE INDEX account_licenses_account ON account_licenses(account_id);
CREATE TABLE email_challenges (
  id TEXT PRIMARY KEY,
  installation_id TEXT NOT NULL REFERENCES installations(id),
  email TEXT NOT NULL,
  code_hash TEXT NOT NULL,
  expires_at INTEGER NOT NULL,
  attempts INTEGER NOT NULL DEFAULT 0,
  consumed_at INTEGER,
  created_at INTEGER NOT NULL
);
CREATE INDEX email_challenges_expiry ON email_challenges(expires_at);
CREATE TABLE vip_codes (
  id TEXT PRIMARY KEY,
  code_hash TEXT NOT NULL UNIQUE,
  plan TEXT NOT NULL,
  features TEXT NOT NULL,
  major INTEGER NOT NULL,
  device_limit INTEGER NOT NULL CHECK(device_limit BETWEEN 1 AND 100),
  redemption_limit INTEGER NOT NULL CHECK(redemption_limit BETWEEN 1 AND 1000),
  expires_at INTEGER NOT NULL,
  access_ends_at INTEGER NOT NULL,
  revoked INTEGER NOT NULL DEFAULT 0 CHECK(revoked IN (0,1)),
  created_at INTEGER NOT NULL
);
CREATE TABLE vip_redemptions (
  code_id TEXT NOT NULL REFERENCES vip_codes(id),
  account_id TEXT NOT NULL REFERENCES accounts(id),
  license_id TEXT NOT NULL UNIQUE REFERENCES licenses(id),
  redeemed_at INTEGER NOT NULL,
  PRIMARY KEY(code_id,account_id)
);
CREATE TRIGGER enforce_vip_redemption BEFORE INSERT ON vip_redemptions
WHEN NOT EXISTS(SELECT 1 FROM vip_codes WHERE id=NEW.code_id AND revoked=0
  AND expires_at>NEW.redeemed_at AND access_ends_at>NEW.redeemed_at
  AND (SELECT count(*) FROM vip_redemptions WHERE code_id=NEW.code_id)<redemption_limit)
BEGIN SELECT RAISE(ABORT,'vip_unavailable'); END;
CREATE TRIGGER protect_account_license BEFORE INSERT ON account_licenses
WHEN EXISTS(SELECT 1 FROM account_licenses WHERE license_id=NEW.license_id AND account_id!=NEW.account_id)
BEGIN SELECT RAISE(ABORT,'license_already_linked'); END;
