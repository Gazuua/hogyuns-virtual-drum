#include "Theme.h"

namespace ui
{

namespace
{

juce::Colour rgb (juce::uint32 value)    { return juce::Colour (0xff000000u | value); }

void setCommonStage (Theme& t)
{
    t.stage.rugShadow = juce::Colours::black.withAlpha (t.dark ? 0.40f : 0.18f);
    t.stage.editOverlay = t.surface.withAlpha (0.92f);
    t.stage.editText = t.text;
}

/** A: 남색 기 도는 거의 검정, 푸른 보라 액센트, 밑줄 탭. */
Theme makeMidnight()
{
    Theme t;
    t.id = "midnight";
    t.dark = true;

    t.background = rgb (0x0e1117);
    t.surface = rgb (0x161b22);
    t.surfaceRaised = rgb (0x1f2630);
    t.surfaceSunken = rgb (0x0b0e13);
    t.track = rgb (0x2b3442);
    t.border = rgb (0x262d38);
    t.controlOutline = juce::Colours::transparentBlack;
    t.hover = juce::Colours::white.withAlpha (0.06f);
    t.pressed = juce::Colours::white.withAlpha (0.11f);
    t.shadow = juce::Colours::black.withAlpha (0.55f);

    t.text = rgb (0xe6edf3);
    t.textMuted = rgb (0x9da7b3);
    t.textFaint = rgb (0x7d8590);

    t.accent = rgb (0x7c8cff);
    t.accentText = rgb (0x0b0f1a);
    t.danger = rgb (0xf47067);
    t.good = rgb (0x56d364);
    t.warning = rgb (0xe3b341);
    t.info = rgb (0x6cb6ff);
    t.selection = t.surfaceRaised.interpolatedWith (t.accent, 0.26f);

    t.kick = rgb (0xf47067);
    t.snare = rgb (0xf0b357);
    t.hihat = rgb (0x4fd1e0);
    t.tom = rgb (0x6bd68f);
    t.ride = rgb (0x6cb6ff);
    t.cymbal = rgb (0xb794f6);
    t.otherPad = rgb (0x8b98a8);

    t.stage.floor = rgb (0x080a0f);
    t.stage.plankFar = rgb (0x161b27);
    t.stage.plankNear = rgb (0x1d2333);
    t.stage.plankLine = juce::Colours::black.withAlpha (0.45f);
    t.stage.rugFar = rgb (0x1c2240);
    t.stage.rugNear = rgb (0x29315c);
    t.stage.rugTrim = t.accent.withAlpha (0.55f);
    t.stage.light = rgb (0x9fb0ff).withAlpha (0.14f);
    t.stage.vignette = juce::Colours::black.withAlpha (0.70f);
    t.stage.nameText = rgb (0xe6edf3);
    t.stage.nameHalo = juce::Colours::black.withAlpha (0.75f);
    t.stage.keycap = rgb (0x0e1117).withAlpha (0.94f);
    t.stage.keycapEdge = t.accent.withAlpha (0.55f);
    t.stage.keycapText = rgb (0xe6edf3);

    t.field = rgb (0x0a0d12);
    t.fieldLine = rgb (0x1f2631);
    t.fieldText = t.text;

    t.menu = rgb (0x1f2630);
    t.tabTrack = t.surface;
    t.tabSelectedFill = t.selection;
    t.tabSelectedText = t.text;

    t.controlRadius = 6.0f;
    t.panelRadius = 8.0f;
    t.tabs = TabStyle::underline;
    setCommonStage (t);
    return t;
}

/** B: 중성 진회색, 주황 액센트, 알약 탭. */
Theme makeGraphite()
{
    Theme t;
    t.id = "graphite";
    t.dark = true;

    t.background = rgb (0x1c1c1e);
    t.surface = rgb (0x252527);
    t.surfaceRaised = rgb (0x323235);
    t.surfaceSunken = rgb (0x161618);
    t.track = rgb (0x3d3d41);
    t.border = rgb (0x38383b);
    t.controlOutline = juce::Colours::transparentBlack;
    t.hover = juce::Colours::white.withAlpha (0.07f);
    t.pressed = juce::Colours::white.withAlpha (0.12f);
    t.shadow = juce::Colours::black.withAlpha (0.5f);

    t.text = rgb (0xf5f5f7);
    t.textMuted = rgb (0xa1a1a6);
    t.textFaint = rgb (0x8a8a90);

    t.accent = rgb (0xff9f0a);
    t.accentText = rgb (0x1c1204);
    t.danger = rgb (0xff6961);
    t.good = rgb (0x4cd964);
    t.warning = rgb (0xffd60a);
    t.info = rgb (0x64a8ff);
    t.selection = t.surfaceRaised.interpolatedWith (t.accent, 0.24f);

    t.kick = rgb (0xff6961);
    t.snare = rgb (0xffb340);
    t.hihat = rgb (0x5ac8e0);
    t.tom = rgb (0x63d47a);
    t.ride = rgb (0x64a8ff);
    t.cymbal = rgb (0xbf8cf2);
    t.otherPad = rgb (0x8e8e93);

    t.stage.floor = rgb (0x0f0e0d);
    t.stage.plankFar = rgb (0x26221f);
    t.stage.plankNear = rgb (0x302a26);
    t.stage.plankLine = juce::Colours::black.withAlpha (0.45f);
    t.stage.rugFar = rgb (0x2a2a2d);
    t.stage.rugNear = rgb (0x3a3a3e);
    t.stage.rugTrim = t.accent.withAlpha (0.45f);
    t.stage.light = rgb (0xffe7c0).withAlpha (0.14f);
    t.stage.vignette = juce::Colours::black.withAlpha (0.66f);
    t.stage.nameText = rgb (0xf5f5f7);
    t.stage.nameHalo = juce::Colours::black.withAlpha (0.75f);
    t.stage.keycap = rgb (0x1c1c1e).withAlpha (0.94f);
    t.stage.keycapEdge = juce::Colours::white.withAlpha (0.35f);
    t.stage.keycapText = rgb (0xf5f5f7);

    t.field = rgb (0x141416);
    t.fieldLine = rgb (0x2a2a2d);
    t.fieldText = t.text;

    t.menu = rgb (0x2c2c2f);
    t.tabTrack = t.surface;
    t.tabSelectedFill = t.accent;
    t.tabSelectedText = t.accentText;

    t.controlRadius = 8.0f;
    t.panelRadius = 12.0f;
    t.tabs = TabStyle::segment;
    setCommonStage (t);
    return t;
}

/** C: 밝은 바탕, 흰 카드, 파랑 액센트. */
Theme makeLight()
{
    Theme t;
    t.id = "light";
    t.dark = false;

    t.background = rgb (0xf3f4f6);
    t.surface = rgb (0xffffff);
    t.surfaceRaised = rgb (0xffffff);
    t.surfaceSunken = rgb (0xe9ebef);
    t.track = rgb (0xe1e4e9);
    t.border = rgb (0xe3e6ea);
    t.controlOutline = rgb (0xd3d8df);
    t.hover = juce::Colours::black.withAlpha (0.04f);
    t.pressed = juce::Colours::black.withAlpha (0.08f);
    t.shadow = juce::Colours::black.withAlpha (0.12f);

    t.text = rgb (0x111827);
    t.textMuted = rgb (0x4b5563);
    t.textFaint = rgb (0x65707f);

    t.accent = rgb (0x2563eb);
    t.accentText = rgb (0xffffff);
    t.danger = rgb (0xc62828);
    t.good = rgb (0x1a7f45);
    t.warning = rgb (0xa35c00);
    t.info = rgb (0x2f7fe0);
    t.selection = rgb (0xffffff).interpolatedWith (t.accent, 0.13f);

    t.kick = rgb (0xe5484d);
    t.snare = rgb (0xe58a00);
    t.hihat = rgb (0x0ea5b7);
    t.tom = rgb (0x2e9d57);
    t.ride = rgb (0x2f7fe0);
    t.cymbal = rgb (0x8b5cf6);
    t.otherPad = rgb (0x7c8796);

    t.stage.floor = rgb (0xc9c4bb);
    t.stage.plankFar = rgb (0xe4dfd6);
    t.stage.plankNear = rgb (0xd9d2c6);
    t.stage.plankLine = juce::Colours::black.withAlpha (0.10f);
    t.stage.rugFar = rgb (0xd5dbe5);
    t.stage.rugNear = rgb (0xc3ccdb);
    t.stage.rugTrim = t.accent.withAlpha (0.35f);
    t.stage.light = juce::Colours::white.withAlpha (0.30f);
    t.stage.vignette = juce::Colours::black.withAlpha (0.16f);
    t.stage.nameText = rgb (0x111827);
    t.stage.nameHalo = juce::Colours::white.withAlpha (0.80f);
    t.stage.keycap = rgb (0x1f2937).withAlpha (0.94f);
    t.stage.keycapEdge = juce::Colours::white.withAlpha (0.30f);
    t.stage.keycapText = rgb (0xffffff);

    // 노트가 잘 보이도록 연주 판은 밝은 테마에서도 어둡게 둔다.
    t.field = rgb (0x1b2230);
    t.fieldLine = rgb (0x2c3445);
    t.fieldText = rgb (0xf1f5f9);

    t.menu = rgb (0xffffff);
    t.tabTrack = rgb (0xe4e7eb);
    t.tabSelectedFill = rgb (0xffffff);
    t.tabSelectedText = t.text;

    t.controlRadius = 6.0f;
    t.panelRadius = 10.0f;
    t.tabs = TabStyle::segment;
    setCommonStage (t);
    return t;
}

Theme& current()
{
    static Theme theme = makeGraphite();
    return theme;
}

} // namespace

//==============================================================================
juce::Colour Theme::padTint (juce::Colour padColour) const
{
    return surfaceRaised.interpolatedWith (padColour, dark ? 0.34f : 0.20f);
}

juce::Colour Theme::textOn (juce::Colour fill) const
{
    return fill.getPerceivedBrightness() > 0.62f ? juce::Colour (0xff111111) : juce::Colours::white;
}

const Theme& theme()
{
    return current();
}

void setTheme (const juce::String& id)
{
    if (id == "midnight")
        current() = makeMidnight();
    else if (id == "light")
        current() = makeLight();
    else
        current() = makeGraphite();
}

juce::StringArray themeIds()
{
    return { "midnight", "graphite", "light" };
}

juce::String themeDisplayName (const juce::String& id)
{
    if (id == "midnight")    return "Midnight";
    if (id == "light")       return "Light";
    return "Graphite";
}

juce::Colour padColour (const juce::String& padId)
{
    const auto id = padId.toLowerCase();
    const auto& t = theme();

    if (id.startsWith ("kick") || id.startsWith ("bass"))                  return t.kick;
    if (id.startsWith ("snare") || id.startsWith ("sidestick"))            return t.snare;
    if (id.startsWith ("hihat") || id.startsWith ("hat"))                  return t.hihat;
    if (id.startsWith ("tom"))                                             return t.tom;
    if (id.startsWith ("ride"))                                            return t.ride;
    if (id.startsWith ("crash") || id.startsWith ("china")
        || id.startsWith ("splash") || id.startsWith ("cymbal"))           return t.cymbal;

    return t.otherPad;
}

} // namespace ui
