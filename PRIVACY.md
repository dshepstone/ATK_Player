# ATK Player Privacy Policy

**Last Updated:** September 4, 2026

## Overview

ATK Player is a free, open-source desktop media playback and review application developed and maintained by David Shepstone. It is designed primarily for animation and digital media workflows.

ATK Player is a locally installed application. It does not require a user account, subscription, or ATK-operated cloud service to function.

## Data Collection

ATK Player does not intentionally collect personal information, institutional data, user media, or usage information for transmission to the developer.

ATK Player does not require users to provide names, email addresses, account credentials, payment information, or other personal information in order to use the application.

ATK Player does not operate a vendor-hosted service for storing user projects or media.

## Local Data Processing and Storage

ATK Player processes media and project information locally on the user's computer.

Depending on the features used, locally stored information may include:

- media file references and local file paths;
- ATK Player project files;
- playlists;
- bookmarks and review information;
- loop and comparison settings;
- application preferences and configuration information; and
- other information created by the user while working with the application.

This information remains under the control of the user unless the user independently chooses to copy, export, upload, transmit, or otherwise share it.

## Media and Project Files

ATK Player does not require users to upload project files or media files to an external ATK service.

ATK Player project files may contain references to information such as local file paths, project names, production names, review notes, or other information associated with the user's work.

Users should review project files before sharing them publicly or with third parties, particularly when working with confidential, proprietary, institutional, or production material.

## Local API

ATK Player includes an optional Local API for integration with trusted applications running on the same computer, including supported animation and digital media workflows.

The Local API is:

- disabled by default;
- bound to the local loopback interface;
- intended for trusted software running on the same computer; and
- not intended to be exposed to external computers or untrusted networks.

Enabling the Local API allows trusted local applications to interact with supported ATK Player functions. Users should not deliberately proxy or expose the Local API to an external or untrusted network.

Additional information about the Local API security model is available in the project's `SECURITY.md` file.

## Data Sharing and Sale

ATK Player does not intentionally sell, rent, or share personal information, institutional data, or user media with the developer or with third parties.

Because ATK Player is a locally installed application, files that users independently open, save, export, or share remain subject to the user's own storage environment, operating system, network configuration, and any third-party services the user chooses to use.

## Third-Party Open-Source Components

ATK Player incorporates third-party open-source software components, including Qt and FFmpeg.

The inclusion of these components does not create an ATK-operated cloud service or require user project data or media to be hosted by the ATK Player developer.

Information regarding third-party components and applicable licenses is available in `docs/THIRD_PARTY_LICENSES.md` and the distribution's third-party notices.

## Telemetry and Analytics

ATK Player does not intentionally transmit application usage telemetry or analytics to an ATK-operated service.

If telemetry, crash reporting, cloud functionality, or other data-collection functionality is introduced in a future release, this privacy policy should be updated to describe the applicable data practices before or when that functionality is made available.

## Security

ATK Player maintains a separate `SECURITY.md` document describing the project's security model, supported versions, vulnerability reporting process, Local API security, sensitive-information considerations, and coordinated vulnerability disclosure process.

Security vulnerabilities should be reported using the private reporting methods described in `SECURITY.md` rather than through a public GitHub issue.

## Privacy and International Use

ATK Player is designed to minimize privacy risk by processing application data locally and by not requiring an ATK-operated account or cloud service.

This policy describes ATK Player's actual data-handling practices. It should not be interpreted as a claim of independent certification under GDPR, PIPL, ISO 27001, or any other privacy or information-security certification framework.

Users and organizations remain responsible for determining whether their particular use of ATK Player satisfies laws, regulations, contractual obligations, institutional policies, and data-handling requirements applicable to them.

## Changes to This Policy

This privacy policy may be updated if ATK Player's functionality or data-handling practices change.

Material changes affecting the collection, transmission, storage, or sharing of user information should be documented in this policy as part of the applicable software release.

## Contact

Questions regarding ATK Player privacy practices may be directed to the project maintainer:

**David Shepstone**  
Email: david@shepstone.ca

For security vulnerabilities, please follow the reporting process described in `SECURITY.md`.

## Open-Source License

ATK Player is distributed under the MIT License. See the repository's `LICENSE` file for the complete license terms.
