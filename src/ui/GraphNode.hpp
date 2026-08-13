#pragma once

#include "../Stats.hpp"

#include <Geode/Geode.hpp>

namespace pt {

    inline cocos2d::ccColor4F c4f(int r, int g, int b, float a = 1.f) {
        return { r / 255.f, g / 255.f, b / 255.f, a };
    }

    /// Line + min/max band chart drawn with a single CCDrawNode.
    class GraphNode : public cocos2d::CCNode {
    public:
        static GraphNode* create(cocos2d::CCSize size) {
            auto ret = new GraphNode();
            if (ret->initGraph(size)) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

        void setData(
            std::vector<GraphPoint> points, std::time_t from, std::time_t to, Metric metric
        ) {
            m_points = std::move(points);
            m_from   = from;
            m_to     = to;
            m_metric = metric;
            this->rebuild();
        }

    protected:
        bool initGraph(cocos2d::CCSize size) {
            using namespace cocos2d;
            if (!CCNode::init()) return false;

            this->setContentSize(size);
            this->setAnchorPoint({ 0.f, 0.f });

            m_draw = CCDrawNode::create();
            this->addChild(m_draw);

            m_labels = CCNode::create();
            m_labels->setContentSize(size);
            m_labels->setAnchorPoint({ 0.f, 0.f });
            this->addChild(m_labels, 1);

            this->rebuild();
            return true;
        }

        void rebuild() {
            using namespace cocos2d;

            m_draw->clear();
            m_labels->removeAllChildren();

            auto  size = this->getContentSize();
            float left = 32.f, right = 6.f, top = 8.f, bottom = 16.f;
            float px = left;
            float py = bottom;
            float pw = size.width - left - right;
            float ph = size.height - top - bottom;
            if (pw <= 10.f || ph <= 10.f) return;

            // plot background + frame
            m_draw->drawRect({ px, py }, { px + pw, py + ph }, c4f(0, 0, 0, .35f), 1.f, c4f(255, 255, 255, .25f));

            bool anyData = false;
            float maxValue = 0.f;
            for (auto const& p : m_points) {
                if (!p.has) continue;
                anyData  = true;
                maxValue = std::max({ maxValue, p.hi, p.avg });
            }

            if (!anyData) {
                auto none = CCLabelBMFont::create("No data in this range", "chatFont.fnt");
                none->setScale(.5f);
                none->setPosition({ px + pw / 2, py + ph / 2 });
                none->setOpacity(140);
                m_labels->addChild(none);
                return;
            }

            float top10 = this->niceCeil(maxValue);
            if (top10 <= 0.f) top10 = 1.f;

            auto color = this->metricColor();

            // horizontal grid + y labels
            for (int i = 0; i <= 4; ++i) {
                float ratio = i / 4.f;
                float y     = py + ph * ratio;
                m_draw->drawSegment({ px, y }, { px + pw, y }, .25f, c4f(255, 255, 255, i == 0 ? .28f : .12f));

                auto label = CCLabelBMFont::create(
                    fmt::format("{:.0f}", top10 * ratio).c_str(), "chatFont.fnt"
                );
                label->setScale(.32f);
                label->setAnchorPoint({ 1.f, .5f });
                label->setPosition({ px - 4.f, y });
                label->setOpacity(160);
                m_labels->addChild(label);
            }

            size_t columns = m_points.size();
            float  step    = pw / static_cast<float>(columns);

            // min/max band
            for (size_t i = 0; i < columns; ++i) {
                auto const& p = m_points[i];
                if (!p.has) continue;
                float x  = px + step * (static_cast<float>(i) + .5f);
                float y1 = py + std::clamp(p.lo / top10, 0.f, 1.f) * ph;
                float y2 = py + std::clamp(p.hi / top10, 0.f, 1.f) * ph;
                if (y2 - y1 < .5f) y2 = y1 + .5f;
                m_draw->drawSegment(
                    { x, y1 }, { x, y2 },
                    std::max(.6f, step * .35f),
                    c4f(color.r, color.g, color.b, .28f)
                );
            }

            // average line
            bool     havePrev = false;
            CCPoint  prev{};
            for (size_t i = 0; i < columns; ++i) {
                auto const& p = m_points[i];
                if (!p.has) {
                    havePrev = false;
                    continue;
                }
                CCPoint cur{
                    px + step * (static_cast<float>(i) + .5f),
                    py + std::clamp(p.avg / top10, 0.f, 1.f) * ph
                };
                if (havePrev) {
                    m_draw->drawSegment(prev, cur, .8f, c4f(color.r, color.g, color.b, .95f));
                }
                else {
                    m_draw->drawDot(cur, 1.f, c4f(color.r, color.g, color.b, .95f));
                }
                prev     = cur;
                havePrev = true;
            }

            // x axis labels
            bool longSpan = (m_to - m_from) > 2 * 86400;
            for (int i = 0; i <= 3; ++i) {
                float ratio = i / 3.f;
                auto  t     = static_cast<std::time_t>(
                    m_from + static_cast<std::time_t>((m_to - m_from) * ratio)
                );
                auto text = longSpan ? formatDateShort(t) : formatClock(t);

                auto label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
                label->setScale(.32f);
                label->setAnchorPoint({ ratio == 0.f ? 0.f : (ratio == 1.f ? 1.f : .5f), 1.f });
                label->setPosition({ px + pw * ratio, py - 3.f });
                label->setOpacity(160);
                m_labels->addChild(label);
            }

            // unit hint
            auto unit = CCLabelBMFont::create(
                fmt::format("{}{}", metricName(m_metric), *metricUnit(m_metric) ? fmt::format(" ({})", metricUnit(m_metric)) : "").c_str(),
                "chatFont.fnt"
            );
            unit->setScale(.34f);
            unit->setAnchorPoint({ 0.f, 1.f });
            unit->setPosition({ px + 4.f, py + ph - 2.f });
            unit->setColor(color);
            unit->setOpacity(190);
            m_labels->addChild(unit);
        }

        cocos2d::ccColor3B metricColor() const {
            switch (m_metric) {
                case Metric::CpuProc:   return { 255, 170, 60 };
                case Metric::CpuSystem: return { 255, 120, 120 };
                case Metric::Ram:       return { 110, 180, 255 };
                default:                return { 120, 235, 140 };
            }
        }

        /// Round a max value up to something that makes a readable axis.
        float niceCeil(float v) const {
            if (v <= 0.f) return 1.f;
            static const float steps[] = {
                10.f, 20.f, 30.f, 60.f, 75.f, 90.f, 120.f, 144.f, 165.f, 180.f, 240.f, 300.f, 360.f, 480.f, 600.f
            };
            if (m_metric == Metric::Fps) {
                for (float s : steps) {
                    if (v <= s) return s;
                }
            }
            if (m_metric == Metric::CpuProc || m_metric == Metric::CpuSystem) {
                if (v <= 25.f) return 25.f;
                if (v <= 50.f) return 50.f;
                return 100.f;
            }
            float mag = std::pow(10.f, std::floor(std::log10(v)));
            return std::ceil(v / mag) * mag;
        }

        cocos2d::CCDrawNode*    m_draw   = nullptr;
        cocos2d::CCNode*        m_labels = nullptr;
        std::vector<GraphPoint> m_points;
        std::time_t             m_from   = 0;
        std::time_t             m_to     = 0;
        Metric                  m_metric = Metric::Fps;
    };
}
