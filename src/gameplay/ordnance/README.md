# Ordnance flight and material traversal

Start with [ordnance.cppm](ordnance.cppm).

Owns swept three-dimensional contact geometry, blast surface exposure, missile guidance/arc integration, lead and coverage calculations, destruction retirement and ordered compartment material traversal.

`PenetrationTraversal` yields one typed contact at a time. The native adapter applies the ordinary pre-damage, casualty, fault, post-damage and payload-receipt boundaries before resolving that contact with actual absorption. Reactions therefore remain between successive material crossings, including partial compartment failure; no callback collection or universal world context is exposed.

Validation: Existing synthetic combat/controller/continuation system cases after native adapter activation. Graphical/audio/tuning smoke remains separate.
