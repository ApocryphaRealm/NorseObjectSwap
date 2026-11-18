NorseObjectSwap rules
=====================

Every *.ini here swaps the class an actor spawns as, or removes it. Each [section] is one rule (its name is only a
label); rules run in load order, and nothing in the game's files changes.

    [Different training dummy]
    Swap = /Game/Norse/.../BP_Dummy.BP_Dummy_C -> /Game/MyPack/.../BP_Dummy2.BP_Dummy2_C    ; repeatable
    Remove = /Game/Norse/.../BP_Barrel.BP_Barrel_C                                         ; repeatable
    Map = Borre*, -CombatTestMap      ; only on these maps (the map's name, wildcards)
    Live = true                       ; false: only the game's data (below), not actors already placed
    Chance = 100                      ; per actor, the same roll every time

A rule with no Map and no Chance also swaps the class where Norse's data names it: the actor a weapon spawns as
(ItemAsset_Weapon ActorClass), spawn points, mission activities that spawn actors. A weapon held by a unit is best
swapped there (Live = false). Live: an actor of the old class is replaced on the next frame after it appears - the new
class at its place, the old one destroyed. Console: obj.classes [text] (the Blueprint actor classes on the map, with
counts - where to find the paths), obj.apply, obj.reload.
