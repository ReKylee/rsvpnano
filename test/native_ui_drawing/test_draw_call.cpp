#include <cassert>
#include <string>
#include <string_view>
#include "ui/DrawCall.h"

using ui::detail::DrawCall;
using ui::detail::DrawSignature;
using ui::detail::StatelessDraw;

namespace {
    struct Recorder {
        int calls = 0;
        const std::string* borrowed = nullptr;
        std::string_view first, second;
        int value = 0;
    };
    constexpr auto paint = [](Recorder& recorder, ui::Rect, std::string_view first, std::string_view second, int value) {
        ++recorder.calls;
        recorder.first = first;
        recorder.second = second;
        recorder.value = value;
    };
    template<typename... Args>
    auto state(const Args&... args) {
        return DrawCall<decltype(paint), Args...>(args...).signature();
    }
    enum class Flag : uint8_t { Off, On };
    struct Unsupported { int hidden; };
    struct NoCopyText : std::string {
        using std::string::string;
        NoCopyText(const NoCopyText&) = delete;
    };
}

int main() {
    static_assert(StatelessDraw<decltype(paint)>);
    int capture = 42;
    const auto captures = [capture](Recorder&, ui::Rect) { (void)capture; };
    static_assert(!StatelessDraw<decltype(captures)>);
    static_assert(!ui::detail::DrawArgument<Unsupported>);
    static_assert(!ui::detail::DrawArgument<int*>);

    const std::string_view a="ab", b="c", c="a", d="bc";
    assert(state(a,b,1) != state(c,d,1));
    assert(state(a,b,1) != state(b,a,1));
    assert(state(a,b,1) != state(a,b,2));
    const std::string left="ab", right="c";
    assert(state(std::string_view{left},std::string_view{right},1) == state(a,b,1));
    const std::string_view embedded{"a\0b",3};
    assert(state(embedded,b,1) != state(std::string_view{"a"},b,1));

    std::string changing="ab";
    const auto view=std::string_view{changing};
    const auto original=state(view,b,1);
    changing[1]='z';
    assert(state(view,b,1) != original);

    const int value=7;
    const DrawCall<decltype(paint), std::string_view, std::string_view, int> call(a,b,value);
    Recorder recorder;
    call(recorder, ui::Rect{});
    assert(recorder.calls==1 && recorder.first==a && recorder.second==b && recorder.value==value);
    assert(recorder.first.data()==a.data());

    const NoCopyText text="model owned";
    constexpr auto borrowed = [](Recorder& output, ui::Rect, const NoCopyText& text) { output.borrowed=&text; };
    DrawCall<decltype(borrowed),NoCopyText> reference(text);
    reference(recorder,ui::Rect{});
    assert(recorder.borrowed==&text);
    (void)reference.signature();

    constexpr auto otherPaint=[](Recorder&,ui::Rect,std::string_view,std::string_view,int) {};
    assert((call.signature()!=DrawCall<decltype(otherPaint),std::string_view,std::string_view,int>(a,b,value).signature()));
    // A generic painter may render the same integer differently depending on its type.
    constexpr auto generic=[](Recorder&,ui::Rect,const auto&) {};
    const int i=1; const unsigned u=1;
    assert((DrawCall<decltype(generic),int>(i).signature()!=DrawCall<decltype(generic),unsigned>(u).signature()));

    DrawSignature plusZero, minusZero;
    plusZero.append(0.0f);minusZero.append(-0.0f);
    assert(plusZero.value()==minusZero.value());
    DrawSignature one,two;
    one.append(Flag::Off);two.append(Flag::On);assert(one.value()!=two.value());
    for(int member=0;member<4;++member) {
        ui::Rect before{1,2,3,4},after=before;
        switch(member) {case 0:++after.x;break;case 1:++after.y;break;case 2:++after.w;break;case 3:++after.h;break;}
        DrawSignature oldRect,newRect;oldRect.append(before);newRect.append(after);
        assert(oldRect.value()!=newRect.value());
    }
}
