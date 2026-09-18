# Legal position

**This is not legal advice.** It is a record of the provisions this project
relies on, the conditions attached to them, and where the project is exposed
anyway. It is written so the reasoning is on the record and can be checked —
and corrected — rather than assumed.

If the project is ever published, this document should be reviewed by someone
qualified first.

---

## What actually protects the work

A disclaimer protects nothing on its own. These provisions do, and only to the
extent the project's behaviour matches them.

### 1. Facts and functional elements are not copyrightable

**17 U.S.C. §102(b)** — copyright does not extend to any "idea, procedure,
process, system, method of operation". **Feist v. Rural Telephone** (US Supreme
Court, 1991) — facts are not copyrightable, however much labour went into
collecting them.

This is what makes `config/symbols/` and `docs/evidence/` defensible. A function
address, a size, a call count and an API name are facts about a system, not
creative expression. Every decompilation project publishes exactly this.

### 2. Reverse engineering for interoperability

**Sega v. Accolade** (9th Cir., 1992) and **Sony v. Connectix** (9th Cir., 2000)
— intermediate copying during reverse engineering is fair use where it is
necessary to reach unprotected functional elements. Connectix concerned a
PlayStation emulator specifically, so it is close to the facts here.

**Important limit:** the reasoning in both turned on the intermediate copies
**not being distributed**. These cases protect *the act of decompiling*. They do
not protect *publishing the decompiled output*. That distinction is why this
repository commits addresses and names but not reconstructed source.

### 3. Reimplementing an API

**Google v. Oracle** (US Supreme Court, 2021) — reimplementing API declarations
to allow independently written code to interoperate was transformative fair use.

Directly on point: the runtime reimplements the GameCube SDK's public API in
code written from scratch. That is the same shape of act the Court approved.

### 4. UK: a statutory right, not a defence

**Copyright, Designs and Patents Act 1988, s.50B** — decompilation is permitted
to obtain information necessary to create an independent program that can
interoperate. **s.50BA** — observing, studying and testing to determine
underlying ideas and principles is permitted.

**s.296A makes these unwaivable**: a contract term purporting to prohibit them
is void. That is stronger than US fair use, which is a defence raised after the
fact rather than a right.

**But s.50B's conditions are real and must be met:**

| Condition | How this project stands |
|---|---|
| Necessary for interoperability | Yes — the SDK API must be known to reimplement it |
| Information not already readily available | Yes — no public decompilation of this game exists |
| Confined to the parts necessary | Partly. Analysing the whole binary is hard to avoid |
| **Not used to create a substantially similar program** | **The condition to watch.** A reimplemented *SDK* is not similar to the *game*. Argue interoperability, not substitution |

### 5. EU

**Directive 2009/24/EC, Article 6** (decompilation for interoperability) and
**Article 5(3)** (observe, study, test). Article 8 makes these unwaivable. The
UK provisions above implement this directive and survive Brexit.

---

## Where the project is exposed regardless

Stating these plainly is the point of the document.

**Trademark is separate from copyright.** "Metal Gear Solid", "Twin Snakes" and
"Konami" are Konami's marks; "GameCube" and "Nintendo" are Nintendo's. Nominative
use — naming the game a tool works with — is generally permitted. Logos, box
art, and any presentation implying endorsement are not.

**§50B and fair use protect the analysis, not the audience.** They say nothing
about whether end users have lawfully obtained their copy.

**Konami is actively litigating** over MGS2's leaked source, and this game runs
Konami's MGS2 engine. That raises the practical risk well above the theoretical
baseline, independently of how well the copyright argument holds.

**A DMCA takedown does not require being right.** Hosts act on notices, not
merits. Several careful, well-behaved decompilation projects have been taken
down. Correctness reduces the odds; it does not eliminate them.

**Circumvention is a separate question.** 17 U.S.C. §1201(f) has an
interoperability exemption, but it is narrower than fair use. This project does
not circumvent anything — it reads a disc image the user already has — and it
should stay that way. Do not add DRM circumvention.

---

## What the disclaimer is actually for

Not a shield. A disclaimer creates no protection and changes no liability.

What it does do:

- **Records intent contemporaneously.** Fair use and s.50B both turn on
  *purpose*. A statement written before any dispute is better evidence of
  purpose than one written after.
- **Gives a reviewer the facts fast.** Whoever reads a takedown notice decides
  quickly; a clear statement that no game code or assets are distributed and the
  user supplies their own copy is the single most useful thing they can find.
- **Binds our own behaviour.** The statement is only worth anything while it is
  true, which makes it a standing constraint on what may be committed. That is
  its most useful property.

The disclaimer is in [`../README.md`](../README.md). **If a change would make it
untrue, the change is wrong — not the disclaimer.**
