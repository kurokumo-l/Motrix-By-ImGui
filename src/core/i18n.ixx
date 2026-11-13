export module motrix.core:i18n;

import std;

namespace motrix::core {

export enum class Locale {
    EnUS = 0,
    ZhCN = 1,
};

export class Translator {
public:
    explicit Translator(Locale initial = Locale::EnUS) noexcept : locale_(initial) {}

    void SetLocale(Locale locale) noexcept { locale_ = locale; }
    [[nodiscard]] Locale CurrentLocale() const noexcept { return locale_; }

    [[nodiscard]] std::string_view Tr(std::string_view key) const noexcept;

private:
    Locale locale_ = Locale::EnUS;
};

}  // namespace motrix::core
