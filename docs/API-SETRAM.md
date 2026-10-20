# SETRAM: official GTFS and GTFS-Realtime

## Published sources

Authoritative catalogue: [Réseau urbain SETRAM — Le Mans Métropole](https://transport.data.gouv.fr/datasets/gtfs-du-reseau-des-transports-bus-et-tramway-setram-circulant-sur-le-territoire-le-mans-metropole).
The catalogue and the downloads below were exercised over HTTPS on 2026-10-06, returning HTTP 200.

### Current static GTFS (helper default)

[Resource 84170](https://transport.data.gouv.fr/resources/84170) publishes this rolling urban/school-network GTFS endpoint:

```text
https://eur.mecatran.com/utw/ws/gtfsfeed/static/lemans?apiKey=73334a124f3a1654141d6979113106450f291d15&type=gtfs_setram_urbsco
```

Observed ZIP: **2,519,571 bytes**. `feed_info.txt` advertised 2026-09-30 through 2026-11-11, version `1791239405380`, publisher Mecatran. Observed tables: 1,596 stops, 63 routes, 9,074 trips and 203,671 stop-time rows. These counts and validity dates describe that download, not a promise about future downloads. The helper loads `stops.txt`, `routes.txt`, `trips.txt` and `stop_times.txt` directly from the ZIP without extracting archive paths.

### Requested resource 79601

[Resource 79601](https://transport.data.gouv.fr/resources/79601) explicitly links this stable data.gouv download redirect:

```text
https://www.data.gouv.fr/api/1/datasets/r/5339d96c-6d20-4a01-939a-40f7b56d6cc1
```

At verification it returned HTTP 200, **2,421,186 bytes**, redirecting to:

```text
https://static.data.gouv.fr/resources/gtfs-du-reseau-des-transports-bus-et-tramway-setram-circulant-sur-le-territoire-le-mans-metropole/20260831-081252/lemans-8080-gtfs-setram-urbsco.zip
```

Because this was an August snapshot while the same official dataset published the newer rolling feed, the helper defaults to resource 84170's endpoint. To inspect the older resource explicitly, pass the stable redirect as `--static-url` with `--refresh`. The catalogue also has a separate Flex resource; this helper uses the urban/school feed, not that Flex feed.

### GTFS-Realtime

[Resource 81226](https://transport.data.gouv.fr/resources/81226), linked from the same dataset, publishes:

```text
https://eur.mecatran.com/utw/ws/gtfsfeed/realtime/lemans?apiKey=73334a124f3a1654141d6979113106450f291d15
```

The API key is the publicly published shared feed key, not a personal credential. Publication does not establish unlimited request rates or a service-level guarantee.

## Commands (Python standard library only)

Run from the project root:

```sh
python tools/setram.py search "Republique" --refresh
python tools/setram.py search "Gares" --offline
python tools/setram.py probe
python tools/setram.py probe --timeout 10 --max-age 120
python -m unittest discover -s tests -p test_setram_tool.py -v
```

Output is JSON. Stop-name matching ignores case and accents; identifiers and headsigns are preserved exactly. Each stop includes the distinct scheduled `(route_id, direction_id, trip_headsign)` service patterns joined through `stop_times` and `trips`, plus route names. This is a configuration lookup, **not a timetable or a prediction engine**. It does not filter calendars, today's active services, or times of day.

The default cache is `.cache/setram/` under the project root (`static.zip`, `static-source.json`, `realtime.pb`). `search` reuses its static cache when its source URL matches; `--refresh` explicitly replaces it. `--offline` requires a cache and never downloads. A changed `--static-url` triggers a new download. There is no automatic static cache expiry: refresh when the feed changes. `probe` always downloads realtime; it never presents the saved realtime file as a live response.

Both commands accept `--cache-dir` and `--timeout`. Network operations use a positive, bounded urllib **socket timeout** (20 seconds default); this is not a hard wall-clock bound on an entire large transfer. Network, malformed-data and missing-cache errors produce JSON on stderr and exit status 1. HTTP errors include the HTTP status. No-match search returns an empty `stops` array. A successful probe exits 0 even when its `freshness` is `stale` or `future`; consumers must inspect that field.

## Verified configuration examples

These are actual rows in the current downloaded static feed, not test-fixture identifiers:

| Stop name | `stop_id` | `route_id` | `direction_id` | `trip_headsign` |
|---|---|---|---|---|
| République | `2712` | `23` | `0` | `BEAULIEU` |
| République | `2713` | `6` | `0` | `ST MARTIN` |
| République | `2714` | `25` | `0` | `GARE DE CHAMPAGNE` or `PLACE DE L EGLISE` |
| Gares | `1079` | `C4` | `0` | `BELLEVUE` |
| Gares | `2352` | `16` | `1` | `LAFAYETTE` |
| République | `1055` | `T1` | `0` | `UNIVERSITE` or `Université` |
| République | `1056` | `T1` | `1` | `ANTARES STADE` or `Antarès-Stade Marie Marvingt` |

The first three bus stops also contain direction `1` with headsign `REPUBLIQUE`. A terminal can appear in both directions: never infer direction from its stop ID alone. `PLA_REPUB` and `PLA_GAREN` are parent stations (`location_type=1`) with no direct stop-time service rows; use the platform stop ID that actually appears in realtime. Route ID and displayed route name happen to coincide in these examples, but must not be treated as interchangeable in general.

## Live probe and schema evidence

Two sequential real downloads produced:

| HTTP | Bytes | Feed timestamp (Unix seconds) | Feed timestamp UTC | Local fetch UTC | Computed age |
|---|---:|---:|---|---|---:|
| 200 | 49,815 | `1791316651` | 2026-10-06 19:57:31 | 2026-10-06 19:55:31.122 | -119.9 s |
| 200 | 50,405 | `1791316694` | 2026-10-06 19:58:14 | 2026-10-06 19:56:08.246 | -125.8 s |

**The observed feed timestamp was about two minutes ahead of this machine's clock.** A parent verification found system NTP synchronized and HTTP Date from both Google and Mecatran agreeing with local UTC within one second. The embedded firmware deliberately rejects header timestamps more than 60 seconds ahead, displaying "Horloge flux avancee"; it does not silently offset predictions. It was advancing between requests, but this does not prove that either clock was correct or that every prediction was fresh. The helper reports `future` rather than silently clamping negative ages or claiming freshness. Verify/synchronize device time before displaying remaining minutes.

The helper's tiny protobuf reader inspects only `FeedMessage.header` (version field 1, timestamp field 3), validates field lengths, and skips entity payloads. It is **not** a complete GTFS-RT validator and does not calculate departures. A missing header timestamp fails because freshness cannot be established. Unknown supported-wire fields are skipped; protobuf groups are not supported.

Separately, the cached downloads were decoded with the official-schema `gtfs-realtime-bindings` package in scratch space, not as a helper dependency. The second snapshot was initialized and had version `2.0`, incrementality `FULL_DATASET`, and **55 TripUpdate entities** (no vehicle or alert entities in that snapshot). All 55 trip IDs joined the current static `trips.txt`; all 1,206 stop-time update IDs joined its `stops.txt`. The first snapshot contained 54 TripUpdates.

Observed fields include `TripDescriptor.trip_id`, `route_id`, `direction_id`, `start_date`, `schedule_relationship` and `StopTimeUpdate.stop_id`, `stop_sequence`, arrival/departure `time`, `delay`, `uncertainty`. For a verified bus example in the second snapshot:

```text
trip_id: 5112887-B-PS-2627A-HB-SE-PS-L-Ma-J-V-04
route_id: C4
direction_id: 0
stop_id: 1079
arrival.time: 1791316705
departure.time: 1791316705
arrival.uncertainty: 0
```

Other updates had uncertainty 120 seconds. Some updates were already in the past. The first snapshot contained no updates for République bus IDs `2712`, `2713`, `2714`, although it did contain the tram IDs `1055` and `1056`. Static membership does not guarantee realtime coverage at any particular instant.

## Current explicit producer-clock anomaly policy

The embedded runtime keeps `fresh()` strict (maximum 60 seconds ahead), but
`usable()` additionally accepts a header 61–180 seconds ahead as
`FutureTolerated`. Home retains the stable status `Flux avance`, but its footer
duplicate is hidden as requested; source legends and actual errors remain visible (the exact
`+NNNs` offset is logged over Serial), uses the original absolute
event times, and does not alter the device clock or shift predictions. Headers
more than 180 seconds ahead, more than 180 seconds old, or missing are rejected.
This is an explicit local tolerance, not proof that predictions are accurate.
The earlier strict rejection described in the historical observations above
explained empty displays despite valid decoded predictions. A later live probe
again observed about two minutes lead while the feed's HTTP Date agreed with
local UTC; the firmware trust anchor alone also validated a real HTTPS download.

## Freshness and display limits

- Probe age is `local Unix time - FeedHeader.timestamp`. `fresh` requires age between zero and `--max-age` (120 seconds default). This threshold is a local application policy, not a published operator SLA.
- Header freshness is necessary but not sufficient: inspect trip/update timestamps where available, prediction times, schedule relationships and uncertainty in the firmware.
- GTFS-RT event `time` is an absolute Unix timestamp, not a minutes value. Firmware needs a synchronized clock; filter the exact stop, route and optional direction, then discard expired events. Delay-only events are not absolute departure times.
- Handle cancelled trips, skipped stops, missing timestamps, malformed data, future timestamps, stale data and network failure explicitly. Do not treat missing entities as proof of no remaining service.
- **Explicit mixed future-day plans:** with a usable live feed, today's remaining predictions stay first while the embedded official GTFS calendars supplement J+1/J+2. Future rows show J+1/J+2, HH:MM and P (planned) or R (realtime announcement); live epochs win exact duplicates. Plans start after the last live announcement within each future day. Missing announcements do not prove today's service has ended, so today remains realtime-only. Network failures, rejected/stale feeds and invalid clocks remain errors, not planned-data fallback triggers. Never extrapolate past the static feed's validity. See [calendar timetable](TIMETABLE.md) for generation, DST/spillover semantics and verification.
- This project does not establish request quotas, uptime, complete network coverage or whether every supplied event is a measured prediction rather than a producer-provided estimate. Poll conservatively and retain clear freshness information.

Specification references: [GTFS Schedule](https://gtfs.org/documentation/schedule/reference/), [GTFS-Realtime](https://gtfs.org/documentation/realtime/reference/).
