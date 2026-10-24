# Gameplay review — 23 October 2026

Status: review draft. Recommendations are proposals, not approved implementation work.

This review compares the current C++ simulation, gameplay definitions and controls with the game design, resource design, pacing document and roadmap. It is a source review, not a fresh playtest. Implementation gaps below are observable in code/data; balance and enjoyment concerns are hypotheses to validate in play.

## Assessment

The game has a substantial economic sandbox: gather physical resources, haul them to processors, build infrastructure, distribute power, recharge drones and expand. Its strongest identity is the relationship between logistics and territory.

It does not yet have a complete competitive match. There is little military content, no implemented victory/defeat flow, and no automated opponent or networked multiplayer transport found in the current implementation. More economy and terrain content alone will not establish whether the game is enjoyable as an RTS.

The next gameplay milestone should let a player build an economy, make a military investment, contest an expansion, damage the opponent's infrastructure, recover from a setback and finish the match.

## What should be preserved

- **Physical logistics.** Travel distance, cargo capacity, processor location and charging create decisions that a simple global income system would lose.
- **The emergency hub economy.** Reduced-yield processing gives players a starting fallback; dedicated processors offer a measurable improvement.
- **Infrastructure as territory.** Generators, poles, processors and chargers can make an expansion more interesting than placing another collection point.
- **Resource fields.** Large clustered fields provide readable destinations for expansion and potential conflict.
- **Terrain flexibility.** Tilt, modest grading and scaffolding support base layouts without demanding perfectly flat maps. Continue treating placement responsiveness as part of gameplay quality.
- **Exploration versus live information.** Showing discovered static resources and decoration while concealing current enemy movement is a useful distinction.
- **A small, valuable army.** This remains a promising design direction; validate it with a few purposeful units before adding a large roster.

## Current implementation and gaps

| Area | Current state | Gameplay implication |
| --- | --- | --- |
| Opening | Hub, one construction drone, one controllable ground worker; 150 Alloy. The starting worker cannot gather and cannot currently be trained. | One drone must choose between income and construction. The worker needs a clear purpose. |
| Economy | Scrap/Oil/Uranium conversions; powered dedicated processors; renewable Synthetic; physical hauling and charging. | There is an economic loop, but limited reasons to spend its output beyond infrastructure and more construction drones. |
| Production | Modern hub trains construction drones for 75 Alloy. | No trainable military roster or meaningful army composition yet. |
| Buildings | Eleven modern buildings appear in the build palette. | Some choices promise functions their current implementation does not provide; see below. |
| Combat | Worker melee weapon and attack orders exist. The construction drone has no weapon or combat component. | Combat is a foundation, not a playable combined-arms system. |
| Combat resolution | Current attack loop applies damage continuously per tick while in range. Weapon cooldown/accuracy definitions are not used by that attack loop. | Weapon data alone does not yet establish firing cadence or tactical differences. |
| Progression | Few upgrades; modern buildings have no research prerequisites. | Availability is broad, but there are few deliberate progression branches. A large age system is not necessary to fix this first. |
| Information | Fog, explored territory, intelligence memory, sensor tower and two satellite research tiers. | Useful foundation; permanent full vision will need careful balance once actual enemy activity exists. |
| Opponent and ending | Two player starts exist; no AI decision loop, multiplayer transport or match-ending rule found. | A complete adversarial match cannot currently be evaluated through the normal gameplay loop. |
| Orders | Move, gather, deliver, build, repair, recharge, stop and targeted attack commands. | Attack-move, hold/patrol commands and production rally-point commands are missing from the current command interface. |

### Buildable structures that need a functional decision

These are current content gaps, not merely provisional balance values:

- **Storage silo:** has health and storage/drop-off tags, but no storage or processor component and no matching simulation behavior found. Its name currently promises more than it delivers.
- **Resource extractor:** has production and a power demand, but no producer recipe or extraction behavior found.
- **Drone factory:** has production and a power demand, but no recipes assigned to it. Construction drones are produced at the hub.

For each, either finish a narrow useful role or temporarily exclude it from the normal build palette. Do not add another overlapping economic system just to justify a building already present. A reasonable first factory role is military-drone production; that is a proposal, not a decision to move construction-drone production out of the hub.

## Prioritized improvements

### 1. Complete a small match loop

Choose one initial win condition. Destruction of the designated starting command hub is the simplest candidate consistent with the current direction. Define what happens to additional hubs, simultaneous losses, surrender and post-match control. Capture can follow later.

Provide a way to play against opposition. A limited scripted opponent is useful for local iteration; controlled two-player sessions will eventually be needed to assess competitive decisions. Full networking is a separate engineering commitment, not a prerequisite for every early gameplay experiment.

Start with approximately three distinct combat roles: a basic frontline unit, a fast raider/scout and a support or counter unit. Their movement, range, target access and vulnerabilities should differ. Only include air combat if the initial roster has an understandable response to it. Add one affordable defensive option if otherwise losing the first encounter makes recovery impossible.

Implement firing cadence, target eligibility and the intended damage rules before tuning weapon values. Ensure indirect-only combat units can attack: the current attack-command acceptance also requires `directlyControllable`, which will need revisiting if combat drones are introduced.

**Success:** a player can contest an expansion, withdraw, repair/rebuild, attack again and reach a visible match result.

### 2. Make economic spending create competing plans

Alloy currently pays for most meaningful growth. Fuel has a much narrower use, including high-resolution satellite research; Data and Authority lack a playable spending loop. Do not make players manage additional resources before those resources support decisions.

Give the first combat slice an understandable economic tradeoff: more hauling capacity, a stronger processing network, immediate military pressure or a safer expansion. Introduce a Fuel sink only with content that justifies it, such as a particular vehicle or advanced unit. Avoid adding operating fuel to every unit merely to consume a surplus.

Review these specific balance risks:

- **Extra hubs:** 150 Alloy buys emergency processing, power generation, power storage, charging and drone production. Compare this package with specialized expansion infrastructure before raising costs or imposing a hub cap.
- **Renewable Synthetic:** can reduce dependence on contested natural fields. Measure whether its investment, power demand and output allow indefinite safe-base play. Preserve a recovery route without making expansion irrelevant.
- **Repairs:** currently consume drone battery without an Alloy cost in the repair step. Under pressure this may make damage too easy to erase. Decide the intended material cost and repair throughput alongside combat damage.
- **Opening fragility:** processor cost is 125 Alloy and another drone costs 75 against a 150-Alloy start. Check whether multiple openings are viable and whether losing the last drone can leave a player alive but unable to recover.

These are risks to test, not evidence that a particular build order is already dominant.

**Success:** at least two opening plans are credible, and attacking infrastructure has a meaningful but recoverable economic effect.

### 3. Make expansion create interaction

Keep the agreed fixed starting resource fields. Make additional fields attractive through travel efficiency, quantity, position and defensibility rather than removing the safe opening economy.

Construction drones fly and ignore entity obstacles. Consequently, many ground chokepoints do not constrain the current hauling economy. Ground combat, vulnerable processors and charging infrastructure must supply the territorial interaction; attractive cliffs alone will not do that.

Evaluate a nearby expansion, a more exposed efficient field and alternative approach routes. Avoid assuming every layout needs a central objective or central water. Judge each layout by whether players can discover useful alternatives and contest them without one route deciding everything.

**Success:** players have a reason to leave their initial base, can read the risk, and can respond to an expansion without committing to an immediate all-in attack.

### 4. Reduce routine work while preserving decisions

The player should decide where to build, what to protect and which bottleneck to address. Reissuing routine work or diagnosing a silent stall should not supply the difficulty.

Prioritize attack-move and hold-position for the combat slice, then rally points and useful queued orders. Review control groups, idle-drone selection and production shortcuts as a coherent interaction pass. Patrol can wait until it solves a demonstrated problem.

Make status actionable: a stalled drone or processor should explain whether it lacks power, charge, destination capacity, a compatible conversion or a reachable target. Show useful income/throughput information, not only resource balances. Power feedback should explain the limiting supply or link without requiring debug mode.

Preserve the deliberate rule that a new build order clears the previous gather loop. If resuming work is cumbersome, add an explicit queue/resume interaction rather than silently changing that rule.

**Success:** a player can identify and fix a stalled economy from normal gameplay feedback, and moving a placement preview remains responsive during a busy match.

### 5. Give direct control and information systems a purpose

The non-gathering starting worker has movement and melee capability but no established economic job. Decide whether it is primarily a scout, a combatant or another deliberate role. Demonstrate one useful task before expanding possession mechanics. Direct control must remain optional and use the same authoritative combat rules as strategy orders.

Keep the requested top-bar SAT shortcut: first click grants low resolution, second grants high resolution. Keep paid hub research too. For competitive playtests, explicitly record whether the free shortcut is used; otherwise scouting, sensor-tower value and satellite research costs cannot be evaluated meaningfully. Whether the shortcut becomes a development option or a supported match rule is a later product decision.

Low resolution should preserve the agreed static-resource/decoration visibility without exposing current enemy movement. High resolution's permanent live coverage is a major strategic advantage; evaluate its cost and timing against real combat before adding counters or additional surveillance systems.

**Success:** scouting changes a decision, direct control has an optional useful role, and information upgrades have an observable opportunity cost in the tested ruleset.

## Recommended work order

1. Resolve the unfinished build-palette roles and specify the first match's win/loss and recovery rules.
2. Record a short opening playthrough: first delivery, first extra drone, first processor, first power bottleneck and first expansion. Use it to identify dead time before choosing balance changes.
3. Implement the minimal military roster, combat resolution, attack-move/hold, essential production and match conclusion.
4. Provide repeatable opposition and play the complete economy-to-conflict-to-result loop.
5. Tune expansion incentives, repairs, replacement costs, Synthetic and hub efficiency using those sessions.
6. Improve normal-gameplay feedback and input friction where the sessions show problems; profile placement and army commands under representative load.
7. Expand progression, factions and advanced resources only after the smaller match supports different credible strategies.

The design's 60–120 minute match target remains an intention, not an established result. A compressed 15–25 minute development scenario could expose the entire loop sooner; that would be a testing format, not a decision to replace the intended match length.

## What to measure in the first gameplay sessions

Record time to first income, first expansion and first meaningful contact; drone time spent hauling versus charging/waiting; whether Fuel has a useful destination; what gets attacked; time and cost to recover; and why the match ends. Also record moments when the player does not know what to do next.

Check both ends of the skill range: a new player should understand a blocked economy; an experienced RTS player should find decisions beyond building infrastructure faster. A promising session contains a choice, a readable consequence and an opportunity to respond.

Do not treat a successful build or deterministic simulation test as proof of good pacing or balance. No tests, builds or gameplay changes were performed for this review.

## Evidence inspected

- `assets/gameplay/{rules,units,buildings,recipes,conversions,power,weapons,upgrades,countries,specializations}.json`
- `src/simulation/GameSession.cpp` — command acceptance, production, gathering, charging, repairs, resource regeneration and attack resolution.
- `src/simulation/Command.hpp` — available authoritative actions.
- `src/gameplay/GameplayCatalogue.cpp` — weapon/stat definition resolution.
- `src/game/PlayState.cpp` and `src/ui/EntityHudModel.cpp` — controls and gameplay presentation.
- `docs/GAME_DESIGN.md`, `docs/Resource system.md`, `docs/match progress.md`, `docs/ROADMAP.md`, `docs/agents.md`.

The roadmap already classifies the first combat slice as planned. The key discrepancy is the playable palette's apparent breadth compared with the narrower functional content, rather than a claim that the full RTS has already been implemented.
