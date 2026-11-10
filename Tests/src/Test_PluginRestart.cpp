#include "PluginRestart.hpp"
#include "PluginRestartSnapshot.hpp"
#include <Axle/Path.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <windows.h>
#define CHECK(condition) do { if (!(condition)) { std::cerr << "FAIL " << __LINE__ << ": " << #condition << '\n'; return 1; } } while (false)
int wmain(int argc, wchar_t** argv) {
    if ((argc == 5 || argc == 6) && (std::wstring(argv[1]) == L"--product" || std::wstring(argv[1]) == L"--product-fault" || std::wstring(argv[1]) == L"--product-rejected")) {
        Axle::PluginRestart restart; std::string error;
        const bool fault = std::wstring(argv[1]) == L"--product-fault";
        const bool rejected = std::wstring(argv[1]) == L"--product-rejected";
        const std::wstring backend=argc==6?argv[5]:L"dx12"; CHECK(backend==L"dx12"||backend==L"vulkan");
        std::vector<std::wstring> arguments{L"--project=" + std::filesystem::absolute(argv[3]).wstring(),
            L"--rhi="+backend, L"--no-fallback", L"--rhi-validation", L"--expected-plugin-plan=" + std::wstring(argv[4]), L"--verify-restart-exit"};
        const auto root = std::filesystem::absolute(argv[3]).parent_path();
        std::ifstream index(root/"Intermediate/Plugins"/(Axle::PathToUtf8(argv[4])+".generation"));
        const std::string selected((std::istreambuf_iterator<char>(index)),{});
        Axle::PluginRestartSnapshot snapshot; std::vector<std::string> modules;
        if (!selected.empty()) {
            for(const auto& file : std::filesystem::directory_iterator(Axle::PathFromUtf8(selected)))
                if(file.path().extension()==".receipt") modules.push_back(Axle::PathToUtf8(file.path().stem().stem()));
        }
        CHECK(Axle::CapturePluginRestartSnapshot(Axle::PathFromUtf8(selected),modules,snapshot,error));
        arguments.push_back(L"--expected-plugin-generation="+(snapshot.Generation.empty()?L"none":snapshot.Generation.wstring()));
        arguments.push_back(L"--expected-plugin-receipts="+Axle::PathFromUtf8(snapshot.ReceiptSet).wstring());
        if (fault) arguments.push_back(L"--verify-restart-frame-failure");
        CHECK(restart.Launch(argv[2], arguments, error, std::chrono::seconds(30)));
        auto state = Axle::PluginRestart::Result::Waiting;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40);
        while (state == Axle::PluginRestart::Result::Waiting && std::chrono::steady_clock::now() < deadline) {
            state = restart.Poll(error); std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (fault || rejected) {
            CHECK(state == Axle::PluginRestart::Result::Failed);
            CHECK(error.find("(exit 1)") != std::string::npos);
            std::cout << "Native Editor rejected candidate with exit 1: parent remained active PASS\n"; return 0;
        }
        CHECK(state == Axle::PluginRestart::Result::Committed);
        unsigned long code = 1;
        while (!restart.CandidateExited(code) && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(restart.CandidateExited(code) && code == 0);
        std::cout << "Native Editor plugin startup, five completed frames, restart acknowledgement and clean teardown PASS\n"; return 0;
    }
    if (argc > 1) {
        std::string channel; unsigned long parent = 0;
        for (int i = 1; i < argc; ++i) {
            const std::wstring argument = argv[i];
            if (argument.starts_with(L"--restart-channel=")) channel.assign(argument.begin() + 18, argument.end());
            if (argument.starts_with(L"--restart-parent=")) parent = std::stoul(argument.substr(17));
        }
        Axle::PluginRestartChild child; std::string error;
        CHECK(child.Connect(channel, parent, error));
        if (std::wstring(argv[1]) == L"--die") return 7;
        if (std::wstring(argv[1]) == L"--hang") { std::this_thread::sleep_for(std::chrono::seconds(10)); return 9; }
        CHECK(std::wstring(argv[2]) == L"Unicode \u0111\u01b0\u1eddng / spaces / quotes \" / trailing\\");
        CHECK(child.Ready(error));
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < end) {
            const auto state = child.Poll(error);
            if (state == Axle::PluginRestartChild::Result::Committed) return 0;
            if (state == Axle::PluginRestartChild::Result::Failed) return 2;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return 3;
    }
    const auto executable = std::filesystem::absolute(argv[0]); std::string error;
    const auto wait = [&](Axle::PluginRestart& restart) {
        auto state = Axle::PluginRestart::Result::Waiting;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (state == Axle::PluginRestart::Result::Waiting && std::chrono::steady_clock::now() < end) {
            state = restart.Poll(error); std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return state;
    };
    Axle::PluginRestart restart;
    CHECK(!restart.Launch(executable.parent_path() / "missing-editor.exe", {}, error));
    CHECK(!restart.IsWaiting());
    CHECK(restart.Launch(executable, {L"--die"}, error));
    CHECK(wait(restart) == Axle::PluginRestart::Result::Failed);
    CHECK(error.find("exit 7") != std::string::npos);
    CHECK(restart.Launch(executable, {L"--hang"}, error, std::chrono::milliseconds(100)));
    CHECK(wait(restart) == Axle::PluginRestart::Result::Failed);
    CHECK(error.find("timed out") != std::string::npos);
    CHECK(restart.Launch(executable, {L"--hang"}, error));
    CHECK(!restart.Launch(executable, {L"--die"}, error));
    restart.Cancel(); CHECK(!restart.IsWaiting());
    CHECK(restart.Launch(executable, {L"--ready", L"Unicode \u0111\u01b0\u1eddng / spaces / quotes \" / trailing\\"}, error));
    CHECK(wait(restart) == Axle::PluginRestart::Result::Committed);
    CHECK(!restart.IsWaiting());
    std::cout << "Plugin restart: launch failure, child rejection, timeout, cancellation and healthy acknowledgement PASS\n";
    return 0;
}
