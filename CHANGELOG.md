# Changelog

## 1.0.0 - 2026-11-18 - working

### Added
- First release. It contains everything built and tested in 0.1.0, listed here and kept as history below:
- Swap = old -> new and Remove for actor classes: in data (weapon actor class, spawn points, activities) and on actors as they appear (Map, Live, Chance).
- Console obj.classes, obj.apply, obj.reload.
- main.dll carries a Windows version resource (file and product version 1.0.0, shown in its Properties > Details).

### Changed
- Writes into game data are recorded and re-imported at each map load when their assets are no longer loaded (NorseReflect RecordWrite/RecheckWrites) - a safety net; the freeing it guards against was seen once and did not repeat in testing.

## 0.1.0 - 2026-11-18 (tested in game: TEST-RESULT-0.1.0.txt)
### Added
- Swap = old -> new and Remove for actor classes: in data (weapon actor class, spawn points, activities) and on actors as they appear (Map, Live, Chance).
- Console obj.classes, obj.apply, obj.reload.

### Changed (2026-11-18, refinement)
- Writes into game data are recorded and re-imported at each map load when their assets are no longer loaded (NorseReflect RecordWrite/RecheckWrites) - a safety net; the freeing it guards against was seen once and did not repeat in testing.
