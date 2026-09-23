# Changelog

Здесь фиксируются изменения WMCS, заметные пользователю или влияющие на
совместимость. Мелкие commits группируются по release-линии, в которой они
появились.

## [Unreleased]

### Added

- Публичная структура репозитория в стиле Freenetic: двуязычная документация,
  release-страница и issue-шаблоны.
- Первый контролируемый двухроутерный native WMCS pilot на Globitel BT-RB300
  и Cudy TR3000 v1.

### Verified

- Persistent identity, SAS pairing, encrypted control и one-WLAN transaction.
- Rollback, release/forget invariants, daemon restart и router sysupgrade
  preservation.
- OpenWrt APK/IPK packaging, LuCI view и repository contract checks.

### Alpha boundaries

- WMCS ещё не является production mesh release.
- Power-cut matrix, hostile-network tests, multi-member topology и key
  rotation остаются открытыми.
- Roaming policy по умолчанию выключена и не обещает универсальный seamless
  handoff.
