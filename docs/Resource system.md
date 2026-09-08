# Resource System

The economy is built around two raw resource chains, two strategic resources, and one infrastructure system.

**Scrap → Alloy → Construction**  
**Fuel Sources → Fuel → Mechanized Warfare**  
**Data → Research / Cyberwarfare**  
**Authority → Reinforcements / Strategic Influence**  
**Power → Infrastructure Capacity**

Raw resources are not intended to become additional stockpiled currencies. They exist physically on the map, are collected, and are delivered to the appropriate processor.

Once delivered, conversion happens **instantly** if power allows it, otherwise during new power generation.

The player's actual economic stockpiles are primarily **Alloy** and **Fuel**.

This keeps the harvesting system visually important without forcing the player to manage unnecessary intermediate inventories.

---

# Scrap — Raw Resource

Scrap is the primary raw construction resource.

It represents recoverable metal, machinery, electronics, structural components, and industrial material left behind by the old world.

Scrap can be found in:

- Industrial ruins
- Vehicle graveyards
- Destroyed infrastructure

Harvesting units collect Scrap directly from these locations and transport it to an **Alloy Processor**.

The moment Scrap reaches the processor, it is converted into Alloy.

There is no separate Scrap stockpile.

For example:

`100 Scrap delivered → 100 Alloy`

The exact conversion rate can be adjusted for balance, but Scrap should generally have a simple and predictable conversion ratio.

Destroyed units and structures can also leave behind recoverable Scrap. Major battles can therefore temporarily create new economic opportunities on the battlefield.

Over time, easily accessible Scrap fields should become depleted. This gradually pushes players away from their starting positions and toward contested ruins and settlements.

**Primary role:** Physical harvesting and territorial expansion.

**Resource chain:**

`Scrap Field → Harvester → Alloy Processor → Alloy`

---

# Fuel Sources — Raw Resources

Fuel is produced from several types of raw energy resources found throughout the map.

Unlike Scrap, different fuel sources can have different rarity, harvesting characteristics, and conversion efficiency.

Possible sources include:

### Oil

The most common fuel source.

Oil can be found in:

- Oil fields
- Abandoned storage tanks

Oil should provide a reliable and relatively predictable source of Fuel.

Example:

`100 Oil → 100 Fuel`

Oil deposits are therefore the standard fuel resource around which most mechanized economies are built.

---

### Uranium

Uranium is a rarer and more strategically valuable energy resource.

Rather than representing literal vehicle fuel, Fuel is an abstracted military energy resource covering petroleum products, reactor material, chemical propellants, and other strategic energy supplies.

Uranium therefore produces significantly more usable Fuel per unit transported.

Example:

`100 Uranium → 250 Fuel`

Uranium deposits should be uncommon and located in highly contested areas.

They can support powerful late-game economies but should not completely replace conventional Oil infrastructure.

---

### Synthetic

A third source is a mine that can be placed. 
This will turn up fixed amount of synthetic while the machine remains powered. 
Produced synthetic inside the mine can be picked up by a harvesting unit and delivered to either the fuel or alloy processor to exchange in 1:1 rates.

---

## Fuel Conversion

Raw fuel resources are collected and brought directly to a **Fuel Processor**.

Conversion occurs immediately upon delivery.

The raw material is never added to the player's global stockpile.

For example:

`Oil → Harvester → Fuel Processor → Fuel`

Different source types simply determine how much Fuel the delivery produces.

An illustrative balance could be:

| Raw source | Example conversion | Availability |
|---|---:|---|
| Oil | 1.0 Fuel per unit | Common |
| Synthetic | 1.5 Fuel per unit | Uncommon |
| Uranium | 2.5 Fuel per unit | Rare |

These numbers are balance variables rather than fixed rules.

This system allows certain areas of the map to be considerably more valuable without introducing another currency.

A player sees a Uranium deposit and immediately understands that controlling it could support a much larger mechanized army.

**Primary role:** Creates strategically unequal territory worth contesting.

**Resource chain:**

`Fuel Source → Harvester → Fuel Processor → Fuel`

---

# Alloy — Processed Resource

Alloy is the primary construction currency.

It is created instantly whenever Scrap is delivered to an Alloy Processor.

Alloy is used for:

- Buildings
- Infantry equipment
- Vehicles
- Defensive structures
- Production facilities
- Repairs
- Mechanical upgrades

Alloy should be the resource players spend most frequently throughout the match.

Because processing is near instantaneous, the important economic decisions are:

- Where to harvest Scrap
- How many harvesters to operate
- Where processors are positioned
- How safely resources can be transported
- Which Scrap fields are worth defending

Processor placement therefore matters.

A processor built close to a large Scrap field increases economic efficiency but may also expose valuable infrastructure to enemy raids.

**Primary role:** General construction and production currency.

**Resource chain:**

`Scrap → Alloy → Base / Army`

---

# Fuel — Processed Resource

Fuel represents the strategic energy required to manufacture and deploy advanced military machinery.

It is produced instantly whenever Oil, Uranium, or another compatible fuel source reaches a Fuel Processor.

Fuel is primarily required for:

- Tanks
- Armored vehicles
- Aircraft
- Heavy drones
- Artillery platforms
- Large mechanical units
- Certain advanced weapons
- High-tier military production

Fuel should primarily be a **production resource**, not a constant movement requirement.

A tank should not suddenly become immobilized because the player reaches zero Fuel.

Instead, insufficient Fuel prevents the construction of additional mechanized units or the activation of particularly powerful systems.

This keeps Fuel strategically important without introducing excessive combat logistics.

Different fuel sources create different economic opportunities.

A player controlling several ordinary Oil deposits may have a stable economy, while another player controlling a single valuable Uranium site might achieve similar output with fewer harvesting operations.

**Primary role:** Controls the scale and sophistication of mechanized warfare.

**Resource chain:**

`Oil / Uranium / Feedstock → Fuel → Heavy Military`

---

# Data — Advanced Strategic Resource

Data represents technological knowledge, encrypted information, software, intelligence databases, artificial intelligence systems, and access to digital infrastructure.

Unlike Scrap and fuel resources, Data is not primarily collected by conventional harvesters.

It can be acquired through:

- Data centers
- Communications towers
- Research facilities
- Satellite uplinks
- Abandoned server complexes
- Captured technological objectives
- Reconnaissance systems
- Hacking enemy infrastructure

Data has two competing uses.

## Research

Players can permanently invest Data into:

- Unit upgrades
- New technologies
- Improved sensors
- Better cyber capabilities
- Advanced weapons
- Specialized units
- Production improvements

## Cyberwarfare

Players can instead spend Data immediately on cyber operations.

Possible operations include:

- Disable an enemy turret
- Jam radar
- Interrupt production
- Reveal enemy structures
- Disrupt communications
- Disable automated systems
- Spoof sensors
- Compromise drones
- Conceal friendly movements
- Temporarily disrupt enemy Power distribution

This creates a persistent strategic decision:

**Invest Data for permanent technological superiority or spend it now for an immediate battlefield advantage.**

Data should become relevant relatively early in the match, ideally around the time players establish their first meaningful expansion.

**Primary role:** Technology and digital warfare.

**Strategic choice:**

`Data → Research OR Cyber Operations`

---

# Authority — Strategic Resource

Authority represents the faction's political influence, military reputation, command priority, and ability to draw support from forces beyond the immediate battlefield.

Unlike Alloy and Fuel, Authority cannot be harvested.

It must be earned through strategic success.

Sources can include:

- Capturing objectives
- Holding settlements
- Controlling communication centers
- Destroying important enemy infrastructure
- Completing secondary objectives
- Maintaining territorial dominance
- Winning major engagements
- Securing important regions

Authority connects what happens on the battlefield to the wider conflict outside the playable map.

## Immediate Military Support

Authority can be spent to request:

- Infantry reinforcements
- Vehicle reinforcements
- Emergency supply deliveries
- Artillery support
- Reconnaissance flights
- Specialist units
- Drone support
- Rapid-response forces

These units or effects originate outside the map rather than from the player's normal factories.

Off-map reinforcements should supplement normal production rather than replace it.

## Strategic Influence

Authority can instead be invested into longer-term advantages:

- Gain support from settlements
- Recruit local militia
- Secure regional supply routes
- Gain access to neutral infrastructure
- Establish forward deployment locations
- Unlock regional bonuses
- Gain cooperation from neutral factions

This creates another important decision:

**Spend Authority for immediate military assistance or preserve it for long-term strategic control.**

**Primary role:** Converts battlefield success into external military and political power.

**Strategic choice:**

`Authority → Reinforcements OR Influence`

---

# Power — Infrastructure System

Power is not a conventional resource and does not enter the player's stockpile.

Instead, it represents electrical generation capacity.

Power can be produced by:

- Solar panels
- Fuel generators
- Industrial power stations
- Advanced reactors
- Captured electrical infrastructure

Buildings consume Power while operating.

Major consumers can include:

- Factories
- Radar systems
- Data centers
- Defensive turrets
- Cyberwarfare facilities
- Advanced production buildings
- Repair systems

The fundamental rule is:

`Power Production ≥ Power Demand`

If demand exceeds available production, the base should experience degraded performance rather than an immediate total shutdown.

Possible penalties include:

- Slower production
- Reduced radar coverage
- Slower Data generation
- Disabled advanced defenses
- Reduced repair speed
- Reduced cyberwarfare capability

Power infrastructure therefore becomes an important military target without becoming another currency the player constantly spends.