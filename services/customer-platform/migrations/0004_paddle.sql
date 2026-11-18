-- Merchant identifiers never replace Looksy account identity. Separate staging/live databases are required.
CREATE TABLE paddle_accounts (
  account_id TEXT PRIMARY KEY REFERENCES accounts(id),
  customer_id TEXT NOT NULL UNIQUE,
  environment TEXT NOT NULL CHECK(environment IN ('sandbox','live'))
);
CREATE TABLE paddle_checkouts (
  transaction_id TEXT PRIMARY KEY,
  account_id TEXT NOT NULL REFERENCES accounts(id),
  customer_id TEXT NOT NULL,
  price_id TEXT NOT NULL,
  environment TEXT NOT NULL CHECK(environment IN ('sandbox','live')),
  created_at INTEGER NOT NULL,
  checked_at INTEGER NOT NULL DEFAULT 0,
  fulfilled_at INTEGER
);
CREATE INDEX paddle_checkouts_pending ON paddle_checkouts(fulfilled_at,checked_at);
CREATE TABLE paddle_licenses (
  license_id TEXT PRIMARY KEY REFERENCES licenses(id),
  account_id TEXT NOT NULL REFERENCES accounts(id),
  customer_id TEXT NOT NULL,
  transaction_id TEXT NOT NULL,
  subscription_id TEXT UNIQUE,
  environment TEXT NOT NULL CHECK(environment IN ('sandbox','live'))
);
CREATE INDEX paddle_licenses_transaction ON paddle_licenses(transaction_id);
-- Evaluated inside fulfillment's atomic batch after all remote provider reads.
CREATE TRIGGER paddle_enabled_account BEFORE INSERT ON paddle_licenses
WHEN NOT EXISTS(SELECT 1 FROM accounts WHERE id=NEW.account_id AND disabled=0)
  OR EXISTS(SELECT 1 FROM privacy_tombstones p JOIN licenses l ON l.customer_id=p.customer_id WHERE l.id=NEW.license_id)
BEGIN SELECT RAISE(ABORT,'account_disabled'); END;
