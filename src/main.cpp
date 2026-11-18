// NorseObjectSwap - swap the class an actor spawns as, or remove it, by rule (UE4SS v4.0.0 C++ mod for NORSE: Oath of
// Blood), 2026-11-18. Two layers from the same rules:
//  - data: class and asset references in Norse's data - the actor a weapon spawns as (ItemAsset_Weapon ActorClass),
//    spawn points and their unit data (SpawnClass), mission activities that spawn actors - are swapped where they are
//    held, at each map load (rules with no Map and no Chance).
//  - live: an actor of the old class that begins play is replaced - the new class spawned at its transform, the old one
//    destroyed - or only destroyed (Remove). Map and Chance limit it. Done on the next frame, never inside BeginPlay.
// Nothing in the game's files changes. Modelled on Base Object Swapper (Skyrim). Plan: ASSESSMENT-AND-PLAN.md.
//
// objects\*.ini (NorseManifest order), one [section] per rule; its name is only a label:
//   [Different training dummy]
//   Swap = /Game/Norse/.../BP_Dummy.BP_Dummy_C -> /Game/MyPack/.../BP_Dummy2.BP_Dummy2_C    ; repeatable
//   Remove = /Game/Norse/.../BP_Barrel.BP_Barrel_C                                         ; repeatable
//   Map = Borre*, -CombatTestMap      ; live layer: only on these maps (the map's name, wildcards)
//   Live = true                       ; false: data layer only (a weapon held by a unit is better swapped there)
//   Chance = 100                      ; live layer, per actor, the same roll every time
// Console: obj.classes [text] (actor classes on the map, by count), obj.reload, obj.apply.
#include "NorseReflect.hpp"
#include "NorseManifest.hpp"
#include "NorseFilters.hpp"

#include <deque>
#include <map>
#include <set>

namespace fs = std::filesystem;
using namespace nrn;
using namespace nrn::ue;
using namespace norse;

namespace
{
    fs::path g_mod;

    fs::path ModDir()
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ModDir), &self);
        wchar_t buf[MAX_PATH * 2]{};
        GetModuleFileNameW(self, buf, static_cast<DWORD>(std::size(buf)));
        return fs::path(buf).parent_path().parent_path();
    }

    std::wstring PathOfObj(UObject* o)
    {
        std::wstring full = o->GetFullName(nullptr);
        return full.substr(full.find(L' ') + 1);
    }
    // /Game/.../X.X:PersistentLevel.Actor -> X
    std::wstring MapOf(const std::wstring& path)
    {
        std::wstring pkg = path.substr(0, path.find(L':'));
        pkg = pkg.substr(0, pkg.find(L'.'));
        return pkg.substr(pkg.find_last_of(L'/') + 1);
    }

    struct Pair
    {
        std::wstring from, to; // to empty = Remove
        UObject* toObj = nullptr;
        int32_t toIdx = -1;
        bool Ready() const { return to.empty() || (toObj && Alive(toObj, toIdx)); }
    };
    struct Rule
    {
        std::string name, where;
        std::vector<Pair> pairs;
        Filter maps;
        bool live = true;
        int chance = 100;
    };
    std::vector<Rule> g_rules;

    void LoadRules()
    {
        g_rules.clear();
        fs::path dir = g_mod / "objects";
        std::vector<fs::path> files;
        std::error_code ec;
        if (fs::is_directory(dir, ec))
            for (const auto& f : fs::directory_iterator(dir, ec))
                if (f.is_regular_file() && Lower(f.path().extension().wstring()) == L".ini") files.push_back(f.path());
        nm::Manifest man;
        man.Resolve(g_mod.parent_path().parent_path());
        files = man.Arrange(files, [](const std::string& s) { NRN_INFO("load order: %s", s.c_str()); });
        for (const auto& f : files)
        {
            Rule* cur = nullptr;
            int n = 0;
            for (const auto& raw : ReadLines(f))
            {
                ++n;
                std::wstring l = Trim(StripComment(raw));
                if (l.empty()) continue;
                std::string where = Narrow(f.filename().wstring()) + ":" + std::to_string(n);
                if (l.front() == L'[' && l.back() == L']')
                {
                    g_rules.push_back({});
                    cur = &g_rules.back();
                    cur->name = Narrow(Trim(l.substr(1, l.size() - 2)));
                    cur->where = where;
                    continue;
                }
                size_t eq = l.find(L'=');
                if (!cur || eq == std::wstring::npos)
                {
                    NRN_WARN("%s: not a [rule] or key = value line - skipped", where.c_str());
                    continue;
                }
                std::wstring k = Lower(Trim(l.substr(0, eq))), v = Trim(l.substr(eq + 1));
                if (k == L"swap")
                {
                    size_t arrow = v.find(L"->");
                    if (arrow == std::wstring::npos) NRN_WARN("%s: Swap needs 'old -> new' - skipped", where.c_str());
                    else cur->pairs.push_back({Trim(v.substr(0, arrow)), Trim(v.substr(arrow + 2))});
                }
                else if (k == L"remove") cur->pairs.push_back({v, L""});
                else if (k == L"map") cur->maps = ParseFilter(v);
                else if (k == L"live") cur->live = !(_wcsicmp(v.c_str(), L"false") == 0 || v == L"0");
                else if (k == L"chance") cur->chance = ParseChance(v);
                else NRN_WARN("%s: unknown key '%s' (Swap, Remove, Map, Live, Chance)", where.c_str(), Narrow(k).c_str());
            }
        }
        size_t pairs = 0;
        for (const Rule& r : g_rules) pairs += r.pairs.size();
        NRN_INFO("object rules: %zu rule(s), %zu swap/remove line(s) from %zu file(s)", g_rules.size(), pairs, files.size());
    }

    // Load each new class or asset; a target freed since (a map change frees loaded assets even in the root set -
    // NorseModelSwap 0.1.0) is loaded again
    void ResolveTargets()
    {
        for (Rule& r : g_rules)
            for (Pair& p : r.pairs)
            {
                if (p.to.empty()) continue;
                if (p.toObj && !Alive(p.toObj, p.toIdx)) p.toObj = nullptr;
                if (p.toObj) continue;
                p.toObj = LoadKept(p.to.c_str());
                if (!p.toObj) NRN_WARN("[%s] %s: not found - this swap is skipped", r.name.c_str(), Narrow(p.to).c_str());
                else p.toIdx = IndexOf(p.toObj);
            }
    }

    // ------------------------------------------------------------------ data layer
    const wchar_t* kHolders[] = {L"ItemAsset", L"SpawnPoint", L"SpawnPointDataObject", L"Activity_SpawnActor", L"DecorationCharacter"};

    std::wstring ReplaceAllI(const std::wstring& s, const std::wstring& from, const std::wstring& to, int& count)
    {
        std::wstring ls = Lower(s), lf = Lower(from), out;
        size_t pos = 0;
        for (size_t hit; (hit = ls.find(lf, pos)) != std::wstring::npos; pos = hit + from.size())
        {
            // whole path only: the next character must not continue the name (BP_Axe_C must not hit BP_Axe_C2)
            wchar_t next = hit + from.size() < s.size() ? s[hit + from.size()] : L'\0';
            if (iswalnum(next) || next == L'_')
            {
                out += s.substr(pos, hit + from.size() - pos);
                continue;
            }
            out += s.substr(pos, hit - pos) + to;
            ++count;
        }
        return out + s.substr(pos);
    }

    void ApplyData(const char* why)
    {
        std::vector<const Pair*> global;
        for (const Rule& r : g_rules)
            if (r.maps.Empty() && r.chance >= 100)
                for (const Pair& p : r.pairs)
                    if (!p.to.empty() && p.Ready()) global.push_back(&p);
        if (global.empty()) return;
        int objects = 0, fields = 0, refs = 0;
        for (const wchar_t* holder : kHolders)
        {
            std::vector<UObject*> objs;
            try
            {
                RC::Unreal::UObjectGlobals::FindAllOf(holder, objs);
            }
            catch (...)
            {
            }
            for (UObject* o : objs)
            {
                ++objects;
                auto* cls = reinterpret_cast<UStruct*>(Reflect::Get().ClassOf(o));
                ForEachProp(cls, [&](FProperty* p) {
                    void* addr = reinterpret_cast<uint8_t*>(o) + p->GetOffset_Internal();
                    std::wstring text = Export(p, addr, o);
                    if (text == L"<unreadable>" || text.size() < 8) return;
                    int hits = 0;
                    std::wstring now = text;
                    for (const Pair* s : global) now = ReplaceAllI(now, s->from, s->to, hits);
                    if (!hits) return;
                    bool faulted = false;
                    if (!SehImport(p, now.c_str(), addr, o, &faulted) || faulted)
                    {
                        NRN_WARN("%s.%s: Unreal did not accept the swapped value", Narrow(ShortName(PathOfObj(o))).c_str(), Narrow(p->GetName()).c_str());
                        return;
                    }
                    ++fields;
                    RecordWrite(o, p, addr, now, ShortName(PathOfObj(o)) + L"." + std::wstring(p->GetName()));
                    refs += hits;
                    NRN_INFO("data: %d reference(s) swapped in %s.%s", hits, Narrow(ShortName(PathOfObj(o))).c_str(), Narrow(p->GetName()).c_str());
                });
            }
        }
        NRN_INFO("data swaps (%s): %d reference(s) in %d field(s), %d holder(s) looked at", why, refs, fields, objects);
    }

    // ------------------------------------------------------------------ live layer
    std::wstring ClassPath(UObject* o)
    {
        UClass* c = Reflect::Get().ClassOf(o);
        return c ? PathOfObj(reinterpret_cast<UObject*>(c)) : L"";
    }

    struct Pending
    {
        UObject* actor;
        int32_t index;
        const Rule* rule;
        const Pair* pair;
    };
    std::deque<Pending> g_pending;
    std::set<UObject*> g_ours; // actors this mod spawned: never swapped again
    int g_swapped = 0, g_removed = 0;

    // Which rule and line (if any) apply to an actor that has just begun play
    bool Match(UObject* actor, const Rule*& rule, const Pair*& pair)
    {
        std::wstring cls = ClassPath(actor);
        if (cls.empty()) return false;
        std::wstring path = PathOfObj(actor);
        for (const Rule& r : g_rules)
        {
            if (!r.live) continue;
            for (const Pair& p : r.pairs)
            {
                if (_wcsicmp(p.from.c_str(), cls.c_str()) != 0) continue;
                if (!r.maps.Empty())
                {
                    std::wstring map = Lower(MapOf(path));
                    if (!r.maps.inc.empty() && std::none_of(r.maps.inc.begin(), r.maps.inc.end(), [&](auto& g) { return Glob(g.c_str(), map.c_str()); })) continue;
                    if (std::any_of(r.maps.exc.begin(), r.maps.exc.end(), [&](auto& g) { return Glob(g.c_str(), map.c_str()); })) continue;
                }
                if (r.chance < 100 && static_cast<int>(Fnv(path + L"|" + Widen(r.name)) % 100) >= r.chance) continue;
                rule = &r;
                pair = &p;
                return true;
            }
        }
        return false;
    }

    UObject* Gs()
    {
        static UObject* gs = FindObj(L"/Script/Engine.Default__GameplayStatics");
        return gs;
    }

    bool Destroy(UObject* actor)
    {
        Buf b;
        return Reflect::Get().Call(actor, L"K2_DestroyActor", b.b, 0, sizeof(b.b));
    }

    // GetTransform() -> FTransform (0x60); BeginDeferredActorSpawnFromClass(ctx @0, class @8, transform @0x10,
    // collision @0x70, owner @0x78, scale method @0x80) -> @0x88; FinishSpawningActor(actor @0, transform @0x10,
    // scale method @0x70) -> @0x78
    UObject* SpawnAt(UObject* like, UObject* cls)
    {
        Buf t;
        if (!Reflect::Get().Call(like, L"GetTransform", t.b, 0x60, sizeof(t.b))) return nullptr;
        UObject* owner = nullptr;
        {
            Buf o;
            if (Reflect::Get().Call(like, L"GetOwner", o.b, 8, sizeof(o.b))) std::memcpy(&owner, o.b, 8);
        }
        Buf a;
        std::memcpy(a.b, &like, 8);
        std::memcpy(a.b + 8, &cls, 8);
        std::memcpy(a.b + 0x10, t.b, 0x60);
        a.b[0x70] = 2; // ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn
        std::memcpy(a.b + 0x78, &owner, 8);
        a.b[0x80] = 1; // ESpawnActorScaleMethod::MultiplyWithRoot
        if (!Reflect::Get().Call(Gs(), L"BeginDeferredActorSpawnFromClass", a.b, 0x90, sizeof(a.b))) return nullptr;
        UObject* spawned = nullptr;
        std::memcpy(&spawned, a.b + 0x88, 8);
        if (!spawned) return nullptr;
        g_ours.insert(spawned);
        Buf f;
        std::memcpy(f.b, &spawned, 8);
        std::memcpy(f.b + 0x10, t.b, 0x60);
        f.b[0x70] = 1;
        if (!Reflect::Get().Call(Gs(), L"FinishSpawningActor", f.b, 0x80, sizeof(f.b))) return nullptr;
        return spawned;
    }

    void Tick()
    {
        if (g_pending.empty()) return;
        ResolveTargets();
        while (!g_pending.empty())
        {
            Pending p = g_pending.front();
            g_pending.pop_front();
            if (!Alive(p.actor, p.index)) continue;
            std::string name = Narrow(ShortName(PathOfObj(p.actor)));
            try
            {
                if (p.pair->to.empty())
                {
                    bool ok = Destroy(p.actor);
                    if (ok) ++g_removed;
                    NRN_INFO("%s: removed%s (%s)", name.c_str(), ok ? "" : " FAILED", p.rule->name.c_str());
                    continue;
                }
                if (!p.pair->Ready())
                {
                    NRN_WARN("%s: %s not loaded - left as it is", name.c_str(), Narrow(ShortName(p.pair->to)).c_str());
                    continue;
                }
                UObject* now = SpawnAt(p.actor, p.pair->toObj);
                if (!now)
                {
                    NRN_WARN("%s: spawning %s failed - the original is kept (%s)", name.c_str(), Narrow(ShortName(p.pair->to)).c_str(), p.rule->name.c_str());
                    continue;
                }
                Destroy(p.actor);
                ++g_swapped;
                NRN_INFO("%s (%s) -> %s (%s)", name.c_str(), Narrow(ShortName(p.pair->from)).c_str(), Narrow(ShortName(PathOfObj(now))).c_str(),
                         p.rule->name.c_str());
            }
            catch (...)
            {
                NRN_ERROR("%s: live swap threw", name.c_str());
            }
        }
    }

    void ApplyLive(const char* why)
    {
        int queued = 0;
        std::set<std::wstring> classes;
        for (const Rule& r : g_rules)
            if (r.live)
                for (const Pair& p : r.pairs) classes.insert(ShortName(p.from));
        for (const auto& c : classes)
        {
            std::vector<UObject*> found;
            try
            {
                RC::Unreal::UObjectGlobals::FindAllOf(c.c_str(), found);
            }
            catch (...)
            {
            }
            for (UObject* o : found)
            {
                if (ShortName(PathOfObj(o)).rfind(L"Default__", 0) == 0 || g_ours.count(o) || !Alive(o, IndexOf(o))) continue;
                const Rule* r = nullptr;
                const Pair* p = nullptr;
                if (Match(o, r, p))
                {
                    g_pending.push_back({o, IndexOf(o), r, p});
                    ++queued;
                }
            }
        }
        NRN_INFO("live (%s): %d actor(s) queued", why, queued);
    }

    void ListClasses(const std::wstring& text)
    {
        std::vector<UObject*> all;
        try
        {
            RC::Unreal::UObjectGlobals::FindAllOf(L"Actor", all);
        }
        catch (...)
        {
        }
        std::map<std::wstring, int> count;
        for (UObject* a : all)
        {
            if (!Alive(a, IndexOf(a))) continue;
            std::wstring c = ClassPath(a);
            if (c.rfind(L"/Script/", 0) == 0) continue; // native classes: not swappable assets
            if (!text.empty() && Lower(c).find(Lower(text)) == std::wstring::npos) continue;
            ++count[c];
        }
        for (const auto& [c, n] : count) NRN_INFO("  %4d  %s", n, Narrow(c).c_str());
        NRN_INFO("obj.classes: %zu blueprint actor class(es), %zu actor(s) looked at", count.size(), all.size());
    }

    bool RunCommand(const std::wstring& c)
    {
        std::wstring cmd = Trim(c), arg;
        if (size_t sp = cmd.find(L' '); sp != std::wstring::npos)
        {
            arg = Trim(cmd.substr(sp + 1));
            cmd = cmd.substr(0, sp);
        }
        cmd = Lower(cmd);
        NRN_INFO("command: %s %s", Narrow(cmd).c_str(), Narrow(arg).c_str());
        if (cmd == L"obj.reload")
        {
            LoadRules();
            ResolveTargets();
            ApplyData("obj.reload");
            ApplyLive("obj.reload");
        }
        else if (cmd == L"obj.apply")
        {
            ResolveTargets();
            ApplyData("obj.apply");
            ApplyLive("obj.apply");
        }
        else if (cmd == L"obj.classes") ListClasses(arg);
        else NRN_WARN("unknown command - obj.classes [text] | obj.apply | obj.reload");
        return true;
    }
} // namespace

class NorseObjectSwap : public RC::CppUserModBase
{
  public:
    NorseObjectSwap()
    {
        ModName = L"NorseObjectSwap";
        ModVersion = L"" NOS_VERSION;
        ModDescription = L"Swap the class an actor spawns as, or remove it, by rule: in data and on actors as they appear.";
        ModAuthors = L"ApocryphaRealm";
        for (auto& s : g_nopVtbl) s = reinterpret_cast<void*>(&NopFn);
        g_errOut.vtbl = g_nopVtbl;
        g_mod = ModDir();
        std::wstring dir;
        for (const auto& raw : ReadLines(g_mod / "NorseObjectSwap.ini"))
        {
            std::wstring l = Trim(raw);
            if (l.rfind(L"sLogDir=", 0) == 0) dir = Trim(l.substr(8));
        }
        Log::Get().Open((dir.empty() ? g_mod : fs::path(dir)) / "NorseObjectSwap.log", 2);
        Log::Get().Write(Level::Error, "NorseObjectSwap v%s", NOS_VERSION);
        LoadRules();
    }

    auto on_unreal_init() -> void override
    {
        try
        {
            RC::Unreal::Hook::RegisterInitGameStatePostCallback([](RC::Unreal::AGameModeBase*) {
                if (g_swapped || g_removed) NRN_INFO("last map: %d swapped, %d removed", g_swapped, g_removed);
                g_swapped = g_removed = 0;
                g_pending.clear();
                g_ours.clear();
                try
                {
                    ResolveTargets();
                    RecheckWrites("map load"); // before the pass: it exports first, and a freed pointer would fault
                    ApplyData("map load");
                }
                catch (...)
                {
                    NRN_ERROR("map-load data pass threw");
                }
            });
            RC::Unreal::Hook::RegisterBeginPlayPostCallback([](RC::Unreal::AActor* a) {
                auto* o = reinterpret_cast<UObject*>(a);
                if (g_rules.empty() || !o || g_ours.count(o)) return;
                try
                {
                    const Rule* r = nullptr;
                    const Pair* p = nullptr;
                    if (Match(o, r, p)) g_pending.push_back({o, IndexOf(o), r, p});
                }
                catch (...)
                {
                }
            });
            RC::Unreal::Hook::RegisterEngineTickPreCallback([](RC::Unreal::UEngine*, float) { Tick(); });
            RC::Unreal::Hook::RegisterProcessConsoleExecCallback(
                    [](UObject*, const wchar_t* cmd, RC::Unreal::FOutputDevice&, UObject*) -> bool {
                        if (!cmd || _wcsnicmp(cmd, L"obj.", 4) != 0) return false;
                        static ULONGLONG last = 0;
                        static std::wstring lastCmd;
                        ULONGLONG now = GetTickCount64();
                        if (lastCmd == cmd && now - last < 250) return true;
                        last = now;
                        lastCmd = cmd;
                        return RunCommand(cmd);
                    });
        }
        catch (const std::exception& e)
        {
            NRN_ERROR("callback registration failed: %s", e.what());
        }
    }
};

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod()
{
    return new NorseObjectSwap();
}

extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}
