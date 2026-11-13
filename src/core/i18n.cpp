module motrix.core;

import std;

namespace motrix::core {

namespace {

struct TranslationEntry {
    std::string_view key;
    std::string_view enUs;
    std::string_view zhCn;
};

constexpr TranslationEntry kEntries[] = {
    {"app.tasks", "Tasks", "任务列表"},
    {"app.preferences", "Preferences", "偏好设置"},
    {"app.add_task", "Add Task", "新建任务"},
    {"app.about", "About Motrix", "关于 Motrix"},
    {"task.active", "Downloading", "下载中"},
    {"task.waiting", "Waiting", "等待中"},
    {"task.stopped", "Stopped", "已停止"},
    {"task.empty", "There are no current tasks", "当前没有下载任务"},
    {"task.refresh_list", "Refresh Task List", "刷新任务列表"},
    {"task.resume_all", "Resume All Tasks", "恢复所有任务"},
    {"task.pause_all", "Pause All Tasks", "暂停所有任务"},
    {"task.purge_record", "Purge Task Record", "清除下载记录"},
    {"task.delete_selected", "Delete Selected Tasks", "移除选中的任务"},
    {"task.new_task", "New Task", "新建任务"},
    {"task.uri_tips",
     "Please enter one download link per line (HTTP/HTTPS/Magnet):",
     "请添加多个下载链接，每行一个（支持 HTTP / HTTPS / Magnet）："},
    {"task.rename_label", "Rename:", "重命名:"},
    {"task.rename_placeholder", "Optional", "选填"},
    {"task.save_to", "Save to:", "存储路径:"},
    {"task.splits", "Splits:", "分片数:"},
    {"app.cancel", "Cancel", "取 消"},
    {"app.submit", "Submit", "提 交"},
    {"app.close", "Close", "关 闭"},
    {"prefs.appearance", "Appearance:", "外观:"},
    {"prefs.language", "Language:", "语言:"},
    {"task.remaining", "Remaining", "剩余"},
    {"task.pause", "Pause Task", "暂停任务"},
    {"task.resume", "Resume Task", "恢复任务"},
    {"task.stop_seeding", "Stop Seeding", "停止做种"},
    {"task.restart", "Restart", "重新下载"},
    {"task.delete", "Delete Task", "移除任务"},
    {"task.remove_record", "Remove Task Record", "移除下载记录"},
    {"task.show_in_folder", "Show Task In Folder", "在文件夹中显示"},
    {"task.copy_link", "Copy Link", "拷贝链接"},
    {"task.more_info", "Task Details", "任务详情"},
    {"prefs.basic", "Basic", "基础设置"},
    {"prefs.advanced", "Advanced", "进阶设置"},
    {"prefs.lab", "Lab", "实验室"},
    {"prefs.theme_auto", "Auto", "自动"},
    {"prefs.theme_light", "Light", "浅色"},
    {"prefs.theme_dark", "Dark", "深色"},
    {"speed.max", "MAX", "MAX"},
};

}  // namespace

std::string_view Translator::Tr(std::string_view key) const noexcept {
    for (const auto& entry : kEntries) {
        if (entry.key == key) {
            return locale_ == Locale::ZhCN ? entry.zhCn : entry.enUs;
        }
    }
    return key;
}

}  // namespace motrix::core
