// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "MapCatalog.hpp"
#include "MapMetadata.hpp"
#include "SettingsModel.hpp"

#include <string>
#include <functional>
#include <vector>

// Device-independent state, text and geometry of the in-game map selector: a paged list of the installed
// maps beside a preview of the selected one.
namespace MapModel {

using SettingsModel::Language;

// Name under which the window registers the selected map's preview for its hint template.
inline constexpr const wchar_t *preview_bitmap = L"MapPreview";

// Window geometry in pixels, relative to the window's top-left corner.
struct Layout {
    static constexpr int width = 580;
    static constexpr int title_y = 24;
    static constexpr int first_row_y = 50;
    static constexpr int row_height = 20;
    static constexpr int visible_rows = 14;
    static constexpr int list_x = 55;
    static constexpr int list_width = 200;
    static constexpr int pager_y = 332;
    static constexpr int previous_x = list_x;
    static constexpr int next_x = list_x + list_width - 20;
    static constexpr int pager_width = 20;
    static constexpr int preview_x = 275;
    static constexpr int preview_y = first_row_y;
    static constexpr int preview_width = 140;
    static constexpr int preview_height = 130;
    static constexpr int bonus_x = 425;
    static constexpr int bonus_y = 70;
    static constexpr int detail_width = 280;
    static constexpr int caption_y = 184;
    static constexpr int description_y = 244;
    static constexpr int description_rows = 5;
    static constexpr int description_scroll_y = 344;
    static constexpr int legend_y = 365;
    static constexpr int error_y = 365;
    static constexpr int buttons_y = 395;
    static constexpr int content_height = 448;
};

struct Hit {
    int index = -1;  // Map under the point, or -1.
    int bonus = -1;
    int scroll = 0;
    int action = 0; // 1 Start, -1 Cancel.
    int page = 0;    // -1 or +1 when a pager arrow was hit.
};

enum class Error { None, Unreadable, Save };

class Window {
public:
    // Selects the current map when it is listed, otherwise the first map.
    Window(std::vector<MapCatalog::Map> maps, const std::wstring &current, Language language,
           const MapMetadata::Catalog &metadata = {}, BattleChoice::Bonus bonus = BattleChoice::Bonus::None);

    const std::vector<MapCatalog::Map> &maps() const { return m_Maps; }
    int selected() const { return m_Selected; }  // -1 when the list is empty.
    const MapCatalog::Map *selected_map() const;
    int first_visible() const { return m_Selected < 0 ? 0 : m_Selected / Layout::visible_rows * Layout::visible_rows; }
    bool is_current(int index) const { return index == m_Current; }

    // Each returns true when the selection changed; the list does not wrap.
    bool select(int index);
    bool move(int delta);
    bool page(int delta);

    void set_error(Error error) { m_Error = error; }
    Error error() const { return m_Error; }

    std::wstring display_name(int index) const;
    std::wstring characteristics() const;
    std::wstring factions() const;
    const std::vector<std::wstring> &description() const { return m_Description; }
    // The UI supplies actual font widths; tests and non-rendering callers use conservative columns.
    void set_text_measure(std::function<size_t(std::wstring_view)> measure);
    int description_offset() const { return m_DescriptionOffset; }
    bool scroll_description(int delta);
    BattleChoice::Bonus bonus() const { return m_Bonus; }
    bool select_bonus(int index);
    std::wstring bonus_label(int index) const;
    std::wstring bonus_effect() const;
    std::wstring start_label() const;
    std::wstring cancel_label() const;
    std::wstring title() const;
    std::wstring legend() const;
    std::wstring pager() const;  // "page/pages"
    std::wstring caption() const;  // Selected map name, marked when it is a loose file.
    std::wstring missing_preview() const;
    std::wstring empty_text() const;
    std::wstring error_text() const;

private:
    std::vector<MapCatalog::Map> m_Maps;
    int m_Selected = -1;
    int m_Current = -1;
    Language m_Language;
    Error m_Error = Error::None;
    std::vector<MapMetadata::Entry> m_Metadata;
    int m_DescriptionOffset = 0;
    BattleChoice::Bonus m_Bonus;
    std::function<size_t(std::wstring_view)> m_TextMeasure;
    std::vector<std::wstring> m_Description;
    void wrap_selected_description();
    const MapMetadata::Entry *metadata() const;

    const wchar_t *text(const wchar_t *english, const wchar_t *russian) const;
};

int page_count(const Window &window);
Hit hit(const Window &window, int x, int y);

// Size of a width x height image scaled to fit the preview box (by default this window's) with its aspect
// ratio kept; zero for an empty image.
SettingsModel::Resolution preview_size(int width, int height);
SettingsModel::Resolution preview_size(int width, int height, SettingsModel::Resolution box);

std::vector<std::wstring> wrap_description(const std::wstring &text, size_t columns = 34);
std::vector<std::wstring> wrap_description(const std::wstring &text, size_t width,
                                          const std::function<size_t(std::wstring_view)> &measure);

// CMatrixHint template: the settings frame, text actions, the visible page of names,
// the pager, and the preview bitmap centred in its box, or a placeholder when preview has no size.
std::wstring hint_template(const Window &window, SettingsModel::Resolution preview);

}  // namespace MapModel
