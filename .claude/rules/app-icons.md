# App icons

Every app with a UI has two SVG files in its app root, next to `manifest.properties`:

- `icon.svg`: colour icon.
- `icon_mono.svg`: monochrome icon.

## Which apps get icons

Apps with a UI only. Headless apps (`app.N.headless=true` in `manifest.properties`) get none.

## Colour icon (`icon.svg`)

- `width="96" height="96" viewBox="0 0 96 96"`.
- Full-bleed background (a `<rect width="96" height="96">`), then a glyph on top.
- One flat background colour. Gradients only for small accents.
- The glyph should say what the app does (a clock for Clock, a skull for Doom), not just its name.
- Use the brand colour when the app is about a brand (e.g. Espressif red `#E7352C` for ESP apps). Drop words like "Bridge" or "Demo" from the metaphor.
- Keep strong contrast between glyph and background, and between elements (e.g. text on tiles).
- No `<text>`. Fonts are not available at render time. Draw digits and letters as paths.

## Mono icon (`icon_mono.svg`)

- `width="24" height="24" viewBox="0 0 96 96"`.
- Pure black (`#000000`) on transparent. No other colours, no opacity, no gradients.
- No border or frame around the glyph.
- Glyph fills about 80px of the 96px canvas (83%), centred. Match the Material icons in `Tactility/Data/system/app_icon/*_mono.svg`, which fill roughly 75-85%.
- Stroked glyphs: stroke width 3-4 before scaling, round caps and joins. The icon renders at 24px, so a 4 unit stroke is about 1px. Nothing thinner.
- Avoid detail smaller than ~4 units. It disappears at 24px.
- Text inside a mono icon is usually not legible at 24px. Skip it unless the glyph is large.

## Checking

- Render both at full size and at the real size (24px for mono, upscaled with a point filter) before committing. `magick -background white -density 768 icon_mono.svg -resize 96x96 out.png` works.
- Confirm the glyph is not clipped and is centred.
