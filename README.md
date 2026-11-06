# rougarou

An indoor bouldering game set in the rougarou rock lounge. Walk the gym, pick a problem by its grade, pull on at its start holds, then move each hand and foot yourself, shift your weight, and try to send it. Physics and your own strength are the only limits.

**Play in the browser:** https://beerooyay.github.io/rougarou/ (desktop and phone)

## How to play

- **The menu** (M, Esc or the round button) lists every problem with your sends, best time and flashes, and lets you climb one straight away, walk to it, or set it. Settings change your shirt, shorts and shoe colors, sound, look speed and graphics.
- **Clips:** press the red dot (or the backtick key) to record the game with its sound. Clips stay in your browser, and the viewer trims, splits and downloads them.
- **Route setting:** pick "set it" on a problem, then drag holds across the wall, turn them (Q and E, the arrows or the mouse wheel), change their type, add or remove holds. A turned hold is pulled a different way, so the climbing changes too. Your sets are saved in the browser and can be reset.

- **Walking the gym:** WASD to walk (Shift to jog), drag to look around. On a phone the left thumb walks and the right thumb looks. Each problem has a tag with its grade. Walk up to one and click or tap its two start holds (or press E, or the pull on button) to pull on. `[` and `]` take you to the previous or next problem.
- **Climbing on desktop:** click a hand or foot, move the mouse to reach, then click to place it on whatever it touches. To shift your hips, use WASD or drag on empty wall. Esc puts the limb back, X lets it hang, J jumps down to the mats, R retries, and G shows the whole gym.
- **Climbing on a phone:** drag a hand or foot onto a hold. To shift your hips, drag on empty wall.

A problem is sent when both hands are matched on the top hold and held under control. Come off and you fall onto the mats, then get back up and can walk back to the start.

## What the climbing models

- **Three points of contact.** The limbs on the wall share your weight according to their strength. Legs are far stronger than arms, and a smear carries only a fraction.
- **Hold types.** Jugs, crimps, slopers, pinches and pockets each hold differently, depending on which way you pull them.
- **Steep walls peel you off.** Slabs press your feet in instead.
- **Reaching shifts your hips and turns your torso.** This is what makes cross moves possible.
- **Hands and feet travel at human speed.** A limb only takes weight once it arrives.
- **Deadpoints.** A hold caught while your body is still locks right in. One caught mid-swing has to be fought for.
- **Forearm pump.** Bent-arm lock-offs pump you far faster than hanging off straight arms.
- **Gradual slipping.** A brief overload is held, a sustained one creeps off, and a big one rips straight off.
- **Bare wall is friction only.** A palm or a smear can be pushed against but never pulled on. It grips only as hard as something presses the body into the wall: a slab leaning you onto it, or hands on good holds pulling your hips in. Overhangs pull you off and take that squeeze away.
- **Limb weight and flagging.** Each leg is about a sixth of your mass and hangs where you put it, so a free leg turns the body. Flag it across to the side of your holds to stop a barn door.
- **You need a hand on.** Feet alone only hold you on a low-angle slab. Anywhere steeper you tip back off.

## Layout

- `unreal/` is the main build, in Unreal 5.8 with a C++ module. `ClimbSim.*` is the climbing model and the source of truth.
- `web/` is the browser port (three.js, no build step). It is deployed to GitHub Pages by `.github/workflows/pages.yml`. `node web/check.mjs` proves every problem can be sent.
- `unity/` and `godot/` are the original prototypes.

### Unreal player and menus

Launch locally with `./play-unreal.command`. The problems menu offers **climb
it** and **walk there**. **M** opens problems, settings and help. **V** switches
between third and first person. In first person, look at a hold and press **R**
or **L**, then **A** for that arm or **L** for that leg; **1/2** are alternate
hand picks and **3/4** are foot picks. Arrow keys latch a hip shift, **Space**
centers, and **Escape** frees the cursor for the retry and limb buttons.

First-person climbing incorporates [Corey's web contribution](https://github.com/ceeboozwah/rougarou/commit/cefc14d6fcfd438305062b45ede2829359e2313e).
The native player uses a fitted deform rig, synchronized locomotion, and spring
driven garment bones. See `unreal/Tools/README.md` for the local asset pipeline
and reproducible visual checks.

The human-motion port from [Corey's Form Lab work](https://github.com/ceeboozwah/rougarou/tree/3d30a187e736c28c76b58840d0d3248573175737)
adds fixed swing/twist limits, wall-aware elbow/knee swivels, shared spine and
scapular movement, bounded pelvis adjustments, and temporal smoothing. The local
posture fit uses six training problems and three held-out problems; its report,
reproduction commands and current contact-error limits are documented in
[the motion port notes](unreal/Tools/Upstream/README.md).

Normal native launches use the personalized CC4 player: the saved face/head,
5 ft 7 in height, reshaped shoes with measured outsole contacts, ivory tee and
forest green shorts. The body retains the CC4 skin weights. Garment edge motion
is bounded to 3 cm at hem controls, 1.5 cm at sleeves and 2.1 cm at cuffs; this
is lightweight secondary motion, not a full fabric solver. `-coreybaseline`
selects the preserved original. The earlier Chaos experiments remain opt-in
through `-modularplayer` and `-garmentcandidate`. See the character tools notes
and `Saved/PersonalPlayer` captures for the review and known art limitations.
