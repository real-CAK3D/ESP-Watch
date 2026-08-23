# GTA-Nav Contact Pack

The dashboard has a built-in contact and secret-number system in `app.js`.

Important boundary:

- Use original GTA-Nav names, responses, and voice lines by default.
- Do not copy Rockstar's exact phone UI art, exact hidden-number database, or exact dialogue transcripts.
- User-owned custom contacts can be added to the `contacts` array.
- User-authored secret-number responses can be added to `secretResponses`.

The current app supports:

- Real `tel:` links for contacts.
- A dialer that checks known contacts first.
- A secret-number response table for Easter-egg style behavior.
- Mechanic-style text-to-speech using browser speech synthesis.
