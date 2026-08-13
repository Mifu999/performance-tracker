#pragma once

#include "../Stats.hpp"
#include "GraphNode.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>

namespace pt {

    class StatsPopup : public geode::Popup {
    public:
        static StatsPopup* create();

    protected:
        enum class Range { Today, Week, Month, All, Custom };

        bool init();

        void onRange(cocos2d::CCObject* sender);
        void onTab(cocos2d::CCObject* sender);
        void onMetric(cocos2d::CCObject* sender);
        void onExport(cocos2d::CCObject* sender);
        void onFolder(cocos2d::CCObject* sender);
        void onInfo(cocos2d::CCObject* sender);
        void onCustom(cocos2d::CCObject* sender);

        void applyRange();
        void reload();
        void updateButtons();

        void buildOverview();
        void buildGraph();
        void buildLevels();
        void buildSessions();

        cocos2d::CCNode* makeStatRow(
            char const* label, std::string const& value,
            cocos2d::ccColor3B color, float width
        );

        Range       m_range      = Range::Today;
        Metric      m_metric     = Metric::Fps;
        int         m_tab        = 0;
        std::time_t m_from       = 0;
        std::time_t m_to         = 0;
        std::time_t m_customFrom = 0;
        std::time_t m_customTo   = 0;

        std::vector<Sample> m_samples;
        Aggregate           m_agg;

        cocos2d::CCNode*        m_tabNodes[4] = {};
        cocos2d::CCLabelBMFont* m_rangeLabel  = nullptr;
        GraphNode*              m_graph       = nullptr;

        // CCMenuItemSpriteExtra is a GD class in the global namespace, not cocos2d
        std::vector<CCMenuItemSpriteExtra*> m_rangeButtons;
        std::vector<CCMenuItemSpriteExtra*> m_tabButtons;
        std::vector<CCMenuItemSpriteExtra*> m_metricButtons;
    };
}
