# HUD Rendering Notes

- Display the ring base at the `score_ring` bounds from the layout JSON.
- Draw live score progress as a gold arc over the base; use a 270-degree travel from -135 to +135 degrees.
- Keep the decorative cyan outer arcs static.
- Draw the score and status text in JUCE.
- Metric pods are backgrounds only; draw labels and values in code.
