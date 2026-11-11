# Security Policy

## Supported versions

RoofRoof is in an early platform stage (spec 001). Only the latest release is supported.

| Version | Supported |
|---------|-----------|
| latest  | ✓ |
| older   | ✗ |

## Scope

RoofRoof is a local-network device. It is designed to run on your home or observatory network and is not intended to be directly exposed to the public internet. By default there is no authentication on the web interface or REST API; **Password-protect changes** (HTTP Basic Auth) can be turned on for everything that changes something, and reads stay open. Treat network access as equivalent to physical access.

Known non-issues (by design):

- Authentication is off by default, and when on it protects changes only; reads are open
- No HTTPS (local network only), so a password travels in plain text on the LAN
- The firmware and web UI upload endpoints are unauthenticated until the password is turned on
- ASCOM Alpaca has no authentication in its specification: while Alpaca is on, anything on the network can send the roof's Dome open, close and stop commands. They pass the same weather and park checks as every other command, but can't be limited to one client

If you intend to expose RoofRoof to untrusted networks, place it behind a reverse proxy with authentication, or a VPN. Never rely on the network or this firmware's web interface as the only thing standing between a person and a moving roof: keep physical safety interlocks independent. See [docs/user-guide/security.md](docs/user-guide/security.md).

## Reporting a vulnerability

Please **do not** open a public GitHub issue for security vulnerabilities.

Report vulnerabilities via Discord: **.dean0**

Include:

- A description of the vulnerability
- Steps to reproduce
- Potential impact
- Any suggested fix if you have one

You can expect an acknowledgement within 7 days and a resolution timeline within 30 days for confirmed issues.
