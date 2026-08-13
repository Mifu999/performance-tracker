#include "StatsPopup.hpp"

#include "../Storage.hpp"
#include "../SystemStats.hpp"
#include "../Tracker.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/file.hpp>

#include <algorithm>
#include <functional>

using namespace geode::prelude;
using namespace pt;

namespace {
    constexpr float kPopupW    = 460.f;
    constexpr float kPopupH    = 300.f;
    constexpr float kContentW  = 430.f;
    constexpr float kContentH  = 156.f;
    constexpr float kRowHeight = 19.f;

    CCMenu* makeMenu(CCNode* parent, CCSize size, Anchor anchor, CCPoint offset) {
        auto menu = CCMenu::create();
        menu->setContentSize(size);
        menu->ignoreAnchorPointForPosition(false);
        menu->setAnchorPoint({ .5f, .5f });
        parent->addChildAtPosition(menu, anchor, offset);
        return menu;
    }

    CCMenuItemSpriteExtra* makeButton(
        CCMenu* menu, CCObject* target, SEL_MenuHandler sel,
        char const* text, int width, bool active, int tag, float x, float y
    ) {
        auto spr = ButtonSprite::create(
            text, width, true, "bigFont.fnt",
            active ? "GJ_button_01.png" : "GJ_button_04.png",
            22.f, .45f
        );
        spr->setScale(.72f);

        auto btn = CCMenuItemSpriteExtra::create(spr, target, sel);
        btn->setTag(tag);
        btn->setPosition({ x, y });
        menu->addChild(btn);
        return btn;
    }

    std::string truncate(std::string s, size_t max) {
        if (s.size() <= max) return s;
        s.resize(max > 1 ? max - 1 : 1);
        s += "\u2026";
        return s;
    }

    ccColor3B fpsColor(float fps) {
        if (fps <= 0.f)   return { 190, 190, 190 };
        if (fps >= 100.f) return { 130, 240, 150 };
        if (fps >= 55.f)  return { 180, 240, 150 };
        if (fps >= 30.f)  return { 255, 215, 120 };
        return { 255, 130, 130 };
    }

    // -----------------------------------------------------------------
    // Custom range picker
    // -----------------------------------------------------------------
    class RangePopup : public Popup {
    public:
        using Callback = std::function<void(std::time_t, std::time_t)>;

        static RangePopup* create(std::time_t from, std::time_t to, Callback cb) {
            auto ret = new RangePopup();
            if (ret->init(from, to, std::move(cb))) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

    protected:
        bool init(std::time_t from, std::time_t to, Callback cb) {
            if (!Popup::init(300.f, 180.f)) return false;

            m_callback = std::move(cb);
            this->setTitle("Custom range");

            auto hint = CCLabelBMFont::create("Dates are inclusive (YYYY-MM-DD)", "chatFont.fnt");
            hint->setScale(.4f);
            hint->setOpacity(160);
            m_mainLayer->addChildAtPosition(hint, Anchor::Top, ccp(0, -44));

            auto fromLabel = CCLabelBMFont::create("From", "bigFont.fnt");
            fromLabel->setScale(.35f);
            m_mainLayer->addChildAtPosition(fromLabel, Anchor::Center, ccp(-95, 25));

            m_fromInput = TextInput::create(150.f, "YYYY-MM-DD", "chatFont.fnt");
            m_fromInput->setFilter("0123456789-");
            m_fromInput->setMaxCharCount(10);
            m_fromInput->setString(gd::string(dayKey(from)));
            m_mainLayer->addChildAtPosition(m_fromInput, Anchor::Center, ccp(20, 25));

            auto toLabel = CCLabelBMFont::create("To", "bigFont.fnt");
            toLabel->setScale(.35f);
            m_mainLayer->addChildAtPosition(toLabel, Anchor::Center, ccp(-95, -5));

            m_toInput = TextInput::create(150.f, "YYYY-MM-DD", "chatFont.fnt");
            m_toInput->setFilter("0123456789-");
            m_toInput->setMaxCharCount(10);
            m_toInput->setString(gd::string(dayKey(to)));
            m_mainLayer->addChildAtPosition(m_toInput, Anchor::Center, ccp(20, -5));

            auto applySpr = ButtonSprite::create("Apply", "goldFont.fnt", "GJ_button_01.png", .8f);
            applySpr->setScale(.8f);
            auto applyBtn = CCMenuItemSpriteExtra::create(
                applySpr, this, menu_selector(RangePopup::onApply)
            );
            m_buttonMenu->addChildAtPosition(applyBtn, Anchor::Bottom, ccp(0, 30));

            return true;
        }

        void onApply(CCObject*) {
            std::time_t from = 0, to = 0;
            std::string fromStr = m_fromInput->getString().c_str();
            std::string toStr   = m_toInput->getString().c_str();

            if (!parseDayKey(fromStr, from) || !parseDayKey(toStr, to)) {
                FLAlertLayer::create(
                    "Invalid date",
                    "Please use the <cy>YYYY-MM-DD</c> format for both dates.",
                    "OK"
                )->show();
                return;
            }
            if (to < from) std::swap(from, to);

            if (m_callback) m_callback(from, to);
            this->onClose(nullptr);
        }

        TextInput* m_fromInput = nullptr;
        TextInput* m_toInput   = nullptr;
        Callback   m_callback;
    };
}

// ---------------------------------------------------------------------
// StatsPopup
// ---------------------------------------------------------------------

StatsPopup* StatsPopup::create() {
    auto ret = new StatsPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool StatsPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    this->setTitle("Performance Stats");

    // make sure the window in progress is on disk before we read anything
    Tracker::get().closeAndFlush();

    auto now = std::time(nullptr);
    m_customFrom = dayStart(now);
    m_customTo   = dayStart(now);

    m_rangeLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_rangeLabel->setScale(.4f);
    m_rangeLabel->setOpacity(175);
    m_mainLayer->addChildAtPosition(m_rangeLabel, Anchor::Top, ccp(0, -64));

    for (int i = 0; i < 4; ++i) {
        auto node = CCNode::create();
        node->setContentSize({ kContentW, kContentH });
        node->setAnchorPoint({ .5f, .5f });
        node->setVisible(i == 0);
        m_mainLayer->addChildAtPosition(node, Anchor::Center, ccp(0, -26));
        m_tabNodes[i] = node;
    }

    // bottom actions
    auto actions = makeMenu(m_mainLayer, { 440.f, 26.f }, Anchor::Bottom, ccp(0, 22));
    makeButton(actions, this, menu_selector(StatsPopup::onExport), "Export CSV", 90, false, 0, 90.f, 13.f);
    makeButton(actions, this, menu_selector(StatsPopup::onFolder), "Open folder", 95, false, 0, 195.f, 13.f);
    makeButton(actions, this, menu_selector(StatsPopup::onInfo), "?", 26, false, 0, 275.f, 13.f);

    this->updateButtons();
    this->applyRange();
    return true;
}

void StatsPopup::updateButtons() {
    // The range and tab rows are rebuilt so the selected one can use the
    // highlighted button texture.
    if (auto old = m_mainLayer->getChildByID("range-menu"_spr)) old->removeFromParent();
    if (auto old = m_mainLayer->getChildByID("tab-menu"_spr)) old->removeFromParent();

    auto rangeMenu = makeMenu(m_mainLayer, { 440.f, 26.f }, Anchor::Top, ccp(0, -44));
    rangeMenu->setID("range-menu"_spr);

    char const* rangeNames[] = { "Today", "7 days", "30 days", "All", "Custom" };
    m_rangeButtons.clear();
    for (int i = 0; i < 5; ++i) {
        float x = 440.f / 5.f * (static_cast<float>(i) + .5f);
        m_rangeButtons.push_back(makeButton(
            rangeMenu, this, menu_selector(StatsPopup::onRange),
            rangeNames[i], 74, static_cast<int>(m_range) == i, i, x, 13.f
        ));
    }

    auto tabMenu = makeMenu(m_mainLayer, { 440.f, 26.f }, Anchor::Top, ccp(0, -84));
    tabMenu->setID("tab-menu"_spr);

    char const* tabNames[] = { "Overview", "Graph", "Levels", "Sessions" };
    m_tabButtons.clear();
    for (int i = 0; i < 4; ++i) {
        float x = 440.f / 4.f * (static_cast<float>(i) + .5f);
        m_tabButtons.push_back(makeButton(
            tabMenu, this, menu_selector(StatsPopup::onTab),
            tabNames[i], 92, m_tab == i, i, x, 13.f
        ));
    }
}

void StatsPopup::applyRange() {
    auto now   = std::time(nullptr);
    auto today = dayStart(now);

    switch (m_range) {
        case Range::Today:
            m_from = today;
            break;
        case Range::Week:
            m_from = dayOffset(now, -6);
            break;
        case Range::Month:
            m_from = dayOffset(now, -29);
            break;
        case Range::All: {
            auto earliest = Storage::get().earliestDay();
            m_from = earliest != 0 ? earliest : today;
            break;
        }
        case Range::Custom:
            m_from = m_customFrom;
            break;
    }

    if (m_range == Range::Custom) {
        m_to = m_customTo + 86399; // end of the last selected day
    }
    else {
        m_to = now;
    }

    this->reload();
}

void StatsPopup::reload() {
    m_samples = Storage::get().query(m_from, m_to);
    m_agg     = aggregate(m_samples);

    std::string label;
    if (dayKey(m_from) == dayKey(m_to)) {
        label = fmt::format("{} \u2022 {} tracked", dayKey(m_from), formatDuration(m_agg.seconds));
    }
    else {
        label = fmt::format(
            "{} \u2192 {} \u2022 {} tracked",
            dayKey(m_from), dayKey(m_to), formatDuration(m_agg.seconds)
        );
    }
    m_rangeLabel->setString(label.c_str());

    for (auto* node : m_tabNodes) {
        if (node) node->removeAllChildren();
    }
    m_graph = nullptr;
    m_metricButtons.clear();

    this->buildOverview();
    this->buildGraph();
    this->buildLevels();
    this->buildSessions();
}

// ---------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------

CCNode* StatsPopup::makeStatRow(
    char const* label, std::string const& value, ccColor3B color, float width
) {
    auto row = CCNode::create();
    row->setContentSize({ width, kRowHeight });
    row->setAnchorPoint({ 0.f, .5f });

    auto name = CCLabelBMFont::create(label, "chatFont.fnt");
    name->setScale(.42f);
    name->setAnchorPoint({ 0.f, .5f });
    name->setPosition({ 0.f, kRowHeight / 2 });
    name->setOpacity(165);
    row->addChild(name);

    auto val = CCLabelBMFont::create(value.c_str(), "bigFont.fnt");
    val->setAnchorPoint({ 1.f, .5f });
    val->setPosition({ width, kRowHeight / 2 });
    val->setScale(.36f);
    val->setColor(color);

    // keep long values inside their column
    float maxW = width * .58f;
    if (val->getScaledContentSize().width > maxW) {
        val->setScale(.36f * maxW / val->getScaledContentSize().width);
    }
    row->addChild(val);

    return row;
}

void StatsPopup::buildOverview() {
    auto parent = m_tabNodes[0];
    if (!parent) return;

    if (m_agg.empty()) {
        auto none = CCLabelBMFont::create("No data recorded in this range", "chatFont.fnt");
        none->setScale(.55f);
        none->setOpacity(150);
        parent->addChildAtPosition(none, Anchor::Center, ccp(0, 0), false);
        return;
    }

    struct Row {
        char const* label;
        std::string value;
        ccColor3B   color;
    };

    std::vector<Row> left, right;

    left.push_back({ "Tracked time", formatDuration(m_agg.seconds), { 255, 255, 255 } });
    left.push_back({ "Average FPS", fmtFloat(m_agg.avgFps()), fpsColor(m_agg.avgFps()) });
    left.push_back({ "Average 1% low", fmtFloat(m_agg.avgLow1()), fpsColor(m_agg.avgLow1()) });
    left.push_back({
        "Best FPS",
        fmt::format("{} @ {}", fmtFloat(m_agg.fpsMax), formatClock(m_agg.fpsMaxAt)),
        { 130, 240, 150 }
    });
    left.push_back({
        "Worst FPS",
        fmt::format("{} @ {}", fmtFloat(m_agg.fpsMin), formatClock(m_agg.fpsMinAt)),
        { 255, 130, 130 }
    });
    left.push_back({ "Time under 60 FPS", fmt::format("{}%", fmtFloat(m_agg.pctBelow60())), { 255, 215, 120 } });
    left.push_back({ "Time under 30 FPS", fmt::format("{}%", fmtFloat(m_agg.pctBelow30())), { 255, 150, 150 } });
    left.push_back({
        "Under FPS target",
        m_agg.pctBelowTarget() < 0.f ? "-" : fmt::format("{}%", fmtFloat(m_agg.pctBelowTarget())),
        { 255, 200, 140 }
    });

    right.push_back({
        "Avg CPU (game)",
        m_agg.avgCpu() < 0.f ? "-" : fmt::format("{}%", fmtFloat(m_agg.avgCpu())),
        { 255, 190, 110 }
    });
    right.push_back({
        "Peak CPU (game)",
        m_agg.cpuMax < 0.f ? "-" : fmt::format("{}% @ {}", fmtFloat(m_agg.cpuMax), formatClock(m_agg.cpuMaxAt)),
        { 255, 170, 90 }
    });
    right.push_back({
        "Avg CPU (system)",
        m_agg.avgSys() < 0.f ? "-" : fmt::format("{}%", fmtFloat(m_agg.avgSys())),
        { 255, 150, 150 }
    });
    right.push_back({
        "Average RAM",
        m_agg.avgRam() < 0.f ? "-" : fmt::format("{} MB", fmtFloat(m_agg.avgRam(), 0)),
        { 130, 190, 255 }
    });
    right.push_back({
        "Peak RAM",
        m_agg.ramMax < 0 ? "-" : fmt::format("{} MB @ {}", m_agg.ramMax, formatClock(m_agg.ramMaxAt)),
        { 110, 170, 255 }
    });
    right.push_back({
        "In levels",
        formatDuration(
            m_agg.secondsByContext[static_cast<size_t>(Context::Playing)] +
            m_agg.secondsByContext[static_cast<size_t>(Context::Paused)]
        ),
        { 255, 255, 255 }
    });
    right.push_back({
        "In editor",
        formatDuration(m_agg.secondsByContext[static_cast<size_t>(Context::Editor)]),
        { 255, 255, 255 }
    });
    right.push_back({
        "Levels seen",
        fmt::format("{}", m_agg.levels.size()),
        { 255, 255, 255 }
    });

    float colW = kContentW / 2.f - 14.f;
    for (size_t i = 0; i < left.size(); ++i) {
        auto row = this->makeStatRow(left[i].label, left[i].value, left[i].color, colW);
        row->setPosition({ 4.f, kContentH - kRowHeight * (static_cast<float>(i) + .5f) - 2.f });
        parent->addChild(row);
    }
    for (size_t i = 0; i < right.size(); ++i) {
        auto row = this->makeStatRow(right[i].label, right[i].value, right[i].color, colW);
        row->setPosition({ kContentW / 2.f + 10.f, kContentH - kRowHeight * (static_cast<float>(i) + .5f) - 2.f });
        parent->addChild(row);
    }
}

void StatsPopup::buildGraph() {
    auto parent = m_tabNodes[1];
    if (!parent) return;

    m_graph = GraphNode::create({ kContentW - 4.f, kContentH - 30.f });
    m_graph->setPosition({ 2.f, 28.f });
    parent->addChild(m_graph);

    size_t columns = 90;
    m_graph->setData(buildSeries(m_samples, m_from, m_to, columns, m_metric), m_from, m_to, m_metric);

    auto menu = CCMenu::create();
    menu->setContentSize({ kContentW, 24.f });
    menu->ignoreAnchorPointForPosition(false);
    menu->setAnchorPoint({ .5f, .5f });
    menu->setPosition({ kContentW / 2.f, 12.f });
    parent->addChild(menu);

    char const* names[]   = { "FPS", "CPU game", "CPU sys", "RAM" };
    Metric      metrics[] = { Metric::Fps, Metric::CpuProc, Metric::CpuSystem, Metric::Ram };

    for (int i = 0; i < 4; ++i) {
        float x = kContentW / 4.f * (static_cast<float>(i) + .5f);
        m_metricButtons.push_back(makeButton(
            menu, this, menu_selector(StatsPopup::onMetric),
            names[i], 86, m_metric == metrics[i], static_cast<int>(metrics[i]), x, 12.f
        ));
    }
}

void StatsPopup::buildLevels() {
    auto parent = m_tabNodes[2];
    if (!parent) return;

    std::vector<LevelStat> levels;
    levels.reserve(m_agg.levels.size());
    for (auto const& [id, stat] : m_agg.levels) {
        if (stat.seconds >= 5.0) levels.push_back(stat);
    }
    std::sort(levels.begin(), levels.end(), [](LevelStat const& a, LevelStat const& b) {
        return a.avgFps() < b.avgFps();
    });

    if (levels.empty()) {
        auto none = CCLabelBMFont::create("No level recorded in this range", "chatFont.fnt");
        none->setScale(.55f);
        none->setOpacity(150);
        parent->addChildAtPosition(none, Anchor::Center, ccp(0, 0), false);
        return;
    }

    auto header = CCLabelBMFont::create("Heaviest levels first (avg / 1% low / worst / time)", "chatFont.fnt");
    header->setScale(.38f);
    header->setOpacity(150);
    header->setAnchorPoint({ .5f, 1.f });
    header->setPosition({ kContentW / 2.f, kContentH });
    parent->addChild(header);

    float listH = kContentH - 14.f;
    auto  list  = ScrollLayer::create(CCSize{ kContentW, listH });
    list->setPosition({ 0.f, 0.f });
    parent->addChild(list);

    float rowH  = 20.f;
    float total = std::max(listH, rowH * static_cast<float>(levels.size()));
    list->m_contentLayer->setContentSize({ kContentW, total });

    for (size_t i = 0; i < levels.size(); ++i) {
        auto const& lv = levels[i];

        auto row = CCNode::create();
        row->setContentSize({ kContentW, rowH });
        row->setPosition({ 0.f, total - rowH * (static_cast<float>(i) + 1.f) });

        if (i % 2 == 1) {
            auto bg = CCLayerColor::create({ 0, 0, 0, 40 }, kContentW, rowH);
            bg->setPosition({ 0.f, 0.f });
            row->addChild(bg, -1);
        }

        auto name = CCLabelBMFont::create(
            truncate(Storage::get().levelName(lv.id), 26).c_str(), "bigFont.fnt"
        );
        name->setScale(.32f);
        name->setAnchorPoint({ 0.f, .5f });
        name->setPosition({ 8.f, rowH / 2 });
        row->addChild(name);

        auto stats = CCLabelBMFont::create(
            fmt::format(
                "{} / {} / {} / {}",
                fmtFloat(lv.avgFps()), fmtFloat(lv.avgLow1()),
                fmtFloat(lv.fpsMin), formatDuration(lv.seconds)
            ).c_str(),
            "chatFont.fnt"
        );
        stats->setScale(.4f);
        stats->setAnchorPoint({ 1.f, .5f });
        stats->setPosition({ kContentW - 8.f, rowH / 2 });
        stats->setColor(fpsColor(lv.avgFps()));
        row->addChild(stats);

        list->m_contentLayer->addChild(row);
    }

    list->scrollToTop();
}

void StatsPopup::buildSessions() {
    auto parent = m_tabNodes[3];
    if (!parent) return;

    auto const& all = Storage::get().sessions();

    std::vector<SessionInfo> shown;
    for (auto const& s : all) {
        if (s.end >= m_from && s.start <= m_to && s.seconds > 0) shown.push_back(s);
    }
    std::reverse(shown.begin(), shown.end());

    if (shown.empty()) {
        auto none = CCLabelBMFont::create("No session in this range", "chatFont.fnt");
        none->setScale(.55f);
        none->setOpacity(150);
        parent->addChildAtPosition(none, Anchor::Center, ccp(0, 0), false);
        return;
    }

    auto header = CCLabelBMFont::create("Most recent first (duration / avg FPS)", "chatFont.fnt");
    header->setScale(.38f);
    header->setOpacity(150);
    header->setAnchorPoint({ .5f, 1.f });
    header->setPosition({ kContentW / 2.f, kContentH });
    parent->addChild(header);

    float listH = kContentH - 14.f;
    auto  list  = ScrollLayer::create(CCSize{ kContentW, listH });
    list->setPosition({ 0.f, 0.f });
    parent->addChild(list);

    float rowH  = 20.f;
    float total = std::max(listH, rowH * static_cast<float>(shown.size()));
    list->m_contentLayer->setContentSize({ kContentW, total });

    for (size_t i = 0; i < shown.size(); ++i) {
        auto const& s = shown[i];

        auto row = CCNode::create();
        row->setContentSize({ kContentW, rowH });
        row->setPosition({ 0.f, total - rowH * (static_cast<float>(i) + 1.f) });

        if (i % 2 == 1) {
            auto bg = CCLayerColor::create({ 0, 0, 0, 40 }, kContentW, rowH);
            bg->setPosition({ 0.f, 0.f });
            row->addChild(bg, -1);
        }

        auto when = CCLabelBMFont::create(
            fmt::format("{}{}", formatDateTime(s.start), s.clean ? "" : "  (crash?)").c_str(),
            "bigFont.fnt"
        );
        when->setScale(.3f);
        when->setAnchorPoint({ 0.f, .5f });
        when->setPosition({ 8.f, rowH / 2 });
        if (!s.clean) when->setColor({ 255, 170, 170 });
        row->addChild(when);

        auto stats = CCLabelBMFont::create(
            fmt::format("{} / {} FPS", formatDuration(s.seconds), fmtFloat(s.avgFps())).c_str(),
            "chatFont.fnt"
        );
        stats->setScale(.4f);
        stats->setAnchorPoint({ 1.f, .5f });
        stats->setPosition({ kContentW - 8.f, rowH / 2 });
        stats->setColor(fpsColor(s.avgFps()));
        row->addChild(stats);

        list->m_contentLayer->addChild(row);
    }

    list->scrollToTop();
}

// ---------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------

void StatsPopup::onRange(CCObject* sender) {
    int tag = sender->getTag();
    if (tag == 4) {
        this->onCustom(sender);
        return;
    }
    m_range = static_cast<Range>(tag);
    this->updateButtons();
    this->applyRange();
    for (int i = 0; i < 4; ++i) {
        if (m_tabNodes[i]) m_tabNodes[i]->setVisible(i == m_tab);
    }
}

void StatsPopup::onCustom(CCObject*) {
    auto popup = RangePopup::create(m_customFrom, m_customTo, [this](std::time_t from, std::time_t to) {
        m_customFrom = from;
        m_customTo   = to;
        m_range      = Range::Custom;
        this->updateButtons();
        this->applyRange();
        for (int i = 0; i < 4; ++i) {
            if (m_tabNodes[i]) m_tabNodes[i]->setVisible(i == m_tab);
        }
    });
    if (popup) popup->show();
}

void StatsPopup::onTab(CCObject* sender) {
    m_tab = sender->getTag();
    for (int i = 0; i < 4; ++i) {
        if (m_tabNodes[i]) m_tabNodes[i]->setVisible(i == m_tab);
    }
    this->updateButtons();
}

void StatsPopup::onMetric(CCObject* sender) {
    m_metric = static_cast<Metric>(sender->getTag());
    if (m_tabNodes[1]) {
        m_tabNodes[1]->removeAllChildren();
        m_metricButtons.clear();
        this->buildGraph();
    }
}

void StatsPopup::onExport(CCObject*) {
    auto path = Storage::get().exportCsv(m_from, m_to);
    if (path.empty()) {
        Notification::create("Nothing to export in this range", NotificationIcon::Warning)->show();
        return;
    }
    Notification::create(
        fmt::format("Exported to {}", path.filename().string()),
        NotificationIcon::Success
    )->show();
}

void StatsPopup::onFolder(CCObject*) {
    file::openFolder(Storage::get().dataDir());
}

void StatsPopup::onInfo(CCObject*) {
    auto& storage = Storage::get();

    std::string cpuLine = SystemStats::cpuSupported()
        ? fmt::format("CPU readings: <cg>available</c> ({} cores)", SystemStats::get().coreCount())
        : "CPU readings: <cr>not available on this platform</c>";
    std::string ramLine = SystemStats::ramSupported()
        ? "RAM readings: <cg>available</c>"
        : "RAM readings: <cr>not available on this platform</c>";

    FLAlertLayer::create(
        "Performance Tracker",
        fmt::format(
            "Frame times are measured every frame; each data point covers a few "
            "seconds and is tagged with the level you were in.\n\n"
            "<cy>1% low</c> over a long range is the <cy>average of each window's "
            "1% low</c>, not a true global percentile - the raw frame times are not "
            "kept that long.\n\n"
            "{}\n{}\n\nData on disk: <cy>{}</c>",
            cpuLine, ramLine, formatBytes(storage.diskUsage())
        ),
        "OK"
    )->show();
}
