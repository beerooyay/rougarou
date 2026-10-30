# rougarou

An indoor bouldering game set in the rougarou rock lounge. You move each hand and foot yourself, shift your weight, and try to send the problem. Physics and your own strength are the only limits.

**Play in the browser:** https://beerooyay.github.io/rougarou/ (desktop and phone)

## How to play

- **Desktop:** click a hand or foot, move the mouse to reach, then click to place it on whatever it touches. To shift your hips, use WASD or drag on empty wall. Esc puts the limb back, X lets it hang, R retries, `[` and `]` change the problem, and G shows the whole gym.
- **Phone:** drag a hand or foot onto a hold. To shift your hips, drag on empty wall.

A problem is sent when both hands are matched on the top hold and held under control.

## What the climbing models

- **Three points of contact.** The limbs on the wall share your weight according to their strength. Legs are far stronger than arms, and a smear carries only a fraction.
- **Hold types.** Jugs, crimps, slopers, pinches and pockets each hold differently, depending on which way you pull them.
- **Steep walls peel you off.** Slabs press your feet in instead.
- **Reaching shifts your hips and turns your torso.** This is what makes cross moves possible.
- **Hands and feet travel at human speed.** A limb only takes weight once it arrives.
- **Deadpoints.** A hold caught while your body is still locks right in. One caught mid-swing has to be fought for.
- **Forearm pump.** Bent-arm lock-offs pump you far faster than hanging off straight arms.
- **Gradual slipping.** A brief overload is held, a sustained one creeps off, and a big one rips straight off.

## Layout

- `unreal/` is the main build, in Unreal 5.8 with a C++ module. `ClimbSim.*` is the climbing model and the source of truth.
- `web/` is the browser port (three.js, no build step). It is deployed to GitHub Pages by `.github/workflows/pages.yml`. `node web/check.mjs` proves every problem can be sent.
- `unity/` and `godot/` are the original prototypes.
