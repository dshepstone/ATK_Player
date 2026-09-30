# Code signing policy

Official Windows releases of ATK Player are built from the public source code in this repository through the project's automated GitHub Actions build process.

ATK Player is preparing to apply to SignPath Foundation for trusted Windows code signing. Approval has not been established, signing is not yet integrated, and current releases remain unsigned.

If approved, the signing attribution will be: Free code signing provided by [SignPath.io](https://signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).

## Project roles

ATK Player is currently maintained by a single project maintainer. The same maintainer performs the roles required for source control, review, and release signing approval.

- **Author / Committer: David Shepstone** — Project creator and maintainer. Responsible for authorizing source-code and build-system changes committed to the ATK Player repository.
- **Reviewer: David Shepstone** — Reviews and accepts changes before they become part of an official ATK Player release, including code created with or assisted by development tools and AI coding assistants.
- **Approver: David Shepstone** — Responsible for manually approving official ATK Player release artifacts for code signing.

## Development and release responsibility

ATK Player is an open-source project released under the [MIT License](LICENSE). Development may use AI-assisted software-development tools. AI systems are development tools rather than project maintainers or release approvers. All source code, build configuration, dependencies, and official releases accepted into the project remain under the review, control, and responsibility of the project maintainer.

Future official signed artifacts must originate from the project's public, source-controlled GitHub Actions build process after tests pass. Each signing request must be manually approved by David Shepstone. Only ATK Player's own executable and installer will be submitted for signing; upstream dependency binaries may be included in packages but must not be signed using ATK Player's subscription.

All project members must use multi-factor authentication for repository and SignPath access. On September 30, 2026, David Shepstone confirmed that GitHub multi-factor authentication is enabled with passkeys or another second factor; SignPath account security must be configured before signing begins. The maintainer will cooperate with SignPath Foundation in investigating reported policy violations.

## Privacy

See the ATK Player [Privacy Policy](PRIVACY.md) for information about application privacy and network communication.

## Security

Security reporting and supported-release information are documented in [SECURITY.md](SECURITY.md).
