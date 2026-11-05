# Viewscreen ship handling prototype

Enable **Test Missions**, host **Free Flight**, select the **Courier** and take Helm. On the host Viewscreen, open **Settings → Display**. The three switches start on and are local to the running application. Use an external camera angle where the Courier's stern and exhaust are visible.

| Switch | On | Off |
| --- | --- | --- |
| Thruster ignition and cutoff | The existing forward plume and its light flare on ignition and decay after thrust drops. | The plume and light follow applied thrust directly. |
| Thrust camera movement | A small directional offset follows acceleration and settles as the ship coasts. | The camera follows its ordinary ship-relative pose. |
| Engine sound | The existing engine loop rises quickly under thrust and fades more slowly at cutoff. | The engine loop is muted. |

For a comparison, turn all three off, then repeat the same manoeuvre with one switch on at a time. Start at rest, request forward speed, coast at the requested speed, then request zero. Try lateral thrust to isolate camera response. Finish by turning all three on. Reduced motion suppresses the plume transition and camera offset. Engine audio follows the host's existing output and SFX settings.

The Courier's forward plume and engine loop use actual applied forward thrust. Braking or lateral thrust can move the camera without producing a forward plume. These are presentation effects with no authored fields or saved state.
