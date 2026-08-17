# API Keys

The BOM Manager can query live pricing/stock from Mouser, DigiKey, and Octopart (Nexar). If you do not provide keys it falls back to cached and manually-entered prices.

## Mouser

1. Create or log in to your Mouser account.
2. Go to **Account > API Keys** (or Mouser API Portal).
3. Generate a new API key for the Search API.
4. Add to `config.yaml`:

```yaml
mouser:
  api_key: "your-key-here"
```

## DigiKey

1. Log in to the DigiKey API Portal: https://developer.digikey.com/
2. Create an app to get a **Client ID** and **Client Secret**.
3. Add to `config.yaml`:

```yaml
digikey:
  client_id: "your-client-id"
  client_secret: "your-client-secret"
  sandbox: false
```

Set `sandbox: true` to test against the DigiKey sandbox environment.

## Octopart / Nexar

Octopart is now part of Nexar.

1. Sign up at https://nexar.com/
2. Create a supply application to get an access token.
3. Add the token to `config.yaml`:

```yaml
octopart:
  api_key: "your-nexar-token"
```

## McMaster-Carr (Official API)

McMaster-Carr offers an approved-customer Product Information API. Contact **eprocurement@mcmaster.com** to request access. They will provide:

- A username and password
- A client certificate (`.p12` or `.pem`) and its password

Add to `config.yaml`:

```yaml
mcmaster:
  username: "your-mcmaster-username"
  password: "your-mcmaster-password"
  cert_path: "/path/to/mcmaster-client-cert.pem"
  cert_password: "cert-password-if-any"
```

If the official API credentials are not configured, the tool falls back to web scraping the public product pages.

## Environment Variables

You can also set keys as environment variables (useful for CI):

```bash
export BOM_MOUSER_API_KEY="..."
export BOM_DIGIKEY_CLIENT_ID="..."
export BOM_DIGIKEY_CLIENT_SECRET="..."
export BOM_OCTOPART_API_KEY="..."
export BOM_MCMASTER_USERNAME="..."
export BOM_MCMASTER_PASSWORD="..."
export BOM_MCMASTER_CERT_PATH="..."
export BOM_MCMASTER_CERT_PASSWORD="..."
```
