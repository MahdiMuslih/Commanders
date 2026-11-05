# Project Plan: A Five-Year MMORPG Built from Scratch

**Document type:** Pre-production planning: prerequisites, requirements and a phased roadmap
**Status:** Draft v1.0, to be locked at the end of Phase 0
**Scope:** A full-stack, original-IP, WoW-*style* (third-person, tab-target, zone-based, raid-and-dungeon) MMORPG
**Not in scope here:** implementation code. This document defines what gets built, in what order, and how we know each step is done.

> **IP note.** "WoW-style" describes the genre and the systems: classes, talents, quests, dungeons, raids, an auction house, guilds. Every name, world, character, art asset and piece of lore must be original. Legal checks this at every phase gate (see Constraints in each phase).

---

## 1. Executive Summary

We will build an original-IP, tab-target fantasy MMORPG over 60 months in six gated phases. The phases are: Architecture Lock (M1–6), Vertical Slice (M7–16), Core MMO Systems & Scale (M17–30), Content at Scale (M31–46), Polish/Security/Beta (M47–54), and Launch & Live Ops (M55–60+).

The core bet is a **single shared Rust simulation core** (`sim-core`, built on `bevy_ecs`). The headless server and the **Bevy** client both compile it, so prediction and reconciliation run the *same* movement and combat math on both sides by construction, not by discipline.

Clients connect over **QUIC (quinn)** to a stateless **edge gateway tier**. Gateways forward to spatially partitioned **zone/cell simulation servers** at 20 Hz, run as **Agones** fleets on Kubernetes. Dynamic **layering** absorbs hotspots, so there are no realm caps. A tiered **area-of-interest** system bounds bandwidth and CPU per player.

All durable state lives in **PostgreSQL**, sharded by character UID across 4,096 virtual buckets with consistent-hash placement. Currency and items are recorded in an **append-only double-entry ledger**. **Valkey (Redis-compatible)** holds only ephemeral state, so any simulation pod can die and respawn without losing committed data.

AI agents write most of the code. Humans own architecture sign-off, every change to netcode, persistence, economy and security, all art and design direction, and QA. Every gate is measurable: tick-time percentiles, bot-swarm CCU, chaos-test data-loss counts, penetration-test findings and content-volume counts.

The two largest risks are **content throughput** (MMOs die from too little content, not bad netcode) and **engine/tooling maturity**. We handle the first with procedural-assisted tooling and an early content-cost model. We handle the second with an explicit Phase 0 fallback (Unreal Engine 5 client plus the same Rust `sim-core` over FFI).

---

## 0. Global Definitions & Targets (apply to every phase)

### 0.1 Terminology

| Term | Meaning in this plan |
|---|---|
| **Shard** | One world cluster: a full copy of the open world, made of N zone/cell servers, the gateways in front of them, and instance fleets. Target: **≥10,000 CCU per shard at launch**. |
| **Zone server / Cell** | One Agones `GameServer` pod that simulates one spatial region (a zone, or a sub-cell of a dense zone). Single-threaded ECS schedule per cell; many cells per node. |
| **Layer** | A parallel copy of a zone *inside* a shard. It is spawned when zone population passes a threshold (default 300) and merged when it drops. This removes fixed realm caps and queues. |
| **Instance** | A dungeon, raid or battleground. Short-lived Agones `GameServer` allocated on demand. |
| **Global services** | Account, character, social, chat, guild, mail, auction, matchmaking and so on. Shared across all shards. This is what lets us scale to **100k+ CCU** by adding shards and moving players between shards and layers. |
| **Hot path** | Client input → gateway → sim tick → state delta → gateway → client. |

### 0.2 Non-negotiable performance budgets

| Metric | Target | How it's measured |
|---|---|---|
| Local feedback for a player action (predicted) | ≤ 1 rendered frame (≤16.7 ms at 60 fps) | Client telemetry |
| Server-confirmed action-to-feedback | **p95 < 100 ms** for players with ≤ 50 ms RTT to their region (≤ 50 ms network + ≤ 50 ms server) | Bot-swarm and beta telemetry: input timestamp → ack receipt |
| Server internal latency (gateway ingress → sim → gateway egress) | p99 ≤ 50 ms at 20 Hz; architecture must allow 30 Hz (≤ 33 ms) without redesign | OpenTelemetry spans |
| Sim tick duration | p99 ≤ 25 ms (50% of the 50 ms budget), p99.9 ≤ 40 ms | Tick profiler histogram |
| World-state broadcast (any state change → every interested observer) | Near AoI ring: p99 ≤ 100 ms. Mid ring: p99 ≤ 250 ms. Far ring/zone-wide: **p99.9 ≤ 1,250 ms under full load** | Per-entity replication latency sampling |
| Downstream bandwidth per player | Median ≤ 32 kbps, p99 ≤ 128 kbps (25-player raid combat) | Gateway metrics |
| Zone/layer handoff | Success ≥ 99.99%; visible stall ≤ 250 ms | Handoff tracer |
| Durable data loss on any single pod/node failure | **0 committed economic/progression transactions.** Soft state (position, HP) RPO ≤ 10 s | Chaos tests |

> **Interpreting "world state broadcast under 1250 ms".** We treat 1,250 ms as the hard ceiling for the *farthest* AoI tier (zone-wide events, distant entities updated at 1 Hz). Gameplay-relevant nearby state is held to the much tighter near and mid ring numbers above.

### 0.3 Human oversight model for an AI-primary coding team

- **AI agents** write most code, tests, infrastructure-as-code, tooling and documentation. They work through PRs only and cannot merge to protected branches.
- **Human approval is mandatory** (enforced by CODEOWNERS) for: `sim-core`, the netcode protocol, the persistence layer and schema migrations, ledger/economy, auth/security, infra and production configs, and anything that touches PII or payments.
- Every architecture decision is an **ADR** (Architecture Decision Record) authored by AI and signed off by the Technical Director.
- **Humans own** all art direction, game and narrative design, level design, audio direction, final QA sign-off, community and legal.
- **AI-assisted but human-curated:** quest text drafts, item stat generation within designer-defined budgets, procedural terrain passes, test-case generation.

---

# PART 1: Prerequisites & Requirements by Phase

---

## Phase 0: Pre-Production & Architecture Lock (M1–M6)

### 1. Technical prerequisites (must be true *before* Phase 0 starts)
- Product vision approved: a one-page high concept, target audience (PC, 16+, MMO veterans and lapsed players), business model chosen (**subscription plus cosmetic shop, no pay-to-win, no paid random loot**).
- Source control and identity: GitHub organization, SSO (Okta or Google Workspace), hardware security keys for admins, a secrets manager chosen (Vault or cloud KMS).
- Cloud accounts created with an org-level billing guardrail: AWS or GCP, *one* primary chosen, and the second kept for DR in Phase 4.
- AI agent environment: sandboxed runners with repository access and no production credentials.

### 2. Team/resource prerequisites
- **Must be hired:** Technical Director/Principal Architect (human, final technical authority), Game Director, Art Director, Lead Game Designer, Producer, and 1 senior backend/netcode engineer as a human reviewer. Fractional: legal counsel (IP, privacy), a security advisor.
- **Tools procured:** GitHub Enterprise, Linear or Jira, Figma, Blender, Substance 3D (Painter/Designer) seats, Perforce *or* Git LFS for art (decision is part of this phase), Grafana Cloud or a self-hosted observability trial.
- **Infra:** one non-production Kubernetes cluster (EKS/GKE, 3 node pools: system, general, game-server). Budget ceiling for spikes: about $15k/month.

### 3. Requirements

**Functional (things Phase 0 must produce)**
- Game Design Document v1: core loop, 2 starting races and 4 classes for the slice, plus a full-launch target of 4 races and 8 classes. Combat model (tab-target, GCD 1.5 s, cast/channel, threat), progression to level 50, dungeons, raids, PvP scope.
- Technical Design Document and ADR set covering engine, language, network protocol, sharding model, persistence model, scripting model, orchestration, observability and the CI/CD model.
- A **content cost model**: hours per zone, quest, dungeon, creature, armor set and spell VFX, with a cost for launch scope.
- Wire protocol spec v0: message taxonomy, channel types, snapshot/delta format, quantization rules.

**Non-functional**
- Two **throwaway technical spikes** that de-risk the stack (code is discarded; the findings are kept):
  - **Spike A, sim and net:** headless `bevy_ecs` sim plus quinn datagrams plus a headless bot client. Target: 1,000 bots in one cell at 20 Hz with p99 tick ≤ 25 ms on a single 8-vCPU node.
  - **Spike B, client fidelity:** a Bevy client rendering 300 skinned, animated characters plus a 1 km² terrain at ≥ 60 fps on min-spec (GTX 1060 6 GB / RX 580, 4-core CPU, 16 GB RAM) and ≥ 120 fps on rec-spec (RTX 3060).
- A threat model (STRIDE) for auth, gateway, sim and economy.
- Privacy-by-design data inventory (which PII is stored where).

**Constraints**
- **Legal:** original-IP clearance process; trademark search on the working title; GDPR, UK GDPR, CCPA/CPRA plan; age gate 13+ (COPPA-avoidant) with regional overrides (EU 16 where required); a stance on loot-box regulation (none paid).
- **Budget:** Phase 0 roughly $1.0–1.5M (people plus cloud). Total 5-year envelope: **$45–75M** (see the Phase 3 headcount peak).
- **Platform:** Windows 10/11 x64 is the primary target. Linux/Steam Deck is secondary. macOS (Apple Silicon) is a stretch goal. A web client (WASM/WebGPU) is experimental and not a launch commitment.

### 4. Exit criteria / gate (Phase 0 → Phase 1)
| # | Criterion | Pass condition |
|---|---|---|
| G0.1 | Spike A | 1,000 bots in one cell at 20 Hz, p99 tick ≤ 25 ms, 30-minute soak, no memory growth > 5% |
| G0.2 | Spike B | Min-spec and rec-spec fps targets met in a captured benchmark scene. **If this fails, the ADR switches the client to UE5 + Rust `sim-core` via FFI.** No re-litigating afterwards. |
| G0.3 | ADRs | ≥ 25 ADRs signed by the Technical Director, including all items in the Part 2 "Tech Decisions" column |
| G0.4 | GDD v1 + content cost model | Signed by the Game Director and Producer. Launch scope fits the budget within ±15% |
| G0.5 | Threat model | Reviewed by the security advisor; mitigations mapped to phases |
| G0.6 | Hiring plan | Phase 1 roles signed or offers accepted |
| G0.7 | Legal | Working-title trademark clear; privacy plan approved |

---

## Phase 1: Core Foundation & Vertical Slice (M7–M16)

### 1. Technical prerequisites
- All Phase 0 gates passed. Engine and language locked by ADR.
- Monorepo layout decided: `crates/sim-core`, `crates/protocol`, `server/*`, `client/*`, `services/*`, `tools/*`, `infra/*`, `content/*`.
- Rust toolchain pinned (`rust-toolchain.toml`) and Bevy version pinned. An upgrade policy is defined: one engine upgrade window per quarter, never during a gate month.
- Art pipeline contract: Blender → glTF 2.0, KTX2/Basis textures, naming, LOD and skeleton conventions. A shared humanoid skeleton is specified.
- Wire protocol spec v0 approved.

### 2. Team/resource prerequisites
- **Humans:** +1 senior gameplay engineer (reviewer), +1 tools/pipeline engineer, +1 DevOps/SRE, 2 concept artists, 2 3D character/environment artists, 1 animator, 1 technical artist, 2 game designers (systems and quests), 1 QA lead, 1 audio designer (contract). Roughly 18–22 people.
- **Infra:** a CI fleet with GPU runners (client build and render smoke tests) and Testcontainers-capable Linux runners. Dev and staging clusters. CloudNativePG operator installed. An artifact registry (OCI) and an asset CDN bucket.
- **Tools:** Tracy profiler, RenderDoc, a crash-reporting backend (Sentry with a native minidump pipeline), Grafana stack.

### 3. Requirements

**Functional (the vertical slice)**
- **World:** one zone of about 1.5 km² with terrain, foliage, a hub town, 2 sub-areas and a day/night cycle.
- **Gameplay:** 2 races, 4 classes (tank, healer, melee DPS, caster DPS) at levels 1–10, 6 abilities each. Tab-targeting, auto-attack, GCD, cast bars, threat, death and resurrection.
- **Content:** 15 quests (kill, collect, escort, interact), 12 creature types, 1 five-player dungeon with 3 bosses, scripted in Luau.
- **Systems:** account creation and login, character create/select/delete, inventory and equipment, loot, vendors, XP and leveling, party (5), basic chat (say, party, zone), basic UI (unit frames, action bars, quest log, bags, minimap).
- **Netcode:** client prediction for movement and ability *starts*, reconciliation with input replay, entity interpolation (100 ms buffer), snapshot delta compression, near/mid/far AoI.

**Non-functional**
- **Performance:** 500 bots plus 20 humans in the slice zone on a single cell. p99 tick ≤ 25 ms. All §0.2 hot-path budgets met at ≤ 50 ms RTT.
- **Persistence:** characters, inventory and quest state in Postgres. A crash of the zone pod loses 0 committed transactions.
- **Testing:** CI with Testcontainers (Postgres, Valkey, NATS). proptest invariants for combat math, inventory conservation, currency conservation and movement determinism (client and server `sim-core` produce bit-identical results for the same inputs).
- **Security:** inputs only (no client state assertions). Server-side movement speed, cooldown and range validation. Per-connection rate limits.

**Constraints**
- **Platform:** Windows builds are mandatory. Linux builds run in CI.
- **Budget:** cloud spend ≤ $25k/month.
- **Legal:** contributor/contractor IP assignment for every artist and designer. Asset provenance log (no unlicensed references in shipped assets).

### 4. Exit criteria / gate (Phase 1 → Phase 2)
| # | Criterion | Pass condition |
|---|---|---|
| G1.1 | Playable slice | 20 internal humans complete the zone and dungeon in a 2-hour session. ≥ 70% rate it "fun to replay" on an internal survey (design gate, human-judged) |
| G1.2 | Server perf | 500 bots + 20 humans, p99 tick ≤ 25 ms, 2-hour soak |
| G1.3 | Hot path | Server-confirmed action-to-feedback p95 < 100 ms at 50 ms emulated RTT, 2% packet loss |
| G1.4 | Determinism | 10M-step proptest/fuzz run: 0 divergences between client and server `sim-core` builds (Windows client vs Linux server) |
| G1.5 | Durability | Chaos: 50 random zone-pod kills during scripted trading/looting → 0 lost or duplicated items/currency (ledger reconciliation passes) |
| G1.6 | CI/CD | Main branch green ≥ 95% of days. Full pipeline ≤ 25 min. Every merge deploys automatically to dev |
| G1.7 | Art pipeline | Artist-to-in-game for a new prop ≤ 15 min without engineer help |

---

## Phase 2: Core MMO Systems & Server Scalability (M17–M30)

### 1. Technical prerequisites
- Vertical slice gates passed. `sim-core` APIs stable at v1 (semver; breaking changes need an ADR).
- Microservice boundaries defined (bounded-context map) with gRPC/protobuf contracts versioned in `crates/protocol` and `proto/`.
- Database sharding design locked: 4,096 virtual buckets (`bucket = xxhash64(character_uid) mod 4096`), a bucket→cluster directory in Postgres (replicated, cached in Valkey), and a bucket-migration procedure designed.
- Bot client exists as a headless build of the real client netcode, not a separate re-implementation.

### 2. Team/resource prerequisites
- **Humans:** +1 senior backend engineer (distributed systems), +1 SRE, +1 DBA/data engineer, +1 security engineer, 2 more designers (economy, PvE encounters), +2 QA. Art headcount grows toward Phase 3 (environment and outsourcing coordination). Roughly 30–35 people.
- **Infra:** a load-test cluster able to burst to **≥ 400 vCPU for bots plus ≥ 300 vCPU for servers**. Agones installed. NATS JetStream and Redpanda clusters. Multi-AZ staging.
- **Tools:** k6 for HTTP/gRPC load, a custom bot-swarm controller, Chaos Mesh, pgbench, Argo CD, Argo Rollouts.

### 3. Requirements

**Functional**
- **World topology:** multi-zone shard with seamless zone transitions, a cell split for dense zones, **dynamic layering** (spawn and merge), and party-aware layer assignment (party and guild members are pulled into the same layer).
- **Instancing:** on-demand dungeon/raid instances via Agones allocation, with instance lockouts.
- **Global services:** Auth/Account, Character, World Directory/Matchmaker, Chat (channels, whisper, guild, cross-shard), Social (friends, ignore, presence), Guild, Mail, **Auction House**, Trade, Group Finder, Leaderboards.
- **Economy:** append-only, double-entry ledger for all currency and item-ownership transfers. Every item has a unique instance ID and a provenance chain.
- **Login queue fallback:** only for global overload protection. It is not a realm cap.

**Non-functional**
- **Scalability:** horizontal scale of every stateless service under HPA. No single point of failure (multi-AZ, ≥ 3 replicas, PodDisruptionBudgets).
- **Performance:** 10,000 bot CCU on one shard. AH search p99 ≤ 200 ms. Chat fan-out p99 ≤ 300 ms global.
- **Durability:** Postgres HA (CloudNativePG, synchronous replica in a second AZ). PITR backups. RPO 0 for ledger, RTO ≤ 5 min per bucket cluster.
- **Operability:** zero-downtime rolling deploys for services; zone-server **drain-and-migrate** (players are handed off to a fresh pod running the new build).
- **Security:** mTLS service-to-service (Linkerd), signed session tokens (short-lived, rotating), a WAF on HTTP ingress, and a DDoS posture (cloud L3/L4 protection plus gateway-level per-IP admission).

**Constraints**
- **Budget:** cloud ≤ $60k/month steady, plus load-test bursts approved per event.
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
| G2.7 | Economy invariants | proptest plus a production-like simulation of 10M random AH/trade/mail ops: ledger sum conserved, no item duplication |

---

## Phase 3: Content Production & World Building at Scale (M31–M46)

### 1. Technical prerequisites
- Phase 2 gates passed. **Engine and engineering feature freeze on core systems**: new systems only via the change-control board.
- Content tooling is production-ready:
  - **World editor** (a Bevy-based app): terrain sculpt/paint, spawn placement, pathing nav-mesh bake, quest-giver placement, live connect to a dev shard.
  - **Content DB:** design data as typed schemas (RON/TOML in git, compiled to binary). A web editor that opens PRs. Schema validation in CI.
  - **Luau scripting:** hot reload on dev shards, a debugger, and per-script CPU/memory budgets.
  - **Encounter sandbox:** run boss fights with bot raids for tuning.
- Asset budgets defined per zone (triangles, draw calls, texture memory, streaming radius).

### 2. Team/resource prerequisites
- **Peak headcount:** about 60–75, including **outsourcing partners** for environment props, armor sets and creature models (2 vendors, so we're never single-sourced).
- **Human roles added:** 4–6 level/world designers, 4 quest designers/writers, 2 encounter designers, 1 narrative lead, 3 more animators, 2 VFX artists, 1 UI/UX designer, 1 composer (contract), localization vendor, +4 QA (functional plus content), a community manager (for alpha).
- **Infra:** a persistent internal alpha shard (24/7). A CDN for patch distribution. A patcher/launcher with delta patches.

### 3. Requirements

**Functional (launch content scope, committed)**
- **World:** 2 continents, **14 open-world zones** (levels 1–50) plus 4 capital/hub cities.
- **Classes and races:** 8 classes × 3 specializations, 4 races (2 per faction), with talents.
- **Quests:** about **1,400 quests** total (~100 per zone; AI drafts the text, humans edit and own it).
- **Instances:** 8 five-player dungeons, **2 raids** (10/25 players, 6 + 8 bosses), 2 battlegrounds plus world-PvP objectives.
- **Professions:** 4 gathering, 6 crafting, with a full crafting-economy loop into the Auction House.
- **Account systems:** account-wide cosmetics/mounts and achievements.
- **Localization:** EN, DE, FR, ES, PT-BR at launch (KO, ZH-TW post-launch). The string pipeline is ready from M31.

**Non-functional**
- Content must stay inside its performance budget. CI runs **automated zone perf captures** (bot camera flythroughs) and fails a build that regresses fps > 5% on min-spec.
- Every quest has an automated **bot completion test** (the scripted bot completes the quest end-to-end on a dev shard nightly).
- The scripting sandbox enforces instruction-count and memory limits. A runaway script is killed and reported without stalling the tick.

**Constraints**
- **Budget:** about 55% of the total program budget lands here (people plus outsourcing). Cloud ≤ $80k/month.
- **Legal:** cultural review of content per launch region; ratings submissions prepared (ESRB/PEGI/USK); music and voice rights cleared for worldwide perpetual use.
- **Schedule:** content is produced in **4 waves of 4 months**. Each wave ends with a playable internal milestone.

### 4. Exit criteria / gate (Phase 3 → Phase 4)
| # | Criterion | Pass condition |
|---|---|---|
| G3.1 | Content complete | 100% of launch zones, dungeons, raids and quests are *content complete* (playable start to finish, placeholder audio/VFX allowed in ≤ 10%) |
| G3.2 | Quest automation | ≥ 98% of quests pass nightly bot-completion tests 5 nights in a row |
| G3.3 | Leveling | 3 internal testers reach 1→50 in 90–130 hours of /played without blocking bugs |
| G3.4 | **Bot swarm at scale** (prerequisite for Phase 4) | **50,000 bot CCU across ≥ 5 shards** running *real content* (questing, dungeons, AH, chat), 8-hour soak, all §0.2 budgets met |
| G3.5 | Client perf | Min-spec ≥ 45 fps p5 in capital cities with 100 players visible; ≥ 60 fps median elsewhere |
| G3.6 | Alpha | Internal plus friends-and-family alpha (≤ 500 people) for ≥ 8 weeks. Crash-free sessions ≥ 97% |
| G3.7 | Localization | All strings extracted; ≥ 90% translated in the 5 launch languages |

---

## Phase 4: Polish, Security, and Large-Scale Testing (M47–M54)

### 1. Technical prerequisites
- Phase 3 gates passed. **Content lock** (new content only via exception).
- A production environment exists in **≥ 2 regions** (NA and EU), each multi-AZ, and is built only from Terraform/Argo CD. A disaster-recovery region is designated.
- Payments integrated with a merchant of record (Xsolla or Paddle for global VAT/GST handling, or Stripe plus a tax engine).
- Customer-support tooling (GM tools, ticketing, account actions) is functional and audited.

### 2. Team/resource prerequisites
- **Humans:** QA grows to about 15 (including an outsourced test partner). A live-ops producer, 2 GMs or support leads, a data analyst, a 24/7 on-call SRE rotation (≥ 6 people), community team ×3, a marketing partner.
- **External:** third-party **penetration test firm**, an anti-cheat vendor (Easy Anti-Cheat or BattlEye for client integrity, layered on server authority), a DDoS-mitigation provider for UDP (e.g., Cloudflare Spectrum or a cloud-native equivalent), and a load-test partner for real-client distributed tests.

### 3. Requirements

**Functional**
- Closed beta (M47–M51) and open-beta stress weekends (M52–M53).
- Character transfers and **cross-shard layering** live.
- Full GM toolset: ban/suspend, item restore from ledger, chat moderation, and a "rollback a player to a ledger point" tool.
- Telemetry and analytics: funnel, retention, economy dashboards (money supply, sinks/faucets, price indices).
- In-game reporting and an automated toxicity filter (chat).

**Non-functional**
- **Security:** pen test with 0 critical and 0 high findings open. Server-side heuristics for speed/teleport/no-clip and cooldown abuse, bot-farming detection v1, and gold-seller detection on ledger patterns. Rate limiting on every endpoint. Secrets rotation.
- **Performance:** 100k bot CCU (see the gate). Patch download ≤ 30 GB at install, with deltas ≤ 2 GB typical.
- **Reliability:** SLOs defined and dashboards live. Login availability 99.9%, shard availability 99.5% excluding scheduled maintenance (target: none needed).
- **Compliance:** GDPR data export/delete flows tested; age rating certificates received.

**Constraints**
- **Budget:** beta cloud ≤ $150k/month peak. Marketing spend is a separate budget.
- **Legal:** EULA, ToS, Privacy Policy and a Code of Conduct published. Refund policies per region (EU 14-day rules).
- **Platform:** Steam/Epic storefront requirements met (if selling there) plus our own launcher.

### 4. Exit criteria / gate (Phase 4 → Phase 5)
| # | Criterion | Pass condition |
|---|---|---|
| G4.1 | **100k CCU** | 100,000 bot CCU across ≥ 10 shards in 2 regions, 12-hour soak, all §0.2 budgets met, global services < 60% CPU |
| G4.2 | Real-human stress | Open-beta weekend peak ≥ 25,000 humans. Login p95 ≤ 5 s, crash-free sessions ≥ 98.5%, no Sev-1 > 30 min |
| G4.3 | Security | Pen test: 0 critical/high open. Anti-cheat catches 100% of the internal red-team's 10 scripted cheat classes in staging |
| G4.4 | DR | Full region-failover drill: login restored in the other region ≤ 30 min, 0 ledger loss |
| G4.5 | Quality | 0 open Sev-1 bugs. ≤ 25 Sev-2 with approved workarounds |
| G4.6 | Economy | 4 weeks of beta economy data: money supply growth within the design band (±20% of model) |
| G4.7 | Ops readiness | Runbooks for the top 30 incident types; on-call rotation staffed; 2 game-day exercises passed |
| G4.8 | Go/No-Go | Signed by the Game Director, Technical Director, Producer, Head of Live Ops and Legal |

---

## Phase 5: Launch, Live Operations, and Post-Launch Sustainment (M55–M60 and beyond)

### 1. Technical prerequisites
- All Phase 4 gates passed. Launch build is tagged and reproducible from source.
- Capacity is pre-provisioned for **2.5× forecast peak CCU** (reserved plus on-demand burst limits raised with the cloud provider in writing).
- Kill switches (feature flags) exist for every non-core system: AH, mail, trade, group finder, shop, specific quests, specific instances.
- Hotfix pipeline: server-side data/script hotfix in ≤ 30 min. Client patch in ≤ 4 h.

### 2. Team/resource prerequisites
- **Live team:** live-ops producer, 24/7 SRE/on-call, 24/7 GM/support (in-house plus a vendor for follow-the-sun), community, economy designer, data team. Content team pivots to the first content update (x.1) and the first expansion pre-production.
- **Vendors:** CDN contracts for launch-week bandwidth, a support desk vendor, a localization vendor on retainer.

### 3. Requirements

**Functional**
- Launch with headstart (early access by pre-order), with login-queue protection for global overload only.
- Live event system (seasonal events driven by data and Luau, with no client patch).
- Cadence: **patch x.0.5 at +8 weeks, content update x.1 at +16 weeks**, and expansion pre-production from M57.

**Non-functional**
- **SLOs:** login ≥ 99.9%, gameplay availability ≥ 99.5% (excluding announced maintenance; target is zero-downtime). Hot-path p95 < 100 ms.
- **Cost:** infra cost per CCU tracked weekly. Target ≤ $0.35 per peak-CCU per day by M60.
- **Security:** a weekly anti-cheat ban-wave cadence; ledger forensics for every reported dupe within 24 h.

**Constraints**
- Regulatory: consumer-protection rules on virtual currency, regional payment rules, and data-residency obligations (EU data stays in the EU region).

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
Milestones: M6 Arch Lock · M16 Vertical Slice · M30 10k-CCU Shard · M40 Int. Alpha · M46 Content Complete
            M47 Closed Beta · M52 Open Beta · M55 LAUNCH · M59 Content Update x.1
```

Phases are **gated**: engineering work for phase N+1 starts only after gate N passes. Art and design pre-production for later phases runs in parallel and is labelled as such. A failed gate allows **one 6–8 week remediation sprint** taken from the schedule buffer (see §4).

## 2.2 Roadmap table

| Phase | Months | Objective | Key Workstreams | Tech Decisions (choice → why) | Deliverables | Gate Criteria | Risks → Mitigation |
|---|---|---|---|---|---|---|---|
| **0 Pre-Production & Architecture Lock** | M1–M6 (6) | Lock the architecture, the scope and the cost model with evidence from throwaway spikes. | **Client:** Bevy rendering spike. **Server:** headless ECS sim spike. **Net:** quinn datagram spike, protocol spec v0. **DB:** sharding/ledger design. **Infra:** non-prod K8s, Terraform skeleton. **Tooling:** repo, CI skeleton, AI-agent sandbox. | See §2.3 for the full stack. Key decision: **Rust everywhere for simulation** (memory safety, performance, one language for the AI agents to master, shared `sim-core`). | GDD v1, TDD, ≥ 25 ADRs, protocol spec v0, threat model, content cost model, spike reports with benchmarks | G0.1–G0.7 | **Bevy can't hit fidelity** → pre-agreed UE5 + FFI fallback. **Scope too big** → the cost model forces cuts *now*. |
| **1 Core Foundation & Vertical Slice** | M7–M16 (10) | Prove the full stack end to end with a slice that's fun to replay and meets every hot-path budget. | **Client:** character controller, prediction/reconciliation, interpolation, UI framework, glTF/KTX2 pipeline, audio. **Server:** zone sim, combat, AI/pathing (navmesh via recast-rs), Luau host. **DB:** character/inventory/quest schema, ledger v1. **Net:** gateway v1, snapshot deltas, AoI rings. **Infra:** dev/staging, Agones PoC, CNPG. **Tooling:** CI/CD, Testcontainers, proptest, Tracy, crash pipeline. | **Luau (mlua) for content scripts** → sandboxed, typed, fast, hot-reloadable, familiar to designers. **sqlx + Postgres 17** → compile-time-checked SQL. **bitcode/custom bit-packing** for hot path, **protobuf** for services. | Playable slice build, bot client v1, CI/CD pipeline, ledger v1, perf dashboards, Slice Review report | G1.1–G1.7 | **Prediction drift** → `sim-core` determinism tests in CI. **AI code quality drift** → CODEOWNERS on critical crates, architecture lint rules, weekly human architecture review. |
| **2 Core MMO Systems & Server Scalability** | M17–M30 (14) | Turn the slice into a horizontally scalable, fault-tolerant 10k-CCU shard with every core MMO service. | **Client:** zone streaming, social UI, AH UI, group finder. **Server:** multi-zone shard, cell split, layering, zone handoff, instances. **Services:** auth, character, directory, chat, social, guild, mail, AH, trade, group finder, leaderboards. **DB:** bucket sharding, directory, migration tooling, PITR. **Net:** gateway session migration, QUIC connection migration, WebTransport/WebSocket fallback. **Infra:** Agones fleets/autoscaler, HPA, Linkerd, NATS, Redpanda, Argo Rollouts. **Tooling:** bot-swarm controller, Chaos Mesh suites. | **Agones** → standard for stateful game-server lifecycle on K8s. **NATS JetStream** → low-latency service messaging and chat fan-out. **Redpanda** → durable audit/analytics stream. **Valkey** → open-license Redis-compatible cache. **Application-level bucket sharding** over Citus/Yugabyte → predictable, no exotic distributed-SQL tail latency. | 10k-CCU shard, all global services, sharded DB with live migration, chaos test suite, bot-swarm platform, runbooks v1 | G2.1–G2.7 | **Hotspot collapse** → layering plus cell split plus AoI LOD. **DB bottleneck** → buckets allow re-placement. Write-behind for soft state; synchronous commit only for ledger. **Service sprawl** → bounded-context map; ≤ 12 services at launch. |
| **3 Content Production & World Building** | M31–M46 (16) | Produce the full launch world and content at quality and inside performance budgets, using tooling that scales human effort. | **Client:** world streaming at continent scale, LOD/impostors, VFX system, localization. **Server:** encounter scripting, PvP/BGs, professions, achievements. **DB:** content data versioning. **Net:** raid-scale optimization (40 entities in combat in near ring). **Infra:** 24/7 alpha shard, patcher/CDN. **Tooling:** world editor, content DB editor, encounter sandbox, nightly quest bots, perf captures. | **Content as data in git** (typed RON → binary) → diffable, reviewable, CI-validated. **Delta patcher** (zstd-chunked, content-addressed) → small patches. **Outsourcing** for props/armor → throughput. | 14 zones, 4 cities, ~1,400 quests, 8 dungeons, 2 raids, 2 BGs, 8 classes, professions, localization ×5, internal alpha | G3.1–G3.7 (incl. **50k bot CCU**) | **Content under-delivery** → 4-month waves with burn-down, the cost model updated each wave, cut-list pre-agreed (e.g., 2nd raid → x.1). **Perf regressions from art** → CI perf captures fail the build. |
| **4 Polish, Security & Large-Scale Testing** | M47–M54 (8) | Make it secure, stable, polished and proven at 100k CCU with real humans before money changes hands. | **Client:** polish, accessibility, crash fixing, anti-cheat SDK. **Server:** cheat heuristics, bot detection, GM tools. **DB:** DR replication, GDPR flows. **Net:** DDoS hardening, gateway admission control. **Infra:** 2-region prod, DR region, SLOs. **Tooling:** analytics dashboards, support desk. | **EAC/BattlEye** as an *integrity layer* (never a replacement for server authority). **Merchant of record** (Xsolla/Paddle) → global tax compliance. **Cloud L3/L4 DDoS plus UDP proxy protection.** | Closed beta, open-beta stress weekends, pen-test report, DR drill report, SLO dashboards, launch candidate build | G4.1–G4.8 (incl. **100k bot CCU**) | **Beta reveals design problems** → closed beta starts at M47, giving 8 months to react. **Security findings late** → pen test at M48 *and* M53. |
| **5 Launch, Live Ops & Sustainment** | M55–M60+ (6) | Launch stably and settle into a predictable live-ops cadence with sustainable unit costs. | **Client:** hotfix cadence, event content. **Server:** live events, economy tuning, ban waves. **DB:** capacity management, partition rotation. **Infra:** cost optimization, reserved capacity. **Tooling:** live dashboards, incident management. | **Feature flags everywhere** (kill switches). **Reserved plus spot** for bot/test, **on-demand** for game servers at launch, moving to reserved after 90 days. | Launch, x.0.5 patch, x.1 content update, expansion pre-production pitch, post-launch review | G5.1–G5.5 | **Launch overload** → 2.5× capacity, headstart, global admission queue. **Day-1 exploits** → ledger forensics plus item-restore tooling plus per-feature kill switches. |

## 2.3 Consolidated Tech Stack (locked by Phase 0 ADRs)

| Layer | Choice | Justification | Rejected alternatives (why) |
|---|---|---|---|
| **Simulation language** | **Rust** (stable, pinned) | Memory safety without GC pauses (tick-time p99), fearless concurrency, excellent for AI-generated code because the compiler catches whole classes of bugs, one language shared by client and server | C++ (memory-safety bugs at scale, slower AI iteration); C# (GC pauses on server at 10k-entity scale need constant tuning) |
| **ECS** | **bevy_ecs** in a shared `sim-core` crate | Archetype ECS with parallel schedules; usable headless; *identical* components and systems on both sides | Unity DOTS (licensing risk after the 2023 runtime-fee episode; server cost per instance); flecs/EnTT (C/C++) |
| **Client engine** | **Bevy** (pinned version, forked if needed) + custom renderer extensions | Same ECS and language as the server; a stylized WoW-like art direction doesn't need photoreal features; WGPU gives Vulkan/DX12/Metal and WebGPU (web client option) | **UE5** is the *fallback* (best tooling and fidelity, but the shared math would cross a C++/Rust FFI boundary); Godot (ECS not native; C# GC) |
| **Transport** | **QUIC via quinn**: unreliable *datagrams* for snapshots/inputs, reliable streams for events/RPC/chat | Built-in TLS 1.3 encryption, congestion control, connection migration (Wi-Fi ↔ mobile, gateway failover), and the same protocol powers **WebTransport** for browsers | Raw UDP plus netcode.io/renet (we would have to build encryption and migration ourselves); ENet/LiteNetLib (non-Rust) |
| **Web fallback** | WebTransport first, **WebSocket** (binary frames) as the last fallback | Covers restrictive networks and the experimental web client | — |
| **Hot-path serialization** | Schema-driven **bit-packed delta snapshots** (derive macros in `crates/protocol`), quantized positions (cm precision) and angles (10–12 bit); control messages in **bitcode** | Minimal bytes per entity per tick; no JSON anywhere on the hot path | protobuf on hot path (varint overhead, no bit-packing); FlatBuffers (zero-copy is less useful than size here) |
| **Service RPC** | **gRPC (tonic) + protobuf** | Versioned contracts, polyglot tooling, streaming | REST/JSON internally (slower, weaker contracts) |
| **Messaging** | **NATS JetStream** (service events, chat, presence fan-out); **Redpanda** (ledger audit, analytics, anti-cheat stream) | NATS: low-latency subject routing. Redpanda: Kafka-compatible durable log without JVM/ZooKeeper | RabbitMQ (fan-out at scale); Kafka (operational weight) |
| **Primary DB** | **PostgreSQL 17** on **CloudNativePG**, PgBouncer, 4,096 virtual buckets → N clusters | ACID for characters, inventory, quests and economy. Bucket indirection gives consistent-hash-like minimal movement when adding clusters | YugabyteDB/CockroachDB (re-evaluated at the Phase 2 gate if bucket ops prove too heavy); MongoDB (no multi-doc ACID at the needed guarantees) |
| **Economy ledger** | Append-only **double-entry ledger** tables (time-partitioned) + Redpanda mirror | Auditability, dupe forensics, item restore, conservation invariants checkable in SQL | Mutable balances only (no forensics) |
| **Cache / ephemeral** | **Valkey** cluster (Redis-compatible) | Sessions, presence, leaderboards (sorted sets), hot reads. **Never the system of record** | DragonflyDB (kept as a drop-in candidate if Valkey memory cost hurts) |
| **Orchestration** | **Kubernetes** (EKS or GKE) + **Agones** (zone cells, instances) + StatefulSets (Postgres, NATS, Redpanda) + **HPA/KEDA** (stateless services) | Industry-standard; Agones handles allocation, health and graceful shutdown for stateful game servers | Bare-metal custom orchestrator (operational cost) |
| **Ingress / LB** | **Envoy Gateway** (HTTP/gRPC) or **ingress-nginx**; cloud **NLB** (UDP/QUIC) → gateway tier | L7 routing, rate limiting, mTLS termination; NLB for UDP | — |
| **Service mesh** | **Linkerd** | mTLS by default, low overhead | Istio (heavier) |
| **Scripting** | **Luau** (designer content, client UI addons); **wasmtime** (any third-party/modded server extensions) | Luau: sandboxed, typed, fast interpreter, instruction-count interrupts. WASM: hard isolation plus fuel metering for untrusted code | Plain Lua 5.4 (weaker sandboxing/typing) |
| **Navigation** | Recast/Detour (via Rust bindings) baked per zone/cell | Proven navmesh solution | — |
| **IaC / GitOps** | **Terraform** + **Argo CD** + **Argo Rollouts** | Reproducible environments; progressive delivery with automatic rollback | — |
| **Observability** | **OpenTelemetry** → Prometheus/Mimir, Loki, Tempo, Grafana; Tracy (in-process profiling); Sentry (client crashes) | One telemetry standard from client to DB | — |
| **Testing** | cargo-nextest, **Testcontainers** (Postgres/Valkey/NATS), **proptest**, cargo-fuzz, k6, custom **bot swarm**, Chaos Mesh | Real databases in CI; invariant-based testing for economy and combat | Mocks for DB (hide real behavior) |
| **Art pipeline** | Blender → glTF 2.0, KTX2/Basis Universal, Substance; Perforce *or* Git LFS (ADR in Phase 0) | Open formats; GPU-compressed textures | FBX (proprietary, inconsistent) |

## 2.4 Reference Runtime Architecture (target by end of Phase 2)

```
                         ┌───────────────── Global Services (multi-AZ, HPA) ─────────────────┐
Client ──HTTPS──► Envoy ─►│ Auth/Account · Character · World Directory/Matchmaker · Chat     │
  │                       │ Social · Guild · Mail · Auction · Trade · Group Finder · Shop     │
  │                       └───────┬─────────────────────────┬───────────────────────┬─────────┘
  │                               │gRPC                     │NATS (events)          │
  │ QUIC (datagrams+streams)      ▼                         ▼                       ▼
  └──► NLB ──► Gateway tier ◄──► Zone/Cell servers (Agones Fleets, 20 Hz, bevy_ecs) ─► Persistence
              (stateless,         ├─ Layering controller (spawn/merge layers)          Service
               session map        ├─ Instances (dungeons/raids/BGs, Agones allocate)     │
               in Valkey)         └─ AoI: near (every tick) / mid (5 Hz) / far (1 Hz)    ▼
                                                                         PostgreSQL buckets ×4096
                                              Valkey (sessions, presence, caches)   → N CNPG clusters
                                              Redpanda (ledger mirror, analytics, anti-cheat)
```

**Key mechanics:**
- **Gateway tier.** It terminates QUIC, authenticates the session token, and routes inputs to the cell that owns the player. During a zone or layer handoff it **re-routes server-side without the client reconnecting**. Gateways are stateless apart from a session map that can be rebuilt from Valkey.
- **Cell ownership.** One authoritative owner per entity. Border regions use **ghost entities** (read-only replicas from neighbor cells) so AoI crosses cell borders seamlessly.
- **Durability contract.**
  - *Transactional* events (loot assignment, trade, AH, mail, quest completion, level-up, currency changes) are sent to the Persistence Service and **acknowledged only after a synchronous Postgres commit**. The sim doesn't block a tick on this; the event stays "pending" for ≤ 2 ticks typically.
  - *Soft state* (position, HP, buffs) is checkpointed every 10 s and on handoff/logout.
  - A dead cell respawns, rehydrates from the DB, and players reconnect through the gateway.
- **Anti-cheat on the server.** Clients send inputs (move intent, ability ID, target ID, sequence number). The server validates each one: speed envelope, collision, line-of-sight, range, cooldown, GCD, resources, ownership and value conservation on trade. Each check feeds a per-player **suspicion score** streamed to Redpanda.

---

# 3. Critical Path & Dependencies

The critical path runs through **`sim-core` → netcode → persistence/ledger → sharding → bot swarm → content tooling → content → scale certification → launch.** Anything that slips on this chain moves launch. Everything else has float.

| Phase N must deliver... | ...so that Phase N+1 isn't blocked | Lead-time note |
|---|---|---|
| **P0 → P1:** a locked engine/language ADR, protocol spec v0, art pipeline contract, `sim-core` boundaries, hiring done | P1 can't build prediction/reconciliation without the protocol and shared-core boundaries. Artists can't produce slice assets without the pipeline contract | Senior human reviewers must be hired by **M5**: AI output can't merge on critical crates without them |
| **P1 → P2:** a deterministic `sim-core`, a headless **bot client that uses the real netcode**, ledger v1, CI with Testcontainers | Every Phase 2 scale gate depends on the bot swarm. A bot that doesn't use real netcode produces meaningless load results. Sharding builds on ledger semantics | Bot client must exist by **M14** to rehearse the M16 gate |
| **P2 → P3:** scalable shard, layering, instances, stable service APIs, **content tooling v1** (world editor, content DB, Luau hot reload) | Content teams scale from ~8 to ~40 people. Without tooling they're idle or produce unscalable content. Instances are needed for dungeon/raid production | Tooling is the most under-estimated dependency. A dedicated tools team starts in **M22**, not at M31 |
| **P3 → P4:** content complete, **50k-CCU bot swarm on real content**, client perf budgets met, localization extracted | Beta can't start with missing content (first impressions). Scale certification at 100k needs the 50k platform. Loc vendors need 3–4 months | Outsourcing contracts signed by **M28** for Phase 3 ramp |
| **P4 → P5:** 100k CCU certified, pen test clean, DR drill passed, payments/legal live, ops staffed | Launch without these risks a catastrophic first week (the most expensive failure in MMOs) | Cloud capacity reservations and limit increases requested by **M50** |
| **P5 → Sustainment:** stable SLOs, live cadence, unit costs at target | Expansion development needs a stable live base and a funded team | Expansion pre-production starts **M57** |

**Parallel (off-critical-path) tracks:** art style exploration and concept (M3–M30), music/audio identity (M10+), narrative and world bible (M2–M30), marketing and community building (M36+), platform/storefront certification (M44+), localization (M34+).

---

# 4. Top 5 Risks to the Five-Year Plan and Mitigations

| # | Risk | Likelihood / Impact | Early warning signals | Mitigations |
|---|---|---|---|---|
| **1** | **Content throughput falls short.** Launch-scope content (14 zones, ~1,400 quests, 10 instances) can't be produced on time or budget. Historically the #1 MMO killer. | High / Critical | Wave 1 (M31–M34) delivers < 80% of planned zone-equivalents; cost per quest > 1.3× model | **Content cost model** from Phase 0, re-baselined every wave. **Tooling investment** starts M22 (world editor, encounter sandbox, quest bot tests). **AI-assisted drafting** (quest text, item stats, spawn layouts, terrain passes) with human editorial ownership. **Two outsourcing vendors.** A **pre-agreed cut list** in priority order: 2nd raid → x.1, 14 → 12 zones, 3rd spec per class deferred, 2 BGs → 1. **Modular zone kits** to reuse art. |
| **2** | **Engine/tooling immaturity (Bevy).** Missing editor features, API churn, rendering gaps or performance cliffs at MMO scale. | Medium / High | Spike B misses fps targets; Bevy upgrades costing > 2 engineer-weeks each; artists blocked on editor features | The **Phase 0 hard gate with a pre-approved UE5 fallback** (Rust `sim-core` linked via FFI so server logic is unchanged). **Pin and fork** Bevy, upgrading only in scheduled windows. Engine-agnostic `sim-core` (no rendering types). Build in-house tools early (from M22). Contribute upstream to reduce fork drift. |
| **3** | **Scaling cliffs in practice.** Hotspots (city events, world bosses), database write amplification, or global-service fan-out (chat/presence) breaking the 100 ms / 1,250 ms budgets at 10k–100k CCU. | Medium / Critical | Tick p99 creeping > 20 ms at 5k CCU; Postgres WAL rate > 60% of disk throughput; NATS fan-out latency growth super-linear in CCU | **Bot swarm from Phase 2** with escalating gates (10k → 30k → 50k → 100k). **AoI LOD tiers** (near/mid/far rates). **Layering plus cell split** for hotspots. **Write-behind** soft state; synchronous commits only for ledger. **Bucket indirection** so DB clusters can be added without re-hashing. **Architecture allows 30 Hz and per-cell thread budgets.** Capacity tests run before every phase gate, not just at the end. |
| **4** | **AI-primary development produces architectural drift and hidden defects.** Code volume outpaces human review, inconsistent patterns, subtle concurrency and economy bugs (e.g., item dupes). | Medium / High | Rising change-failure rate; review queue > 3 days; proptest/fuzz finding new invariant failures week over week | **CODEOWNERS with mandatory human approval** on critical crates. **ADRs** as binding context for agents. **Invariant-first testing** (proptest for conservation laws, fuzzing for protocol parsers, Testcontainers for real DB semantics). **Architecture lints** (crate dependency rules, banned APIs, no `unsafe` outside approved modules). Weekly human architecture review. **DORA metrics** tracked; agents throttled if change-failure rate > 15%. Ledger reconciliation runs continuously in every environment. |
| **5** | **Security, cheating and economy exploits at launch.** Dupes, speed/teleport hacks, botting, gold-selling, DDoS, or account takeovers destroy trust in week one. | High / High | Red-team success in staging; abnormal ledger flows in beta; credential-stuffing spikes | **Server-authoritative inputs-only model** from day one. **Double-entry ledger** with real-time conservation checks and per-item provenance (any dupe is detectable and reversible). **Per-feature kill switches** (AH, trade, mail). **Two pen tests** (M48, M53), plus a bug bounty from open beta. Client integrity via EAC/BattlEye as a *supplementary* layer. **WebAuthn/TOTP 2FA** and an authenticator incentive. UDP DDoS protection at the edge plus gateway admission control. Bot detection v1 in beta, ML-based v2 post-launch. |

**Additional tracked risks (lower tier):**
- *Funding/market timing:* hold a 6-month cash runway buffer at every gate.
- *Key-person loss:* every critical crate has ≥ 2 human reviewers trained on it.
- *Scope creep:* change-control board; new features only by trading scope out.
- *Regulatory change on monetization:* no paid random loot, so exposure is minimal.

**Schedule buffer:** the 60-month plan holds **≈ 3 months of explicit buffer**. Each phase plan uses roughly 92% of its calendar, and gate-remediation sprints draw from that buffer. If cumulative slip exceeds the buffer by the **M30 gate**, the cut list (Risk #1) is executed automatically. Launch isn't moved until the cut list is spent.

---

*Next step after approval of this document: start Phase 0. The first concrete outputs are the ADR template and backlog, the Spike A/B specifications with their pass/fail benchmarks, and the content cost model worksheet.*
