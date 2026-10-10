# Immich Custom Client — Planning Notes

Context gathered from a planning conversation, for use as background when starting implementation with Claude Code.

## Goal

Build a custom Immich desktop client for Linux (Debian/Ubuntu), replacing the idea of reusing the official web UI. Two driving motivations:

1. **Custom UI/UX** — Apple Photos-inspired look and feel, rather than the stock Immich web client.
2. **AI-assisted printable photo albums** — a natural-language interface to generate print-ready photo albums (e.g., "20-page landscape A5 album of our Japan trip, April 2026, modern style, mix of portraits and landscapes, not overloaded"), with a browsable on-screen preview and either a print button or integration with a print service.

A third use case (offline/local caching of the library) emerged as important, partly for its own sake, partly because album layout quality requires reliable local access to full-resolution originals.

## Immich architecture (background)

- **immich-server** (Node.js/TypeScript, NestJS, loosely Hexagonal Architecture): handles REST API requests, orchestrates background jobs via Redis/BullMQ, stores data in Postgres (with pgvector for embeddings).
- **immich-machine-learning** (Python/FastAPI): stateless model inference — face detection + recognition (InsightFace models, e.g. `buffalo_l`), CLIP embeddings for smart search, object detection. Externalized from the server; the server sends preview images and gets back embeddings/bounding boxes.
- **Web client** (SvelteKit): thin — just a REST API consumer, no ML or heavy processing. Not offline-first; not a good porting target for a new client.
- **Official mobile app** (Flutter): built as a separate client against the API, not by reusing the web UI. This is the model to follow for a new client, not porting the SvelteKit app.
- **Immich CLI** (`@immich/cli`, npm package): thin, upload/backup-focused only (`immich upload`, `login`, `server-info`). No face recognition, job, or search commands — not useful beyond backup scripting.
- Facial recognition is **triggered automatically server-side** after thumbnail generation (via BullMQ job chaining) — the client sends no explicit "recognize" command in normal use. Manual/admin triggering happens via the Jobs API (`PUT /jobs/faceDetection`, `PUT /jobs/facialRecognition`, body `{"command":"start"}`), separate from the CLI entirely.
- Immich is open source, AGPL-3.0, monorepo on GitHub (`immich-app/immich`).

## Existing third-party clients surveyed (none are a full web-UI equivalent; none do proactive offline sync)

- **immich-go** (`simulot/immich-go`) — Go, upload/import tool (Google Takeout, iCloud, folders, server-to-server migration). Upload-only.
- **immich-sync** (`JonaEnz/immich-sync`) — Go daemon, watches folders, uploads via systemd service. Upload-only.
- **immich-sync** (`christopherjohnmayor/immich-sync`) — thin wrapper around the official CLI, no custom API client. Upload-only.
- **immich-power-tools** (`varun-raj/immich-power-tools`) — self-hosted admin dashboard for bulk metadata fixes, people/album organization. Not a viewer.
- **immich-deduper** (`RazgrizHsu/immich-deduper`) — visual similarity duplicate finder, works against Immich DB directly.
- **Mimick** (`nicx17/mimick`) — closest existing analogue. Rust + GTK4/libadwaita Linux desktop client.
  - Core "Sync Engine" is explicitly **one-way, upload-only** ("One-Way Mirroring: Maintains strictly read-only access to local system files").
  - Has an optional **Library View**: thumbnail grid, CLIP smart search, OCR text search, lightbox with full-res download.
  - Per-folder rules can be set to **Full** or **Download Only** sync method (needs `asset.read`+`asset.download` scopes) enabling actual downloads, and the Library View offers **on-demand, bidirectional** album↔folder sync ("Sync Now", manual, not continuous background download).
  - Good prior art for: local sync-state indexing, SHA-1 dedup, retry-queue handling, encrypted API-key keystore (via `oo7`/Secret Service).
  - Does **not** implement proactive/continuous "keep originals on device" — that's still open ground. Confirmed as an unresolved community feature request (GitHub Discussion #11344 on the main Immich repo).

## Decision: build a thin native client against the REST API, not port the web UI

Reasoning: the web client's data layer assumes an always-reachable server and isn't designed for offline-first; retrofitting that is as much work as building fresh. The official mobile app already takes the "separate client, same API" approach. The API is stable, documented (OpenAPI spec, generated TS/Dart clients available).

## Use cases for "keep originals on device"

1. Photos always available locally on the computer.
2. Local editing support (though server-side caching could partly satisfy this).
3. Local folder → auto-upload to server (the inverse direction, which Mimick and the other tools already do well).
4. **Disaster recovery** — local copy protects against server failure, corruption, accidental deletion, ransomware. This is the use case that most distinguishes true "keep originals" from simple caching.
5. **Selective offline browsing while traveling** — a chosen subset (album, date range, person), not a full mirror. Closer to Google Drive selective sync than a blanket copy.
6. **Bandwidth/metered constraints** — relevant given the two-home (Lannion Oct–Apr / secondary home May–Sep) setup, if connectivity differs between locations.
7. **Third-party app interoperability** — RAW converters, print software, file-manager previews need real files on disk.
8. **Migration independence** — a full local mirror acts as a standing export, protects against project abandonment or wanting to switch tools later.

### Decided direction

- **Storage/sync policy**: selective, on-demand (OneDrive-style), not "sync everything always."
- **Linux implementation options considered**:
  - **FUSE-based virtual filesystem** (closest analogue to OneDrive Files On-Demand; no native Windows Cloud Files API / macOS File Provider equivalent exists on Linux). Would use `libfuse` (C) or the `fuser` crate (Rust). More faithful to true on-demand semantics; Nautilus/GTK has no native "online-only" icon overlay system, so UX will be rougher than Windows Explorer unless custom file-manager integration is built.
  - **App-level browser** (like Mimick's Library View) — simpler, not filesystem-transparent, explicit per-photo/album download action. Recommended as the pragmatic MVP; FUSE layer could follow once sync/caching logic is proven.

## Conflict handling (local edits vs. server-side changes)

- A **lock mechanism** is feasible but has real limits:
  - Immich has no native locking primitive — would need to build one (custom metadata field/external table).
  - Only enforceable across instances of *your own* client. The official web UI's native edit-in-place feature won't respect a custom lock field unless the server itself is forked.
  - Needs a lease/TTL, not a permanent flag, to avoid stuck locks on crash.
  - Doesn't help if the device is offline when the lock is taken — conflict can still occur on reconnect.
- **Alternative: optimistic concurrency** — compare `updatedAt`/checksum before pushing edits, prompt user on mismatch. Doesn't require other clients' cooperation; standard practice (git/Dropbox-style).

## AI-assisted album generation — architecture sketch

The interesting, differentiating feature. Breaking down what it requires:

- **Asset selection**: parse natural-language brief → query Immich search API (metadata/date-range filters + CLIP-based smart search for content like "portraits vs landscapes").
- **Curation/culling**: quality scoring (sharpness, exposure, duplicate/burst detection — similar to immich-deduper's approach) + diversity balancing.
- **Layout generation**: real constraint-based layout engine (grid templates, margin rules, orientation-pairing) — not something to ask an LLM to compute in free text. Output should be print-ready (correct physical dimensions, e.g., A5 landscape = 210×148mm, bleed/margins) as PDF or per-page SVG → PDF.
- **On-screen browsable preview**: page-flip viewer over the generated pages (well-solved UI problem).
- **Print integration**: either "export PDF for the user's own printer/service" (simplest, no liability) or direct API integration with a print-on-demand service (note: major European ones like CEWE/Saal Digital don't generally have open consumer APIs — would likely mean generating spec-compliant PDFs rather than true API integration).

### Key architectural insight

Don't try to have the LLM do better free-form reasoning than "ask Claude Code to look at photos and build an album." Instead, build **purpose-built tools** and let an LLM (via the same tool-use/agentic pattern Claude Code itself uses) orchestrate them:

- `search_photos(query, date_range, people, location)` → hits Immich's structured search/metadata directly.
- `score_quality(asset_id)` → deterministic sharpness/exposure/duplicate scoring, not LLM guesswork.
- `propose_layout(photos, page_count, format, style)` → real constraint-based/template layout engine, parameterized by the LLM's choices (not asked to lay out pixels itself).
- `render_preview(layout)` → produces actual PDF/page images.
- `apply_edit(page_id, instruction)` → refinement loop operates on a real document model, not regenerated prose each time.

The LLM's role: interpreting intent, picking/sequencing photos, choosing style, pacing — what LLMs are actually good at. Deterministic code handles physical layout math and print-spec compliance.

Practical starting point: prototype using the Claude API with tool use, wiring up 4–5 tools like the above, with a thin UI (chat + page preview) on top. Reuse an existing open-source photo-book layout/constraint library rather than building a layout engine from scratch.

## Cost model for the AI component

- **Cloud API (Claude)**: pay-per-token, no subscription required. Sonnet 5 pricing: $2/MTok input, $10/MTok output (per Claude API pricing page, Sept 2026). An album-generation call (photo metadata + reasoning + layout plan) would likely be a few thousand tokens — a few cents per album for personal use. No subscription needed unless distributing the app to other users who'd need their own API key or a cost-covering plan.
- **Local LLM** (Llama/Mistral/Qwen via Ollama or llama.cpp): zero ongoing cost, fits privacy-conscious/self-hosted preferences, but weaker at nuanced multi-constraint instructions than cloud frontier models — more viable if the pipeline reasons mostly over structured metadata (photo scores, dates, faces) rather than raw pixels, since the heavy vision work is already done by Immich's own ML service.
- **Recommendation discussed**: cloud API for quality/simplicity given the low per-album cost; local only if zero external dependency is a hard requirement or the app is meant for wider distribution.

## Open questions / not yet decided

- Sequencing: build sync/offline viewer first as foundation, or prototype the album-generation pipeline first since it's the novel, exciting part? (Raised, not resolved — album generation likely needs reliable local full-res originals anyway, so the two threads reinforce each other.)
- UI toolkit choice not yet decided (GTK4/libadwaita like Mimick vs. Qt6 vs. custom-rendered for closer Apple Photos-style polish).
- Print-service integration approach (generic PDF export vs. specific provider integration) not yet chosen.
- FUSE virtual filesystem vs. app-level browser for the on-demand storage layer — leaning app-level browser as MVP, FUSE as a later stretch goal.
