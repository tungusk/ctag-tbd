# Security policy

## Reporting a vulnerability

Please **do not** open a public issue for security vulnerabilities.

Report privately to **security@dadamachines.com** (or use GitHub's "Report a
vulnerability" / private security advisory feature on this repository if
enabled). Include:

- a description of the issue and its impact,
- steps to reproduce / a proof of concept if possible,
- affected component (firmware engine, REST API, WebUI, OTA/flasher, …) and
  version / channel.

We'll acknowledge receipt, work with you on a fix and a coordinated disclosure
timeline, and credit you (if you wish) in the release notes.

## Scope

In scope: the TBD-16 firmware (DSP engine, REST API, OTA), the WebUI, the
browser flasher, the macro/preset/rack layer. Out of scope: third-party
dependencies (report those upstream — see `THIRD-PARTY.md`), and physical-access
attacks that require opening the device.

## Supported versions

We support the **current `stable` channel** and the latest `staging` build.
Older firmware versions are not patched; please update.
