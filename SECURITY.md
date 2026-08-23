# Security Policy

## Supported Versions

Security fixes are currently applied to the latest public release of ATK Player.

| Version | Supported |
|---|---|
| 0.2.x | Yes |
| Earlier development builds | No |

Users should update to the latest available release before reporting a security issue.

---

## Reporting a Vulnerability

Please do **not** report security vulnerabilities through a public GitHub issue.

Use GitHub's private vulnerability reporting or Security Advisory feature for this repository whenever it is available.

When reporting a vulnerability, include as much of the following information as possible:

- ATK Player version
- Windows version
- steps required to reproduce the issue
- expected behavior
- actual behavior
- whether the Local API was enabled
- Local API port, if relevant
- whether Maya or Toon Boom Harmony integration was involved
- relevant media container or codec information, if the issue is media-related
- crash logs or diagnostic output, if available

Please avoid including private production media, credentials, personal information, or other sensitive material unless it is necessary to reproduce the issue.

---

## Local API Security Model

ATK Player includes an optional local automation API for integration with tools such as Autodesk Maya and Toon Boom Harmony.

The Local API is:

- disabled by default
- bound only to the local loopback interface
- not intended to be exposed to a network
- designed for trusted software running on the same computer
- bounded by request and pending-output size limits

The default Local API address is:

```text
127.0.0.1:45571
```

Enabling the Local API allows trusted local applications to control ATK Player and access supported project, playback, review, comparison, and export operations.

Do not deliberately proxy or expose the Local API to another computer or an untrusted network.

---

## Sensitive Information

ATK Player project files may contain references to local media paths.

Before sharing an `.atkproj` file publicly, review it for:

- local file paths
- project names
- production names
- review notes
- other information that may identify private media or production work

ATK Player does not require users to upload project or media files to an external ATK service.

---

## Third-Party Components

ATK Player uses third-party open-source components including Qt and FFmpeg.

Security issues that are specific to a third-party dependency may also need to be reported to the upstream project.

See:

- `docs/THIRD_PARTY_LICENSES.md`
- `packaging/windows/THIRD_PARTY_NOTICES.txt`

for dependency and distribution information.

---

## Disclosure

Please allow reasonable time to investigate and prepare a fix before publicly disclosing a security vulnerability.

If a vulnerability is confirmed, the project may publish:

- a patched ATK Player release
- release notes describing the affected versions
- mitigation or upgrade guidance
- a GitHub Security Advisory where appropriate
