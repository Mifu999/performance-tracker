#include "Storage.hpp"
#include "Tracker.hpp"
#include "ui/Overlay.hpp"
#include "ui/StatsPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/cocos.hpp>

using namespace geode::prelude;
using namespace pt;

// ---------------------------------------------------------------------
// Frame tick
//
// CCScheduler::update runs exactly once per rendered frame, which makes it
// the cheapest reliable place to measure frame times without owning a node
// in every scene.
// ---------------------------------------------------------------------
class $modify(PTScheduler, CCScheduler) {
    void update(float dt) {
        CCScheduler::update(dt);
        Tracker::get().onFrame();
    }
};

// ---------------------------------------------------------------------
// Main menu button
// ---------------------------------------------------------------------
class $modify(PTMenuLayer, MenuLayer) {
    void onPerformanceStats(CCObject*) {
        if (auto popup = StatsPopup::create()) {
            popup->show();
        }
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        if (!Mod::get()->getSettingValue<bool>("menu-button")) return true;

        auto bottomMenu = this->getChildByID("bottom-menu");
        if (!bottomMenu) {
            log::warn("bottom-menu not found, skipping the main menu button");
            return true;
        }

        CCNode* sprite = CCSprite::create("menu-icon.png"_spr);
        if (sprite) {
            // match whatever height the buttons already sitting in the row use,
            // so the icon lines up no matter which mods added what
            float target = 40.f;
            if (auto children = bottomMenu->getChildren()) {
                if (children->count() > 0) {
                    auto first = static_cast<CCNode*>(children->objectAtIndex(0));
                    float height = first->getScaledContentSize().height;
                    if (height > 5.f) target = height;
                }
            }
            limitNodeSize(sprite, { target, target }, 999.f, .05f);
        }
        else {
            // never leave the menu without a working button if the sprite is missing
            sprite = ButtonSprite::create("FPS", "bigFont.fnt", "GJ_button_01.png", .6f);
        }

        auto btn = CCMenuItemSpriteExtra::create(
            sprite, this,
            static_cast<SEL_MenuHandler>(&PTMenuLayer::onPerformanceStats)
        );
        btn->setID("stats-button"_spr);
        bottomMenu->addChild(btn);
        bottomMenu->updateLayout();

        return true;
    }
};

// ---------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------
$on_mod(Loaded) {
    Tracker::get().onLoad();
}

$on_game(Loaded) {
    // the director and a scene exist by now, so the overlay can attach safely
    PerfOverlay::get();
}

$on_game(Exiting) {
    Tracker::get().onExit();
}
