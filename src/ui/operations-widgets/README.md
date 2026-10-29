# Operations panel state

Start with [operations.cppm](operations.cppm).

OperationsWidgets owns officer selection, notices, supply quantity and panel readouts. Explicit context collaborators provide the permitted ConsoleModel, asset lookup, button registration and page selection; no Application is retained.

Dependencies: ConsoleModel, Rml markup/image registration and localisation. Geometry/capture stay with ConsoleSurface.

Validation: Existing personnel, away, craft, docking and logistics system cases; actual Console presentation is manually checked.

Logical widget parts reuse these renderers with a Controller and optional
operation scope. `board` and `owns` filter the existing permitted projection;
they do not keep another gameplay model. Native part actions still go through
ConsoleModel admission. Teams reuse ConsoleModel's existing order selection and
compartment dispatch. Renderers put readout text in `WidgetPanel::readouts` and
empty placeholders in its markup. Both ordinary and logical hosts use the existing
DOM updater to refresh telemetry without rebuilding controls or reparsing markup.
