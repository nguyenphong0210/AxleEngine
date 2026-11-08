#include "DocumentGuard.hpp"
#include <iostream>
#include <stdexcept>

#define CHECK(value) do { if (!(value)) { std::cerr << "FAIL " << __LINE__ << ": " << #value << '\n'; return 1; } } while(false)
int main() {
    Axle::DocumentGuard guard;
    bool sceneDirty = true, privateDirty = true, failPrivate = true;
    int savedScene = 0, savedPrivate = 0, actions = 0;
    guard.SetCollector([&] { return std::vector<Axle::GuardedDocument>{
        {"scene", "Scene", "Scene.axlescene", [&] { return sceneDirty; },
            [&](std::string&) { ++savedScene; sceneDirty = false; return true; },
            [&](std::string&) { sceneDirty = false; return true; }},
        {"private", "Assembly", "Part.axle", [&] { return privateDirty; },
            [&](std::string& error) { ++savedPrivate; if (failPrivate) { error = "write denied"; return false; } privateDirty = false; return true; },
            [&](std::string&) { privateDirty = false; return true; }}
    }; });
    std::function<void()> apply;
    apply = [&] { if (guard.Check("apply", "Apply plugin changes", apply)) ++actions; };
    apply(); CHECK(guard.IsPending() && actions == 0 && guard.Documents().size() == 2);
    CHECK(!guard.Check("restart", "Restart", [] {})); CHECK(guard.Title() == "Apply plugin changes");
    CHECK(!guard.Resolve(Axle::DocumentGuard::Choice::Save));
    CHECK(actions == 0 && guard.IsPending() && !sceneDirty && privateDirty && savedScene == 1 && savedPrivate == 1);
    CHECK(guard.Error().find("write denied") != std::string::npos);
    guard.Resolve(Axle::DocumentGuard::Choice::Cancel); CHECK(privateDirty && actions == 0 && !guard.IsPending());
    apply(); failPrivate = false; CHECK(guard.Resolve(Axle::DocumentGuard::Choice::Save));
    CHECK(actions == 1 && !sceneDirty && !privateDirty && !guard.IsPending());
    sceneDirty = privateDirty = true; apply(); CHECK(guard.Resolve(Axle::DocumentGuard::Choice::Discard));
    CHECK(actions == 2 && !sceneDirty && !privateDirty && savedScene == 1 && savedPrivate == 2);
    bool first = true, second = true;
    guard.SetCollector([&] { return std::vector<Axle::GuardedDocument>{
        {"a", "First buffer", "Same.axle", [&] { return first; }, [&](std::string&) { first = false; return true; }, [&](std::string&) { first = false; return true; }},
        {"b", "Second buffer", "./Same.axle", [&] { return second; }, [&](std::string&) { second = false; return true; }, [&](std::string&) { second = false; return true; }}
    }; });
    apply(); CHECK(!guard.Resolve(Axle::DocumentGuard::Choice::Save) && first && second && actions == 2);
    CHECK(guard.Error().find("same file") != std::string::npos);
    guard.Resolve(Axle::DocumentGuard::Choice::Cancel);
    guard.SetCollector([&] { return std::vector<Axle::GuardedDocument>{
        {"a", "Scene", "A.axlescene", [&] { return first; }, [&](std::string&) { first = false; return true; }, [&](std::string&) { first = false; return true; }},
        {"b", "Coupled document", "B.axle", [&] { return second; }, [&](std::string&) { second = false; first = true; return true; }, [&](std::string&) { second = false; return true; }}
    }; });
    apply(); CHECK(!guard.Resolve(Axle::DocumentGuard::Choice::Save) && first && guard.IsPending() && actions == 2);
    CHECK(guard.Resolve(Axle::DocumentGuard::Choice::Save) && actions == 3);
    first = false; second = true;
    apply();
    CHECK(!guard.Resolve(Axle::DocumentGuard::Choice::Save) && first && actions == 3);
    CHECK(guard.IsPending());
    CHECK(guard.Resolve(Axle::DocumentGuard::Choice::Save) && !first && actions == 4);
    first = true;
    CHECK(!guard.Check("throws", "Throwing continuation", [] { throw std::runtime_error("failed action"); }));
    CHECK(!guard.Resolve(Axle::DocumentGuard::Choice::Save));
    first = true;
    CHECK(!guard.Check("throws", "A new request", [] {}));
    guard.Resolve(Axle::DocumentGuard::Choice::Cancel);
    std::cout << "Document guard Save/Discard/Cancel, private save failure, conflicts and action ownership: PASS\n";
    return 0;
}
