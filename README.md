# vcm_switch
Force feedback switch concept using voice coil motors

# Moving Coil Design
The first design I made was a moving coil design. The coil is relativly light compared to the magnets which would allow for faster master movements.


<img width="1067" height="852" alt="{Cross section of moving coil design}" src="https://github.com/user-attachments/assets/934ccd31-9c0d-4a07-a008-7f90d6c4dd7f" />

The bobbin and core are 3d printed out of PETG. The bobbin is wound with two layers of 30AWG magnet wire (about 60 turns).
The magnets, washers, and core are glued together and can be mounted to the PCB with a screw.

The PCB section is 19.05mm for reference, so it fits within a standard key unit. The height is 14mm. Adding an MX style stem wouuld add another 4mm.

From simulations I found that a ferrous spacer between the magnets increases efficiency by 5-10% while a ferrous core decreases efficency by 5-10%.

The spring could possibly be replaced with a small magnet to provide the passive force instead.

The magnetic field generated from the coil is very little relative to the magnets but may still need to be adjusted for in software.

## Issues with this design:

Melts bobbin after a couple minutes.

No rotational movement constraint.

No vertical movement constraint.

Wires on moving parts.

## Simulation
<img width="1618" height="973" alt="Moving coil VCM simulation" src="https://github.com/user-attachments/assets/186a46bd-645d-4086-91c4-96243d16341d" />
There is a very consistant response across the travel of the switch which then falls off quickly after the edge of the coil starts to move past the bottom magnet.
The flat response area can be extened for more switch travel by increasing the coil height.

In practice I estimate the force to be about 60g (0.6N) at 5A. I will do proper tests on this in the future.

## Pictures
<img width="1705" height="1279" alt="image" src="https://github.com/user-attachments/assets/693bae00-1249-4b2b-bf0a-f47eb23e39ab" />
<img width="1705" height="1279" alt="image" src="https://github.com/user-attachments/assets/cc07a811-cf3e-4aa5-9688-4eae204eb426" />


# Moving Magnet Design
This section will be updated when the protype is finished.

This design uses copper rods for a smooth linear motion and rotational constraint. It is not ideal because it adds complexity, relies on friction fitment, the coil is far from the magnets, and it is very wide using the whole key unit.

Designing the top stem part to be a square or other polygon and be able to print horizontally may be a better solution but that would only have one point of contact which could lead to bad wobble.

Feedback for any other ideas would be appretiated.

<img width="916" height="876" alt="{7D4D76DD-6E0B-40AD-A36A-D032F15F9EBD}" src="https://github.com/user-attachments/assets/5a702273-71ae-4369-b504-e070f2874428" />
<img width="1241" height="965" alt="{0341034A-AC02-4D13-881B-9EF00B67B949}" src="https://github.com/user-attachments/assets/6fe5ea91-d0b1-457b-abea-6234de60c02b" />
