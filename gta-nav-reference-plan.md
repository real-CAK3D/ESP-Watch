# GTA-Nav Reference Plan

References:

- `andyr00d/GTA-GPS`: Android/B4A app that shows a GTA-style location text overlay when the user moves to a new location.
- `Game Maps IRL`: iPhone navigation app pattern with stylized map themes, saved home/custom markers, search, smoother navigation UX, and CarPlay support.

Implementation direction:

- Watch:
  - GTA V-inspired rectangular minimap palette.
  - Stable map rendering with no continuous full-screen redraw.
  - Tap map body for close/overview zoom.
  - Top-left returns to map, top-right advances screen.
  - Places screen supports swipe scrolling and tap selection.
  - GPS bridge commands update live marker, parked marker, and route target.

- Phone dashboard:
  - Three legally distinct phone layout modes inspired by the three protagonist-phone idea: Luxury, Street, Metro.
  - Trackify-style GPS page for live GPS bridge and parking save.
  - Contact pack and secret-number response table using original GTA-Nav responses.
  - Future: replace static map preview with generated OSM road tiles from `osm-tools`.

Boundaries:

- Do not copy Rockstar art, exact UI screens, full in-game phone databases, exact hidden-number responses, or exact voice/dialogue.
- Use original names/responses and user-provided custom data.
