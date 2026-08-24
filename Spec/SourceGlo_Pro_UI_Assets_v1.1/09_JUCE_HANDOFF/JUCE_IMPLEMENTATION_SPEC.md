# JUCE Implementation Specification

## 1. Coordinate and resize model

The approved canvas is 1491 × 1055. Build all component rectangles in that coordinate space. Apply one uniform scale and center the design. Do not use independent horizontal and vertical scaling.

A practical structure is:

```cpp
class SourceGloEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr float designWidth  = 1491.0f;
    static constexpr float designHeight = 1055.0f;

    void resized() override
    {
        const auto area = getLocalBounds().toFloat();
        const float scale = juce::jmin (area.getWidth() / designWidth,
                                       area.getHeight() / designHeight);
        const float w = designWidth * scale;
        const float h = designHeight * scale;
        designBounds = { (area.getWidth() - w) * 0.5f,
                         (area.getHeight() - h) * 0.5f,
                         w, h };
        // Apply bounds from SourceGloLayout.h through scaledRect().
    }
};
```

## 2. Asset caching

- Load PNG and SVG resources once in the editor constructor or a dedicated asset cache.
- Convert SVG icons to `juce::Drawable` once.
- Use `juce::ImageCache` only with deliberate keys; a dedicated cache class is easier to audit.
- Never call `juce::Drawable::createFromImageData` inside `paint()`.

## 3. Background strategy

Draw `sourceglo_shell_1491x1055.png` scaled into the design rectangle. It supplies the chassis, base panels, low-level lines, and texture. Dynamic controls sit over the shell.

The shell is not a screenshot of the full UI. It contains no dynamic score, labels, lists, graphs, or controls, so all live data remains editable and crisp.

## 4. Knobs

Use a vertical 128-frame filmstrip. Frame index:

```cpp
const auto norm = (float) slider.valueToProportionOfLength (slider.getValue());
const int frame = juce::jlimit (0, 127, juce::roundToInt (norm * 127.0f));
const int frameHeight = filmstrip.getHeight() / 128;
g.drawImage (filmstrip,
             targetX, targetY, targetW, targetH,
             0, frame * frameHeight, filmstrip.getWidth(), frameHeight);
```

The macro knob source frame is 96 × 96. The trim knob source frame is 52 × 52. Use high-quality resampling only when scale changes.

## 5. Analyzer threading

- Audio thread writes FFT input to a lock-free FIFO.
- UI/analyzer worker consumes blocks and prepares display vectors.
- `paint()` draws already-prepared points.
- Use atomics for scalar meter/stat values.
- Never allocate, load files, or lock a mutex in `processBlock()`.

## 6. Repaint rates

- meters: 30–60 Hz
- spectrum/radar: 30–45 Hz
- text stats: 10–20 Hz
- diagnostic list: update only when analysis result changes

Prefer component-local repaint calls instead of repainting the entire editor.

## 7. Layer order

1. chassis shell
2. panel-specific subtle overlays
3. analyzer grids and HUD base images
4. dynamic graph fills and progress arcs
5. controls/cards/list rows
6. text and values
7. hover/focus rings
8. debug bounds overlay

## 8. Text

Use system font lookup; do not bundle font files. The preferred family is Inter Display/Inter. Use `juce::FontOptions` where available. Keep text contrast consistent with the token file.

## 9. UI states

Every control needs normal, hover, pressed, disabled, and keyboard-focus treatment. Use the supplied button assets for normal/hover/down/disabled states.

## 10. Host safety

- Actions such as Analyze, Fix Source, Save, and Browse Library run outside the audio thread.
- Any file indexing runs on a worker thread.
- Bypass should use smoothed wet/dry or host bypass support to avoid clicks.
