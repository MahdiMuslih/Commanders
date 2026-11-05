# Project Plan: A Five-Year MMORPG Built from Scratch

**Document type:** Pre-production planning: prerequisites, requirements and a phased roadmap
**Status:** Draft v2.0 (**Unreal Engine 5 + C++ revision**), to be locked at the end of Phase 0
**Scope:** A full-stack, original-IP, WoW-*style* (third-person, tab-target, zone-based, raid-and-dungeon) MMORPG
**Not in scope here:** implementation code. This document defines what gets built, in what order, and how we know each step is done.

> **IP note.** "WoW-style" describes the genre and the systems: classes, talents, quests, dungeons, raids, an auction house, guilds. Every name, world, character, art asset and piece of lore must be original. Legal checks this at every phase gate (see Constraints in each phase).

### Revision history

| Version | Change |
|---|---|
| v1.0 | Rust shared simulation core, Bevy client, quinn (QUIC) transport. |
| **v2.0** | **Engine and language changed to Unreal Engine 5 and C++.** UE5 is the client and the content toolchain. The shared simulation core is now an engine-independent **C++20** library compiled into both the UE5 client and a **custom headless C++ zone server**. We don't use UE's built-in actor replication for the open world. Phase 0 tests, tech stack, team, budget and risks are updated to match. The web client is dropped. |

---

## 1. Executive Summary

We will build an original-IP, tab-target fantasy MMORPG over 60 months in six gated phases. The phases are: Architecture Lock (M1–6), Vertical Slice (M7–16), Core MMO Systems & Scale (M17–30), Content at Scale (M31–46), Polish/Security/Beta (M47–54), and Launch & Live Ops (M55–60+).

**Unreal Engine 5 with C++** is the client and the content toolchain. World Partition, PCG, Nanite, MetaHuman/Mutable-based character customization and the Unreal Editor give us AAA-proven tooling and cut the cost of making content.

The core technical bet is a **single shared C++20 simulation core** (`SimCore`). It has no Unreal dependency, so the same source is compiled into a UE5 client module and into a **custom headless C++ zone server** built on the **EnTT** ECS. Client prediction and reconciliation therefore run the same movement and combat code on both sides by construction.

We deliberately **don't rely on Unreal's built-in actor replication for the open world.** It's designed for matches of around 100 players per server and has no seamless server-to-server handoff. Instead, clients connect over **QUIC (Microsoft msquic)** to a stateless **edge gateway tier**. Gateways forward to spatially partitioned zone/cell servers running at 20 Hz as **Agones** fleets on Kubernetes. Dynamic **layering** absorbs hotspots, so there are no realm caps. A tiered **area-of-interest** system bounds bandwidth and CPU per player.

All durable state lives in **PostgreSQL**, sharded by character UID across 4,096 virtual buckets with consistent-hash placement. Currency and items are recorded in an **append-only double-entry ledger**. **Valkey (Redis-compatible)** holds only ephemeral state, so any server pod can die and respawn without losing committed data.

AI agents write most of the code. Humans own architecture sign-off, every change to netcode, persistence, economy and security, all art and design direction, and QA.

The largest risks are **content throughput**, the **cost of the custom network layer and the UE↔server bridge**, and **C++ memory safety in AI-generated code**. We handle these with UE's content tools plus an early content-cost model, a Phase 0 network-scale test with a hard gate, and mandatory sanitizers, fuzzing and property-based tests in CI.

---

## 0. Global Definitions & Targets (apply to every phase)

### 0.1 Terminology

| Term | Meaning in this plan |
|---|---|
| **Shard** | One world cluster: a full copy of the open world, made of N zone/cell servers, the gateways in front of them, and instance fleets. Target: **≥10,000 CCU per shard at launch**. |
| **Zone server / Cell** | One Agones `GameServer` pod running the **custom headless C++ server** (not an Unreal dedicated server). It simulates one spatial region: a zone, or a sub-cell of a dense zone. Single-threaded EnTT schedule per cell; many cells per node. |
| **Layer** | A parallel copy of a zone *inside* a shard. It is spawned when zone population passes a threshold (default 300) and merged when it drops. This removes fixed realm caps and queues. |
| **Instance** | A dungeon, raid or battleground. Short-lived Agones `GameServer` running the same C++ server binary, allocated on demand. |
| **Global services** | Account, character, social, chat, guild, mail, auction, matchmaking and so on. Shared across all shards. This is what lets us scale to **100k+ CCU** by adding shards and moving players between shards and layers. |
| **`SimCore`** | The engine-independent C++20 library holding movement, collision queries against exported world geometry, combat math, cooldowns, resources and auras. Compiled into the UE5 client (as a UE module wrapping the library) and into the server. **It must never include Unreal headers.** |
| **Net plugin** | A UE5 plugin (`MMONet`) that owns the msquic connection, decodes snapshots, runs prediction/reconciliation through `SimCore`, and drives Unreal actors as *visual views* of server entities. |
| **World export** | An Unreal commandlet pipeline that exports collision, navigation input geometry, spawn points, interactables and zone metadata from World Partition levels into the server's binary world format. |
| **Hot path** | Client input → gateway → sim tick → state delta → gateway → client. |

### 0.2 Non-negotiable performance budgets

| Metric | Target | How it's measured |
|---|---|---|
| Local feedback for a player action (predicted) | ≤ 1 rendered frame (≤16.7 ms at 60 fps) | Client telemetry (Unreal Insights counters exported to OpenTelemetry) |
| Server-confirmed action-to-feedback | **p95 < 100 ms** for players with ≤ 50 ms RTT to their region (≤ 50 ms network + ≤ 50 ms server) | Bot-swarm and beta telemetry: input timestamp → ack receipt |
| Server internal latency (gateway ingress → sim → gateway egress) | p99 ≤ 50 ms at 20 Hz; architecture must allow 30 Hz (≤ 33 ms) without redesign | OpenTelemetry spans |
| Sim tick duration | p99 ≤ 25 ms (50% of the 50 ms budget), p99.9 ≤ 40 ms | Tick profiler histogram (Tracy plus exported metrics) |
| World-state broadcast (any state change → every interested observer) | Near AoI ring: p99 ≤ 100 ms. Mid ring: p99 ≤ 250 ms. Far ring/zone-wide: **p99.9 ≤ 1,250 ms under full load** | Per-entity replication latency sampling |
| Downstream bandwidth per player | Median ≤ 32 kbps, p99 ≤ 128 kbps (25-player raid combat) | Gateway metrics |
| Zone/layer handoff | Success ≥ 99.99%; visible stall ≤ 250 ms (no loading screen, no Unreal level travel) | Handoff tracer |
| Durable data loss on any single pod/node failure | **0 committed economic/progression transactions.** Soft state (position, HP) RPO ≤ 10 s | Chaos tests |
| Client frame rate | Min-spec ≥ 60 fps median in open world, ≥ 45 fps p5 in capitals; rec-spec ≥ 90 fps median | Automated Unreal performance captures (Gauntlet) |

> **Interpreting "world state broadcast under 1250 ms".** We treat 1,250 ms as the hard ceiling for the *farthest* AoI tier (zone-wide events, distant entities updated at 1 Hz). Gameplay-relevant nearby state is held to the much tighter near and mid ring numbers above.

### 0.3 Client/server consistency rules for `SimCore` (new in v2.0)

The v1.0 plan got cross-platform determinism largely from Rust. In C++ we must enforce it explicitly:

- **Combat, resources, cooldowns and all economic math use integers or fixed-point only.** These must match bit-for-bit between client and server.
- **Movement uses float**, but under strict rules:
  - One compiler family: **clang-cl** for the Windows client module and **clang** for the Linux server, with the same LLVM major version pinned.
  - `-ffp-contract=off`, no fast-math, and no x87.
  - Our own implementations of the transcendental functions `SimCore` uses, with no calls to `std::sin` and friends.
  - Movement divergence must stay within **≤ 1 mm per 10 s of simulated movement**. Any larger error is corrected by server reconciliation, but larger errors are a CI failure.
- `SimCore` has no globals, no wall-clock reads and no allocation on the hot path. Everything it needs comes in as input.

### 0.4 Human oversight model for an AI-primary coding team

- **AI agents** write most code, tests, infrastructure-as-code, tooling and documentation. They work through PRs (Git) or shelved changelists (Perforce) only and cannot submit to protected branches/streams.
- **Human approval is mandatory** (enforced by CODEOWNERS in Git, plus Perforce stream permissions and Swarm reviews) for: `SimCore`, the `MMONet` plugin and wire protocol, the persistence layer and schema migrations, ledger/economy, auth/security, infra and production configs, and anything that touches PII or payments.
- **C++-specific rule:** AI-authored C++ cannot merge unless it passes AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer test runs plus clang-tidy, with no new warnings.
- Every architecture decision is an **ADR** authored by AI and signed off by the Technical Director.
- **Humans own** all art direction, game and narrative design, level design, audio direction, final QA sign-off, community and legal.
- **AI-assisted but human-curated:** quest text drafts, item stat generation within designer-defined budgets, PCG graph authoring, test-case generation.

---

# PART 1: Prerequisites & Requirements by Phase

---

## Phase 0: Pre-Production & Architecture Lock (M1–M6)

### 1. Technical prerequisites (must be true *before* Phase 0 starts)
- Product vision approved: a one-page high concept, target audience (PC, 16+, MMO veterans and lapsed players), business model chosen (**subscription plus cosmetic shop, no pay-to-win, no paid random loot**).
- **Engine decision made: Unreal Engine 5, C++.** An Epic Games developer account exists with GitHub linked, so the team has access to engine source. The EULA has been reviewed by counsel (royalty terms, see Constraints).
- Source control and identity: **Perforce Helix Core** (for the Unreal project and all binary assets), a **GitHub** organization (for `SimCore`, protocol, server, services, infra), SSO (Okta or Google Workspace), hardware security keys for admins, a secrets manager chosen (Vault or cloud KMS).
- Cloud accounts created with an org-level billing guardrail: AWS or GCP, *one* primary chosen, and the second kept for DR in Phase 4.
- AI agent environment: sandboxed Windows and Linux runners with repository access and no production credentials. Windows runners have a source-built UE5 installed.

### 2. Team/resource prerequisites
- **Must be hired:** Technical Director/Principal Architect (human, final technical authority), Game Director, Art Director, Lead Game Designer, Producer, **1 senior networking/server C++ engineer** (human reviewer) and **1 senior Unreal engine/gameplay engineer** (human reviewer, owns the `MMONet` plugin and UE integration). Fractional: legal counsel (IP, privacy, Epic EULA), a security advisor.
- **Tools procured:** Perforce Helix Core (with Swarm), GitHub Enterprise, Linear or Jira, Figma, Blender and/or Maya, Substance 3D (Painter/Designer) seats, Houdini (indie/FX seats for PCG and tool development), Grafana Cloud or a self-hosted observability trial.
- **Build infrastructure:** a **Horde** server (Epic's build and CI orchestrator) with **Unreal Build Accelerator (UBA)**, a shared **Derived Data Cache** (Unreal Cloud DDC), and **Unreal Game Sync** for artists and designers.
- **Infra:** one non-production Kubernetes cluster (EKS/GKE, 3 node pools: system, general, game-server) plus a Windows build pool. Budget ceiling: about $25k/month (higher than v1.0 because of the Unreal build farm).

### 3. Requirements

**Functional (things Phase 0 must produce)**
- Game Design Document v1: core loop, 2 starting races and 4 classes for the slice, plus a full-launch target of 4 races and 8 classes. Combat model (tab-target, GCD 1.5 s, cast/channel, threat), progression to level 50, dungeons, raids, PvP scope.
- Technical Design Document and ADR set covering the UE version and upgrade policy, the `SimCore` boundary, the network protocol, the UE↔server bridge (`MMONet`), the world-export pipeline, the sharding model, persistence, scripting, orchestration, observability, build/CI and source-control layout.
- A **content cost model**: hours per zone, quest, dungeon, creature, armor set and spell VFX, using Unreal tooling (PCG, World Partition, Mutable), with a cost for launch scope.
- Wire protocol spec v0: message taxonomy, channel types, snapshot/delta format, quantization rules, and a schema compiler that generates C++ encoders/decoders for both the client and the server.

**Non-functional: four throwaway technical tests** (code is discarded; findings are kept)
- **Test A, server sim and network scale.** A headless C++ server (EnTT + msquic datagrams + `SimCore` prototype) and a lightweight headless C++ bot client (no Unreal). Target: **1,000 bots in one cell at 20 Hz with p99 tick ≤ 25 ms** on a single 8-vCPU node.
- **Test B, client fidelity at MMO crowd scale.** A UE5 client rendering **300 animated player characters with modular armor** (Mutable or skeletal-mesh merging, Animation Budget Allocator, animation sharing, LOD/impostors for distant players) and a 2 km² World Partition landscape with PCG foliage.
  - Min-spec (GTX 1070 / RX 5500 XT 8 GB, 6-core CPU, 16 GB RAM, SSD) at **≥ 60 fps** with Lumen off and baked/sky lighting.
  - Rec-spec (RTX 3060 / RX 6600) at **≥ 90 fps**.
- **Test C, `SimCore` consistency.** The same `SimCore` source built into a UE5 module (Windows, clang-cl) and the Linux server (clang). A 10M-step randomized run checks the §0.3 rules: integer/fixed-point systems bit-identical, movement within tolerance.
- **Test D, the Unreal-native alternative (for the record).** An Unreal dedicated server using **Iris** replication + Replication Graph + Mass, measuring maximum players per process at p99 tick ≤ 25 ms. This documents why the custom server is chosen. If the result is unexpectedly strong (≥ 1,000 players per process), the ADR re-opens the question at the gate.

Also required:
- A threat model (STRIDE) for auth, gateway, sim and economy.
- Privacy-by-design data inventory (which PII is stored where).

**Constraints**
- **Legal:** original-IP clearance process; trademark search on the working title; GDPR, UK GDPR, CCPA/CPRA plan; age gate 13+ (COPPA-avoidant) with regional overrides (EU 16 where required); a stance on loot-box regulation (none paid).
- **Unreal licensing:** the standard EULA charges **5% royalty on gross revenue above $1M** per product, including subscriptions and shop sales. Lower rates may be available if the game also launches on the Epic Games Store. Counsel and the Producer must model this. Phase 0 includes a **request to Epic for a custom license quote**, and the business case must hold at the standard 5%.
- **Budget:** Phase 0 roughly $1.2–1.7M (people, cloud, build farm). Total 5-year development envelope: **$47–78M** (v1.0 figure plus the Unreal build infrastructure and Perforce). Engine royalties are revenue-linked and modeled separately.
- **Platform:** **Windows 10/11 x64 (DX12)** is the primary target. Steam Deck via Proton is a secondary target. Native Linux and macOS clients are post-launch stretch goals. **No web client**: Unreal has no practical browser target, and Pixel Streaming is too expensive at MMO scale.

### 4. Exit criteria / gate (Phase 0 → Phase 1)
| # | Criterion | Pass condition |
|---|---|---|
| G0.1 | Test A (server scale) | 1,000 bots in one cell at 20 Hz, p99 tick ≤ 25 ms, 30-minute soak, no memory growth > 5%, ASan/UBSan clean in the debug soak |
| G0.2 | Test B (client fidelity) | Min-spec and rec-spec fps targets met in a captured benchmark scene. **If this fails:** raise min-spec one GPU tier or lower crowd LOD targets. The engine choice is not revisited. |
| G0.3 | Test C (`SimCore` consistency) | 0 integer/fixed-point divergences; movement within §0.3 tolerance across 10M steps |
| G0.4 | Test D (UE-native alternative) | Report published; custom-server ADR signed with the measured numbers |
| G0.5 | ADRs | ≥ 25 ADRs signed by the Technical Director, including all items in the §2.3 tech stack table |
| G0.6 | GDD v1 + content cost model | Signed by the Game Director and Producer. Launch scope fits the budget within ±15% |
| G0.7 | Threat model | Reviewed by the security advisor; mitigations mapped to phases |
| G0.8 | Build infrastructure | A clean Horde build of the UE5 project and the server in ≤ 60 min cold and ≤ 15 min incremental |
| G0.9 | Hiring and legal | Phase 1 roles signed or offers accepted; working-title trademark clear; privacy plan approved; Epic royalty modeled in the business case |

---

## Phase 1: Core Foundation & Vertical Slice (M7–M16)

### 1. Technical prerequisites
- All Phase 0 gates passed. UE5 version and server toolchain locked by ADR.
- **UE version policy:** a source-built engine fork on a pinned UE 5.x release. Upgrade at most once per year, never during a gate quarter. Engine modifications are kept to a minimum and listed in an engine-changes register.
- **Repository layout:**
  - Git monorepo: `SimCore/`, `Protocol/` (schema plus generated C++), `Server/` (zone server, gateway), `Services/`, `Tools/`, `Infra/`, `Content/` (design data).
  - Perforce depot: the Unreal project (`Game/`, `Plugins/MMONet`, art).
  - CI mirrors pinned `SimCore` and `Protocol` releases from Git into Perforce `ThirdParty/`, and every build stamps the `SimCore` commit hash. **A client and server with mismatched hashes refuse to connect.**
- Art pipeline contract: DCC (Blender/Maya) → FBX/USD → Unreal, with naming, LOD, Nanite eligibility, a shared humanoid skeleton and the modular armor slot specification.
- World export pipeline v0 working from Test C/A (collision and nav geometry into the server format).
- Wire protocol spec v0 approved.

### 2. Team/resource prerequisites
- **Humans:** +1 senior gameplay engineer (reviewer), +1 **build/release engineer** (Horde, UBA, Perforce, cooking), +1 tools engineer (Unreal Editor plugins), +1 DevOps/SRE, 2 concept artists, 2 3D character/environment artists, 1 animator, 1 technical artist, 2 game designers (systems and quests), 1 QA lead, 1 audio designer (contract). Roughly 20–24 people.
- **Infra:** Horde agents (Windows with GPUs for cooks and render smoke tests, Linux for the server), Testcontainers-capable Linux runners, dev and staging clusters, the CloudNativePG operator, an artifact registry (OCI) and a CDN bucket for client builds.
- **Tools:** Unreal Insights (client), Tracy (server), RenderDoc/PIX, crash reporting (Unreal crash reporter feeding Sentry or BugSplat, with symbol upload from Horde), Grafana stack.

### 3. Requirements

**Functional (the vertical slice)**
- **World:** one zone of about 1.5 km² in World Partition with PCG foliage, a hub town, 2 sub-areas and a day/night cycle.
- **Gameplay:** 2 races, 4 classes (tank, healer, melee DPS, caster DPS) at levels 1–10, 6 abilities each. Tab-targeting, auto-attack, GCD, cast bars, threat, death and resurrection.
- **Content:** 15 quests (kill, collect, escort, interact), 12 creature types, 1 five-player dungeon with 3 bosses. Server-side logic scripted in Luau.
- **Systems:** account creation and login, character create/select/delete (with Mutable-based customization), inventory and equipment, loot, vendors, XP and leveling, party (5), basic chat (say, party, zone), basic UI built with UMG + CommonUI (unit frames, action bars, quest log, bags, minimap).
- **Netcode (in `MMONet`):** client prediction for movement and ability *starts* through `SimCore`, reconciliation with input replay, entity interpolation (100 ms buffer), snapshot delta compression, near/mid/far AoI. Unreal actors are spawned, pooled and despawned as views of server entities.
- **World export v1:** collision, navmesh input geometry (baked by Recast/Detour on the server side), spawn points and interactables exported from the slice zone. CI fails if the exported data is stale relative to the level.

**Non-functional**
- **Performance:** 500 bots plus 20 humans in the slice zone on a single cell. p99 tick ≤ 25 ms. All §0.2 hot-path budgets met at ≤ 50 ms RTT. Client meets min-spec fps with 40 players visible.
- **Persistence:** characters, inventory and quest state in Postgres. A crash of the zone pod loses 0 committed transactions.
- **Testing:**
  - GoogleTest unit tests and **RapidCheck** property-based tests for combat math, inventory conservation, currency conservation and `SimCore` consistency.
  - **Testcontainers** (testcontainers-go harness) provisions Postgres, Valkey and NATS for service tests *and* for the C++ server integration tests.
  - **Unreal Automation tests and Gauntlet** for client functional and performance tests.
  - Sanitizer builds (ASan, UBSan, TSan) of the server run nightly.
  - libFuzzer on every protocol decoder.
- **Security:** inputs only (no client state assertions). Server-side movement speed, cooldown and range validation. Per-connection rate limits.

**Constraints**
- **Platform:** Windows client builds are mandatory on every change to the Unreal project. The server builds on Linux only.
- **Budget:** cloud plus build farm ≤ $35k/month.
- **Legal:** contributor/contractor IP assignment for every artist and designer. Asset provenance log (no unlicensed references in shipped assets; Fab/Marketplace asset licenses recorded per asset).

### 4. Exit criteria / gate (Phase 1 → Phase 2)
| # | Criterion | Pass condition |
|---|---|---|
| G1.1 | Playable slice | 20 internal humans complete the zone and dungeon in a 2-hour session. ≥ 70% rate it "fun to replay" on an internal survey (design gate, human-judged) |
| G1.2 | Server perf | 500 bots + 20 humans, p99 tick ≤ 25 ms, 2-hour soak |
| G1.3 | Hot path | Server-confirmed action-to-feedback p95 < 100 ms at 50 ms emulated RTT, 2% packet loss |
| G1.4 | `SimCore` consistency | 10M-step randomized run (Windows client module vs Linux server): 0 integer/fixed-point divergences, movement within §0.3 tolerance |
| G1.5 | Durability | Chaos: 50 random zone-pod kills during scripted trading/looting → 0 lost or duplicated items/currency (ledger reconciliation passes) |
| G1.6 | Memory safety | 7 consecutive nightly sanitizer runs clean; 24 h of protocol fuzzing with 0 crashes |
| G1.7 | CI/CD | Main stream green ≥ 95% of days. Incremental client build + cook ≤ 30 min; server pipeline ≤ 20 min. Every merge deploys automatically to dev |
| G1.8 | Art pipeline | Artist-to-in-game for a new prop ≤ 15 min without engineer help; a level edit reaches the dev shard (via world export) ≤ 30 min |

---

## Phase 2: Core MMO Systems & Server Scalability (M17–M30)

### 1. Technical prerequisites
- Vertical slice gates passed. `SimCore` APIs stable at v1 (semantic versioning; breaking changes need an ADR).
- Microservice boundaries defined (bounded-context map) with gRPC/protobuf contracts versioned in `Protocol/`.
- **Services language locked by ADR: Go** for global services (fast development, strong gRPC and Postgres tooling, simple concurrency). Performance-critical game processes (zone server, gateway, persistence writer) stay **C++**. *If the project requires C++ for every process, services move to C++ (gRPC C++ + libpqxx) and Phase 2 grows by about 2 months; see §4, Risk 5.*
- Database sharding design locked: 4,096 virtual buckets (`bucket = xxhash64(character_uid) mod 4096`), a bucket→cluster directory in Postgres (replicated, cached in Valkey), and a bucket-migration procedure designed.
- Two bot clients exist:
  - **Headless C++ bot** (`SimCore` + protocol + msquic, no Unreal): thousands per node, for scale.
  - **Unreal client in `-nullrhi` mode** driven by automation: dozens per node, for full-fidelity end-to-end checks.

### 2. Team/resource prerequisites
- **Humans:** +1 senior backend engineer (distributed systems, Go), +1 SRE, +1 DBA/data engineer, +1 security engineer, +1 Unreal tools engineer (content tooling), 2 more designers (economy, PvE encounters), +2 QA. Art headcount grows toward Phase 3 (environment and outsourcing coordination). Roughly 33–38 people.
- **Infra:** a load-test cluster able to burst to **≥ 400 vCPU for bots plus ≥ 300 vCPU for servers**. Agones installed. NATS JetStream and Redpanda clusters. Multi-AZ staging.
- **Tools:** k6 for HTTP/gRPC load, a custom bot-swarm controller, Chaos Mesh, pgbench, Argo CD, Argo Rollouts.

### 3. Requirements

**Functional**
- **World topology:** multi-zone shard with **seamless zone transitions** (no Unreal level travel: the client streams World Partition cells while `MMONet` switches the server owner), a cell split for dense zones, **dynamic layering** (spawn and merge), and party-aware layer assignment (party and guild members are pulled into the same layer).
- **Instancing:** on-demand dungeon/raid instances via Agones allocation, with instance lockouts.
- **Global services:** Auth/Account, Character, World Directory/Matchmaker, Chat (channels, whisper, guild, cross-shard), Social (friends, ignore, presence), Guild, Mail, **Auction House**, Trade, Group Finder, Leaderboards.
- **Economy:** append-only, double-entry ledger for all currency and item-ownership transfers. Every item has a unique instance ID and a provenance chain.
- **Login queue fallback:** only for global overload protection. It is not a realm cap.
- **Content tooling v1 (Unreal Editor plugins):** spawn and patrol placement, quest-object placement, encounter preview, a "push to dev shard" button that runs world export and hot-reloads the server zone, and a design-data editor for the content DB.

**Non-functional**
- **Scalability:** horizontal scale of every stateless service under HPA. No single point of failure (multi-AZ, ≥ 3 replicas, PodDisruptionBudgets).
- **Performance:** 10,000 bot CCU on one shard. AH search p99 ≤ 200 ms. Chat fan-out p99 ≤ 300 ms global.
- **Durability:** Postgres HA (CloudNativePG, synchronous replica in a second AZ). PITR backups. RPO 0 for ledger, RTO ≤ 5 min per bucket cluster.
- **Operability:** zero-downtime rolling deploys for services; zone-server **drain-and-migrate** (players are handed off to a fresh pod running the new build).
- **Security:** mTLS service-to-service (Linkerd), signed session tokens (short-lived, rotating), a WAF on HTTP ingress, and a DDoS posture (cloud L3/L4 protection plus gateway-level per-IP admission).

**Constraints**
- **Budget:** cloud plus build farm ≤ $70k/month steady, plus load-test bursts approved per event.
- **Legal:** the data-retention policy is implemented (ledger kept 7 years; chat logs 90 days, unless an abuse report extends them).
- **Platform:** the client must still meet min-spec with 40 players visible in a city.

### 4. Exit criteria / gate (Phase 2 → Phase 3)
| # | Criterion | Pass condition |
|---|---|---|
| G2.1 | Single-shard scale | **10,000 bot CCU** on one shard, 4-hour soak. p99 tick ≤ 25 ms in every cell. Hot-path p95 < 100 ms. Far-ring broadcast p99.9 ≤ 1,250 ms |
| G2.2 | Hotspot | 2,000 bots converge on one city. Layering keeps every layer ≤ target pop. No tick > 40 ms p99.9 |
| G2.3 | Multi-shard | **30,000 bot CCU across 3 shards** sharing global services, 2-hour soak, with no global-service saturation (CPU < 60%) |
| G2.4 | Chaos | Random kills of zone pods, gateway pods, service pods, a Postgres primary and a Valkey primary during the soak → **0 ledger discrepancies**, reconnect ≤ 15 s, and no player-visible data loss beyond soft-state RPO |
| G2.5 | Bucket migration | Live migration of 256 buckets between clusters under load. Affected players see ≤ 2 s stall. 0 data loss |
| G2.6 | Zero-downtime deploy | Rolling deploy of every service plus a zone drain under 5k CCU: < 0.1% disconnects |
| G2.7 | Economy invariants | RapidCheck/rapid property tests plus a production-like simulation of 10M random AH/trade/mail ops: ledger sum conserved, no item duplication |
| G2.8 | Seamless travel | 1,000 scripted cross-zone runs by the Unreal `-nullrhi` bots: 0 loading screens, stall ≤ 250 ms p99 |
| G2.9 | Content tooling | A designer places a new quest NPC and objective in the Unreal Editor and plays it on the dev shard in ≤ 10 min, without engineer help |

---

## Phase 3: Content Production & World Building at Scale (M31–M46)

### 1. Technical prerequisites
- Phase 2 gates passed. **Engine and engineering feature freeze on core systems**: new systems only via the change-control board. **No Unreal Engine version upgrades during Phase 3** unless a security or platform issue forces one.
- Content tooling is production-ready:
  - **Unreal Editor** is the world editor: World Partition, PCG graphs for biomes and dressing, Data Layers for phasing, custom plugins for spawns, quests and encounters.
  - **Content DB:** design data as typed schemas (in Git, compiled to binary for the server and imported as Unreal DataAssets for the client). A web or in-editor data editor that opens PRs. Schema validation in CI.
  - **Luau scripting:** hot reload on dev shards, a debugger, and per-script CPU/memory budgets.
  - **Encounter sandbox:** run boss fights with bot raids for tuning.
  - **World export** runs automatically on every level submit.
- Asset budgets defined per zone (Nanite and non-Nanite triangle budgets, draw calls, texture streaming pool, World Partition streaming radius).

### 2. Team/resource prerequisites
- **Peak headcount:** about 60–75, including **outsourcing partners** for environment props, armor sets and creature models (2 vendors, so we're never single-sourced; both must be experienced in Unreal-ready asset delivery).
- **Human roles added:** 4–6 level/world designers, 4 quest designers/writers, 2 encounter designers, 1 narrative lead, 3 more animators, 2 VFX artists (Niagara), 1 UI/UX designer, 1 composer (contract), a localization vendor, +4 QA (functional plus content), a community manager (for alpha).
- **Infra:** a persistent internal alpha shard (24/7). A CDN for patch distribution. A launcher with delta patching built on Unreal IoStore chunks.

### 3. Requirements

**Functional (launch content scope, committed)**
- **World:** 2 continents, **14 open-world zones** (levels 1–50) plus 4 capital/hub cities.
- **Classes and races:** 8 classes × 3 specializations, 4 races (2 per faction), with talents.
- **Quests:** about **1,400 quests** total (~100 per zone; AI drafts the text, humans edit and own it).
- **Instances:** 8 five-player dungeons, **2 raids** (10/25 players, 6 + 8 bosses), 2 battlegrounds plus world-PvP objectives.
- **Professions:** 4 gathering, 6 crafting, with a full crafting-economy loop into the Auction House.
- **Account systems:** account-wide cosmetics/mounts and achievements.
- **Localization:** EN, DE, FR, ES, PT-BR at launch (KO, ZH-TW post-launch). Unreal's localization pipeline (String Tables, gather/export) is set up from M31; server-side strings use the same string IDs.

**Non-functional**
- Content must stay inside its performance budget. CI runs **automated Gauntlet perf captures** (bot camera flythroughs per zone) and fails a build that regresses fps > 5% on min-spec hardware in the test lab.
- Every quest has an automated **bot completion test** (the headless bot completes the quest end-to-end on a dev shard nightly).
- The scripting sandbox enforces instruction-count and memory limits. A runaway script is killed and reported without stalling the tick.
- Cook and patch size: full install ≤ 60 GB; typical content patch ≤ 3 GB.

**Constraints**
- **Budget:** about 55% of the total program budget lands here (people plus outsourcing). Cloud plus build farm ≤ $90k/month.
- **Legal:** cultural review of content per launch region; ratings submissions prepared (ESRB/PEGI/USK); music and voice rights cleared for worldwide perpetual use; Fab/Marketplace asset licenses audited.
- **Schedule:** content is produced in **4 waves of 4 months**. Each wave ends with a playable internal milestone.

### 4. Exit criteria / gate (Phase 3 → Phase 4)
| # | Criterion | Pass condition |
|---|---|---|
| G3.1 | Content complete | 100% of launch zones, dungeons, raids and quests are *content complete* (playable start to finish, placeholder audio/VFX allowed in ≤ 10%) |
| G3.2 | Quest automation | ≥ 98% of quests pass nightly bot-completion tests 5 nights in a row |
| G3.3 | Leveling | 3 internal testers reach 1→50 in 90–130 hours of /played without blocking bugs |
| G3.4 | **Bot swarm at scale** (prerequisite for Phase 4) | **50,000 bot CCU across ≥ 5 shards** running *real content* (questing, dungeons, AH, chat), 8-hour soak, all §0.2 budgets met |
| G3.5 | Client perf | Min-spec ≥ 45 fps p5 in capital cities with 100 players visible; ≥ 60 fps median elsewhere; no shader-compilation hitches > 50 ms after the first run (PSO cache shipped) |
| G3.6 | Alpha | Internal plus friends-and-family alpha (≤ 500 people) for ≥ 8 weeks. Crash-free sessions ≥ 97% |
| G3.7 | Localization | All strings extracted; ≥ 90% translated in the 5 launch languages |

---

## Phase 4: Polish, Security, and Large-Scale Testing (M47–M54)

### 1. Technical prerequisites
- Phase 3 gates passed. **Content lock** (new content only via exception).
- A production environment exists in **≥ 2 regions** (NA and EU), each multi-AZ, and is built only from Terraform/Argo CD. A disaster-recovery region is designated.
- Payments integrated with a merchant of record (Xsolla or Paddle for global VAT/GST handling, or Stripe plus a tax engine).
- Customer-support tooling (GM tools, ticketing, account actions) is functional and audited.
- Storefront decision made (own launcher, Steam, Epic Games Store) and the Unreal royalty rate confirmed for that choice.

### 2. Team/resource prerequisites
- **Humans:** QA grows to about 15 (including an outsourced test partner). A live-ops producer, 2 GMs or support leads, a data analyst, a 24/7 on-call SRE rotation (≥ 6 people), community team ×3, a marketing partner.
- **External:** third-party **penetration test firm** (scope includes the client binary and the `MMONet` protocol), **Easy Anti-Cheat via Epic Online Services** (free, native Unreal integration) for client integrity layered on server authority, a DDoS-mitigation provider for UDP (e.g., Cloudflare Spectrum or a cloud-native equivalent), and a load-test partner for real-client distributed tests.

### 3. Requirements

**Functional**
- Closed beta (M47–M51) and open-beta stress weekends (M52–M53).
- Character transfers and **cross-shard layering** live.
- Full GM toolset: ban/suspend, item restore from ledger, chat moderation, and a "rollback a player to a ledger point" tool.
- Telemetry and analytics: funnel, retention, economy dashboards (money supply, sinks/faucets, price indices).
- In-game reporting and an automated toxicity filter (chat).

**Non-functional**
- **Security:** pen test with 0 critical and 0 high findings open. Server-side heuristics for speed/teleport/no-clip and cooldown abuse, bot-farming detection v1, and gold-seller detection on ledger patterns. Rate limiting on every endpoint. Secrets rotation. Client binary hardened (symbols stripped from shipping builds, protocol encryption via QUIC TLS, EAC enabled).
- **Performance:** 100k bot CCU (see the gate). Patch download ≤ 60 GB at install, with deltas ≤ 3 GB typical.
- **Reliability:** SLOs defined and dashboards live. Login availability 99.9%, shard availability 99.5% excluding scheduled maintenance (target: none needed).
- **Compliance:** GDPR data export/delete flows tested; age rating certificates received.

**Constraints**
- **Budget:** beta cloud ≤ $150k/month peak. Marketing spend is a separate budget.
- **Legal:** EULA, ToS, Privacy Policy and a Code of Conduct published. Refund policies per region (EU 14-day rules). Required Unreal Engine attribution in credits and on packaging.
- **Platform:** Steam/Epic storefront requirements met (if selling there) plus our own launcher; Steam Deck compatibility review passed.

### 4. Exit criteria / gate (Phase 4 → Phase 5)
| # | Criterion | Pass condition |
|---|---|---|
| G4.1 | **100k CCU** | 100,000 bot CCU across ≥ 10 shards in 2 regions, 12-hour soak, all §0.2 budgets met, global services < 60% CPU |
| G4.2 | Real-human stress | Open-beta weekend peak ≥ 25,000 humans. Login p95 ≤ 5 s, crash-free sessions ≥ 98.5%, no Sev-1 > 30 min |
| G4.3 | Security | Pen test: 0 critical/high open. Anti-cheat (server heuristics plus EAC) catches 100% of the internal red-team's 10 scripted cheat classes in staging |
| G4.4 | DR | Full region-failover drill: login restored in the other region ≤ 30 min, 0 ledger loss |
| G4.5 | Quality | 0 open Sev-1 bugs. ≤ 25 Sev-2 with approved workarounds |
| G4.6 | Economy | 4 weeks of beta economy data: money supply growth within the design band (±20% of model) |
| G4.7 | Ops readiness | Runbooks for the top 30 incident types; on-call rotation staffed; 2 game-day exercises passed |
| G4.8 | Go/No-Go | Signed by the Game Director, Technical Director, Producer, Head of Live Ops and Legal |

---

## Phase 5: Launch, Live Operations, and Post-Launch Sustainment (M55–M60 and beyond)

### 1. Technical prerequisites
- All Phase 4 gates passed. Launch build is tagged and reproducible from source (Perforce changelist plus Git commit, recorded together in Horde).
- Capacity is pre-provisioned for **2.5× forecast peak CCU** (reserved plus on-demand burst limits raised with the cloud provider in writing).
- Kill switches (feature flags) exist for every non-core system: AH, mail, trade, group finder, shop, specific quests, specific instances.
- Hotfix pipeline: server-side data/script hotfix in ≤ 30 min. Client patch in ≤ 4 h (including cook and IoStore chunk diff).

### 2. Team/resource prerequisites
- **Live team:** live-ops producer, 24/7 SRE/on-call, 24/7 GM/support (in-house plus a vendor for follow-the-sun), community, economy designer, data team. Content team pivots to the first content update (x.1) and the first expansion pre-production.
- **Vendors:** CDN contracts for launch-week bandwidth, a support desk vendor, a localization vendor on retainer.

### 3. Requirements

**Functional**
- Launch with headstart (early access by pre-order), with login-queue protection for global overload only.
- Live event system (seasonal events driven by data and Luau server scripts, plus Data Layers in the client, with no client code patch).
- Cadence: **patch x.0.5 at +8 weeks, content update x.1 at +16 weeks**, and expansion pre-production from M57.

**Non-functional**
- **SLOs:** login ≥ 99.9%, gameplay availability ≥ 99.5% (excluding announced maintenance; target is zero-downtime). Hot-path p95 < 100 ms.
- **Cost:** infra cost per CCU tracked weekly. Target ≤ $0.35 per peak-CCU per day by M60.
- **Security:** a weekly anti-cheat ban-wave cadence; ledger forensics for every reported dupe within 24 h.

**Constraints**
- Regulatory: consumer-protection rules on virtual currency, regional payment rules, and data-residency obligations (EU data stays in the EU region).
- Unreal royalty reporting to Epic each quarter.

### 4. Exit criteria (Launch success / move to steady-state sustainment, evaluated at M60)
| # | Criterion | Pass condition |
|---|---|---|
| G5.1 | Stability | 30 consecutive days without a Sev-1 lasting longer than 60 min |
| G5.2 | Retention | D30 retention ≥ 35% of launch-week subscribers (business gate, set in Phase 0 model) |
| G5.3 | Economy | No unresolved duplication exploit. Inflation within the design band |
| G5.4 | Cadence | x.0.5 and x.1 shipped on schedule ±2 weeks |
| G5.5 | Cost | Infra cost per CCU at or below target. Autoscaling verified through a 3× weekend-to-weekday swing |

---

# PART 2: Five-Year Phased Roadmap

## 2.1 Timeline at a glance

```
Year:      |------ Y1 ------|------ Y2 ------|------ Y3 ------|------ Y4 ------|------ Y5 ------|
Month:     1    6 7        16 17           30 31                      46 47      54 55        60
Phase 0    [=====]
Phase 1          [=========]
Phase 2                     [=============]
Phase 3                                    [===============]
Phase 4                                                     [=======]
Phase 5                                                             [=====]→ live
Art pre-prod (parallel)  [~~~~~~~~~~~~~~~~~~~~~~]   (concept/style/pipeline, feeds Phase 3)
Tools team (parallel)                   [~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~]  (Unreal Editor plugins, from M22)
Milestones: M6 Arch Lock · M16 Vertical Slice · M30 10k-CCU Shard · M40 Int. Alpha · M46 Content Complete
            M47 Closed Beta · M52 Open Beta · M55 LAUNCH · M59 Content Update x.1
```

Phases are **gated**: engineering work for phase N+1 starts only after gate N passes. Art and design pre-production for later phases runs in parallel and is labelled as such. A failed gate allows **one 6–8 week remediation sprint** taken from the schedule buffer (see §4).

## 2.2 Roadmap table

| Phase | Months | Objective | Key Workstreams | Tech Decisions (choice → why) | Deliverables | Gate Criteria | Risks → Mitigation |
|---|---|---|---|---|---|---|---|
| **0 Pre-Production & Architecture Lock** | M1–M6 (6) | Lock the architecture, the scope and the cost model with evidence from four throwaway tests. | **Client:** UE5 crowd-rendering test (Mutable, Animation Budget Allocator, World Partition, PCG). **Server:** headless C++ EnTT sim test. **Net:** msquic datagram test, protocol spec v0 and schema compiler. **`SimCore`:** consistency test across clang-cl/clang. **DB:** sharding/ledger design. **Infra:** non-prod K8s, Terraform skeleton, Horde + UBA + DDC, Perforce. **Tooling:** AI-agent sandbox on Windows and Linux. | See §2.3. Key decisions: **UE5 for the client and tools** (proven AAA content pipeline). **Custom C++ server sharing `SimCore`** (UE replication isn't built for 10k-player seamless worlds). **C++20 everywhere in game processes.** | GDD v1, TDD, ≥ 25 ADRs, protocol spec v0, threat model, content cost model, Test A–D reports with benchmarks, Epic royalty model | G0.1–G0.9 | **Custom net layer too slow/costly** → Test A gate; GameNetworkingSockets as a transport fallback. **Min-spec too low for UE5** → raise one GPU tier. **Scope too big** → the cost model forces cuts *now*. |
| **1 Core Foundation & Vertical Slice** | M7–M16 (10) | Prove the full stack end to end with a slice that's fun to replay and meets every hot-path budget. | **Client:** `MMONet` plugin (prediction, reconciliation, interpolation, entity→actor views), character controller on `SimCore`, UMG/CommonUI framework, Mutable characters, MetaSounds audio. **Server:** zone sim, combat, AI/pathing (Recast/Detour on exported geometry), Luau host. **DB:** character/inventory/quest schema, ledger v1. **Net:** gateway v1, snapshot deltas, AoI rings. **Infra:** dev/staging, Agones PoC, CNPG. **Tooling:** world export v1, Horde CI/CD, Testcontainers, RapidCheck, sanitizer nightlies, fuzzing, Gauntlet, crash pipeline. | **Luau embedded in the C++ server** for content scripts → sandboxed, typed, fast, hot-reloadable, written in C++ so it embeds cleanly. **libpqxx + Postgres 17** for the C++ persistence writer. **Schema-generated bit-packed C++ codecs** for the hot path; **protobuf** for services. | Playable slice build, headless C++ bot v1, Horde pipeline, world export v1, ledger v1, perf dashboards, Slice Review report | G1.1–G1.8 | **Prediction drift** → §0.3 rules and the `SimCore` consistency test in CI. **AI C++ defects** → sanitizers, fuzzing, CODEOWNERS on critical modules, weekly human architecture review. **Slow UE builds** → UBA, shared DDC, incremental cooks. |
| **2 Core MMO Systems & Server Scalability** | M17–M30 (14) | Turn the slice into a horizontally scalable, fault-tolerant 10k-CCU shard with every core MMO service. | **Client:** seamless World Partition streaming across server owners, social UI, AH UI, group finder. **Server:** multi-zone shard, cell split with ghost entities, layering, zone handoff, instances. **Services (Go):** auth, character, directory, chat, social, guild, mail, AH, trade, group finder, leaderboards. **DB:** bucket sharding, directory, migration tooling, PITR. **Net:** gateway session migration, QUIC connection migration, TCP fallback for UDP-blocked networks. **Infra:** Agones fleets/autoscaler, HPA, Linkerd, NATS, Redpanda, Argo Rollouts. **Tooling:** bot-swarm controller, Chaos Mesh suites, Unreal Editor content plugins v1. | **Go for global services** → faster to build and operate than C++ for CRUD/RPC-heavy work, with strong gRPC and Postgres libraries. **Agones** → standard for stateful game-server lifecycle on K8s. **NATS JetStream** → low-latency service messaging and chat fan-out. **Redpanda** → durable audit/analytics stream. **Valkey** → open-license Redis-compatible cache. **Application-level bucket sharding** over Citus/Yugabyte → predictable, no exotic distributed-SQL tail latency. | 10k-CCU shard, all global services, sharded DB with live migration, chaos test suite, bot-swarm platform, content tooling v1, runbooks v1 | G2.1–G2.9 | **Hotspot collapse** → layering plus cell split plus AoI LOD. **DB bottleneck** → buckets allow re-placement. Write-behind for soft state; synchronous commit only for ledger. **Service sprawl** → bounded-context map; ≤ 12 services at launch. **Client hitches during handoff** → handoff decoupled from World Partition streaming; G2.8 gate. |
| **3 Content Production & World Building** | M31–M46 (16) | Produce the full launch world and content at quality and inside performance budgets, using tooling that scales human effort. | **Client:** continent-scale World Partition, HLODs, Nanite environments, Niagara VFX, PSO caching, localization. **Server:** encounter scripting, PvP/BGs, professions, achievements. **DB:** content data versioning. **Net:** raid-scale optimization (40 entities in combat in near ring). **Infra:** 24/7 alpha shard, launcher/CDN. **Tooling:** PCG biome graphs, encounter sandbox, nightly quest bots, Gauntlet perf captures, automatic world export. | **Unreal Editor as the world editor** with **PCG** for dressing → big productivity gain over hand-placing. **Content data in Git** (typed schemas → binary for server, DataAssets for client) → diffable, reviewable, CI-validated. **IoStore chunked delta patching** → small patches. **Outsourcing** for props/armor → throughput. | 14 zones, 4 cities, ~1,400 quests, 8 dungeons, 2 raids, 2 BGs, 8 classes, professions, localization ×5, internal alpha | G3.1–G3.7 (incl. **50k bot CCU**) | **Content under-delivery** → 4-month waves with burn-down, the cost model updated each wave, cut-list pre-agreed (e.g., 2nd raid → x.1). **Perf regressions from art** → Gauntlet captures fail the build. **Shader hitches** → PSO cache gathering in every alpha session. |
| **4 Polish, Security & Large-Scale Testing** | M47–M54 (8) | Make it secure, stable, polished and proven at 100k CCU with real humans before money changes hands. | **Client:** polish, accessibility, crash fixing, EAC integration. **Server:** cheat heuristics, bot detection, GM tools. **DB:** DR replication, GDPR flows. **Net:** DDoS hardening, gateway admission control. **Infra:** 2-region prod, DR region, SLOs. **Tooling:** analytics dashboards, support desk. | **Easy Anti-Cheat via Epic Online Services** as an *integrity layer* (never a replacement for server authority) → free and native to Unreal. **Merchant of record** (Xsolla/Paddle) → global tax compliance. **Cloud L3/L4 DDoS plus UDP proxy protection.** | Closed beta, open-beta stress weekends, pen-test report, DR drill report, SLO dashboards, launch candidate build | G4.1–G4.8 (incl. **100k bot CCU**) | **Beta reveals design problems** → closed beta starts at M47, giving 8 months to react. **Security findings late** → pen test at M48 *and* M53. |
| **5 Launch, Live Ops & Sustainment** | M55–M60+ (6) | Launch stably and settle into a predictable live-ops cadence with sustainable unit costs. | **Client:** hotfix cadence, event content via Data Layers. **Server:** live events, economy tuning, ban waves. **DB:** capacity management, partition rotation. **Infra:** cost optimization, reserved capacity. **Tooling:** live dashboards, incident management. | **Feature flags everywhere** (kill switches). **Reserved plus spot** for bot/test, **on-demand** for game servers at launch, moving to reserved after 90 days. | Launch, x.0.5 patch, x.1 content update, expansion pre-production pitch, post-launch review | G5.1–G5.5 | **Launch overload** → 2.5× capacity, headstart, global admission queue. **Day-1 exploits** → ledger forensics plus item-restore tooling plus per-feature kill switches. |

## 2.3 Consolidated Tech Stack (locked by Phase 0 ADRs)

| Layer | Choice | Justification | Rejected alternatives (why) |
|---|---|---|---|
| **Client engine** | **Unreal Engine 5**, source-built, pinned 5.x release, C++ | AAA-proven rendering, animation, VFX (Niagara), audio (MetaSounds) and UI (UMG/CommonUI). World Partition, HLOD and PCG for very large worlds. Mutable for modular character customization. Shipped MMO precedent with Unreal clients. Team already has UE5 C++ experience | Unity (C#, not C++; weaker large-world tools; pricing-trust concerns); Bevy (v1.0 choice: tooling immaturity was a top-5 risk) |
| **Language** | **C++20** for the client, `SimCore`, zone server, gateway and persistence writer | One language for every performance-critical process; UE is C++ natively; large body of public code for AI agents | Rust (v1.0 choice; would need FFI into UE); C# server (GC pauses at 10k-entity scale) |
| **Shared simulation** | **`SimCore`**: engine-independent C++20 library, integer/fixed-point for combat and economy, strict-float rules for movement (§0.3) | Same code predicts on the client and decides on the server; no Unreal headers, so the server stays lean and free of engine dependencies | Duplicating logic in UE gameplay code and server code (guaranteed divergence) |
| **Server simulation** | **Custom headless C++ zone server** with **EnTT** ECS, single-threaded per cell, many cells per node | Measured 1,000 players per cell target (Test A); tiny memory footprint per process; full control of handoff, layering and AoI | **Unreal dedicated server with Iris + Replication Graph + Mass** (benchmarked in Test D: built for match-scale player counts, ~1–4 GB per process, no seamless server handoff); flecs (strong alternative, kept as fallback) |
| **Client↔server bridge** | **`MMONet` UE plugin**: owns the connection, runs `SimCore` prediction, maps server entities to pooled Unreal actors | Keeps Unreal as the presentation layer; gameplay truth stays in `SimCore` | UE actor replication for gameplay state (match-scale design) |
| **Transport** | **QUIC via msquic** (Microsoft, MIT): unreliable *datagrams* for snapshots/inputs, reliable streams for events/RPC/chat | Built-in TLS 1.3 encryption, congestion control, connection migration (gateway failover, network changes), production-hardened, C API | **Valve GameNetworkingSockets** (kept as fallback: encrypted reliable/unreliable UDP, battle-tested, simpler); raw UDP plus a custom reliability layer (we'd have to build encryption and migration ourselves) |
| **Fallback transport** | TCP/TLS (WebSocket framing) through the gateway for networks that block UDP | Keeps restrictive corporate/hotel networks playable at degraded quality | — |
| **Hot-path serialization** | Schema-driven **bit-packed delta snapshots** with a code generator producing C++ for client and server; quantized positions (cm precision) and angles (10–12 bit) | Minimal bytes per entity per tick; one schema, no hand-written encoders; no JSON anywhere on the hot path | protobuf on hot path (varint overhead, no bit-packing); FlatBuffers (zero-copy is less useful than size here) |
| **Service RPC** | **gRPC + protobuf** (Go services; C++ gRPC client in the zone server) | Versioned contracts, polyglot tooling, streaming | REST/JSON internally (slower, weaker contracts) |
| **Global services** | **Go** (ADR; switchable to C++ if C++-only is a hard rule) | Faster development and simpler operation for CRUD and fan-out services; excellent Postgres (pgx), NATS and gRPC libraries | C++ services (≈ +2 months in Phase 2 and more AI review load); Java/Kotlin (JVM ops overhead) |
| **Messaging** | **NATS JetStream** (service events, chat, presence fan-out); **Redpanda** (ledger audit, analytics, anti-cheat stream) | NATS: low-latency subject routing. Redpanda: Kafka-compatible durable log without JVM/ZooKeeper | RabbitMQ (fan-out at scale); Kafka (operational weight) |
| **Primary DB** | **PostgreSQL 17** on **CloudNativePG**, PgBouncer, 4,096 virtual buckets → N clusters | ACID for characters, inventory, quests and economy. Bucket indirection gives consistent-hash-like minimal movement when adding clusters | YugabyteDB/CockroachDB (re-evaluated at the Phase 2 gate if bucket ops prove too heavy); MongoDB (no multi-doc ACID at the needed guarantees) |
| **Economy ledger** | Append-only **double-entry ledger** tables (time-partitioned) + Redpanda mirror | Auditability, dupe forensics, item restore, conservation invariants checkable in SQL | Mutable balances only (no forensics) |
| **Cache / ephemeral** | **Valkey** cluster (Redis-compatible) | Sessions, presence, leaderboards (sorted sets), hot reads. **Never the system of record** | DragonflyDB (kept as a drop-in candidate if Valkey memory cost hurts) |
| **Orchestration** | **Kubernetes** (EKS or GKE) + **Agones** (zone cells, instances) + StatefulSets (Postgres, NATS, Redpanda) + **HPA/KEDA** (stateless services) | Industry-standard; Agones handles allocation, health and graceful shutdown for stateful game servers | Bare-metal custom orchestrator (operational cost) |
| **Ingress / LB** | **Envoy Gateway** (HTTP/gRPC) or **ingress-nginx**; cloud **NLB** (UDP/QUIC) → gateway tier | L7 routing, rate limiting, mTLS termination; NLB for UDP | — |
| **Service mesh** | **Linkerd** | mTLS by default, low overhead | Istio (heavier) |
| **Scripting** | **Luau** embedded in the C++ server (quests, encounters, events); **wasmtime** (C API) for any third-party/modded server extensions; client UI addons (Luau) deferred to post-launch | Luau: sandboxed, typed, fast, instruction-count interrupts, native C++ embedding. WASM: hard isolation plus fuel metering for untrusted code | Unreal Blueprints for server logic (can't run in our non-Unreal server); plain Lua 5.4 (weaker sandboxing/typing) |
| **Navigation** | **Recast/Detour** (C++) baked from world-export geometry | Same library Unreal uses internally, so nav behaves consistently with the editor view | — |
| **World editor & content tools** | **Unreal Editor** + World Partition + PCG + Data Layers + custom editor plugins + world-export commandlet | Designers and artists work in a mature editor; the server consumes exported data | Building a custom world editor (v1.0 plan; large cost) |
| **Source control** | **Perforce Helix Core** (Unreal project and assets, file locking) + **Git/GitHub** (`SimCore`, protocol, server, services, infra) with CI mirroring | Perforce is the Unreal-industry standard for large binary assets; Git suits code review and AI-agent PR workflows | Git LFS for everything (weak locking for .uasset/.umap at 60+ people) |
| **Build & CI** | **Horde** + **Unreal Build Accelerator** + **Unreal Cloud DDC** + **Unreal Game Sync** for the Unreal side; GitHub Actions for Git-side code; Argo CD for deployment | Unreal builds and cooks are the slowest part of the loop; Epic's tools are designed for them | Jenkins-only (no native UE build distribution) |
| **IaC / GitOps** | **Terraform** + **Argo CD** + **Argo Rollouts** | Reproducible environments; progressive delivery with automatic rollback | — |
| **Observability** | **OpenTelemetry** → Prometheus/Mimir, Loki, Tempo, Grafana; **Tracy** (server profiling); **Unreal Insights** (client profiling); Sentry or BugSplat (client crashes via the Unreal crash reporter) | One telemetry standard from client to DB | — |
| **Testing** | GoogleTest, **RapidCheck** (C++ property tests), Go `rapid` (service property tests), **Testcontainers** (Postgres/Valkey/NATS), libFuzzer, ASan/UBSan/TSan, clang-tidy, **Unreal Automation + Gauntlet**, k6, custom **bot swarm**, Chaos Mesh | Real databases in CI; invariant-based testing for economy and combat; sanitizers compensate for C++'s lack of memory safety | Mocks for DB (hide real behavior) |
| **Anti-cheat** | Server authority + heuristics (primary); **Easy Anti-Cheat via Epic Online Services** (client integrity) | EAC is free and integrates natively with Unreal | BattlEye (paid; kept as an alternative) |
| **Art pipeline** | Blender/Maya → FBX/USD → Unreal; Substance; Houdini for PCG tooling; Nanite for static environment meshes | Native Unreal import paths; Nanite cuts LOD authoring work for environments | — |

## 2.4 Reference Runtime Architecture (target by end of Phase 2)

```
                         ┌──────────────── Global Services (Go, multi-AZ, HPA) ───────────────┐
UE5 Client ─HTTPS─► Envoy►│ Auth/Account · Character · World Directory/Matchmaker · Chat     │
 (MMONet + SimCore)       │ Social · Guild · Mail · Auction · Trade · Group Finder · Shop     │
  │                       └───────┬─────────────────────────┬───────────────────────┬─────────┘
  │                               │gRPC                     │NATS (events)          │
  │ QUIC/msquic (datagrams+streams)▼                        ▼                       ▼
  └──► NLB ──► Gateway tier ◄──► Zone/Cell servers (C++, EnTT + SimCore, Agones, 20 Hz) ─► Persistence
              (C++, stateless,    ├─ Layering controller (spawn/merge layers)          Writer (C++)
               session map        ├─ Instances (dungeons/raids/BGs, Agones allocate)     │
               in Valkey)         └─ AoI: near (every tick) / mid (5 Hz) / far (1 Hz)    ▼
                                                                         PostgreSQL buckets ×4096
                                              Valkey (sessions, presence, caches)   → N CNPG clusters
                                              Redpanda (ledger mirror, analytics, anti-cheat)

Content path:  Unreal Editor (World Partition, PCG, plugins) ─► world-export commandlet ─► server world data
               Design data (Git schemas) ─► binary for server + DataAssets for client
```

**Key mechanics:**
- **Gateway tier.** It terminates QUIC, authenticates the session token, and routes inputs to the cell that owns the player. During a zone or layer handoff it **re-routes server-side without the client reconnecting**. Gateways are stateless apart from a session map that can be rebuilt from Valkey.
- **Client presentation.** `MMONet` receives entity snapshots and spawns or updates pooled Unreal actors. World Partition streams terrain and props around the camera independently of which server owns the player, so a server handoff never triggers an Unreal level travel or loading screen.
- **Cell ownership.** One authoritative owner per entity. Border regions use **ghost entities** (read-only replicas from neighbor cells) so AoI crosses cell borders seamlessly.
- **Durability contract.**
  - *Transactional* events (loot assignment, trade, AH, mail, quest completion, level-up, currency changes) are sent to the Persistence Writer and **acknowledged only after a synchronous Postgres commit**. The sim doesn't block a tick on this; the event stays "pending" for ≤ 2 ticks typically.
  - *Soft state* (position, HP, buffs) is checkpointed every 10 s and on handoff/logout.
  - A dead cell respawns, rehydrates from the DB, and players reconnect through the gateway.
- **Anti-cheat on the server.** Clients send inputs (move intent, ability ID, target ID, sequence number). The server validates each one: speed envelope, collision against exported geometry, line-of-sight, range, cooldown, GCD, resources, ownership and value conservation on trade. Each check feeds a per-player **suspicion score** streamed to Redpanda.

---

# 3. Critical Path & Dependencies

The critical path runs through **`SimCore` → `MMONet` netcode → world export → persistence/ledger → sharding → bot swarm → Unreal content tooling → content → scale certification → launch.** Anything that slips on this chain moves launch. Everything else has float.

| Phase N must deliver... | ...so that Phase N+1 isn't blocked | Lead-time note |
|---|---|---|
| **P0 → P1:** UE version and fork policy, Perforce + Horde + DDC working, protocol spec v0 and schema compiler, art pipeline contract, `SimCore` boundary, hiring done | P1 can't build prediction/reconciliation without the protocol and the shared-core boundary. Artists can't produce slice assets without the pipeline contract and build farm | The senior Unreal and C++ networking reviewers must be hired by **M5**: AI output can't merge on critical modules without them |
| **P1 → P2:** a consistent `SimCore`, the `MMONet` plugin, **world export v1**, a **headless C++ bot that uses the real protocol**, ledger v1, CI with Testcontainers and sanitizers | Every Phase 2 scale gate depends on the bot swarm. A bot that doesn't use the real protocol produces meaningless load results. The server can't simulate zones it can't load. Sharding builds on ledger semantics | Bot client must exist by **M14** to rehearse the M16 gate |
| **P2 → P3:** scalable shard, layering, seamless travel, instances, stable service APIs, **Unreal content tooling v1** (editor plugins, push-to-shard, content DB, Luau hot reload) | Content teams scale from ~8 to ~40 people. Without tooling they're idle or produce unscalable content. Instances are needed for dungeon/raid production | Tooling is the most under-estimated dependency. A dedicated Unreal tools team starts in **M22**, not at M31 |
| **P3 → P4:** content complete, **50k-CCU bot swarm on real content**, client perf budgets met (including the PSO cache), localization extracted | Beta can't start with missing content (first impressions). Scale certification at 100k needs the 50k platform. Loc vendors need 3–4 months | Outsourcing contracts signed by **M28** for Phase 3 ramp |
| **P4 → P5:** 100k CCU certified, pen test clean, DR drill passed, payments/legal live, storefront and royalty terms confirmed, ops staffed | Launch without these risks a catastrophic first week (the most expensive failure in MMOs) | Cloud capacity reservations and limit increases requested by **M50** |
| **P5 → Sustainment:** stable SLOs, live cadence, unit costs at target | Expansion development needs a stable live base and a funded team | Expansion pre-production starts **M57** |

**Parallel (off-critical-path) tracks:** art style exploration and concept (M3–M30), music/audio identity (M10+), narrative and world bible (M2–M30), marketing and community building (M36+), platform/storefront certification (M44+), localization (M34+), Unreal Engine upgrade evaluation (once per year, outside gate quarters and never in Phase 3).

---

# 4. Top 5 Risks to the Five-Year Plan and Mitigations

| # | Risk | Likelihood / Impact | Early warning signals | Mitigations |
|---|---|---|---|---|
| **1** | **Content throughput falls short.** Launch-scope content (14 zones, ~1,400 quests, 10 instances) can't be produced on time or budget. Historically the #1 MMO killer. | High / Critical | Wave 1 (M31–M34) delivers < 80% of planned zone-equivalents; cost per quest > 1.3× model | **Unreal's content tools** (PCG biome graphs, World Partition, Nanite environments, Fab assets) cut per-zone cost. **Content cost model** from Phase 0, re-baselined every wave. **Tools team from M22** (editor plugins, push-to-shard, encounter sandbox, quest bot tests). **AI-assisted drafting** (quest text, item stats, PCG graphs) with human editorial ownership. **Two outsourcing vendors.** A **pre-agreed cut list** in priority order: 2nd raid → x.1, 14 → 12 zones, 3rd spec per class deferred, 2 BGs → 1. **Modular zone kits** to reuse art. |
| **2** | **The custom network layer and UE↔server bridge cost more than planned.** Bypassing Unreal replication means building `MMONet`, entity→actor views, seamless handoff with World Partition, and the world-export pipeline ourselves. | Medium / High | Test A or G1.3 near their thresholds; world-export data frequently stale; handoff stalls > 250 ms in G2.8 rehearsals | Phase 0 **Test A and Test C hard gates** before committing. **GameNetworkingSockets** kept as a transport fallback. `MMONet` owned by a named senior human reviewer. World export runs automatically on every level submit and is validated in CI. **Test D** documents the Unreal-native option, so we can make a fact-based re-evaluation if the custom path stalls. |
| **3** | **Scaling cliffs in practice.** Hotspots (city events, world bosses), database write amplification, or global-service fan-out (chat/presence) breaking the 100 ms / 1,250 ms budgets at 10k–100k CCU. | Medium / Critical | Tick p99 creeping > 20 ms at 5k CCU; Postgres WAL rate > 60% of disk throughput; NATS fan-out latency growth super-linear in CCU | **Bot swarm from Phase 2** with escalating gates (10k → 30k → 50k → 100k). **AoI LOD tiers** (near/mid/far rates). **Layering plus cell split** for hotspots. **Write-behind** soft state; synchronous commits only for ledger. **Bucket indirection** so DB clusters can be added without re-hashing. **Architecture allows 30 Hz and per-cell thread budgets.** Capacity tests run before every phase gate, not just at the end. **Client side:** crowd LOD, animation budgets and actor pooling verified at 100+ visible players. |
| **4** | **AI-generated C++ brings memory-safety bugs, architectural drift and hidden defects.** Code volume outpaces human review: use-after-free, data races, undefined behavior, and subtle economy bugs (e.g., item dupes). | Medium-High / High | Rising change-failure rate; review queue > 3 days; sanitizer or fuzzer findings rising week over week; crash rate in alpha above target | **Mandatory sanitizer gates** (ASan/UBSan/TSan) and clang-tidy on every merge. **24/7 fuzzing** of protocol decoders and script bindings. **RapidCheck property tests** for conservation laws, **Testcontainers** for real DB semantics. **CODEOWNERS/stream permissions with mandatory human approval** on critical modules. **ADRs** as binding context for agents. **Coding rules:** C++ Core Guidelines subset, no raw owning pointers, no exceptions across module boundaries, `SimCore` allocation-free on the hot path. **DORA metrics** tracked; agents throttled if change-failure rate > 15%. Ledger reconciliation runs continuously in every environment. |
| **5** | **Security, cheating and economy exploits at launch.** Dupes, speed/teleport hacks, botting, gold-selling, DDoS, or account takeovers destroy trust in week one. | High / High | Red-team success in staging; abnormal ledger flows in beta; credential-stuffing spikes | **Server-authoritative inputs-only model** from day one. **Double-entry ledger** with real-time conservation checks and per-item provenance (any dupe is detectable and reversible). **Per-feature kill switches** (AH, trade, mail). **Two pen tests** (M48, M53), plus a bug bounty from open beta. **EAC** as a *supplementary* client-integrity layer. **WebAuthn/TOTP 2FA** and an authenticator incentive. UDP DDoS protection at the edge plus gateway admission control. Bot detection v1 in beta, ML-based v2 post-launch. |

**Additional tracked risks (lower tier):**
- *Unreal Engine royalties and licensing:* 5% of gross revenue above $1M is a material cost for a subscription game. Mitigation: model it in Phase 0, request a custom license quote from Epic, and confirm the rate for the chosen storefronts before Phase 4.
- *Unreal Engine upgrades and fork drift:* each upgrade can cost weeks. Mitigation: keep engine modifications minimal and registered, upgrade at most yearly, never during Phase 3 or a gate quarter.
- *Build-farm cost and iteration speed:* Unreal builds and cooks are slow. Mitigation: Horde, UBA and a shared DDC from Phase 0, with build-time gates (G0.8, G1.7).
- *"C++ for every process" becomes a hard requirement:* if Go services are ruled out, add about 2 months to Phase 2 and one more senior C++ backend reviewer.
- *Funding/market timing:* hold a 6-month cash runway buffer at every gate.
- *Key-person loss:* every critical module (`SimCore`, `MMONet`, world export, ledger) has ≥ 2 human reviewers trained on it.
- *Scope creep:* change-control board; new features only by trading scope out.
- *Regulatory change on monetization:* no paid random loot, so exposure is minimal.

**Schedule buffer:** the 60-month plan holds **≈ 3 months of explicit buffer**. Each phase plan uses roughly 92% of its calendar, and gate-remediation sprints draw from that buffer. If cumulative slip exceeds the buffer by the **M30 gate**, the cut list (Risk #1) is executed automatically. Launch isn't moved until the cut list is spent.

---

*Next step after approval of this document: start Phase 0. The first concrete outputs are the ADR template and backlog, the Test A–D specifications with their pass/fail benchmarks, the Epic licensing inquiry, and the content cost model worksheet.*
