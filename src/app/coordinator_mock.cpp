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
                          bool zeroSpeed = false) {
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

void PopulateMockBtDetails(AppStateSnapshot& state) {
    using namespace motrix::domain;
    state.detailedTrackers = {
        "udp://tracker.opentrackr.org:1337/announce",
        "https://tracker.tamersunion.org:443/announce",
        "udp://open.stealth.si:80/announce",
    };
    state.detailedPeers = {
        TaskPeerView{"-qB4650-mock1",
                     "qBittorrent 4.6.5",
                     "185.125.190.58",
                     51413,
                     "ffff",
                     1420000ull,
                     84000ull,
                     false,
                     false,
                     true,
                     1.0f},
        TaskPeerView{"-TR4050-mock2",
                     "Transmission 4.0.5",
                     "91.189.91.157",
                     51414,
                     "ffc0",
                     680000ull,
                     124000ull,
                     false,
                     false,
                     false,
                     0.625f},
        TaskPeerView{"-DE2110-mock3",
                     "Deluge 2.1.1",
                     "203.0.113.42",
                     58846,
                     "f800",
                     310000ull,
                     0ull,
                     false,
                     true,
                     false,
                     0.3125f},
        TaskPeerView{"aria2/1.37.0",
                     "aria2",
                     "198.51.100.77",
                     6881,
                     "fff0",
                     950000ull,
                     52000ull,
                     false,
                     false,
                     false,
                     0.75f},
    };
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
    waitingTask.uploadLength = 148897792ull;
    waitingTask.connections = 19;
    waitingTask.numSeeders = 28;
    waitingTask.numPieces = 48;
    waitingTask.pieceLength = 26843546ull;
    waitingTask.bitfieldHex = "fff8c7a24e19";
    waitingTask.dir = "C:/Users/Public/Downloads";
    waitingTask.uri = "magnet:?xt=urn:btih:3b245504cf5f11bbdbe1201cea6a6bf45aee1bc0&dn=archlinux-2026."
                      "11.01-x86_64.iso";
    waitingTask.btInfo = TaskBittorrentInfo{
        .name = "archlinux-2026.11.01-x86_64",
        .infoHash = "3b245504cf5f11bbdbe1201cea6a6bf45aee1bc0",
        .creationDate = "2026-11-01 08:30:00",
        .comment = "Arch Linux Official ISO & Release Bundle",
        .creationTimestamp = 1793521800,
    };
    waitingTask.files = {
        TaskFileView{1,
                     "archlinux-2026.11.01-x86_64/archlinux-2026.11.01-x86_64.iso",
                     1170210816ull,
                     476053504ull,
                     true},
        TaskFileView{
            2, "archlinux-2026.11.01-x86_64/showcase-walkthrough.mp4", 88080384ull, 34955264ull, true},
        TaskFileView{3, "archlinux-2026.11.01-x86_64/release-soundtrack.flac", 25811968ull, 0ull, false},
        TaskFileView{4, "archlinux-2026.11.01-x86_64/cover-art.png", 4387020ull, 4387020ull, true},
    };
    waitingTask.filesTotal = waitingTask.files.size();
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
    auto eraseMockTask = [&](std::string_view gid) {
        std::erase_if(state.downloadingTasks, [&](const auto& t) { return t.gid == gid; });
        std::erase_if(state.waitingTasks, [&](const auto& t) { return t.gid == gid; });
        std::erase_if(state.stoppedTasks, [&](const auto& t) { return t.gid == gid; });
        std::erase(state.selectedTaskGids, gid);
        if (state.detailedTaskGid == gid) {
            state.detailedTaskGid.clear();
            state.detailedTask.reset();
            state.detailedTrackers.clear();
            state.detailedPeers.clear();
        }
    };
    if (const auto* removeCmd = std::get_if<RemoveTaskCmd>(&cmd)) {
        eraseMockTask(removeCmd->gid);
        PruneSelectionGids(state);
        return;
    }
    if (const auto* batchRemoveCmd = std::get_if<RemoveTasksCmd>(&cmd)) {
        for (const auto& gid : batchRemoveCmd->gids) {
            eraseMockTask(gid);
        }
        PruneSelectionGids(state);
        return;
    }
    if (const auto* selCmd = std::get_if<ApplyTaskFileSelectionCmd>(&cmd)) {
        const auto applyTo = [&](std::vector<motrix::domain::DownloadTaskView>& list) {
            for (auto& t : list) {
                if (t.gid != selCmd->gid) {
                    continue;
                }
                for (auto& f : t.files) {
                    f.selected = std::ranges::find(selCmd->selectedIndices, f.index)
                                 != selCmd->selectedIndices.end();
                }
            }
        };
        applyTo(state.downloadingTasks);
        applyTo(state.waitingTasks);
        applyTo(state.stoppedTasks);
        RefreshMockDetail(state);
        return;
    }
    HandleBulkAndSubmit(state, cmd);
}

void AppCoordinator::RefreshMockDetail(AppStateSnapshot& state) {
    using namespace motrix::domain;
    if (state.detailedTaskGid.empty()) {
        state.detailedTask.reset();
        state.detailedTrackers.clear();
        state.detailedPeers.clear();
        return;
    }

    const DownloadTaskView* found = FindTaskByGid(state, state.detailedTaskGid);
    if (!found) {
        state.detailedTask.reset();
        state.detailedTrackers.clear();
        state.detailedPeers.clear();
        return;
    }

    state.detailedTask = *found;
    if (found->taskType == TaskType::BitTorrent) {
        PopulateMockBtDetails(state);
    } else {
        state.detailedTrackers.clear();
        state.detailedPeers.clear();
    }
}

void AppCoordinator::HandleBulkAndSubmit(AppStateSnapshot& state, const Command& cmd) {
    if (std::holds_alternative<ResetSessionCmd>(cmd)) {
        state.downloadingTasks.clear();
        state.waitingTasks.clear();
        state.stoppedTasks.clear();
        state.selectedTaskGid.clear();
        state.selectedTaskGids.clear();
        state.detailedTaskGid.clear();
        state.detailedTask.reset();
        state.detailedTrackers.clear();
        state.detailedPeers.clear();
        return;
    }
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
    if (const auto* addUriCmd = std::get_if<SubmitAddUriCmd>(&cmd)) {
        RecordHistoryDirectory(state.prefs, addUriCmd->dir);
        for (const auto& uri : addUriCmd->uris) {
            motrix::domain::DownloadTaskView task{};
            task.gid = std::format("{:016x}", nextLocalGid_++);
            task.name = !addUriCmd->outName.empty() ? addUriCmd->outName : uri;
            task.uri = uri;
            task.dir = addUriCmd->dir;
            task.status = motrix::domain::TaskStatus::Active;
            task.taskType = motrix::domain::IsMagnetUri(uri) ? motrix::domain::TaskType::BitTorrent
                                                             : motrix::domain::TaskType::Http;
            state.downloadingTasks.push_back(std::move(task));
        }
        state.activeModal = ActiveModal::None;
        if (addUriCmd->newTaskShowDownloading.value_or(state.prefs.newTaskShowDownloading)) {
            state.activeCategory = motrix::domain::TaskCategory::Downloading;
            state.activeSection = 0;
        }
        logger_.Info("app", "Submitted {} URI(s) (mock)", addUriCmd->uris.size());
        return;
    }
    if (const auto* addTorCmd = std::get_if<SubmitAddTorrentCmd>(&cmd)) {
        RecordHistoryDirectory(state.prefs, addTorCmd->dir);
        motrix::domain::DownloadTaskView task{};
        task.gid = std::format("{:016x}", nextLocalGid_++);
        task.name = !addTorCmd->outName.empty() ? addTorCmd->outName : "torrent-task";
        task.dir = addTorCmd->dir;
        task.status = motrix::domain::TaskStatus::Active;
        task.taskType = motrix::domain::TaskType::BitTorrent;
        state.downloadingTasks.push_back(std::move(task));
        state.activeModal = ActiveModal::None;
        if (addTorCmd->newTaskShowDownloading.value_or(state.prefs.newTaskShowDownloading)) {
            state.activeCategory = motrix::domain::TaskCategory::Downloading;
            state.activeSection = 0;
        }
        logger_.Info("app", "Submitted torrent '{}' (mock)", addTorCmd->outName);
    }
}

}  // namespace motrix::app
