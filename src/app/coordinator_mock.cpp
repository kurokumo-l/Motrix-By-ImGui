module motrix.app;

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;

namespace motrix::app {

namespace {

bool MoveTaskBetweenLists(std::vector<motrix::domain::DownloadTaskView>& from,
                          std::vector<motrix::domain::DownloadTaskView>& to,
                          std::string_view gid,
                          motrix::domain::TaskStatus newStatus,
                          bool zeroSpeed) {
    auto it = std::find_if(from.begin(), from.end(), [&](const auto& t) { return t.gid == gid; });
    if (it == from.end()) {
        return false;
    }
    it->status = newStatus;
    if (zeroSpeed) {
        it->downloadSpeed = 0;
        it->uploadSpeed = 0;
    }
    to.push_back(std::move(*it));
    from.erase(it);
    return true;
}

}  // namespace

void AppCoordinator::SeedPreviewTasks(AppStateSnapshot& state) {
    using namespace motrix::domain;
    state.engineState = EngineConnectionState::PreviewMock;
    state.engineInfo = motrix::engine::EngineVersionInfo{
        .kind = motrix::engine::EngineKind::Aria2Next,
        .product = "aria2-next",
        .version = "3.1.0",
        .enabledFeatures = {"Async DNS", "BitTorrent", "HTTPS", "SQLiteState"},
    };

    DownloadTaskView ubuntu{};
    ubuntu.gid = "0001000000000001";
    ubuntu.name = "ubuntu-18.04.1.0-live-server-amd64.iso";
    ubuntu.status = TaskStatus::Active;
    ubuntu.taskType = TaskType::Http;
    ubuntu.totalLength = 851443712ull;
    ubuntu.completedLength = 371510476ull;
    ubuntu.downloadSpeed = 19922944ull;
    ubuntu.connections = 16;
    ubuntu.dir = "C:/Users/Public/Downloads";
    ubuntu.uri = "https://releases.ubuntu.com/18.04/ubuntu-18.04.1.0-live-server-amd64.iso";
    state.downloadingTasks.push_back(std::move(ubuntu));

    DownloadTaskView waitingTask{};
    waitingTask.gid = "0001000000000002";
    waitingTask.name = "archlinux-2026.11.01-x86_64.iso";
    waitingTask.status = TaskStatus::Paused;
    waitingTask.taskType = TaskType::BitTorrent;
    waitingTask.totalLength = 1288490188ull;
    waitingTask.completedLength = 515396075ull;
    waitingTask.dir = "C:/Users/Public/Downloads";
    state.waitingTasks.push_back(std::move(waitingTask));

    DownloadTaskView stoppedTask{};
    stoppedTask.gid = "0001000000000003";
    stoppedTask.name = "Motrix-Setup-1.8.19.exe";
    stoppedTask.status = TaskStatus::Complete;
    stoppedTask.taskType = TaskType::Http;
    stoppedTask.totalLength = 83571507ull;
    stoppedTask.completedLength = 83571507ull;
    stoppedTask.dir = "C:/Users/Public/Downloads";
    state.stoppedTasks.push_back(std::move(stoppedTask));
}

void AppCoordinator::HandleTaskAction(AppStateSnapshot& state, const Command& cmd) {
    if (const auto* c = std::get_if<PauseTaskCmd>(&cmd)) {
        MoveTaskBetweenLists(state.downloadingTasks,
                             state.waitingTasks,
                             c->gid,
                             motrix::domain::TaskStatus::Paused,
                             true);
        return;
    }
    if (const auto* c = std::get_if<ResumeTaskCmd>(&cmd)) {
        MoveTaskBetweenLists(state.waitingTasks,
                             state.downloadingTasks,
                             c->gid,
                             motrix::domain::TaskStatus::Active,
                             false);
        return;
    }
    if (const auto* c = std::get_if<RestartTaskCmd>(&cmd)) {
        MoveTaskBetweenLists(state.stoppedTasks,
                             state.downloadingTasks,
                             c->gid,
                             motrix::domain::TaskStatus::Active,
                             false);
        return;
    }
    if (const auto* c = std::get_if<RemoveTaskCmd>(&cmd)) {
        std::erase_if(state.downloadingTasks, [&](const auto& t) { return t.gid == c->gid; });
        std::erase_if(state.waitingTasks, [&](const auto& t) { return t.gid == c->gid; });
        std::erase_if(state.stoppedTasks, [&](const auto& t) { return t.gid == c->gid; });
        if (state.detailedTaskGid == c->gid) {
            state.detailedTaskGid.clear();
        }
        return;
    }
    HandleBulkAndSubmit(state, cmd);
}

void AppCoordinator::HandleBulkAndSubmit(AppStateSnapshot& state, const Command& cmd) {
    if (std::holds_alternative<PurgeStoppedCmd>(cmd)) {
        state.stoppedTasks.clear();
        return;
    }
    if (std::holds_alternative<PauseAllCmd>(cmd)) {
        for (auto& t : state.downloadingTasks) {
            t.status = motrix::domain::TaskStatus::Paused;
            t.downloadSpeed = 0;
            t.uploadSpeed = 0;
            state.waitingTasks.push_back(std::move(t));
        }
        state.downloadingTasks.clear();
        return;
    }
    if (std::holds_alternative<ResumeAllCmd>(cmd)) {
        for (auto& t : state.waitingTasks) {
            t.status = motrix::domain::TaskStatus::Active;
            state.downloadingTasks.push_back(std::move(t));
        }
        state.waitingTasks.clear();
        return;
    }
    if (const auto* c = std::get_if<SubmitAddUriCmd>(&cmd)) {
        for (const auto& uri : c->uris) {
            motrix::domain::DownloadTaskView task{};
            task.gid = std::format("{:016x}", nextLocalGid_++);
            task.name = !c->outName.empty() ? c->outName : uri;
            task.uri = uri;
            task.dir = c->dir;
            task.status = motrix::domain::TaskStatus::Active;
            task.taskType = motrix::domain::IsMagnetUri(uri) ? motrix::domain::TaskType::BitTorrent
                                                             : motrix::domain::TaskType::Http;
            state.downloadingTasks.push_back(std::move(task));
        }
        state.activeModal = ActiveModal::None;
        if (state.prefs.newTaskShowDownloading) {
            state.activeCategory = motrix::domain::TaskCategory::Downloading;
            state.activeSection = 0;
        }
        logger_.Info("app", "Submitted {} URI(s) (mock)", c->uris.size());
    }
}

}  // namespace motrix::app
