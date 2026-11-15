# App icons

Every app with a UI has two SVG files:

- `icon.svg`: colour icon.
- `icon_mono.svg`: monochrome icon.

For an app package (with a `manifest.properties`) they sit in the app root, next to it. Built-in apps of Tactility (a C++ `AppManifest`, such as Files) are different, see "Built-in apps".

## Which apps get icons

Apps with a UI only. Headless apps (`app.N.headless=true` in `manifest.properties`) get none.

## Built-in apps

Built-in apps have no package folder. Their icons are embedded in the firmware and chosen by a table in the launcher:

- Put the files in `Data/system/app_icon/` as `<app id>.svg` and `<app id>_mono.svg` (e.g. `tactility.files.svg`). The same rules below apply, with the colour file as `icon.svg` and the mono file as `icon_mono.svg`.
- Add the app to the table of its launcher mode, which refers to the embedded assets: `BUILT_IN_ICONS` in `Tactility/Source/app/launcher/AppsMode.cpp` for apps (`BUILT_IN_ICON("tactility.files", tactility_files)`), or `SETTINGS_ICONS` in `Tactility/Source/app/launcher/SettingsMode.cpp` for settings (`SETTINGS_ICON(...)`). An app that is not in the table gets the default icon.
- Remove the entry when you delete the icon files, or the build fails.

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
- Glyph fills about 80px of the 96px canvas (83%), centred. Match the Material icons in `Data/system/app_icon/*_mono.svg`, which fill roughly 75-85%.
- Stroked glyphs: stroke width 3-4 before scaling, round caps and joins. The icon renders at 24px, so a 4 unit stroke is about 1px. Nothing thinner.
- Avoid detail smaller than ~4 units. It disappears at 24px.
- Text inside a mono icon is usually not legible at 24px. Skip it unless the glyph is large.

## Checking

- Render both at full size and at the real size (24px for mono) before committing.
  - Full size: `magick -background white -density 768 icon_mono.svg -resize 96x96 full.png`.
  - Real size: render the mono icon at 24px first, then upscale that raster with a point filter, so the detail that disappears at 24px shows: `magick -background white -density 96 icon_mono.svg small.png && magick small.png -filter point -resize 400% preview.png`.
- Confirm the glyph is not clipped and is centred.

## Tooling

`tt icon-studio` builds both files from a pasted Material Symbols SVG (https://fonts.google.com/icons) or an existing icon, with live previews at 96 px and 24 px. It tight-crops and centres the glyph, replaces the colours, and writes `icon.svg` + `icon_mono.svg` straight into an app folder. It also has a searchable Material Icons browser that converts a font glyph into SVG (needs `pip install fonttools`). Run `tt icon-studio` from the SDK.

