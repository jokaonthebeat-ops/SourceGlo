#pragma once
#include <JuceHeader.h>

namespace sourceglo
{
struct Design
{
    static constexpr float width  = 1491.0f;
    static constexpr float height = 1055.0f;
};

struct Bounds
{
    static constexpr juce::Rectangle<float> header           {   6,   4, 1479,  64 };
    static constexpr juce::Rectangle<float> logo             {  19,  16,  320,  42 };
    static constexpr juce::Rectangle<float> sourcePanel      {  10,  75,  243, 894 };
    static constexpr juce::Rectangle<float> heroPanel        { 254,  75,  780, 445 };
    static constexpr juce::Rectangle<float> diagnosticsPanel {1037,  75,  444, 446 };
    static constexpr juce::Rectangle<float> scoreRing        { 492,  81,  414, 392 };
    static constexpr juce::Rectangle<float> tonePod          { 399, 114,   96,  96 };
    static constexpr juce::Rectangle<float> punchPod         { 392, 279,   96,  96 };
    static constexpr juce::Rectangle<float> levelPod         { 882, 114,   96,  96 };
    static constexpr juce::Rectangle<float> phasePod         { 886, 279,   96,  96 };
    static constexpr juce::Rectangle<float> fitPod           { 642, 376,  102, 102 };
    static constexpr juce::Rectangle<float> analyzeButton    { 350, 462,  187,  43 };
    static constexpr juce::Rectangle<float> fixButton        { 574, 462,  199,  43 };
    static constexpr juce::Rectangle<float> abButton         { 807, 462,  168,  43 };
    static constexpr juce::Rectangle<float> lowerMainPanel   { 253, 528,  760, 441 };
    static constexpr juce::Rectangle<float> spectrumPanel    { 262, 574,  395, 263 };
    static constexpr juce::Rectangle<float> fitPanel         { 665, 574,  339, 263 };
    static constexpr juce::Rectangle<float> macrosPanel      { 253, 842,  760, 127 };
    static constexpr juce::Rectangle<float> rescuePanel      {1017, 528,  464, 441 };
    static constexpr juce::Rectangle<float> footer           {   6, 976, 1479,  72 };
};

inline juce::Rectangle<int> scaleRect (juce::Rectangle<float> designRect,
                                       juce::Rectangle<float> designArea,
                                       float scale)
{
    return juce::Rectangle<float> (designArea.getX() + designRect.getX() * scale,
                                   designArea.getY() + designRect.getY() * scale,
                                   designRect.getWidth() * scale,
                                   designRect.getHeight() * scale).toNearestInt();
}
}
