# VCM Switch V0.1

Force feedback keyboard switch using voice coil motor.
This could be used for haptic feedback or to adjust force curves and tactility for the users preferences.

## Mechanical design

This was designed to be SLA printed with a high temp resin such as Syria Tech Sculpt.
The stem components and magnets are assembled with high temp epoxy in a fixture to align the magnets.

The opposing magnets cause a strong magnetic field in the small area where they meet. Only the part of the coil in this magnetic field is contributing the force. Additionally the ends of the magnets have an opposite magnetic field causing a force in the other direction, decreasing the force in the desired direction. By being able to turn off parts of the coil when not contributing to the desired force the switch will be more efficient for reduced heat and have a higher peak force. Two coils are used in this design due to small stroke length and driving simplicity.

The coils were wound with 0.1mm (38 AWG) enamel wire which measured about 58 Ohms giving a max current of 200mA at 12V.

A spring from a low profile switch was used to provide a light baseline force that can be altered with the coils.

![[Images/vcm_switch_cross_section_labled.png|542]]

![[Images/vcm_switch_render_bottom.png|542]] 

![[Images/vcm_switch_render_top.png|530]]

## Electronics

Two Hall Effect sensors (AH49E) in the centers of the coils were used to track the position of the switch in hopes of not picking up the coils magnetic field by being off axis.

A quad half bridge motor driver (TB67H410NG) was used to be able to drive each coil in both directions.

![[Images/test_fixture.jpg]]

## Firmware

The two sensors have a offset sinusoidal output across the travel of the switch which allows for an arctan2 function to be used to get an angle which is relative to the travel position. In theory this should be close to linear and can be scaled and offset to calculate the position. However this proved to be inaccurate and linear interpolation with a lookup table was used instead.

The sensors pick up enough of the coils magnetic field for almost a millimeter of deviation when the coil is on at full power. The change in angle relative to the duty cycle is very linear and does not change depending on which coils are active. 

![[Images/coil_offset_test.png]]

This allows for a constant multiplied by the duty cycle to be used to calculate the correct offset. The following figure shows the calculated position while ramping the duty cycle from 0-100% in both directions with and without the offset.

![[Images/coil_offset_comparison.png]]

A three point median filter was used to remove any large spikes of noise without causing too much delay.

![[Images/filtered_data_comparison.png]]

When the coil heats up the resistance increases which causes less coil current and therefore less magnetic field produced by the coil causing the offset to be inaccurate. Temperature compensation was not implemented for this prototype and was just cooled between tests.

A cross fade was implemented for a smooth transition when switching between the two coils.

## Tests
These tests were performed with a 12V supply drawing about 2.5W per coil at 100% duty cycle.

### Travel Tests

Baseline - no current through the coils
![[Images/no_coil.png]]

Top coil only - 100% duty cycle![[Images/top_coil_100dc.png]]

Bottom coil only - 100% duty cycle
![[Images/bottom_coil_100dc.png]]

Both coils - 100% duty cycle (5W)
![[Images/both_coil_100dc.png]]

Dynamic coils - 100% duty cycle (2.5W)
![[Images/dynamic_coil_100dc.png]]


### Duty Cycle Tests

0 to 100% duty cycle - mid stroke
![[Images/duty_cycle_test.png]]