# XeLL native library browsing 0.3

**Subsequent hardware result:** the user has now confirmed 0.3 graphics/controller/audio diagnostics, Ethernet/HTTP/authentication, library discovery, movie pagination and metadata/details on Xbox 360. Pending-test wording below records the original 0.3 development stage. Version 0.4 extends these sources with music streaming/decoding; see [NATIVE_MUSIC.md](NATIVE_MUSIC.md). The direct 0.3 ELF is retained unchanged for rollback, while the diagnostic archive now packages 0.4.

This adds browsing to the existing working 0.2 project. The user has confirmed 0.2 on a real Xbox 360 through XeLL: graphics/controller, audio, Ethernet, Jellyfin HTTP, username/password login, HTTP 200 and an access token held in RAM. Those results apply to 0.2. The 0.3 code cross-compiles and passes host tests against real Jellyfin; its new screens and requests still require Xbox testing.

## Download and test on Xbox

Download [dist/xenonfin-xell-browse.elf](../dist/xenonfin-xell-browse.elf), rename it **xenon.elf**, copy to your FAT32 USB drive and boot using your working XeLL method. Keep [xenonfin-xell-http.elf](../dist/xenonfin-xell-http.elf), the unchanged 0.2 build, for rollback. `xenonfin-xbox-boot-diagnostic.tar.gz` now also contains the 0.3 ELF and these instructions. Verify downloads against `dist/SHA256SUMS`.

1. Initialize **Network / DHCP diagnostics** with A; check ACK lease verified. Use the existing **Jellyfin server** page and keyboard to set, for example, `192.168.68.202:8096`, then A to verify the public endpoint. Numeric IPv4 HTTP and the original trusted LAN restriction remain.
2. Enter username/password on **Jellyfin login**, then select Sign in. Credentials and token remain RAM-only; the password is wiped after submission. These steps are unchanged from 0.2. Rebooting requires a new login.
3. Use RB to enter **Libraries**. It loads the actual accessible views for this user; A/X retries if needed. Server names, collection types and counts are not fabricated. A restricted user should see only permitted libraries.
4. **Up/Down** or left stick selects an item. **A** opens folders or leaf-item details. **Y** opens details for the selected folder/item, including series, seasons, artists and albums. **B** returns to the previous list, restoring its page/selection; at Libraries root it returns to Login. LB/RB still changes diagnostic sections.
5. **Left/Right** requests previous/next pages. Each media request is capped at **10 items**. Try a movie library with more than 10 films, then return from details and check selection/page restoration. A movie opens details with the actual title, type, year, runtime, played/resume data and available rating/genres/overview.
6. TV: open a library, series, season, episode. Y on a series/season shows its metadata; A on an episode shows episode details and available series/season/episode numbers.
7. Music opens **Artists**. A opens an artist's albums, an album's tracks, then track details. At the music library's artist/album/track list, **Start** cycles **Artists → Albums → Tracks**. This uses server filters and paginates each view. Y opens artist/album metadata. Track details show available artist, album, track number and duration.
8. Other collection types use the same actual folder/item hierarchy. A book opens metadata; it does not open a reader. Empty libraries show an explicit message. **X** refreshes the current list. Overview text is capped at 768 UTF-8 bytes; **Up/Down** scrolls four visible lines. The inherited console font displays non-ASCII/control bytes as dots; UTF-8 is validated and retained in memory.
9. While loading, **B cancels**. Test a stopped server, cable loss, a deleted item/library and permission changes. The same connect/idle 8-second and total 20-second deadlines apply. Failures retain the previous screen. HTTP 403 displays access denied; **401 clears the RAM token and browsing state** and directs you to sign in. After a link/server recovery, refresh or reopen the intended item. A retry does not happen indefinitely in the background.
10. Recheck the original graphics, controller, DHCP and audio pages, including the A-operated tone. Confirm the UI remains responsive during page loads, cancellation and repeated page/back cycles. Report console model, video mode, server version, library types, HTTP status/error, page counts and the failing navigation sequence. Do not include passwords or access tokens.

Missing metadata is shown as absent/zero rather than inferred. The browser displays only the first artist and up to three genres. Long titles are bounded to 192 bytes; detail titles wrap, while lists show a short preview. There is no artwork fetch/rendering, video decoding, stream request or music playback in this stage.

## Native implementation

`platform/xenon/browse.c` and `browse.h` implement bounded parsing, request construction and navigation. They are linked directly into the PowerPC ELF. `main.c` adds one Libraries section to the existing diagnostics/login loop. The original Ethernet/DHCP, DMA/PHY guards, graphics initialization, controller keyboard and audio path remain. Authenticated requests reuse the raw lwIP HTTP client with no blocking sockets, threads or new networking stack.

- Accessible libraries: `GET /Users/{userId}/Views?IncludeExternalContent=false`. This Jellyfin endpoint has no server pagination; the bounded response is windowed locally into 10-library screens without allocating all entries.
- Folder contents: `GET /Items?UserId=...&ParentId=...&StartIndex=...&Limit=10`, ascending index/name order, no image data requested.
- Artists: `GET /Artists?UserId=...&ParentId=...&StartIndex=...&Limit=10`.
- Albums/tracks: `/Items` with the authenticated user, library parent, recursive `MusicAlbum`/`Audio` type filters. Artist albums also filter by `ArtistIds` within the selected music library.
- Details: `GET /Users/{userId}/Items?Ids=...&StartIndex=0&Limit=1&Fields=Overview,Genres&EnableImages=false&EnableUserData=true`. User filtering remains server-side. This avoids requesting media sources or large playback payloads.
- Every browsing request sends the existing token in `Authorization: MediaBrowser ... Token="..."`; it is never placed in a URL, screen or file. Header/path inputs reject injection; user/item identifiers must be GUIDs.

JSON documents are fully validated with bounded nesting, strict UTF-8/escapes, typed numeric/boolean/string fields, duplicate target-field rejection and checked integer ranges. Long display strings truncate at UTF-8 boundaries. Page size, record counts and server offsets are checked before accepting a page. Parsing happens into fixed staging storage; failures cannot overwrite the current list or commit navigation. History stores only locations (up to 12), not all previous responses. Back refetches the parent page and restores its selection when it still exists.

There are no new heap allocations in browsing or JSON. Fixed storage holds one current page, one staging page, a detail item and 12 locations. Existing limits remain: body **32 KiB**, headers **8 KiB**, outgoing request **4 KiB**. A server response exceeding these limits produces a visible error; no unbounded fallback allocation is made. Browsing responses and request buffers are wiped after use, and logout/server/account changes clear browser/session state. Public diagnostics never display an authenticated response body.

## Build and checks

```sh
DEVKITXENON=/workspace/setup/devkit make xenon
make native-test
make -C platform/xenon/tests SANITIZE=1 test
python3 tests/native_server.py --browse
bash scripts/package.sh
```

The existing pinned toolchain builds `build/xenon/xenonfin-boot.elf32`; packaging copies it to `dist/xenonfin-xell-browse.elf`. The old HTTP ELF is preserved. Package creation also requires the existing desktop binary, as before; it does not build or modify desktop playback code.

Host checks exercise the same native C parsers, browsing state machine, authenticated HTTP builder and lwIP callback client used by the ELF. Tests cover malformed/oversized/inconsistent JSON, Unicode truncation, integer overflow, pagination, empty results, cancellation, errors, history limits, metadata and expiry. AddressSanitizer/UndefinedBehaviorSanitizer checks pass.

The integration test launches official Jellyfin **10.10.7** with disposable data and accounts. It indexes 12 metadata-only movies, a TV series/season/episode, two short silent MP3 tracks with artist/album tags, an empty library and a minimal EPUB book. It checks actual native requests and responses through POSIX TCP on the host: multi-page movies and details; series/season/episode details; artists/albums/tracks and metadata; empty/other libraries; expired-token 401; and a restricted user's single accessible library. No video is generated or played, and the user's server is not modified.

Successful host integration and ELF inspection establish API/build behavior, not Xbox hardware operation. Remaining acceptance: new library screens on your console, safe-area text at your video mode, real LAN paging/hierarchy/metadata, expired-session recovery, cancellation/cable loss and repeated navigation without memory growth or freezes.
