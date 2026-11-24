# PrismOS

**One Core. Many Experiences.**

PrismOS is an independently developed embedded system distribution based on [Aster Firmware](https://github.com/a505283042/aster-firmware), initially targeting FakePod Nano Pro (ESP32-S31).

## Status / 当前状态

Planning and repository initialization. The full upstream firmware source has **not yet been imported**. No PrismOS firmware build or hardware validation is claimed.

- [中文开发方案](docs/DEVELOPMENT_PLAN.zh-CN.md)
- [Reference baseline](https://github.com/a505283042/aster-firmware/commit/2ff46e11bb63f12b613fc7fc071a6014d243b832)

## Goals

- Preserve existing audio, device services, AOT runtime and ABI compatibility.
- Develop a modular native UI shell with switchable themes and font families.
- Establish an independent distribution channel and application store.
- Preserve upstream licenses, notices and third-party attribution.

## Source import / 基线导入

From a Git-enabled computer authenticated to GitHub, use a temporary directory:

```bash
git clone https://github.com/a505283042/PrismOS.git PrismOS
cd PrismOS
git remote add aster https://github.com/a505283042/aster-firmware.git
git fetch aster main
git merge --allow-unrelated-histories --no-commit 2ff46e11bb63f12b613fc7fc071a6014d243b832
```

The README may conflict: retain the PrismOS introduction and preserve the upstream README under docs/ASTER_UPSTREAM_README.md. Retain all upstream license files. Review imported workflows, release targets, external references and any local conflicts before committing; disable imported release workflows until independently configured. Verify the imported tree against the pinned baseline, then commit and push to PrismOS only. Never force-push or modify the Aster repository.

The Chinese development plan defines the implementation stages and acceptance criteria.
