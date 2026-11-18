# NorseObjectSwap

Swap the class an actor spawns as, or remove it, by rule, for the Norse overhaul (NORSE: Oath of Blood, UE4SS v4.0.0).
Nothing in the game's files changes. Modelled on Base Object Swapper (Skyrim).

```ini
[Common spears spawn as masterwork spears]
Swap = /Game/Norse/Characters/Items/Weapons/Spears/BP_Tier1_Common_Spear_01.BP_Tier1_Common_Spear_01_C -> /Game/Norse/Characters/Items/Weapons/Spears/BP_Tier1_Masterwork_Spear_01.BP_Tier1_Masterwork_Spear_01_C
Live = false

[No sky sphere on the test map]
Remove = /Engine/EngineSky/BP_Sky_Sphere.BP_Sky_Sphere_C
Map = CombatTestMap
```

Two layers:
- **Data** (rules with no `Map` and no `Chance`): the class or asset is swapped where Norse's data names it - the
  actor a weapon spawns as (`ItemAsset_Weapon.ActorClass`), spawn points and their unit data (`SpawnClass`), mission
  activities that spawn actors (`Activity_SpawnActor`), decoration characters. At each map load. Whole paths only
  (`BP_Axe_C` does not touch `BP_Axe_C2`).
- **Live** (`Live = true`, the default): an actor of the old class that begins play is replaced on the next frame -
  the new class spawned at its transform with its owner (BeginDeferredActorSpawnFromClass / FinishSpawningActor), the
  old one destroyed - or only destroyed (`Remove`). `Map` and `Chance` limit it. A weapon a unit holds is better
  swapped in data (`Live = false`): replacing it live would leave the unit pointing at a destroyed actor.

Targets are checked with `Alive()` before use and loaded again when a map change freed them. Console: `obj.classes
[text]` (Blueprint actor classes on the map, with counts: where to find paths), `obj.apply`, `obj.reload`;
NorseDevTools reloads when a file in `objects\` is saved. Author guide: `package\objects\README.txt`; tested rules:
`docs\examples\`.

0.1.0 tested in game (TEST-RESULT-0.1.0.txt). Not yet: `Chance`, placed props on a real mission map.
