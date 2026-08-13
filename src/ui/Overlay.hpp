#pragma once

#include "../Tracker.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/OverlayManager.hpp>

namespace pt {

    /// Small live readout attached to Geode's OverlayManager, which is the
    /// director's notification node and therefore survives scene changes and
    /// draws above everything else.
    class PerfOverlay : public cocos2d::CCNode {
    public:
        static PerfOverlay* get() {
            static PerfOverlay* inst = nullptr;
            if (!inst) {
                inst = new PerfOverlay();
                if (!inst->initOverlay()) {
                    delete inst;
                    inst = nullptr;
                    return nullptr;
                }
                inst->autorelease();
                geode::OverlayManager::get()->addChild(inst, 100);
            }
            return inst;
        }

    protected:
        bool initOverlay() {
            using namespace cocos2d;
            if (!CCNode::init()) return false;

            m_label = CCLabelBMFont::create("", "chatFont.fnt");
            m_label->setAnchorPoint({ 1.f, 1.f });
            m_label->setAlignment(kCCTextAlignmentRight);
            this->addChild(m_label);

            this->setID("overlay"_spr);
            this->schedule(schedule_selector(PerfOverlay::tick), .25f);
            this->tick(0.f);
            return true;
        }

        void tick(float) {
            using namespace cocos2d;
            auto mod = geode::Mod::get();

            bool visible = mod->getSettingValue<bool>("overlay-enabled")
                        && mod->getSettingValue<bool>("enabled");
            this->setVisible(visible);
            if (!visible) return;

            auto& tracker = Tracker::get();
            auto  sys     = tracker.liveSys();
            auto  metrics = mod->getSettingValue<std::string>("overlay-metrics");

            std::string text = fmt::format(
                "{:.0f} FPS\n{:.1f} ms", tracker.liveFps(), tracker.liveFrameTimeMs()
            );

            if (metrics != "FPS") {
                if (sys.procCpu >= 0.f) text += fmt::format("\nCPU {:.0f}%", sys.procCpu);
                else                    text += "\nCPU -";
            }
            if (metrics == "FPS + CPU + RAM") {
                if (sys.ramMB >= 0) text += fmt::format("\nRAM {} MB", sys.ramMB);
                else                text += "\nRAM -";
            }

            m_label->setString(text.c_str());
            m_label->setScale(static_cast<float>(mod->getSettingValue<double>("overlay-scale")));

            float fps = tracker.liveFps();
            if (fps >= 55.f)      m_label->setColor({ 150, 255, 150 });
            else if (fps >= 30.f) m_label->setColor({ 255, 220, 120 });
            else                  m_label->setColor({ 255, 130, 130 });

            auto  win    = CCDirector::sharedDirector()->getWinSize();
            auto  corner = mod->getSettingValue<std::string>("overlay-corner");
            float pad    = 6.f;

            if (corner == "Top Left") {
                m_label->setAnchorPoint({ 0.f, 1.f });
                m_label->setAlignment(kCCTextAlignmentLeft);
                m_label->setPosition({ pad, win.height - pad });
            }
            else if (corner == "Bottom Left") {
                m_label->setAnchorPoint({ 0.f, 0.f });
                m_label->setAlignment(kCCTextAlignmentLeft);
                m_label->setPosition({ pad, pad });
            }
            else if (corner == "Bottom Right") {
                m_label->setAnchorPoint({ 1.f, 0.f });
                m_label->setAlignment(kCCTextAlignmentRight);
                m_label->setPosition({ win.width - pad, pad });
            }
            else {
                m_label->setAnchorPoint({ 1.f, 1.f });
                m_label->setAlignment(kCCTextAlignmentRight);
                m_label->setPosition({ win.width - pad, win.height - pad });
            }
        }

        cocos2d::CCLabelBMFont* m_label = nullptr;
    };
}
