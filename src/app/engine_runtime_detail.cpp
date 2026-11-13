module motrix.app;

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;
import :engine_runtime;

namespace motrix::app {

void EngineRuntime::SetDetailCallback(DetailCallback cb) {
    std::scoped_lock lock(detailMutex_);
    onDetail_ = std::move(cb);
}

void EngineRuntime::SetDetailGid(std::string gid) {
    {
        std::scoped_lock lock(detailMutex_);
        detailGid_ = std::move(gid);
    }
    Enqueue(EngineWorkCommand{.kind = EngineWorkCommand::Kind::FetchDetail});
}

void EngineRuntime::FetchAndPublishDetail(std::string_view gid) {
    if (gid.empty() || !rpc_ || !rpc_->IsConnected()) {
        return;
    }
    auto detail = rpc_->TellStatus(gid);
    if (!detail) {
        return;
    }
    std::vector<motrix::domain::TaskPeerView> peers;
    if (detail->task.taskType == motrix::domain::TaskType::BitTorrent) {
        if (auto peersRes = rpc_->GetPeers(gid); peersRes) {
            peers = std::move(*peersRes);
            for (auto& p : peers) {
                if (!p.seeder && detail->task.numPieces > 0) {
                    p.progress =
                        motrix::domain::BitfieldCompletion(p.bitfieldHex, detail->task.numPieces);
                }
            }
        }
    }
    DetailCallback cb;
    {
        std::scoped_lock lock(detailMutex_);
        cb = onDetail_;
    }
    if (cb) {
        cb(std::move(*detail), std::move(peers));
    }
}

}  // namespace motrix::app
