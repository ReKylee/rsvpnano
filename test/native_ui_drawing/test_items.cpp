#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

#include "ui/Ui.h"

namespace {
    using ui::Context;
    using ui::Icon;
    using ui::Rect;

    class Surface : public Arduino_Canvas {
    public:
        Surface() : Arduino_Canvas(640, 480, nullptr) {}
        void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) override {
            ++pixelWrites;
            Arduino_Canvas::writePixelPreclipped(x, y, color);
        }
        std::vector<uint16_t> pixels() {
            return {getFramebuffer(), getFramebuffer()+640*480};
        }
        size_t pixelWrites = 0;
    };

    ui::TouchContact contact;
    ui::TouchSampleResult sample(ui::TouchContact& output) {
        output=contact;
        return ui::TouchSampleResult::Contact;
    }
    void connect(Context& context) {
        context.setTouchSource({.surface={640,480},.timing={},.poll=sample});
    }
    void touch(Context& context, bool down, uint16_t x, uint16_t y, uint32_t ms) {
        contact={down,x,y,ms};
        assert(context.pollTouch(ms));
    }
    template<typename Draw>
    void frame(Context& context, Draw&& draw) {
        context.beginFrame(0);
        draw();
        context.endFrame();
    }
    template<typename Draw>
    void idle(Context& context, Surface& output, Draw&& draw) {
        frame(context,draw);
        const auto pixels=output.pixelWrites;
        const int flushes=output.flushes;
        const auto fonts=Arduino_GFX::allFontSelections;
        const auto bounds=Arduino_GFX::allTextBoundsCalls;
        const auto lines=Arduino_GFX::allDrawLines;
        const auto bytes=Arduino_GFX::allTextBytes;
        for(int i=0;i<3;++i) frame(context,draw);
        assert(output.pixelWrites==pixels && output.flushes==flushes);
        assert(Arduino_GFX::allFontSelections==fonts && Arduino_GFX::allTextBoundsCalls==bounds);
        assert(Arduino_GFX::allDrawLines==lines && Arduino_GFX::allTextBytes==bytes);
    }

    void unchangedItems() {
        Surface output;Context context(output);
        bool enabled=true;int scalar=42;
        idle(context,output,[&] {
            context.label({0,0,140,70},"label");
            context.separator({150,0,140,70},"separator");
            context.setting({300,0,140,70},"setting","value");
            context.toggle({450,0,140,70},"toggle",enabled);
            context.button({0,80,140,70},"button",true,Icon::Books,1,"left","right");
            context.iconButton({150,80,140,70},Icon::Device);
            context.tab({300,80,140,70},"tab",true,Icon::Books);
            context.battery({450,80,140,70},70,false,"70%");
            context.progress({0,160,140,70},42);
            context.steps({150,160,140,70},2,4);
            context.slider({300,160,140,70},"slider",scalar,0,100,1);
            context.stepper({450,160,140,70},"stepper",scalar,0,100,1);
            context.dial({0,240,140,70},42,0,100,"dial");
            context.card({150,240,140,70},"card","detail",3,ui::themes::Accent,Icon::Books);
            context.dockItem({300,240,140,70},"dock",Icon::Books,0xFFFF);
            context.progressRing({450,240,140,70},42);
            context.rotary({0,320,140,100},scalar,0,100,1,"rotary");
        });
    }

    void textFieldsAndIcons() {
        Surface output;Context context(output);
        frame(context,[&]{context.setting({10,10,120,60},"ab","c");});
        auto before=output.pixelWrites;
        frame(context,[&]{context.setting({10,10,120,60},"a","bc");});
        assert(output.pixelWrites>before);
        idle(context,output,[&]{context.setting({10,10,120,60},"a","bc");});

        std::string text="first";
        frame(context,[&]{context.button({10,10,120,60},text);});
        text[0]='F';before=output.pixelWrites;
        frame(context,[&]{context.button({10,10,120,60},text);});
        assert(output.pixelWrites>before);
        idle(context,output,[&]{context.button({10,10,120,60},std::string{"First"});});

        frame(context,[&]{context.dockItem({10,10,120,60},"dock",Icon::Books,0xFFFF);});
        before=output.pixelWrites;
        frame(context,[&]{context.dockItem({10,10,120,60},"dock",Icon::Power,0xFFFF);});
        assert(output.pixelWrites>before);
        Surface fresh;Context reference(fresh);
        frame(reference,[&]{reference.dockItem({10,10,120,60},"dock",Icon::Power,0xFFFF);});
        assert(output.pixels()==fresh.pixels());
    }

    void captureValidatedBeforeInput() {
        for(int replacement=0;replacement<3;++replacement) {
            Surface output;Context context(output);connect(context);
            frame(context,[&]{context.button({10,10,120,60},"button");});
            touch(context,true,30,30,100);
            frame(context,[&]{assert(!context.button({10,10,120,60},"button"));});
            touch(context,false,30,30,150);
            frame(context,[&] {
                if(replacement==0) assert(!context.button({12,10,120,60},"button"));
                else if(replacement==1) assert(!context.iconButton({10,10,120,60},Icon::Power));
                else { bool value=false;assert(!context.toggle({10,10,120,60},"toggle",value));assert(!value); }
            });
        }
        Surface output;Context context(output);connect(context);int value=50;
        frame(context,[&]{context.rotary({10,10,120,100},value,0,100,1);});
        touch(context,true,40,40,100);
        frame(context,[&]{assert(!context.rotary({10,10,120,100},value,0,100,1));});
        touch(context,true,56,40,130);
        frame(context,[&]{assert(!context.rotary({12,10,120,100},value,0,100,1));});
        assert(value==50);
        // Removing the captured rotary must not leave a second, rectangle-owned gesture alive.
        frame(context,[]{});
        touch(context,false,56,40,160);
        frame(context,[&]{assert(!context.rotary({10,10,120,100},value,0,100,1));});
        assert(value==50);
    }

    void togglePaintsEditedValue() {
        Surface output;Context context(output);connect(context);bool enabled=false;
        frame(context,[&]{context.toggle({10,10,120,60},"toggle",enabled);});
        touch(context,true,30,30,100);
        frame(context,[&]{assert(!context.toggle({10,10,120,60},"toggle",enabled));});
        touch(context,false,30,30,150);
        frame(context,[&]{assert(context.toggle({10,10,120,60},"toggle",enabled));});
        assert(enabled);
        Surface fresh;Context reference(fresh);
        frame(reference,[&]{reference.toggle({10,10,120,60},"toggle",enabled);});
        assert(output.pixels()==fresh.pixels());
        idle(context,output,[&]{assert(!context.toggle({10,10,120,60},"toggle",enabled));});
    }

    void scalarEditsAndCapacity() {
        Surface output;Context context(output);connect(context);int value=50;
        constexpr Rect rect{10,10,120,60};
        frame(context,[&]{context.slider(rect,"value",value,0,100,1);});
        touch(context,true,100,40,100);
        frame(context,[&]{assert(!context.slider(rect,"value",value,0,100,1));});
        assert(value==50); // Slider preview still commits on release, not on drag.
        touch(context,false,100,40,150);
        frame(context,[&]{assert(context.slider(rect,"value",value,0,100,1));});
        assert(value>50);
        idle(context,output,[&]{assert(!context.slider(rect,"value",value,0,100,1));});
        for(int mode=0;mode<2;++mode) {
            frame(context,[&] {
                for(size_t i=0;i<Context::kSlotCapacity;++i) context.tap({0,0,0,0});
                if(mode==0) assert(!context.rotary(rect,value,0,100,1));
                else assert(!context.stepper({},"hidden",value,0,100,1));
            });
        }
    }

    void customDrawingOwnsInvalidation() {
        Surface output;Context context(output);int width=20;
        constexpr auto paint=[](Arduino_GFX& gfx,Rect rect,int width,std::string_view) {
            gfx.fillRect(rect.x,rect.y,static_cast<int16_t>(width),rect.h,0xF800);
        };
        const auto draw=[&]{context.draw({10,10,120,60},paint,width,std::string{"temporary"});};
        idle(context,output,draw);
        auto before=output.pixelWrites;width=30;
        frame(context,draw);assert(output.pixelWrites>before);
        idle(context,output,draw);
        // No changed flag, explicit signature, persistent label copy or markDrawn call at the call site.
        Surface fresh;Context reference(fresh);
        frame(reference,[&]{reference.draw({10,10,120,60},paint,width,std::string{"temporary"});});
        assert(output.pixels()==fresh.pixels());
        context.invalidate();before=output.pixelWrites;
        frame(context,draw);assert(output.pixelWrites>before);
        idle(context,output,draw);
    }
}

int main() {
    unchangedItems();
    textFieldsAndIcons();
    captureValidatedBeforeInput();
    togglePaintsEditedValue();
    scalarEditsAndCapacity();
    customDrawingOwnsInvalidation();
}
