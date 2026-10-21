#include "effect/effect.h"
#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
namespace KWin {
class HelloEffect : public Effect {
    Q_OBJECT
public:
    HelloEffect() { }
    void prePaintWindow(RenderView *v, EffectWindow *w, WindowPrePaintData &d) override { d.setTransformed(); effects->prePaintWindow(v,w,d);}
    bool isActive() const override { return true; }
    void paintWindow(const RenderTarget &rt, const RenderViewport &vp, EffectWindow *w, int mask, const Region &region, WindowPaintData &data) override {
        data.setScale(QVector2D(0.5, 0.5));
        effects->paintWindow(rt, vp, w, mask, region, data);
    }
};
KWIN_EFFECT_FACTORY(HelloEffect, "metadata.json")
}
#include "hello.moc"
